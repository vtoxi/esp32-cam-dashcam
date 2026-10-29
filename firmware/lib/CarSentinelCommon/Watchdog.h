#pragma once

#include <Arduino.h>

// Thin wrapper over ESP-IDF's task watchdog (esp_task_wdt). Registers the calling task
// (the Arduino loop task, when called from setup()) and must be fed from loop() —
// letting it expire deliberately panics/resets the device rather than hanging silently,
// per Section 59 (avoid uncontrolled reboot loops, but do recover from real hangs).
namespace CarSentinel {

class Watchdog {
public:
    // timeoutSeconds: how long loop() can go without calling feed() before a reset.
    static void begin(uint32_t timeoutSeconds = 10);
    static void feed();
};

}  // namespace CarSentinel
