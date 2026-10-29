#include "BackendQueue.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "BackendQueue";
static const char* QUEUE_PATH = "/config/backend_queue.json";
static const unsigned long BACKOFF_BASE_MS = 5000;
static const unsigned long BACKOFF_MAX_MS = 300000;  // 5 minutes
static const uint8_t BACKOFF_MAX_SHIFT = 6;           // 5s * 2^6 = 320s, already past the cap

BackendQueue::QueuedItem BackendQueue::items[BackendQueue::MAX_QUEUED];
uint16_t BackendQueue::itemCount = 0;

const char* backendCategoryToString(BackendCategory category) {
    switch (category) {
        case BackendCategory::HEARTBEAT: return "HEARTBEAT";
        case BackendCategory::TELEMETRY: return "TELEMETRY";
        case BackendCategory::EVENT: return "EVENT";
        default: return "INCIDENT";
    }
}

static unsigned long backoffDelayMs(uint8_t retryCount) {
    uint8_t shift = retryCount > BACKOFF_MAX_SHIFT ? BACKOFF_MAX_SHIFT : retryCount;
    unsigned long delay = BACKOFF_BASE_MS << shift;
    return delay > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : delay;
}

bool BackendQueue::loadFromDisk() {
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
        Logger::warn(TAG, "Backend queue JSON parse failed: " + String(err.c_str()));
        return false;
    }
    itemCount = 0;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject o : arr) {
        if (itemCount >= MAX_QUEUED) break;
        items[itemCount].category = static_cast<BackendCategory>((uint8_t)(o["category"] | 0));
        items[itemCount].payload = o["payload"] | "";
        items[itemCount].queuedAtMs = o["queuedAtMs"] | 0;
        items[itemCount].retryCount = (uint8_t)(o["retryCount"] | 0);
        items[itemCount].nextRetryAtMs = o["nextRetryAtMs"] | 0;
        itemCount++;
    }
    return true;
}

bool BackendQueue::persist() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint16_t i = 0; i < itemCount; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["category"] = static_cast<uint8_t>(items[i].category);
        o["payload"] = items[i].payload;
        o["queuedAtMs"] = items[i].queuedAtMs;
        o["retryCount"] = items[i].retryCount;
        o["nextRetryAtMs"] = items[i].nextRetryAtMs;
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

void BackendQueue::begin() {
    if (loadFromDisk()) {
        Logger::info(TAG, "Loaded backend queue: " + String(itemCount) + " pending item(s)");
    } else {
        itemCount = 0;
    }
}

bool BackendQueue::enqueue(BackendCategory category, const String& jsonPayload) {
    if (itemCount >= MAX_QUEUED) {
        for (uint16_t i = 1; i < itemCount; i++) {
            items[i - 1] = items[i];
        }
        itemCount--;
        Logger::warn(TAG, "Backend queue full; dropping oldest item to make room");
    }
    items[itemCount].category = category;
    items[itemCount].payload = jsonPayload;
    items[itemCount].queuedAtMs = millis();
    items[itemCount].retryCount = 0;
    items[itemCount].nextRetryAtMs = millis();  // eligible for the very next flush()
    itemCount++;
    Logger::info(TAG, "Queued backend item (" + String(backendCategoryToString(category)) + "), " +
                 String(itemCount) + "/" + String(MAX_QUEUED) + " pending");
    return persist();
}

uint16_t BackendQueue::count() {
    return itemCount;
}

bool BackendQueue::sendOne(RemoteBackend* backend, const QueuedItem& item) {
    switch (item.category) {
        case BackendCategory::HEARTBEAT: return backend->sendHeartbeat(item.payload);
        case BackendCategory::TELEMETRY: return backend->sendTelemetry(item.payload);
        case BackendCategory::EVENT: return backend->sendEvent(item.payload);
        default: return backend->sendIncident(item.payload);
    }
}

void BackendQueue::flush(RemoteBackend* backend) {
    if (itemCount == 0 || !backend) {
        return;
    }
    unsigned long now = millis();
    uint16_t delivered = 0;
    uint16_t i = 0;
    while (i < itemCount) {
        if (now < items[i].nextRetryAtMs) {
            i++;  // not due yet — leave in place, check the next item
            continue;
        }
        if (sendOne(backend, items[i])) {
            delivered++;
            for (uint16_t j = i + 1; j < itemCount; j++) {
                items[j - 1] = items[j];
            }
            itemCount--;
            // Don't advance i — the next item shifted into this slot.
        } else {
            items[i].retryCount++;
            items[i].nextRetryAtMs = now + backoffDelayMs(items[i].retryCount);
            Logger::warn(TAG, "Backend item (" + String(backendCategoryToString(items[i].category)) +
                         ") delivery failed, retry " + String(items[i].retryCount) +
                         " in " + String(backoffDelayMs(items[i].retryCount) / 1000) + "s");
            i++;
        }
    }
    if (delivered > 0) {
        persist();
        Logger::info(TAG, "Backend queue flush delivered " + String(delivered) + " item(s), " +
                     String(itemCount) + " remain");
    } else {
        persist();  // retry counts/timestamps still changed even with zero deliveries
    }
}

}  // namespace CarSentinel
