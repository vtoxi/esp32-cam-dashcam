#include "EvidenceManager.h"
#include "Logger.h"
#include "DeviceConfig.h"

#include <SD_MMC.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "EvidenceManager";
static const char* EVENTS_DIR = "/security/events";
static const char* COUNTER_PATH = "/security/.next_event_number";

bool EvidenceManager::available = false;

int EvidenceManager::nextEventNumber() {
    int n = 1;
    if (SD_MMC.exists(COUNTER_PATH)) {
        File f = SD_MMC.open(COUNTER_PATH, FILE_READ);
        if (f) {
            n = f.parseInt();
            f.close();
            if (n < 1) n = 1;
        }
    }
    File out = SD_MMC.open(COUNTER_PATH, FILE_WRITE);
    if (out) {
        out.print(n + 1);
        out.close();
    }
    return n;
}

bool EvidenceManager::begin() {
    if (!SD_MMC.exists("/security")) {
        if (!SD_MMC.mkdir("/security")) {
            Logger::error(TAG, "Failed to create /security");
            available = false;
            return false;
        }
    }
    if (!SD_MMC.exists(EVENTS_DIR)) {
        if (!SD_MMC.mkdir(EVENTS_DIR)) {
            Logger::error(TAG, "Failed to create " + String(EVENTS_DIR));
            available = false;
            return false;
        }
    }
    available = true;
    Logger::info(TAG, "Evidence storage ready at " + String(EVENTS_DIR));
    return true;
}

String EvidenceManager::createEvent(const String& nodeId, const String& eventType,
                                     const String& severity, const EnvironmentReading& env) {
    if (!available) {
        Logger::warn(TAG, "createEvent called but storage unavailable — event dropped");
        return "";
    }

    int num = nextEventNumber();
    char idBuf[24];
    snprintf(idBuf, sizeof(idBuf), "EVT-%06d", num);
    String eventId = String(idBuf);
    String dir = String(EVENTS_DIR) + "/" + eventId;

    if (!SD_MMC.mkdir(dir)) {
        Logger::error(TAG, "Failed to create event directory " + dir);
        return "";
    }

    JsonDocument doc;
    doc["eventId"] = eventId;
    doc["schemaVersion"] = 1;
    doc["source"] = nodeId;
    doc["type"] = eventType;
    // No NTP yet (Phase 57) — millis() since boot, not wall-clock. Consumers must not
    // treat this as an absolute timestamp until Phase 57 lands.
    doc["uptimeMsAtEvent"] = millis();
    doc["severity"] = severity;
    if (env.valid) {
        doc["environment"]["temperatureC"] = env.temperatureC;
        doc["environment"]["humidityPercent"] = env.humidityPercent;
    }
    doc["evidence"] = JsonArray();
    doc["relatedNodes"] = JsonArray();  // populated once ESP-NOW correlation exists (Phase 7)

    String eventJsonPath = dir + "/event.json";
    File f = SD_MMC.open(eventJsonPath, FILE_WRITE);
    if (!f) {
        Logger::error(TAG, "Failed to open " + eventJsonPath + " for writing");
        return "";
    }
    serializeJson(doc, f);
    f.close();

    Logger::info(TAG, "Created event " + eventId + " type=" + eventType);
    return eventId;
}

bool EvidenceManager::attachImage(const String& eventId, const uint8_t* data, size_t len) {
    if (!available || eventId.isEmpty()) {
        return false;
    }

    String dir = String(EVENTS_DIR) + "/" + eventId;
    String imagePath = dir + "/image_001.jpg";

    File f = SD_MMC.open(imagePath, FILE_WRITE);
    if (!f) {
        Logger::error(TAG, "Failed to open " + imagePath + " for writing");
        return false;
    }
    size_t written = f.write(data, len);
    f.close();

    if (written != len) {
        Logger::error(TAG, "Short write for " + imagePath + " (" + String(written) +
                     "/" + String(len) + " bytes) — SD may be full");
        return false;
    }

    // Append the filename into event.json's evidence array (read-modify-write; event
    // directories are small so this is cheap).
    String eventJsonPath = dir + "/event.json";
    File ef = SD_MMC.open(eventJsonPath, FILE_READ);
    if (!ef) {
        Logger::warn(TAG, "Image saved but could not reopen event.json to record it");
        return true;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, ef);
    ef.close();
    if (err) {
        Logger::warn(TAG, "event.json parse failed while attaching image reference");
        return true;
    }
    doc["evidence"].add("image_001.jpg");
    File ewf = SD_MMC.open(eventJsonPath, FILE_WRITE);
    if (ewf) {
        serializeJson(doc, ewf);
        ewf.close();
    }

    Logger::info(TAG, "Attached image to " + eventId + " (" + String(len) + " bytes)");
    return true;
}

bool EvidenceManager::isAvailable() {
    return available;
}

}  // namespace CarSentinel
