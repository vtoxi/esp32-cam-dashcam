#pragma once

#include <Arduino.h>
#include "RemoteBackend.h"

// Phase 21.3 — the durable queue RemoteSyncManager was missing (docs/IMPLEMENTATION_PLAN.md
// Phase 21.2's own "not implemented" list: "a failed send is currently just dropped").
// Mirrors OfflineQueue's design (bounded, LittleFS-persisted, evict-oldest-when-full)
// for the Gateway->Backend hop the same way OfflineQueue does it for Node->Gateway —
// a deliberate structural echo, not a coincidence, per docs/BACKEND.md Section 6's
// "extend the existing persisted-queue pattern" note.
//
// What this adds beyond OfflineQueue's shape: per-item retry count + exponential
// backoff (a Gateway<->Backend link over the Internet fails very differently than a
// local ESP-NOW hop — hammering a down server every loop() is a real cost OfflineQueue
// never had to consider) and a category tag (heartbeat/telemetry/event/incident) since
// RemoteBackend's interface has a different send method per category.
//
// What this deliberately does NOT add yet (see docs/IMPLEMENTATION_PLAN.md's Phase
// 21.3 entry for the honest gap list): no deduplication by event ID (would need to
// parse each JSON payload for an id field — deferred until a real backend exists to
// verify the payload shape against), and no per-category retention policy (every
// category shares one bounded queue and one eviction rule, not differentiated —
// evidence-specific retention is Phase 21.7's job, not this one's).
namespace CarSentinel {

enum class BackendCategory : uint8_t {
    HEARTBEAT = 0,
    TELEMETRY = 1,
    EVENT = 2,
    INCIDENT = 3,
};

const char* backendCategoryToString(BackendCategory category);

class BackendQueue {
public:
    static const uint16_t MAX_QUEUED = 50;

    // Mounts LittleFS's queue file if present. Assumes LittleFS is already mounted.
    static void begin();

    // Adds an item (evicting the oldest if already at MAX_QUEUED, same as
    // OfflineQueue). Persists immediately.
    static bool enqueue(BackendCategory category, const String& jsonPayload);

    static uint16_t count();

    // Attempts delivery of every item whose backoff timer has elapsed, oldest first,
    // via the given backend's matching send method. Success removes the item; failure
    // advances its retry count and schedules the next attempt with exponential backoff
    // (base 5s, doubling, capped at 5 minutes). Does not stop at the first failure
    // (unlike OfflineQueue::flush()) — a Gateway<->Backend link failing for one item
    // doesn't necessarily mean the next one (which might be due for retry much later)
    // would also fail, so each due item gets its own independent attempt.
    static void flush(RemoteBackend* backend);

private:
    struct QueuedItem {
        BackendCategory category = BackendCategory::TELEMETRY;
        String payload;
        unsigned long queuedAtMs = 0;
        uint8_t retryCount = 0;
        unsigned long nextRetryAtMs = 0;
    };

    static QueuedItem items[MAX_QUEUED];
    static uint16_t itemCount;

    static bool persist();
    static bool loadFromDisk();
    static bool sendOne(RemoteBackend* backend, const QueuedItem& item);
};

}  // namespace CarSentinel
