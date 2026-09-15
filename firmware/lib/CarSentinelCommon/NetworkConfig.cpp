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

}  // namespace CarSentinel
