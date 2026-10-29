#pragma once

#include <Arduino.h>

// Shared-key material for ESP-NOW message authentication (HMAC-SHA256, see
// EspNowProtocol) — Section 41: "authenticated node identity, message validation... no
// credentials in ESP-NOW broadcast." The key itself is never transmitted over the air,
// only used locally to sign/verify; only its HMAC output travels on the wire.
//
// SECURITY LIMITATION (documented, not hidden): every device ships with the same
// compiled-in default key until a real per-deployment key is generated and copied to
// every node's /config/espnow_psk.bin. There is no key-distribution mechanism yet (that
// would need BLE/AP provisioning to grow an "ESP-NOW network key" field, or a future
// phase). Ship-then-change-the-default is a known-weak starting point, called out loudly
// in logs and here rather than presented as secure.
namespace CarSentinel {

class EspNowSecurity {
public:
    static constexpr size_t KEY_LEN = 32;

    // Loads /config/espnow_psk.bin if present; otherwise writes the insecure compiled-in
    // default there and logs a loud warning. Assumes LittleFS is already mounted
    // (DeviceConfig::begin() must run first).
    static void begin();

    static const uint8_t* getKey();
    static size_t getKeyLength();

private:
    static uint8_t key[KEY_LEN];
};

}  // namespace CarSentinel
