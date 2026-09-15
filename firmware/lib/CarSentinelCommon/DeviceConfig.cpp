#include "DeviceConfig.h"
#include "DeviceIdentity.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "DeviceConfig";
static const char* CONFIG_DIR = "/config";
static const char* CONFIG_PATH = "/config/device.json";

DeviceConfigData DeviceConfig::current;

const char* roleToString(DeviceRole role) {
    switch (role) {
        case DeviceRole::GATEWAY: return "GATEWAY";
        case DeviceRole::CAMERA: return "CAMERA";
        case DeviceRole::SENSOR: return "SENSOR";
        case DeviceRole::DISPLAY: return "DISPLAY";
        case DeviceRole::VEHICLE_CONTROLLER: return "VEHICLE_CONTROLLER";
        default: return "UNASSIGNED";
    }
}

DeviceRole roleFromString(const String& value) {
    if (value == "GATEWAY") return DeviceRole::GATEWAY;
    if (value == "CAMERA") return DeviceRole::CAMERA;
    if (value == "SENSOR") return DeviceRole::SENSOR;
    if (value == "DISPLAY") return DeviceRole::DISPLAY;
    if (value == "VEHICLE_CONTROLLER") return DeviceRole::VEHICLE_CONTROLLER;
    return DeviceRole::UNASSIGNED;
}

void DeviceConfig::applyDefaults(const char* defaultNodeIdPrefix, DeviceRole compiledDefaultRole) {
    current = DeviceConfigData();
    current.nodeId = DeviceIdentity::generateDefaultNodeId(defaultNodeIdPrefix);
    current.displayName = current.nodeId;
    current.role = compiledDefaultRole;
    current.hardwareProfile = "UNKNOWN";
    current.firmwareVersion = CARSENTINEL_FIRMWARE_VERSION;
}

void DeviceConfig::migrate(int fromVersion) {
    if (fromVersion == CONFIG_SCHEMA_VERSION) {
        return;
    }
    // No migrations defined yet — schema has only ever been v1. When v2 ships, add:
    //   if (fromVersion < 2) { /* transform current in place */ }
    Logger::warn(TAG, "Config schemaVersion " + String(fromVersion) +
                 " has no defined migration path to " + String(CONFIG_SCHEMA_VERSION) +
                 "; using as-is.");
}

bool DeviceConfig::loadFromDisk() {
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
        Logger::error(TAG, "Config JSON parse failed: " + String(err.c_str()));
        return false;
    }

    int storedVersion = doc["schemaVersion"] | 1;
    current.schemaVersion = storedVersion;
    current.nodeId = doc["nodeId"] | "";
    current.displayName = doc["displayName"] | "";
    current.role = roleFromString(doc["role"] | "UNASSIGNED");
    current.hardwareProfile = doc["hardwareProfile"] | "UNKNOWN";
    current.firmwareVersion = doc["firmwareVersion"] | "";

    if (current.nodeId.isEmpty()) {
        Logger::warn(TAG, "Loaded config has empty nodeId; treating as invalid");
        return false;
    }

    if (storedVersion != CONFIG_SCHEMA_VERSION) {
        migrate(storedVersion);
        current.schemaVersion = CONFIG_SCHEMA_VERSION;
        save(current);
    }

    return true;
}

bool DeviceConfig::begin(const char* defaultNodeIdPrefix, DeviceRole compiledDefaultRole) {
    if (!LittleFS.begin(true)) {
        Logger::error(TAG, "LittleFS mount failed even after format attempt");
        return false;
    }

    if (!LittleFS.exists(CONFIG_DIR)) {
        LittleFS.mkdir(CONFIG_DIR);
    }

    if (loadFromDisk()) {
        Logger::info(TAG, "Loaded config: nodeId=" + current.nodeId +
                     " role=" + String(roleToString(current.role)));
        return true;
    }

    Logger::info(TAG, "No valid config found; creating defaults");
    applyDefaults(defaultNodeIdPrefix, compiledDefaultRole);
    if (!save(current)) {
        Logger::error(TAG, "Failed to persist default config");
        return false;
    }
    return true;
}

const DeviceConfigData& DeviceConfig::get() {
    return current;
}

bool DeviceConfig::save(const DeviceConfigData& data) {
    current = data;
    current.schemaVersion = CONFIG_SCHEMA_VERSION;

    JsonDocument doc;
    doc["schemaVersion"] = current.schemaVersion;
    doc["nodeId"] = current.nodeId;
    doc["displayName"] = current.displayName;
    doc["role"] = roleToString(current.role);
    doc["hardwareProfile"] = current.hardwareProfile;
    doc["firmwareVersion"] = current.firmwareVersion;

    File file = LittleFS.open(CONFIG_PATH, "w");
    if (!file) {
        Logger::error(TAG, "Failed to open " + String(CONFIG_PATH) + " for writing");
        return false;
    }
    if (serializeJson(doc, file) == 0) {
        Logger::error(TAG, "Failed to write config JSON");
        file.close();
        return false;
    }
    file.close();
    return true;
}

bool DeviceConfig::factoryReset(const char* defaultNodeIdPrefix, DeviceRole compiledDefaultRole) {
    Logger::warn(TAG, "Factory reset requested — clearing /config/device.json");
    if (LittleFS.exists(CONFIG_PATH)) {
        LittleFS.remove(CONFIG_PATH);
    }
    applyDefaults(defaultNodeIdPrefix, compiledDefaultRole);
    return save(current);
}

}  // namespace CarSentinel
