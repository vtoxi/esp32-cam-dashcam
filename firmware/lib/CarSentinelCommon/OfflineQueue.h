#pragma once

#include <Arduino.h>
#include "EspNowProtocol.h"

// docs/NETWORK.md Section 4/9: the standalone-mode piece that didn't exist before this
// implementation pass — "events generated while standalone must be queued locally and
// delivered once the gateway becomes reachable again." Persisted (survives reboot,
// same reasoning as IncidentCorrelator's persisted records) and bounded (never grows
// forever — same "evict oldest" pattern already used by NetworkConfig's saved-network
// list and IncidentCorrelator's retention cap).
//
// Deliberately simple: ESP-NOW-delivery only in this pass. A queued event is flushed
// by attempting delivery to the gateway's MAC once TransportManager reports
// ESPNOW_CONNECTED again. Wi-Fi-fallback delivery for queued events is a known,
// documented gap (see docs/NETWORK.md Section 9 / docs/IMPLEMENTATION_PLAN.md) — not
// implemented here, since it needs a gateway-side HTTP ingestion endpoint and a way
// for a node to discover the gateway's Wi-Fi IP, neither of which exist yet.
namespace CarSentinel {

class OfflineQueue {
public:
    static const uint16_t MAX_QUEUED = 20;

    // Mounts LittleFS's queue file if present. Assumes LittleFS is already mounted
    // (DeviceConfig::begin() must run first) — same convention as every other
    // persisted-config class in this project.
    static void begin();

    // Adds an event to the queue (evicting the oldest if already at MAX_QUEUED).
    // Persists immediately — same "durable before the caller moves on" posture as
    // IncidentCorrelator::persist().
    static bool enqueue(EspNowMessageType type, const String& jsonPayload);

    static uint16_t count();

    // Attempts to deliver every queued event, oldest first, via EspNowManager unicast
    // to the gateway's MAC. Stops at the first failure (assumes the gateway dropped
    // again rather than burning through retries against a connection that's already
    // gone) and leaves whatever's left queued. Successfully delivered events are
    // removed and the queue re-persisted. Call this when TransportManager transitions
    // into ESPNOW_CONNECTED.
    static void flush();

private:
    struct QueuedEvent {
        EspNowMessageType type = EspNowMessageType::NODE_STATUS;
        String payload;
        unsigned long queuedAtMs = 0;
    };

    static QueuedEvent items[MAX_QUEUED];
    static uint16_t itemCount;

    static bool persist();
    static bool loadFromDisk();
};

}  // namespace CarSentinel
