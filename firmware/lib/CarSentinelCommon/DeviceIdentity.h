#pragma once

#include <Arduino.h>

// Derives a default, no-provisioning-required node ID from the chip's MAC address, so a
// freshly flashed device is identifiable before BLE/AP provisioning (Phase 2) assigns a
// real name. This is a fallback, never the permanent identity.
namespace CarSentinel {

class DeviceIdentity {
public:
    // e.g. "NODE-A1B2C3" from the last 3 MAC bytes. prefix is role-dependent
    // ("GATEWAY" vs "NODE") so an unprovisioned device's role is still visible in logs.
    static String generateDefaultNodeId(const char* prefix);

    // Full MAC as a colon-separated string, for diagnostics/registry identification.
    static String macAddress();
};

}  // namespace CarSentinel
