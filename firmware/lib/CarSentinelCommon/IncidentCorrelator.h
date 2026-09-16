#pragma once

#include <Arduino.h>

// Gateway-only incident engine. Phase 7 gave this class in-memory-only correlation (an
// ID, a trigger, a list of related node IDs within a short window). Phase 11 adds what
// Section 11/24 actually ask for: the full lifecycle, GPS/IMU/DHT association, evidence
// references, persistence (survives reboot), and storage retention.
//
// Deliberately NOT here: the gateway has no SD card (see docs/HARDWARE.md — its
// hardware profile has sd=false), so incident *records* (metadata + associations +
// evidence references) persist to the gateway's internal flash (LittleFS) under
// /incidents/. The actual JPEG evidence stays where it always has — on each
// contributing node's own SD card (EvidenceManager, Phase 4) — an incident record here
// only references it by (nodeId, localEventId), it never copies image bytes onto the
// gateway. Sensor-agnostic by design: this class has no dependency on GpsManager/
// ImuManager — callers (gateway_main.cpp) gather that data and pass it in via
// IncidentTriggerInfo, keeping the incident engine itself decoupled from specific
// sensor drivers.
namespace CarSentinel {

enum class IncidentState : uint8_t {
    DETECTED = 0,
    CONFIRMING = 1,
    ACTIVE = 2,
    EVIDENCE_COLLECTION = 3,
    NOTIFICATION = 4,
    CLOSED = 5
};

const char* incidentStateToString(IncidentState state);

// Association data gathered by the caller at the moment a trigger fires — this class
// only stores what it's given, it never reads a sensor itself.
struct IncidentTriggerInfo {
    String triggerType;   // "MOTION_DETECTED" or "IMPACT_EVENT"
    String severity;
    bool hasImage = false;  // did the triggering node itself capture an image

    bool hasGps = false;
    double gpsLat = 0, gpsLon = 0;
    float gpsSpeedKmph = 0;

    bool hasImu = false;
    float imuAccelG = 0, imuGyroDps = 0;

    bool hasEnv = false;  // DHT reading from the triggering node's payload
    float envTempC = 0, envHumidity = 0;
};

struct IncidentEvidenceRef {
    String nodeId;
    String localEventId;  // that node's own EvidenceManager event ID, e.g. "EVT-000003"
    bool hasImage = false;
};

struct IncidentRecord {
    bool active = false;  // in-memory correlation slot in use (independent of persisted state)
    String incidentId;
    IncidentState state = IncidentState::DETECTED;
    String triggerNodeId;
    String triggerEventId;
    unsigned long createdAtMs = 0;
    IncidentTriggerInfo trigger;

    static const uint8_t MAX_EVIDENCE = 6;
    IncidentEvidenceRef evidence[MAX_EVIDENCE];
    uint8_t evidenceCount = 0;
};

class IncidentCorrelator {
public:
    static const uint8_t MAX_INCIDENTS = 4;  // concurrent open (in-memory) incidents
    static const unsigned long CORRELATION_WINDOW_MS = 8000;

    // Storage retention (Section 27, applied to the gateway's incident-record flash
    // usage, not SD — there is none here): keeps only the most recent N persisted
    // incident files, deleting the oldest once the cap is exceeded.
    static const uint16_t MAX_STORED_INCIDENTS = 100;

    // Mounts LittleFS's /incidents directory (creating it if missing) and recovers the
    // next-incident-number counter so IDs stay unique across reboots.
    static void begin();

    // Opens a new incident, immediately advances it DETECTED -> ACTIVE ->
    // EVIDENCE_COLLECTION (Section 19's states this project can honestly claim to reach
    // synchronously — NOTIFICATION is entered only once the window closes, see loop()),
    // records the trigger's own evidence reference, and persists. Returns the incident
    // ID (or the existing one's, if called twice for the same trigger).
    static String startIncident(const String& triggerNodeId, const String& triggerEventId,
                                 const IncidentTriggerInfo& info);

    // Adds a related node's capture to the matching open incident and re-persists.
    // Returns false if no open incident matches (its correlation window may have
    // already closed) — a real, logged limitation: a late CAPTURE_RESULT is dropped,
    // not retroactively attached to the now-closed record.
    static bool addRelated(const String& triggerNodeId, const String& triggerEventId,
                            const String& relatedNodeId, const String& localEventId, bool hasImage);

    // Call every loop(): closes any incident whose correlation window has elapsed —
    // transitions to NOTIFICATION (logged placeholder; Phase 12 is what actually acts
    // on it) then CLOSED, persists the final record, and frees the in-memory slot. Also
    // enforces MAX_STORED_INCIDENTS.
    static void loop();

private:
    static IncidentRecord incidents[MAX_INCIDENTS];
    static uint32_t nextIncidentNumber;

    static IncidentRecord* findOpen(const String& triggerNodeId, const String& triggerEventId);
    static void closeIncident(IncidentRecord& inc);
    static bool persist(const IncidentRecord& inc);
    static void enforceRetention();
};

}  // namespace CarSentinel
