#pragma once

#include <Arduino.h>

// Persistent, per-device capability enable flags + GPIO assignments (Section 2.2/49).
// This is what actually gates hardware init: a sensor is only touched if its "enabled"
// flag is true AND its GPIO(s) are configured (>= 0). Separate file
// (/config/capabilities.json) from device.json/network.json so enabling a sensor never
// risks corrupting identity or Wi-Fi state.
namespace CarSentinel {

constexpr int CAPABILITIES_SCHEMA_VERSION = 1;
constexpr int GPIO_UNCONFIGURED = -1;

struct CapabilitiesConfigData {
    int schemaVersion = CAPABILITIES_SCHEMA_VERSION;

    bool camera = false;   // presence only in Phase 3; esp32-camera init lands Phase 4
    bool sd = false;

    bool rcwl = false;
    int rcwlGpio = GPIO_UNCONFIGURED;

    bool dht = false;
    int dhtGpio = GPIO_UNCONFIGURED;

    bool gps = false;
    int gpsRxGpio = GPIO_UNCONFIGURED;
    int gpsTxGpio = GPIO_UNCONFIGURED;

    bool imu = false;
    int imuSdaGpio = GPIO_UNCONFIGURED;
    int imuSclGpio = GPIO_UNCONFIGURED;

    bool display = false;
    int displayCount = 0;
    int display1SdaGpio = GPIO_UNCONFIGURED;
    int display1SclGpio = GPIO_UNCONFIGURED;
    int display2SdaGpio = GPIO_UNCONFIGURED;   // second I2C bus — see docs/wiring/SSD1306.md
    int display2SclGpio = GPIO_UNCONFIGURED;

    // Phase 18 — Vehicle Integration. A digital ignition-sense input (vehicle 12V
    // ignition-switched line, stepped down through a voltage divider/optocoupler to
    // 3.3V logic — never wired directly to a vehicle's 12V rail) that lets security
    // mode react to the vehicle's actual ignition state instead of only inferring it
    // from GPS speed/IMU movement (Section 44). Off by default on every hardware
    // profile until real vehicle wiring is confirmed (Section 45/46) — disabled=false,
    // gpio=GPIO_UNCONFIGURED means "not wired, fall back to GPS/IMU auto-detection".
    bool ignition = false;
    int ignitionGpio = GPIO_UNCONFIGURED;
};

class CapabilitiesConfig {
public:
    // defaults is what gets persisted if /config/capabilities.json doesn't exist yet —
    // caller supplies it (typically from HardwareProfiles::defaultCapabilitiesForProfile)
    // so this class has no compiled-in board knowledge of its own.
    static bool begin(const CapabilitiesConfigData& defaults);

    static const CapabilitiesConfigData& get();
    static bool save(const CapabilitiesConfigData& data);

private:
    static CapabilitiesConfigData current;
    static bool loadFromDisk();
    static void migrate(int fromVersion);
};

}  // namespace CarSentinel
