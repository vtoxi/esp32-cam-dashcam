#pragma once

#include <Arduino.h>

// Phase 18 — Vehicle Integration. Reads a digital ignition-sense line (see
// CapabilitiesConfig.h for the wiring note — never a direct 12V connection) with basic
// debounce. Deliberately minimal: no OBD-II/CAN bus support this phase — that requires
// vehicle-specific PID knowledge and a confirmed physical harness neither of which is
// available yet (see docs/IMPLEMENTATION_PLAN.md Phase 18). A simple ignition-switched
// 12V line (present on essentially every vehicle's accessory/ignition wiring) is the
// one piece of "vehicle integration" this project can honestly implement and test
// without that hardware in hand.
namespace CarSentinel {

class IgnitionSense {
public:
    static void begin(int gpio);

    // Debounced reading — true once the pin has held HIGH for DEBOUNCE_MS
    // continuously. Call every loop(); cheap (one digitalRead + a millis() compare).
    static bool isOn();

    static bool isConfigured();

private:
    static const unsigned long DEBOUNCE_MS = 300;
    static int pin;
    static bool debouncedState;
    static bool rawStateAtLastChange;
    static unsigned long lastChangeMs;
};

}  // namespace CarSentinel
