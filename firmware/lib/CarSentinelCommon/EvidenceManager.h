#pragma once

#include <Arduino.h>

// Local evidence storage on the onboard microSD (Section 27 layout, Section 55 event
// schema — simplified for Phase 4: no GPS/IMU on a camera node, no relatedNodes since
// ESP-NOW doesn't exist yet, no real wall-clock time since NTP is Phase 57). Storage
// looks like:
//   /security/events/<EVENT_ID>/event.json
//   /security/events/<EVENT_ID>/<nodeId>_001.jpg
// No retention/cleanup policy yet (Section 27's maxStoragePercent/retentionDays land
// with the full incident engine, Phase 11) — Phase 4 only proves capture-to-disk works.
namespace CarSentinel {

struct EnvironmentReading {
    bool valid = false;
    float temperatureC = NAN;
    float humidityPercent = NAN;
};

class EvidenceManager {
public:
    // Creates /security/ and /security/events/ if missing. Call once after SdStorage::begin()
    // succeeds; a false return means evidence capture is unavailable this boot (Section 59
    // — caller continues without it, does not halt).
    static bool begin();

    // Allocates a new event ID (persisted counter survives reboot — no NTP yet so IDs
    // are sequential, not timestamp-based) and writes event.json. Returns "" on failure.
    static String createEvent(const String& nodeId, const String& eventType,
                               const String& severity, const EnvironmentReading& env);

    // Appends one JPEG evidence file to an already-created event's directory.
    static bool attachImage(const String& eventId, const uint8_t* data, size_t len);

    static bool isAvailable();

private:
    static bool available;
    static int nextEventNumber();
};

}  // namespace CarSentinel
