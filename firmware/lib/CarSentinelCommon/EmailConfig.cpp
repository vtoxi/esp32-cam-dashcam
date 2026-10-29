#include "EmailConfig.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "EmailConfig";
static const char* CONFIG_PATH = "/config/email_config.json";

EmailConfigData EmailConfig::current;

bool EmailConfig::begin() {
    if (LittleFS.exists(CONFIG_PATH)) {
        File f = LittleFS.open(CONFIG_PATH, "r");
        if (f) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (!err) {
                current.enabled = doc["enabled"] | false;
                current.smtpHost = doc["smtpHost"] | "";
                current.smtpPort = doc["smtpPort"] | 465;
                current.username = doc["username"] | "";
                current.password = doc["password"] | "";
                current.sender = doc["sender"] | "";
                current.recipient = doc["recipient"] | "";
                current.cooldownSeconds = doc["cooldownSeconds"] | 300;
                Logger::info(TAG, "Loaded email config, enabled=" + String(current.enabled) +
                             " host=" + current.smtpHost);
                return true;
            }
            Logger::error(TAG, "email_config.json parse failed: " + String(err.c_str()));
        }
    }
    current = EmailConfigData();
    save(current);
    Logger::info(TAG, "No email config found; created disabled default");
    return true;
}

const EmailConfigData& EmailConfig::get() {
    return current;
}

bool EmailConfig::save(const EmailConfigData& data) {
    current = data;
    JsonDocument doc;
    doc["enabled"] = current.enabled;
    doc["smtpHost"] = current.smtpHost;
    doc["smtpPort"] = current.smtpPort;
    doc["username"] = current.username;
    doc["password"] = current.password;
    doc["sender"] = current.sender;
    doc["recipient"] = current.recipient;
    doc["cooldownSeconds"] = current.cooldownSeconds;

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
