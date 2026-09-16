#include "OfflineQueue.h"
#include "EspNowManager.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "OfflineQueue";
static const char* QUEUE_PATH = "/config/offline_queue.json";

OfflineQueue::QueuedEvent OfflineQueue::items[OfflineQueue::MAX_QUEUED];
uint16_t OfflineQueue::itemCount = 0;

bool OfflineQueue::loadFromDisk() {
    if (!LittleFS.exists(QUEUE_PATH)) {
        return false;
    }
    File file = LittleFS.open(QUEUE_PATH, "r");
    if (!file) {
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Logger::warn(TAG, "Offline queue JSON parse failed: " + String(err.c_str()));
        return false;
    }
    itemCount = 0;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject o : arr) {
        if (itemCount >= MAX_QUEUED) break;
        items[itemCount].type = static_cast<EspNowMessageType>((uint8_t)(o["type"] | 0));
        items[itemCount].payload = o["payload"] | "";
        items[itemCount].queuedAtMs = o["queuedAtMs"] | 0;
        itemCount++;
    }
    return true;
}

bool OfflineQueue::persist() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint16_t i = 0; i < itemCount; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["type"] = static_cast<uint8_t>(items[i].type);
        o["payload"] = items[i].payload;
        o["queuedAtMs"] = items[i].queuedAtMs;
    }
    File file = LittleFS.open(QUEUE_PATH, "w");
    if (!file) {
        Logger::error(TAG, "Failed to open " + String(QUEUE_PATH) + " for writing");
        return false;
    }
    bool ok = serializeJson(doc, file) > 0 || itemCount == 0;
    file.close();
    return ok;
}

void OfflineQueue::begin() {
    if (loadFromDisk()) {
        Logger::info(TAG, "Loaded offline queue: " + String(itemCount) + " pending event(s)");
    } else {
        itemCount = 0;
    }
}

bool OfflineQueue::enqueue(EspNowMessageType type, const String& jsonPayload) {
    if (itemCount >= MAX_QUEUED) {
        // Evict the oldest — same bounded reasoning as every other capped list in this
        // project (NetworkConfig's saved networks, IncidentCorrelator's retention).
        for (uint16_t i = 1; i < itemCount; i++) {
            items[i - 1] = items[i];
        }
        itemCount--;
        Logger::warn(TAG, "Offline queue full; dropping oldest event to make room");
    }
    items[itemCount].type = type;
    items[itemCount].payload = jsonPayload;
    items[itemCount].queuedAtMs = millis();
    itemCount++;
    Logger::info(TAG, "Queued event (" + String(messageTypeToString(type)) + "), " +
                 String(itemCount) + "/" + String(MAX_QUEUED) + " pending");
    return persist();
}

uint16_t OfflineQueue::count() {
    return itemCount;
}

void OfflineQueue::flush() {
    if (itemCount == 0) {
        return;
    }
    uint8_t gatewayMac[6];
    if (!EspNowManager::findGatewayMac(gatewayMac)) {
        Logger::warn(TAG, "flush() called with no known gateway MAC; leaving queue intact");
        return;
    }

    Logger::info(TAG, "Flushing offline queue: " + String(itemCount) + " pending event(s)");
    uint16_t delivered = 0;
    while (itemCount > 0) {
        bool sent = EspNowManager::sendMessage(items[0].type, items[0].payload, gatewayMac);
        if (!sent) {
            Logger::warn(TAG, "Offline queue flush stopped after " + String(delivered) +
                         " delivered — gateway unreachable again, " + String(itemCount) +
                         " event(s) remain queued");
            break;
        }
        delivered++;
        for (uint16_t i = 1; i < itemCount; i++) {
            items[i - 1] = items[i];
        }
        itemCount--;
    }
    persist();
    if (delivered > 0) {
        Logger::info(TAG, "Offline queue flush delivered " + String(delivered) + " event(s)");
    }
}

}  // namespace CarSentinel
