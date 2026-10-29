#include "SecurityModeConfig.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "SecurityModeConfig";
static const char* CONFIG_PATH = "/config/security_mode.json";

SecurityMode SecurityModeConfig::mode = SecurityMode::PARKED;
bool SecurityModeConfig::manualOverride = false;

const char* securityModeToString(SecurityMode mode) {
    switch (mode) {
        case SecurityMode::DISARMED: return "DISARMED";
        case SecurityMode::DRIVING: return "DRIVING";
        case SecurityMode::SERVICE: return "SERVICE";
        default: return "PARKED";
    }
}

SecurityMode securityModeFromString(const String& value) {
    if (value == "DISARMED") return SecurityMode::DISARMED;
    if (value == "DRIVING") return SecurityMode::DRIVING;
    if (value == "SERVICE") return SecurityMode::SERVICE;
    return SecurityMode::PARKED;
}

bool securityModeAllowsMotionAlerts(SecurityMode mode) {
    return mode == SecurityMode::PARKED;
}

void SecurityModeConfig::begin() {
    if (LittleFS.exists(CONFIG_PATH)) {
        File f = LittleFS.open(CONFIG_PATH, "r");
        if (f) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (!err) {
                mode = securityModeFromString(doc["mode"] | "PARKED");
                manualOverride = doc["manualOverride"] | false;
                Logger::info(TAG, "Loaded mode=" + String(securityModeToString(mode)) +
                             " manualOverride=" + String(manualOverride));
                return;
            }
            Logger::error(TAG, "security_mode.json parse failed: " + String(err.c_str()));
        }
    }
    mode = SecurityMode::PARKED;
    manualOverride = false;
    save();
    Logger::info(TAG, "No persisted mode found; defaulting to PARKED");
}

SecurityMode SecurityModeConfig::getMode() {
    return mode;
}

bool SecurityModeConfig::isManualOverride() {
    return manualOverride;
}

bool SecurityModeConfig::setMode(SecurityMode newMode, bool manual) {
    mode = newMode;
    manualOverride = manual;
    Logger::info(TAG, "Mode set to " + String(securityModeToString(mode)) +
                 (manual ? " (manual override)" : " (auto-detected)"));
    return save();
}

bool SecurityModeConfig::save() {
    JsonDocument doc;
    doc["mode"] = securityModeToString(mode);
    doc["manualOverride"] = manualOverride;
    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

}  // namespace CarSentinel
