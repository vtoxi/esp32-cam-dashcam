#pragma once

#include <Arduino.h>

// MPU6050 accelerometer/gyroscope driver (Section 22/23) via direct register access
// over I2CBusManager — no external MPU6050 library, since the register map is small,
// well-documented, and stable, and this avoids resolving yet another third-party
// package's API blind (several earlier phases already needed a compile-fix pass after
// guessing a library/ESP-IDF API; this sidesteps that risk entirely for a driver this
// simple).
//
// Section 23 is explicit: this must never claim "crash detected." It reports
// IMPACT_EVENT-worthy threshold crossings with raw measurements attached — interpretation
// is left to whoever reads the log/event, and thresholds are runtime-configurable
// (persisted), not hardcoded, because they're expected to need tuning against real
// driving data.
namespace CarSentinel {

struct ImuReading {
    bool valid = false;
    float accelXg = 0, accelYg = 0, accelZg = 0;      // g (9.80665 m/s^2), default +/-2g range
    float gyroXdps = 0, gyroYdps = 0, gyroZdps = 0;   // deg/s, default +/-250 deg/s range
    float accelMagnitudeG = 0;                         // sqrt(x^2+y^2+z^2) — ~1g at rest
    float gyroMagnitudeDps = 0;                        // sqrt(x^2+y^2+z^2) — ~0 at rest
};

struct ImuThresholdsData {
    // Placeholder defaults (Section 23: "thresholds must be configurable and later tuned
    // using real driving data") — not derived from any actual measurement yet.
    float accelMagnitudeG = 2.5f;
    float gyroMagnitudeDps = 200.0f;
    unsigned long cooldownMs = 5000;  // don't re-fire every loop tick during one sustained event
};

class ImuManager {
public:
    // Wakes the MPU6050 (clears sleep bit) and loads/creates persisted thresholds at
    // /config/imu_thresholds.json. Returns false if the device doesn't ACK.
    static bool begin(uint8_t busIndex, uint8_t i2cAddress = 0x68);
    static bool isInitialized();

    // Performs a fresh register read. Returns a reading with valid=false on I2C failure.
    static ImuReading read();

    // True at most once per cooldownMs when a reading's magnitude exceeds a configured
    // threshold. Section 23: report IMPACT_EVENT + raw values, never "crash detected."
    static bool checkImpact(const ImuReading& reading);

    static const ImuThresholdsData& getThresholds();
    static bool setThresholds(const ImuThresholdsData& thresholds);

private:
    static bool initialized;
    static uint8_t bus;
    static uint8_t address;
    static ImuThresholdsData thresholds;
    static unsigned long lastImpactMs;

    static bool loadThresholds();
    static bool saveThresholds();
};

}  // namespace CarSentinel
