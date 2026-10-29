#include "MotionEventEngine.h"
#include "Logger.h"

namespace CarSentinel {

static const char* TAG = "MotionEventEngine";

MotionEventConfig MotionEventEngine::cfg;
unsigned long MotionEventEngine::triggerTimestamps[MotionEventEngine::MAX_SAMPLES] = {0};
uint8_t MotionEventEngine::sampleCount = 0;
bool MotionEventEngine::lastRaw = false;
unsigned long MotionEventEngine::cooldownUntil = 0;

void MotionEventEngine::begin(const MotionEventConfig& config) {
    cfg = config;
    sampleCount = 0;
    lastRaw = false;
    cooldownUntil = 0;
    Logger::info(TAG, "Motion event engine: confirmationWindowMs=" + String(cfg.confirmationWindowMs) +
                 " minimumEvents=" + String(cfg.minimumEvents) +
                 " cooldownSeconds=" + String(cfg.cooldownSeconds));
}

void MotionEventEngine::recordTrigger(unsigned long now) {
    // Ring-buffer insert: shift out the oldest sample if full. MAX_SAMPLES is a small
    // fixed bound (8), not dynamic — RCWL rising edges within a debounce window are
    // never going to need more headroom than that.
    if (sampleCount < MAX_SAMPLES) {
        triggerTimestamps[sampleCount++] = now;
    } else {
        for (uint8_t i = 1; i < MAX_SAMPLES; i++) {
            triggerTimestamps[i - 1] = triggerTimestamps[i];
        }
        triggerTimestamps[MAX_SAMPLES - 1] = now;
    }
}

uint8_t MotionEventEngine::countWithinWindow(unsigned long now) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < sampleCount; i++) {
        if (now - triggerTimestamps[i] <= cfg.confirmationWindowMs) {
            count++;
        }
    }
    return count;
}

bool MotionEventEngine::update(bool rawTriggered) {
    unsigned long now = millis();

    // Rising edge only — count each HIGH transition as one "trigger", not every loop
    // tick spent HIGH (RCWL output can stay HIGH for its retrigger interval).
    if (rawTriggered && !lastRaw) {
        recordTrigger(now);
    }
    lastRaw = rawTriggered;

    if (now < cooldownUntil) {
        return false;
    }

    if (countWithinWindow(now) >= cfg.minimumEvents) {
        cooldownUntil = now + (cfg.cooldownSeconds * 1000UL);
        sampleCount = 0;  // reset aggregation window after a confirmed event
        Logger::info(TAG, "Confirmed motion event (cooldown " + String(cfg.cooldownSeconds) + "s)");
        return true;
    }

    return false;
}

}  // namespace CarSentinel
