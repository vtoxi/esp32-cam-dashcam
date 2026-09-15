#pragma once

#include <Arduino.h>

// Persistent, versioned device configuration. This is the concrete implementation of
// the project's core rule: node identity/role/hardware profile are runtime data on
// LittleFS, not compiled into firmware. Phase 1 only models identity/role/hardware
// profile fields; Wi-Fi, sensors, security, etc. are added by later phases as new
// top-level fields behind the same schemaVersion/migration mechanism, not as a rewrite.
namespace CarSentinel {

// Bump when the on-disk JSON shape changes, and add a case to migrate() below.
// A firmware update must never silently invalidate an existing config file (Section 39).
constexpr int CONFIG_SCHEMA_VERSION = 1;

enum class DeviceRole : uint8_t {
    UNASSIGNED = 0,
    GATEWAY,
    CAMERA,
    SENSOR,
    DISPLAY,
    VEHICLE_CONTROLLER
};

const char* roleToString(DeviceRole role);
DeviceRole roleFromString(const String& value);

struct DeviceConfigData {
    int schemaVersion = CONFIG_SCHEMA_VERSION;
    String nodeId;               // defaults to MAC-derived id until provisioned (Phase 2)
    String displayName;          // human-readable name, e.g. "Front Camera"
    DeviceRole role = DeviceRole::UNASSIGNED;
    String hardwareProfile = "UNKNOWN";  // resolved against configs/hardware/*.json in Phase 3
    String firmwareVersion;      // populated from CARSENTINEL_FIRMWARE_VERSION at boot
};

class DeviceConfig {
public:
    // Mounts LittleFS (formatting on first boot if unformatted), loads /config/device.json
    // if present, otherwise creates it from defaults derived from defaultNodeIdPrefix.
    // Returns false only on unrecoverable filesystem failure.
    static bool begin(const char* defaultNodeIdPrefix, DeviceRole compiledDefaultRole);

    static const DeviceConfigData& get();

    // Persists the given data to /config/device.json (schemaVersion is forced to
    // CONFIG_SCHEMA_VERSION on save).
    static bool save(const DeviceConfigData& data);

    // Section 40: clears node configuration, restores defaults, retains firmware itself
    // (does not touch anything on the filesystem outside /config/).
    static bool factoryReset(const char* defaultNodeIdPrefix, DeviceRole compiledDefaultRole);

private:
    static DeviceConfigData current;
    static bool loadFromDisk();
    static void applyDefaults(const char* defaultNodeIdPrefix, DeviceRole compiledDefaultRole);
    // Migrates a just-loaded JSON document in place from its stored schemaVersion up to
    // CONFIG_SCHEMA_VERSION. No-op today (schema is at v1) but keeps the seam open so a
    // future bump doesn't require redesigning the load path.
    static void migrate(int fromVersion);
};

}  // namespace CarSentinel
