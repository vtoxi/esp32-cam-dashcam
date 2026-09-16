#pragma once

#include <Arduino.h>
#include "IncidentCorrelator.h"

// Phase 15 (AI Framework) + Phase 16 (AI Security Assistance) — gateway-only.
//
// An honest scoping decision, not a shortcut: real on-device ML inference (esp-dl's
// face/human detection, present in this toolchain's prebuilt libs) was evaluated and
// rejected for the camera nodes specifically — Phase 14 already showed classic ESP32's
// IRAM budget is razor-thin even without a neural-net model loaded, and esp-dl's memory
// footprint (weights, PSRAM-hungry intermediate buffers) would very likely reopen that
// exact link failure. The gateway (ESP32-S3, 8MB PSRAM, ~20% flash used) has real
// headroom, but nodes don't currently transmit full images to it (evidence stays local
// to each node's SD card by design — see IncidentCorrelator.h).
//
// So: a real *framework* (a pluggable analyzer slot, swappable at runtime via
// setAnalyzer()) whose only implementation today is a transparent, documented
// heuristic — not a black box, and not dressed up as more than it is. If a future
// hardware revision gives a node (or the gateway, fed real image data) enough headroom
// for genuine inference, that becomes a second ThreatAnalyzer registered here; nothing
// else in the incident pipeline needs to change.
namespace CarSentinel {

// Not named LOW/HIGH — those collide with Arduino core's #define LOW 0x0 / #define HIGH
// 0x1 digital-pin-state macros (a real compile failure caught building this, not a
// hypothetical).
enum class ThreatSeverity : uint8_t {
    THREAT_LOW = 0,        // likely benign (e.g. a single brief motion trigger, no corroboration)
    THREAT_SUSPICIOUS = 1, // matches today's pre-Phase-15 default
    THREAT_HIGH = 2,       // multiple corroborating signals or a real impact event
};

const char* threatSeverityToString(ThreatSeverity s);

struct ThreatAssessment {
    ThreatSeverity severity = ThreatSeverity::THREAT_SUSPICIOUS;
    uint8_t confidencePercent = 50;  // 0-100, this analyzer's own confidence in its call
    String reasoning;                // short human-readable "why" — never a bare number
};

// corroboratingNodes: how many OTHER nodes' cameras also captured this same event
// (IncidentRecord::evidenceCount at assessment time) — multi-camera agreement is the
// strongest available signal this project has (Section 18).
typedef ThreatAssessment (*ThreatAnalyzer)(const IncidentTriggerInfo& trigger,
                                            uint8_t corroboratingNodes);

class AIThreatFramework {
public:
    // Combines whatever signals the trigger actually carries (IMU impact magnitude vs.
    // configured thresholds, GPS movement while the vehicle should be stationary,
    // multi-camera corroboration) into a severity + confidence + plain-English reason.
    // Never claims a crash/theft occurred — same "measurements, not verdicts" posture
    // as ImuManager's own impact detection.
    static ThreatAssessment heuristicAnalyzer(const IncidentTriggerInfo& trigger,
                                               uint8_t corroboratingNodes);

    // Runs the currently-registered analyzer (heuristicAnalyzer by default).
    static ThreatAssessment assess(const IncidentTriggerInfo& trigger, uint8_t corroboratingNodes);

    // Swaps in a different analyzer at runtime — the actual "framework" part: nothing
    // else needs to change to try a different scoring approach later.
    static void setAnalyzer(ThreatAnalyzer analyzer);

private:
    static ThreatAnalyzer activeAnalyzer;
};

}  // namespace CarSentinel
