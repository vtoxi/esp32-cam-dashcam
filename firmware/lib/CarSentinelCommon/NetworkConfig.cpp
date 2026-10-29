#include "NetworkConfig.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "NetworkConfig";
static const char* CONFIG_PATH = "/config/network.json";

NetworkConfigData NetworkConfig::current;

void NetworkConfig::migrate(int fromVersion) {
    if (fromVersion == NETWORK_SCHEMA_VERSION) {
        return;
    }
    if (fromVersion == 1) {
        // v1 had only a single ssid/password (still loaded into current.ssid/password
        // by loadFromDisk() above, since those field names/positions didn't move) —
        // fold it into the saved list so it survives as a remembered network too.
        if (!current.ssid.isEmpty() && current.savedCount < MAX_SAVED_NETWORKS) {
            current.saved[current.savedCount].ssid = current.ssid;
            current.saved[current.savedCount].password = current.password;
            current.savedCount++;
        }
        Logger::info(TAG, "Migrated network config v1 -> v2 (single SSID folded into saved list)");
        // Falls through to v2->v3 below (a v1 config skips straight to v3 in one boot).
    }
    if (fromVersion <= 3) {
        // v2->v3 added wifiFallbackEnabled, v3->v4 adds the TransportManager timing
        // knobs — both default via the struct's own defaults (loadFromDisk() already
        // applied them before this runs, since these fields didn't exist in older
        // JSON), so this is a documentation no-op, not a behavior change.
        Logger::info(TAG, "Migrated network config -> v4 (wifiFallbackEnabled + transport timing defaults)");
        return;
    }
    Logger::warn(TAG, "Network config schemaVersion " + String(fromVersion) +
                 " has no defined migration path to " + String(NETWORK_SCHEMA_VERSION) +
                 "; using as-is.");
}

bool NetworkConfig::loadFromDisk() {
    if (!LittleFS.exists(CONFIG_PATH)) {
        return false;
    }
    File file = LittleFS.open(CONFIG_PATH, "r");
    if (!file) {
        Logger::error(TAG, "Failed to open " + String(CONFIG_PATH) + " for reading");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Logger::error(TAG, "Network config JSON parse failed: " + String(err.c_str()));
        return false;
    }

    int storedVersion = doc["schemaVersion"] | 1;
    current.schemaVersion = storedVersion;
    current.ssid = doc["ssid"] | "";
    current.password = doc["password"] | "";
    current.hostname = doc["hostname"] | "";
    current.useStaticIP = doc["useStaticIP"] | false;
    current.staticIP = doc["staticIP"] | "";
    current.gateway = doc["gateway"] | "";
    current.subnet = doc["subnet"] | "";
    current.dns = doc["dns"] | "";
    current.connectTimeoutMs = doc["connectTimeoutMs"] | 15000;
    current.maxRetries = doc["maxRetries"] | 3;
    current.retryIntervalMs = doc["retryIntervalMs"] | 5000;
    current.wifiFallbackEnabled = doc["wifiFallbackEnabled"] | true;
    current.espNowDiscoveryTimeoutMs = doc["espNowDiscoveryTimeoutMs"] | 15000;
    current.espNowRetryIntervalMs = doc["espNowRetryIntervalMs"] | 30000;
    current.espNowHeartbeatTimeoutMs = doc["espNowHeartbeatTimeoutMs"] | 60000;
    current.wifiFallbackDelayMs = doc["wifiFallbackDelayMs"] | 5000;

    current.savedCount = 0;
    JsonArray savedArr = doc["saved"].as<JsonArray>();
    for (JsonObject o : savedArr) {
        if (current.savedCount >= MAX_SAVED_NETWORKS) break;
        current.saved[current.savedCount].ssid = o["ssid"] | "";
        current.saved[current.savedCount].password = o["password"] | "";
        current.savedCount++;
    }

    if (storedVersion != NETWORK_SCHEMA_VERSION) {
        migrate(storedVersion);
        current.schemaVersion = NETWORK_SCHEMA_VERSION;
        save(current);
    }

    return true;
}

bool NetworkConfig::begin(const String& defaultHostname) {
    if (loadFromDisk()) {
        Logger::info(TAG, "Loaded network config: hostname=" + current.hostname +
                     " hasCredentials=" + String(hasCredentials() ? "true" : "false"));
        return true;
    }

    Logger::info(TAG, "No network config found; creating empty (unprovisioned) config");
    current = NetworkConfigData();
    current.hostname = defaultHostname;
    return save(current);
}

const NetworkConfigData& NetworkConfig::get() {
    return current;
}

bool NetworkConfig::hasCredentials() {
    return current.ssid.length() > 0;
}

