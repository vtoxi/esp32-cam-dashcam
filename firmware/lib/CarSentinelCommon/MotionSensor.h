#pragma once

#include <Arduino.h>

// RCWL-0516 digital-output wrapper. Phase 3 scope only: pin init + raw read. Debounce,
// cooldown, confirmation-window aggregation (Section 17's full config) land in Phase 4
// when this feeds the local security engine.
namespace CarSentinel {

class MotionSensor {
public:
    static void begin(int gpio);
    static bool isTriggered();

private:
    static int pin;
};

}  // namespace CarSentinel
