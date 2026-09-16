#include "WiFiManager.h"
#include "Logger.h"
#include "Watchdog.h"

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
            // This loop (and the retry delay below) runs synchronously inside setup(),
            // well past the watchdog's timeout if left unfed — confirmed on real
            // hardware: a task watchdog panic/reboot mid-connect, right where this used
            // to be a bare delay(250).
            Watchdog::feed();
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
            // Same reasoning — a multi-second delay() here must not starve the watchdog.
            unsigned long retryStart = millis();
            while (millis() - retryStart < config.retryIntervalMs) {
                Watchdog::feed();
                delay(250);
            }
        }
    }

    state = WiFiConnState::DISCONNECTED;
    Logger::warn(TAG, "Wi-Fi connect failed after " + String(config.maxRetries) +
                 " attempts; continuing offline");
    return false;
}

bool WiFiManager::connectBestKnown() {
    const NetworkConfigData& cfg = NetworkConfig::get();

    // Candidate order: primary first (unchanged fast path when there's only one
    // network, or it's still reachable), then saved networks not already tried.
    struct Candidate { String ssid, password; };
    Candidate candidates[1 + MAX_SAVED_NETWORKS];
    uint8_t candidateCount = 0;
    if (!cfg.ssid.isEmpty()) {
        candidates[candidateCount++] = {cfg.ssid, cfg.password};
    }
    for (uint8_t i = 0; i < cfg.savedCount; i++) {
        bool alreadyListed = false;
        for (uint8_t j = 0; j < candidateCount; j++) {
            if (candidates[j].ssid == cfg.saved[i].ssid) { alreadyListed = true; break; }
        }
        if (!alreadyListed) {
            candidates[candidateCount++] = {cfg.saved[i].ssid, cfg.saved[i].password};
        }
    }

    if (candidateCount == 0) {
        Logger::info(TAG, "No saved networks at all; skipping connect attempt");
        return false;
    }

    for (uint8_t i = 0; i < candidateCount; i++) {
        NetworkConfigData attemptCfg = cfg;
        attemptCfg.ssid = candidates[i].ssid;
        attemptCfg.password = candidates[i].password;
        Logger::info(TAG, "Trying known network " + String(i + 1) + "/" + String(candidateCount) +
                     ": \"" + candidates[i].ssid + "\"");
        if (connectBlocking(attemptCfg)) {
            if (candidates[i].ssid != cfg.ssid) {
                NetworkConfig::setPrimary(candidates[i].ssid, candidates[i].password);
                Logger::info(TAG, "Promoted \"" + candidates[i].ssid + "\" to primary network");
            }
            return true;
        }
    }

    Logger::warn(TAG, "All " + String(candidateCount) + " known network(s) failed to connect");
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
        if (state != WiFiConnState::CONNECTED) {
            // Transitioned DISCONNECTED -> CONNECTED since the last check — a reconnect
            // just succeeded (possibly with a new IP via DHCP), worth its own log line
            // rather than only ever announcing the IP once at boot.
            Logger::info(TAG, "Reconnected, IP=" + WiFi.localIP().toString());
        }
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
