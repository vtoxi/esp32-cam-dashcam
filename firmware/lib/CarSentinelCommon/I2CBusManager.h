#pragma once

#include <Arduino.h>

// Thin wrapper over up to two hardware I2C peripherals (Wire, Wire1), needed because the
// gateway's two SSD1306 units are both confirmed at address 0x3C with no address-select
// jumper (docs/wiring/SSD1306.md) — they cannot share one bus. Section 52's shared I2C
// bus abstraction, scoped to what Phase 3 actually needs: init + presence probing.
// Full driver logic (rendering pages, reading IMU registers) is Phase 9/13.
namespace CarSentinel {

class I2CBusManager {
public:
    // busIndex 0 = Wire (primary), 1 = Wire1 (secondary, ESP32-S3 only — Wire1 is not
    // available on classic ESP32, so camera nodes must only ever use bus 0).
    static bool begin(uint8_t busIndex, int sdaGpio, int sclGpio);

    // Zero-length write probe — true if a device ACKs at that address on that bus.
    static bool isPresent(uint8_t busIndex, uint8_t address);
};

}  // namespace CarSentinel
