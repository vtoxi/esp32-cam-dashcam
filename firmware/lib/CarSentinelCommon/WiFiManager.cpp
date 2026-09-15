#include "WiFiManager.h"
#include "Logger.h"

#include <WiFi.h>

namespace CarSentinel {

static const char* TAG = "WiFiManager";

WiFiConnState WiFiManager::state = WiFiConnState::DISCONNECTED;
NetworkConfigData WiFiManager::activeConfig;
unsigned long WiFiManager::lastReconnectAttempt = 0;

bool WiFiManager::connectBlocking(const NetworkConfigData& config) {
    if (config.ssid.isEmpty()) {
        Logger::info(TAG, "No saved SSID; skipping connect attempt");
        return false;
    }

    activeConfig = config;
    WiFi.mode(WIFI_STA);
    if (!config.hostname.isEmpty()) {
        WiFi.setHostname(config.hostname.c_str());
    }

    if (config.useStaticIP && config.staticIP.length() > 0) {
        IPAddress ip, gw, sn, dns;
        if (ip.fromString(config.staticIP) && gw.fromString(config.gateway) &&
            sn.fromString(config.subnet)) {
            dns.fromString(config.dns.length() ? config.dns : config.gateway);
            WiFi.config(ip, gw, sn, dns);
        } else {
            Logger::warn(TAG, "Static IP config invalid; falling back to DHCP");
        }
    }

    for (uint8_t attempt = 1; attempt <= config.maxRetries; attempt++) {
        Logger::info(TAG, "Connecting to \"" + config.ssid + "\" (attempt " +
                     String(attempt) + "/" + String(config.maxRetries) + ")");
        state = WiFiConnState::CONNECTING;
        WiFi.begin(config.ssid.c_str(), config.password.c_str());

        unsigned long attemptStart = millis();
        while (millis() - attemptStart < config.connectTimeoutMs) {
            if (WiFi.status() == WL_CONNECTED) {
                state = WiFiConnState::CONNECTED;
                Logger::info(TAG, "Connected, IP=" + WiFi.localIP().toString());
                return true;
            }
            delay(250);
        }

        Logger::warn(TAG, "Connect attempt " + String(attempt) + " timed out");
        WiFi.disconnect();
        if (attempt < config.maxRetries) {
            delay(config.retryIntervalMs);
        }
    }

    state = WiFiConnState::DISCONNECTED;
    Logger::warn(TAG, "Wi-Fi connect failed after " + String(config.maxRetries) +
                 " attempts; continuing offline");
    return false;
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

WiFiConnState WiFiManager::getState() {
    if (WiFi.status() == WL_CONNECTED) {
        state = WiFiConnState::CONNECTED;
    }
    return state;
}

String WiFiManager::localIP() {
    return WiFi.localIP().toString();
}

void WiFiManager::loop() {
    if (activeConfig.ssid.isEmpty()) {
        return;  // never had credentials to reconnect with
    }
    if (WiFi.status() == WL_CONNECTED) {
        state = WiFiConnState::CONNECTED;
        return;
    }

    state = WiFiConnState::DISCONNECTED;
    unsigned long now = millis();
    if (now - lastReconnectAttempt < RECONNECT_INTERVAL_MS) {
        return;  // don't hammer reconnect attempts — camera/security logic must not stall
    }
    lastReconnectAttempt = now;
    Logger::info(TAG, "Wi-Fi dropped; attempting single reconnect (non-blocking policy)");
    WiFi.begin(activeConfig.ssid.c_str(), activeConfig.password.c_str());
}

}  // namespace CarSentinel
