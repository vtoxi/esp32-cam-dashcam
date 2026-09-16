#include "DeviceRegistry.h"
#include "MacAddress.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "DeviceRegistry";
static const char* REGISTRY_PATH = "/config/device_registry.json";

DeviceRegistryEntry DeviceRegistry::devices[DeviceRegistry::MAX_DEVICES];
uint8_t DeviceRegistry::deviceCount = 0;

bool DeviceRegistry::loadFromDisk() {
    if (!LittleFS.exists(REGISTRY_PATH)) {
        return false;
    }
    File f = LittleFS.open(REGISTRY_PATH, "r");
    if (!f) {
        Logger::error(TAG, "Failed to open " + String(REGISTRY_PATH) + " for reading");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Logger::error(TAG, "Registry JSON parse failed: " + String(err.c_str()));
        return false;
    }

    JsonArray arr = doc["devices"].as<JsonArray>();
    deviceCount = 0;
    for (JsonObject obj : arr) {
        if (deviceCount >= MAX_DEVICES) break;
        DeviceRegistryEntry& e = devices[deviceCount++];
        e.nodeId = obj["nodeId"] | "";
        e.displayName = obj["displayName"] | "";
        e.role = obj["role"] | "";
        e.mac = obj["mac"] | "";
        e.hardwareProfile = obj["hardwareProfile"] | "";
        e.firmwareVersion = obj["firmwareVersion"] | "";
        e.ip = obj["ip"] | "";
        e.enabled = obj["enabled"] | true;
    }
    Logger::info(TAG, "Loaded " + String(deviceCount) + " device(s) from registry");
    return true;
}

bool DeviceRegistry::save() {
    JsonDocument doc;
    doc["schemaVersion"] = DEVICE_REGISTRY_SCHEMA_VERSION;
    JsonArray arr = doc["devices"].to<JsonArray>();
    for (uint8_t i = 0; i < deviceCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["nodeId"] = devices[i].nodeId;
        obj["displayName"] = devices[i].displayName;
        obj["role"] = devices[i].role;
        obj["mac"] = devices[i].mac;
        obj["hardwareProfile"] = devices[i].hardwareProfile;
        obj["firmwareVersion"] = devices[i].firmwareVersion;
        obj["ip"] = devices[i].ip;
        obj["enabled"] = devices[i].enabled;
    }

    File f = LittleFS.open(REGISTRY_PATH, "w");
    if (!f) {
        Logger::error(TAG, "Failed to open " + String(REGISTRY_PATH) + " for writing");
        return false;
    }
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

bool DeviceRegistry::begin() {
    if (!loadFromDisk()) {
        deviceCount = 0;
        save();  // create an empty registry file so it always exists thereafter
    }
    return true;
}

DeviceRegistryEntry* DeviceRegistry::find(const String& nodeId) {
    for (uint8_t i = 0; i < deviceCount; i++) {
        if (devices[i].nodeId == nodeId) return &devices[i];
    }
    return nullptr;
}

DeviceRegistryEntry* DeviceRegistry::upsertFromDiscovery(const String& nodeId, const uint8_t mac[6],
                                                           const String& role, const String& displayName,
                                                           const String& ip) {
    String macStr = macToString(mac);
    DeviceRegistryEntry* existing = find(nodeId);
    if (existing) {
        bool changed = false;
        if (existing->mac != macStr) { existing->mac = macStr; changed = true; }
        if (!role.isEmpty() && existing->role != role) { existing->role = role; changed = true; }
        if (!displayName.isEmpty() && existing->displayName != displayName) {
            existing->displayName = displayName;
            changed = true;
        }
        // Older node firmware does not report an IP. Keep a previously learned value
        // until a newer heartbeat reports a replacement instead of erasing it.
        if (!ip.isEmpty() && existing->ip != ip) { existing->ip = ip; changed = true; }
        existing->lastSeenMs = millis();
        if (changed) save();
        return existing;
    }

    if (deviceCount >= MAX_DEVICES) {
        Logger::warn(TAG, "Device registry full (" + String(MAX_DEVICES) +
                     ") — ignoring newly discovered " + nodeId);
        return nullptr;
    }

    DeviceRegistryEntry& e = devices[deviceCount++];
    e.nodeId = nodeId;
    e.displayName = displayName.isEmpty() ? nodeId : displayName;
    e.role = role;
    e.mac = macStr;
    e.ip = ip;
    e.enabled = true;
    e.lastSeenMs = millis();
    Logger::info(TAG, "New device discovered: " + nodeId + " role=" + role + " mac=" + macStr + " ip=" + ip);
    save();
    return &e;
}

void DeviceRegistry::updateHealth(const String& nodeId, uint32_t freeHeap, unsigned long uptimeMs) {
    DeviceRegistryEntry* e = find(nodeId);
    if (!e) return;
    e->lastFreeHeap = freeHeap;
    e->lastUptimeMs = uptimeMs;
    e->lastSeenMs = millis();
}

bool DeviceRegistry::setEnabled(const String& nodeId, bool enabled) {
    DeviceRegistryEntry* e = find(nodeId);
    if (!e) return false;
    e->enabled = enabled;
    return save();
}

bool DeviceRegistry::rename(const String& nodeId, const String& newDisplayName) {
    DeviceRegistryEntry* e = find(nodeId);
    if (!e) return false;
    e->displayName = newDisplayName;
    return save();
}

bool DeviceRegistry::remove(const String& nodeId) {
    for (uint8_t i = 0; i < deviceCount; i++) {
        if (devices[i].nodeId == nodeId) {
            for (uint8_t j = i; j < deviceCount - 1; j++) {
                devices[j] = devices[j + 1];
            }
            deviceCount--;
            return save();
        }
    }
    return false;
}

uint8_t DeviceRegistry::count() {
    return deviceCount;
}

DeviceRegistryEntry* DeviceRegistry::get(uint8_t index) {
    if (index >= deviceCount) return nullptr;
    return &devices[index];
}

}  // namespace CarSentinel
