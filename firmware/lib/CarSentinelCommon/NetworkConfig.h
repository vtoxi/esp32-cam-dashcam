#pragma once

#include <Arduino.h>

// Persistent Wi-Fi configuration, stored separately from DeviceConfig
// (/config/network.json vs /config/device.json) on purpose:
//   - Section 40 factory reset clears "network credentials" and "node configuration" as
//     distinct items; keeping them in separate files makes clearing one without the
//     other trivial if a future phase needs it (e.g. a BLE "forget Wi-Fi" action).
//   - Diagnostics/logging that dumps device.json for debugging never risks leaking a
//     Wi-Fi password (Section 41: no credentials in logs).
namespace CarSentinel {

constexpr int NETWORK_SCHEMA_VERSION = 3;
constexpr uint8_t MAX_SAVED_NETWORKS = 5;

struct WifiNetwork {
    String ssid;
    String password;
};

struct NetworkConfigData {
    int schemaVersion = NETWORK_SCHEMA_VERSION;
    // "Primary" — the last network WiFiManager actually connected to, and what
    // isConnected()/STATUS commands report. Kept as its own field (rather than "just
    // use saved[0]") so existing callers (WiFiManager::connectBlocking(),
    // WiFiManager::loop()'s reconnect) don't need to change at all — multi-network
    // support is an orchestration layer above this, not a rewrite of already
    // hardware-verified connect/reconnect logic.
    String ssid;
    String password;

    // Multiple remembered networks (Section: "multiple wifi remember" — e.g. home +
    // vehicle-mounted hotspot + a bench Wi-Fi). WiFiManager::connectBestKnown() tries
    // these in order when the primary fails to connect, and promotes whichever
    // succeeds to become the new primary. Schema v1->v2 migration folds a v1 config's
    // single ssid/password into saved[0] if it isn't already present.
    WifiNetwork saved[MAX_SAVED_NETWORKS];
    uint8_t savedCount = 0;

    // docs/NETWORK.md's hybrid transport model: ESP-NOW is always the primary
    // transport (started unconditionally on boot, independent of this flag — see
    // gateway_main.cpp/node_main.cpp's setup()); Wi-Fi is only a fallback, and only
    // touched at all when this is true. False means "ESP-NOW only" — the device never
    // calls WiFi.begin(), never opens AP-mode provisioning, and never prompts for
    // Wi-Fi credentials. Defaults true so existing saved configs (and the gateway,
    // which normally wants Internet/dashboard access) keep working exactly as before
    // this flag existed; a pure ESP-NOW node sets it false explicitly (WIFIFALLBACK
    // OFF serial command, or the dashboard Settings page).
    bool wifiFallbackEnabled = true;

    String hostname;              // defaults to nodeId-derived value at first save
    bool useStaticIP = false;
    String staticIP;
    String gateway;
    String subnet;
    String dns;
    uint32_t connectTimeoutMs = 15000;  // per-attempt timeout
    uint8_t maxRetries = 3;             // bounded — Section 10: never block forever
    uint32_t retryIntervalMs = 5000;
};

class NetworkConfig {
public:
    // Loads /config/network.json if present; otherwise creates an empty (no
    // credentials) config with the given default hostname. Assumes DeviceConfig::begin()
    // (and therefore LittleFS) already ran.
    static bool begin(const String& defaultHostname);

    static const NetworkConfigData& get();
    static bool save(const NetworkConfigData& data);
    static bool hasCredentials();

    // Clears ssid/password only (Section 9 "provisioning reset"); keeps hostname and
    // retry tuning. Used when re-entering provisioning without a full factory reset.
    static bool clearCredentials();

    // Adds ssid/password to the saved list (updating the password in place if that
    // ssid is already saved), evicting the oldest entry if already at
    // MAX_SAVED_NETWORKS. Does not change the current primary — that only changes on
    // an actual successful connection (see WiFiManager::connectBestKnown()).
    static bool addNetwork(const String& ssid, const String& password);
    static bool removeNetwork(const String& ssid);

    // Makes ssid/password the primary (what WiFiManager's existing connect/reconnect
    // logic uses) and ensures it's also in the saved list. Called by
    // WiFiManager::connectBestKnown() after a successful connection to a non-primary
    // saved network, and by provisioning when a fresh SSID/password is submitted.
    static bool setPrimary(const String& ssid, const String& password);

    static bool setWifiFallbackEnabled(bool enabled);

private:
    static NetworkConfigData current;
    static bool loadFromDisk();
    static void migrate(int fromVersion);
};

}  // namespace CarSentinel
