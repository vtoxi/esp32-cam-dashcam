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

constexpr int NETWORK_SCHEMA_VERSION = 1;

struct NetworkConfigData {
    int schemaVersion = NETWORK_SCHEMA_VERSION;
    String ssid;
    String password;
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

private:
    static NetworkConfigData current;
    static bool loadFromDisk();
    static void migrate(int fromVersion);
};

}  // namespace CarSentinel
