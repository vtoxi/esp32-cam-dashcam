#include "CapabilitiesConfig.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "CapabilitiesConfig";
static const char* CONFIG_PATH = "/config/capabilities.json";

CapabilitiesConfigData CapabilitiesConfig::current;

void CapabilitiesConfig::migrate(int fromVersion) {
    if (fromVersion == CAPABILITIES_SCHEMA_VERSION) {
        return;
    }
    Logger::warn(TAG, "Capabilities schemaVersion " + String(fromVersion) +
                 " has no defined migration path to " + String(CAPABILITIES_SCHEMA_VERSION) +
                 "; using as-is.");
}

bool CapabilitiesConfig::loadFromDisk() {
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
        Logger::error(TAG, "Capabilities JSON parse failed: " + String(err.c_str()));
        return false;
    }

    int storedVersion = doc["schemaVersion"] | 1;
    current.schemaVersion = storedVersion;
    current.camera = doc["camera"] | false;
    current.sd = doc["sd"] | false;
    current.rcwl = doc["rcwl"]["enabled"] | false;
    current.rcwlGpio = doc["rcwl"]["gpio"] | GPIO_UNCONFIGURED;
    current.dht = doc["dht"]["enabled"] | false;
    current.dhtGpio = doc["dht"]["gpio"] | GPIO_UNCONFIGURED;
    current.gps = doc["gps"]["enabled"] | false;
    current.gpsRxGpio = doc["gps"]["rxGpio"] | GPIO_UNCONFIGURED;
    current.gpsTxGpio = doc["gps"]["txGpio"] | GPIO_UNCONFIGURED;
    current.imu = doc["imu"]["enabled"] | false;
    current.imuSdaGpio = doc["imu"]["sdaGpio"] | GPIO_UNCONFIGURED;
    current.imuSclGpio = doc["imu"]["sclGpio"] | GPIO_UNCONFIGURED;
    current.display = doc["display"]["enabled"] | false;
    current.displayCount = doc["display"]["count"] | 0;
    current.display1SdaGpio = doc["display"]["display1SdaGpio"] | GPIO_UNCONFIGURED;
    current.display1SclGpio = doc["display"]["display1SclGpio"] | GPIO_UNCONFIGURED;
    current.display2SdaGpio = doc["display"]["display2SdaGpio"] | GPIO_UNCONFIGURED;
    current.display2SclGpio = doc["display"]["display2SclGpio"] | GPIO_UNCONFIGURED;

    if (storedVersion != CAPABILITIES_SCHEMA_VERSION) {
        migrate(storedVersion);
        current.schemaVersion = CAPABILITIES_SCHEMA_VERSION;
        save(current);
    }

    return true;
}

bool CapabilitiesConfig::begin(const CapabilitiesConfigData& defaults) {
    if (loadFromDisk()) {
        Logger::info(TAG, "Loaded capabilities config from disk");
        return true;
    }
    Logger::info(TAG, "No capabilities config found; seeding from hardware profile defaults");
    return save(defaults);
}

const CapabilitiesConfigData& CapabilitiesConfig::get() {
    return current;
}

bool CapabilitiesConfig::save(const CapabilitiesConfigData& data) {
    current = data;
    current.schemaVersion = CAPABILITIES_SCHEMA_VERSION;

    JsonDocument doc;
    doc["schemaVersion"] = current.schemaVersion;
    doc["camera"] = current.camera;
    doc["sd"] = current.sd;
    doc["rcwl"]["enabled"] = current.rcwl;
    doc["rcwl"]["gpio"] = current.rcwlGpio;
    doc["dht"]["enabled"] = current.dht;
    doc["dht"]["gpio"] = current.dhtGpio;
    doc["gps"]["enabled"] = current.gps;
    doc["gps"]["rxGpio"] = current.gpsRxGpio;
    doc["gps"]["txGpio"] = current.gpsTxGpio;
    doc["imu"]["enabled"] = current.imu;
    doc["imu"]["sdaGpio"] = current.imuSdaGpio;
    doc["imu"]["sclGpio"] = current.imuSclGpio;
    doc["display"]["enabled"] = current.display;
    doc["display"]["count"] = current.displayCount;
    doc["display"]["display1SdaGpio"] = current.display1SdaGpio;
    doc["display"]["display1SclGpio"] = current.display1SclGpio;
    doc["display"]["display2SdaGpio"] = current.display2SdaGpio;
    doc["display"]["display2SclGpio"] = current.display2SclGpio;

    File file = LittleFS.open(CONFIG_PATH, "w");
    if (!file) {
        Logger::error(TAG, "Failed to open " + String(CONFIG_PATH) + " for writing");
        return false;
    }
    if (serializeJson(doc, file) == 0) {
        Logger::error(TAG, "Failed to write capabilities JSON");
        file.close();
        return false;
    }
    file.close();
    return true;
}

}  // namespace CarSentinel
