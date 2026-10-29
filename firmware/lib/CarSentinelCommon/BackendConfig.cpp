#include "BackendConfig.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "BackendConfig";
static const char* CONFIG_PATH = "/config/backend.json";

BackendConfigData BackendConfig::current;

const char* backendModeToString(BackendMode mode) {
    switch (mode) {
        case BackendMode::CAR_SENTINEL_CLOUD: return "CAR_SENTINEL_CLOUD";
        case BackendMode::CUSTOM_SERVER: return "CUSTOM_SERVER";
        default: return "LOCAL_ONLY";
    }
}

BackendMode backendModeFromString(const String& value) {
    if (value == "CAR_SENTINEL_CLOUD") return BackendMode::CAR_SENTINEL_CLOUD;
    if (value == "CUSTOM_SERVER") return BackendMode::CUSTOM_SERVER;
    return BackendMode::LOCAL_ONLY;
}

const char* syncPolicyToString(SyncPolicy policy) {
    switch (policy) {
        case SyncPolicy::METADATA_ONLY: return "METADATA_ONLY";
        case SyncPolicy::SELECTED: return "SELECTED";
        case SyncPolicy::FULL: return "FULL";
        default: return "OFF";
    }
}

SyncPolicy syncPolicyFromString(const String& value) {
    if (value == "METADATA_ONLY") return SyncPolicy::METADATA_ONLY;
    if (value == "SELECTED") return SyncPolicy::SELECTED;
    if (value == "FULL") return SyncPolicy::FULL;
    return SyncPolicy::OFF;
}

bool BackendConfig::begin() {
    if (LittleFS.exists(CONFIG_PATH)) {
        File f = LittleFS.open(CONFIG_PATH, "r");
        if (f) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (!err) {
                current.enabled = doc["enabled"] | false;
                current.mode = backendModeFromString(doc["mode"] | "LOCAL_ONLY");
                current.baseUrl = doc["baseUrl"] | "";
                current.deviceId = doc["deviceId"] | "";
                current.tenantId = doc["tenantId"] | "";
                current.credential = doc["credential"] | "";
                current.tlsVerify = doc["tlsVerify"] | false;
                current.telemetryIntervalMs = doc["telemetryIntervalMs"] | 30000;
                current.gpsIntervalMs = doc["gpsIntervalMs"] | 10000;
                current.eventsPolicy = syncPolicyFromString(doc["eventsPolicy"] | "SELECTED");
                current.incidentsPolicy = syncPolicyFromString(doc["incidentsPolicy"] | "FULL");
                current.evidencePolicy = syncPolicyFromString(doc["evidencePolicy"] | "METADATA_ONLY");
                // Deliberately never logs `credential` — same "no credentials in logs"
                // posture as EmailConfig/EspNowSecurity.
                Logger::info(TAG, "Loaded backend config: enabled=" + String(current.enabled) +
                             " mode=" + String(backendModeToString(current.mode)) +
                             " baseUrl=" + (current.baseUrl.isEmpty() ? "(none)" : current.baseUrl));
                return true;
            }
            Logger::error(TAG, "backend.json parse failed: " + String(err.c_str()));
        }
    }
    current = BackendConfigData();
    save(current);
    Logger::info(TAG, "No backend config found; created disabled (LOCAL_ONLY) default");
    return true;
}

const BackendConfigData& BackendConfig::get() {
    return current;
}

bool BackendConfig::save(const BackendConfigData& data) {
    current = data;
    JsonDocument doc;
    doc["enabled"] = current.enabled;
    doc["mode"] = backendModeToString(current.mode);
    doc["baseUrl"] = current.baseUrl;
    doc["deviceId"] = current.deviceId;
    doc["tenantId"] = current.tenantId;
    doc["credential"] = current.credential;
    doc["tlsVerify"] = current.tlsVerify;
    doc["telemetryIntervalMs"] = current.telemetryIntervalMs;
    doc["gpsIntervalMs"] = current.gpsIntervalMs;
    doc["eventsPolicy"] = syncPolicyToString(current.eventsPolicy);
    doc["incidentsPolicy"] = syncPolicyToString(current.incidentsPolicy);
    doc["evidencePolicy"] = syncPolicyToString(current.evidencePolicy);

    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) {
        Logger::error(TAG, "Failed to open " + String(CONFIG_PATH) + " for writing");
        return false;
    }
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

}  // namespace CarSentinel
