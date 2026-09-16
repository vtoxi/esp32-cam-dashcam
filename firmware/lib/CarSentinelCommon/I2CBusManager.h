#pragma once

#include <Arduino.h>

// Thin wrapper over up to two hardware I2C peripherals (Wire, Wire1), needed because the
// gateway's two SSD1306 units are both confirmed at address 0x3C with no address-select
// jumper (docs/wiring/SSD1306.md) — they cannot share one bus. Section 52's shared I2C
// bus abstraction. Phase 9 added register read/write for the MPU6050 driver
// (ImuManager) — OLED page rendering (Phase 13) will likely go through a dedicated
// display library instead, not these raw register primitives.
namespace CarSentinel {

class I2CBusManager {
public:
    // busIndex 0 = Wire (primary), 1 = Wire1 (secondary, ESP32-S3 only — Wire1 is not
    // available on classic ESP32, so camera nodes must only ever use bus 0).
    static bool begin(uint8_t busIndex, int sdaGpio, int sclGpio);

    // Zero-length write probe — true if a device ACKs at that address on that bus.
    static bool isPresent(uint8_t busIndex, uint8_t address);

    static bool writeRegister(uint8_t busIndex, uint8_t address, uint8_t reg, uint8_t value);

    // Reads len bytes starting at startReg into buffer. Returns false on any I2C error
    // (device absent, NACK, etc.) — buffer contents are undefined in that case.
    static bool readRegisters(uint8_t busIndex, uint8_t address, uint8_t startReg,
                               uint8_t* buffer, size_t len);
};

}  // namespace CarSentinel