bool NetworkConfig::save(const NetworkConfigData& data) {
    current = data;
    current.schemaVersion = NETWORK_SCHEMA_VERSION;

    JsonDocument doc;
    doc["schemaVersion"] = current.schemaVersion;
    doc["ssid"] = current.ssid;
    doc["password"] = current.password;
    doc["hostname"] = current.hostname;
    doc["useStaticIP"] = current.useStaticIP;
    doc["staticIP"] = current.staticIP;
    doc["gateway"] = current.gateway;
    doc["subnet"] = current.subnet;
    doc["dns"] = current.dns;
    doc["connectTimeoutMs"] = current.connectTimeoutMs;
    doc["maxRetries"] = current.maxRetries;
    doc["retryIntervalMs"] = current.retryIntervalMs;
    doc["wifiFallbackEnabled"] = current.wifiFallbackEnabled;
    doc["espNowDiscoveryTimeoutMs"] = current.espNowDiscoveryTimeoutMs;
    doc["espNowRetryIntervalMs"] = current.espNowRetryIntervalMs;
    doc["espNowHeartbeatTimeoutMs"] = current.espNowHeartbeatTimeoutMs;
    doc["wifiFallbackDelayMs"] = current.wifiFallbackDelayMs;

    JsonArray savedArr = doc["saved"].to<JsonArray>();
    for (uint8_t i = 0; i < current.savedCount; i++) {
        JsonObject o = savedArr.add<JsonObject>();
        o["ssid"] = current.saved[i].ssid;
        o["password"] = current.saved[i].password;
    }

    File file = LittleFS.open(CONFIG_PATH, "w");
    if (!file) {
        Logger::error(TAG, "Failed to open " + String(CONFIG_PATH) + " for writing");
        return false;
    }
    if (serializeJson(doc, file) == 0) {
        Logger::error(TAG, "Failed to write network config JSON");
        file.close();
        return false;
    }
    file.close();
    return true;
}

bool NetworkConfig::clearCredentials() {
    Logger::warn(TAG, "Clearing Wi-Fi credentials (provisioning reset)");
    current.ssid = "";
    current.password = "";
    return save(current);
}

bool NetworkConfig::setWifiFallbackEnabled(bool enabled) {
    NetworkConfigData next = current;
    next.wifiFallbackEnabled = enabled;
    Logger::info(TAG, String("Wi-Fi fallback ") + (enabled ? "enabled" : "disabled") +
                 (enabled ? "" : " — this device now operates ESP-NOW only"));
    return save(next);
}

bool NetworkConfig::addNetwork(const String& ssid, const String& password) {
    if (ssid.isEmpty()) {
        return false;
    }
    NetworkConfigData next = current;
    for (uint8_t i = 0; i < next.savedCount; i++) {
        if (next.saved[i].ssid == ssid) {
            next.saved[i].password = password;
            Logger::info(TAG, "Updated saved network \"" + ssid + "\"");
            return save(next);
        }
    }
    if (next.savedCount >= MAX_SAVED_NETWORKS) {
        // Evict the oldest (index 0) to make room — same "bounded, never grow
        // forever" reasoning as IncidentCorrelator's retention cap.
        for (uint8_t i = 1; i < next.savedCount; i++) {
            next.saved[i - 1] = next.saved[i];
        }
        next.savedCount--;
        Logger::warn(TAG, "Saved network list full; evicting oldest to add \"" + ssid + "\"");
    }
    next.saved[next.savedCount].ssid = ssid;
    next.saved[next.savedCount].password = password;
    next.savedCount++;
    Logger::info(TAG, "Saved new network \"" + ssid + "\" (" + String(next.savedCount) + "/" +
                 String(MAX_SAVED_NETWORKS) + ")");
    return save(next);
}

bool NetworkConfig::removeNetwork(const String& ssid) {
    NetworkConfigData next = current;
    for (uint8_t i = 0; i < next.savedCount; i++) {
        if (next.saved[i].ssid == ssid) {
            for (uint8_t j = i + 1; j < next.savedCount; j++) {
                next.saved[j - 1] = next.saved[j];
            }
            next.savedCount--;
            Logger::info(TAG, "Removed saved network \"" + ssid + "\"");
            return save(next);
        }
    }
    return false;
}

bool NetworkConfig::setPrimary(const String& ssid, const String& password) {
    if (ssid.isEmpty()) {
        return false;
    }
    NetworkConfigData next = current;
    next.ssid = ssid;
    next.password = password;
    bool ok = save(next);
    addNetwork(ssid, password);  // also remember it, in case it wasn't already saved
    return ok;
}

}  // namespace CarSentinel
