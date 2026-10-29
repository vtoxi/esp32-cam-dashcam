#pragma once

#include <Arduino.h>

// Section 16's four security modes, persisted so a mode set before a reboot (or a
// power-cycle mid-drive) survives it, and shared between gateway and node — the
// gateway is the only device with GPS/IMU so it's the one that runs auto-detection
// (Section 44), but the mode itself is meaningful on every device (it gates a node's
// own motion-alert behavior).
namespace CarSentinel {

enum class SecurityMode : uint8_t {
    DISARMED = 0,  // security alerts disabled entirely
    DRIVING = 1,   // motion alerts suppressed (road vibration isn't a security event);
                   // impact detection (Phase 9) stays active — that's exactly a driving-mode concern
    PARKED = 2,    // full security monitoring — the default
    SERVICE = 3    // diagnostics/testing, alerts suppressed like DISARMED
};

const char* securityModeToString(SecurityMode mode);
SecurityMode securityModeFromString(const String& value);

// True for modes where a node should still respond normally to a motion trigger.
// Centralized here so node_main.cpp and any future caller agree on the same rule.
bool securityModeAllowsMotionAlerts(SecurityMode mode);

class SecurityModeConfig {
public:
    // Loads /config/security_mode.json if present; otherwise creates it with the
    // default (PARKED, no manual override).
    static void begin();

    static SecurityMode getMode();

    // True once a mode has been set explicitly (serial MODE command, or a gateway
    // CONFIG_UPDATE on a node) rather than by the gateway's own GPS/IMU auto-detection.
    // The gateway's auto-detection loop checks this and stays out of the way while true.
    static bool isManualOverride();

    static bool setMode(SecurityMode mode, bool manual);

private:
    static SecurityMode mode;
    static bool manualOverride;
    static bool save();
};

}  // namespace CarSentinel
