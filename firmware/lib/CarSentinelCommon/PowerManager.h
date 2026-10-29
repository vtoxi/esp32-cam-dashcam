#pragma once

#include <Arduino.h>
#include "SecurityModeConfig.h"

// Phase 17 — Low-Power Parked Mode. Scoped deliberately small: full deep-sleep between
// RCWL motion events was considered and rejected for this phase — it would tear down
// the Wi-Fi/ESP-NOW connection and camera state that Phase 4/5's already-verified
// motion -> capture -> evidence pipeline depends on being immediately available, and
// re-establishing an ESP-NOW link from cold on every wake risks missing the very
// motion event this device exists to catch. Real, unavoidable trade-off documented here
// rather than hidden: this saves power via Wi-Fi modem sleep (radio duty-cycles between
// beacon intervals instead of staying fully receive-active) whenever the current
// security mode is PARKED — every other mode keeps the radio fully awake, since
// DRIVING/DISARMED/SERVICE have no reason to conserve power the way a vehicle sitting
// parked for hours does. Applies to both roles: both the gateway and every node track
// SecurityModeConfig independently already (Section 16), so this is a one-line hook at
// the same place each already reacts to a mode change.
namespace CarSentinel {

class PowerManager {
public:
    // Call whenever SecurityModeConfig's mode changes (and once at boot with the
    // current mode) — toggles WiFi.setSleep() accordingly. Safe to call redundantly;
    // WiFi.setSleep() is a cheap no-op if the state isn't actually changing.
    static void applyModeChange(SecurityMode mode);
};

}  // namespace CarSentinel
