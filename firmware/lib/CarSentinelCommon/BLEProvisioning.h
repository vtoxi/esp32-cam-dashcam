#pragma once

#include <Arduino.h>

// BLE GATT-based provisioning (Section 9, primary provisioning method). Exposes a
// custom service with write-only credential characteristics (Section 9: "BLE must not
// expose sensitive credentials unnecessarily" — password is write-only, never
// readable back over the air) plus read/write node identity fields and a commit
// trigger that persists everything and reboots.
//
// Built on NimBLE-Arduino (lighter RAM/flash footprint than the stock ESP32 BLE stack,
// important on the AI-Thinker ESP32-CAM's limited heap). API calls here target
// NimBLE-Arduino 1.4.x (std::string-based characteristic values) — see
// firmware/platformio.ini for the pinned version. If the resolved library version
// differs, the write-callback implementations in BLEProvisioning.cpp are the first
// place to check for API mismatches.
namespace CarSentinel {

class BLEProvisioning {
public:
    // deviceName is advertised, e.g. "CarSentinel-A1B2C3".
    static void begin(const String& deviceName);
    static void stop();
    static bool isActive();

    // True once the client has written the Commit characteristic; caller should persist
    // (already done internally on commit — see .cpp) and then stop()+reboot.
    static bool isCommitted();

private:
    static bool active;
    static bool committed;
};

}  // namespace CarSentinel
