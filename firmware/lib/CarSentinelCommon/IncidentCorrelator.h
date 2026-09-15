#pragma once

#include <Arduino.h>

// Gateway-only, in-memory correlation of a triggering camera's motion event with the
// synchronized captures other cameras send in response (Section 18). Deliberately
// lightweight — an incident ID, a trigger, and a list of related node IDs within a
// short correlation window, nothing persisted and no lifecycle states. The full
// Section 11 incident lifecycle (DETECTED -> CONFIRMING -> ACTIVE -> ... -> CLOSED),
// persistence, GPS/IMU/DHT association, and storage cleanup are Phase 11 — this class
// only proves multiple cameras' evidence gets grouped under one incident.
namespace CarSentinel {

struct IncidentRecord {
    bool active = false;
    String incidentId;
    String triggerNodeId;
    String triggerEventId;
    unsigned long createdAtMs = 0;

    static const uint8_t MAX_RELATED = 6;
    String relatedNodeIds[MAX_RELATED];
    uint8_t relatedCount = 0;
};

class IncidentCorrelator {
public:
    static const uint8_t MAX_INCIDENTS = 4;
    // How long an incident stays open for other cameras' CAPTURE_RESULT to arrive
    // before it's closed and logged. Generous relative to ESP-NOW's typical
    // sub-second latency, since a peer camera also has to actually capture a JPEG
    // (tens to low hundreds of ms) before it can report back.
    static const unsigned long CORRELATION_WINDOW_MS = 8000;

    static void begin();

    // Opens a new incident for this trigger (or returns the existing one's ID if
    // somehow called twice for the same trigger — shouldn't normally happen since each
    // motion event has a unique eventId).
    static String startIncident(const String& triggerNodeId, const String& triggerEventId);

    // Adds a related node's capture to the matching open incident. Returns false if no
    // open incident matches (e.g. its correlation window already closed) — a known
    // Phase 7 limitation; Phase 11's persistent incident store would let a late capture
    // still attach correctly.
    static bool addRelated(const String& triggerNodeId, const String& triggerEventId,
                            const String& relatedNodeId);

    // Call every loop(): closes and logs a summary for any incident whose correlation
    // window has elapsed, freeing its slot for reuse.
    static void loop();

private:
    static IncidentRecord incidents[MAX_INCIDENTS];
    static uint32_t nextIncidentNumber;

    static IncidentRecord* findOpen(const String& triggerNodeId, const String& triggerEventId);
    static void closeAndLog(IncidentRecord& inc);
};

}  // namespace CarSentinel
