#pragma once

#include <Arduino.h>

// Minimal diagnostics for Phase 1: the fields every later phase's NODE_STATUS/telemetry
// messages will build on (Section 13, 58). No ESP-NOW/network transport yet — this is
// local-only, logged at boot and periodically.
namespace CarSentinel {

struct DiagnosticsSnapshot {
    unsigned long uptimeMs;
    uint32_t freeHeap;
    uint32_t minFreeHeap;   // lowest free-heap watermark since boot — catches slow leaks
    String resetReason;
};

class Diagnostics {
public:
    static void begin();
    static DiagnosticsSnapshot snapshot();

    // Logs the current snapshot at INFO level. Called periodically from loop() and once
    // at end of setup().
    static void logSnapshot(const char* tag);

    // True if the device is healthy enough to keep operating (Section 58 healthCheck()).
    // Phase 1 only checks free heap; later phases add camera/SD/sensor/network checks.
    static bool healthCheck();

    // Runs once at boot, logs pass/fail per subsystem checked so far (Section 58
    // selfTest()). Phase 1 only has the filesystem/config subsystem to check.
    static bool selfTest();

private:
    static String resetReasonToString();
};

}  // namespace CarSentinel
