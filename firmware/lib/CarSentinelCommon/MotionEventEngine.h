#pragma once

#include <Arduino.h>

// Turns raw RCWL-0516 HIGH/LOW samples into a confirmed motion event, per Section 17:
// debounce, a confirmation window requiring a minimum number of repeated triggers
// (RCWL alone doesn't prove a person is present — this is evidence, not certainty), and
// a cooldown so one loitering trigger doesn't spam events. Deliberately not a library
// with named "sensitivity" presets — thresholds are config, tuned empirically.
namespace CarSentinel {

struct MotionEventConfig {
    uint32_t confirmationWindowMs = 3000;
    uint8_t minimumEvents = 2;
    uint32_t cooldownSeconds = 30;
};

class MotionEventEngine {
public:
    static void begin(const MotionEventConfig& config);

    // Call every loop() tick with the current raw RCWL read. Returns true exactly once
    // when a confirmed event fires (debounced, minimum-events met, not in cooldown).
    static bool update(bool rawTriggered);

private:
    static MotionEventConfig cfg;
    static const uint8_t MAX_SAMPLES = 8;
    static unsigned long triggerTimestamps[MAX_SAMPLES];
    static uint8_t sampleCount;
    static bool lastRaw;
    static unsigned long cooldownUntil;

    static void recordTrigger(unsigned long now);
    static uint8_t countWithinWindow(unsigned long now);
};

}  // namespace CarSentinel
