#pragma once

#include <Arduino.h>
#include "RemoteBackend.h"

// Phase 21.2 — the single place Gateway code calls into for backend sync (docs/BACKEND.md
// Section 3), owning connection state and delegating actual delivery to a RemoteBackend.
// Scope of this sub-phase specifically (per the Phase 21 brief's own breakdown):
// the abstraction + connection state machine, wired into gateway_main.cpp's boot/loop.
// Deliberately NOT yet in this sub-phase (tracked for 21.3+):
//   - No retry/backoff/persisted queue — a failed send is just dropped right now,
//     same honest-gap posture as every other "not yet implemented" piece in this
//     project (see docs/IMPLEMENTATION_PLAN.md's Phase 21.2 entry for the explicit
//     list). Phase 21.3 ("Persistent Remote Queue") is where this becomes durable.
//   - No automatic call sites — nothing in IncidentCorrelator/gateway_main.cpp calls
//     sendEvent()/sendIncident() yet. Wiring those up requires deciding how
//     BackendConfig's sync policy (SyncPolicy enum) filters what actually gets sent,
//     which is a judgment call better made once there's a real backend (21.5) to
//     verify the payloads against, not guessed at here.
namespace CarSentinel {

enum class BackendConnectionState : uint8_t {
    LOCAL_ONLY = 0,   // BackendConfig.enabled == false — no network activity at all, ever
    CONNECTING,
    CONNECTED,
    AUTH_FAILED,
    RETRY_BACKOFF,
};

const char* backendConnectionStateToString(BackendConnectionState state);

class RemoteSyncManager {
public:
    // Reads BackendConfig and configures the active backend accordingly. Safe to call
    // again after a config change (e.g. from the dashboard Settings page) to apply it
    // without a reboot.
    static void begin();

    // Call every loop() iteration on the gateway — non-blocking, drives the connection
    // state machine. A no-op (immediately returns) when in LOCAL_ONLY, so this costs
    // nothing when the backend is disabled — the local-first guarantee (docs/BACKEND.md
    // Section 6) isn't just "traffic doesn't get sent," it's "no CPU/radio time spent
    // even checking."
    static void loop();

    static BackendConnectionState getState();

    // Pass-through to the active backend. Returns false immediately (no attempt made)
    // when state != CONNECTED — callers that care about "queued vs. dropped" will get
    // real queueing once Phase 21.3 lands; today a false return means "not sent,
    // nothing else happened to it."
    static bool sendHeartbeat(const String& jsonPayload);
    static bool sendTelemetry(const String& jsonPayload);
    static bool sendEvent(const String& jsonPayload);
    static bool sendIncident(const String& jsonPayload);

    // Phase 21.7 — not queued on failure (unlike the JSON send* methods above): an
    // evidence image is tens of KB, and BackendQueue/OfflineQueue's persisted-JSON-
    // array-on-LittleFS design was never sized for binary blobs — queueing images
    // durably would need its own design (probably "leave the reference on the node's
    // SD and retry the whole fetch-and-upload later" rather than buffering the image
    // bytes themselves), not a small extension of the existing queue. Documented gap,
    // not an oversight — see docs/IMPLEMENTATION_PLAN.md's Phase 21.7 entry.
    static bool uploadEvidence(const String& incidentId, const String& nodeId,
                                const String& eventId, const uint8_t* data, size_t len);

    // Phase 21.8 — called from loop() with each pending command's (commandType,
    // payload) once polled; returns whether it executed successfully and fills
    // outResultJson with whatever detail is worth reporting back (may be left "{}").
    // Unset by default — a gateway that never registers a handler still polls (and
    // logs "no handler registered" for anything that arrives) rather than silently
    // never checking, so BACKENDSTATUS/logs make an unwired command flow obvious
    // rather than a mystery.
    typedef bool (*CommandHandler)(const String& commandType, const String& payloadJson, String& outResultJson);
    static void setCommandHandler(CommandHandler handler);

private:
    static RemoteBackend* backend;
    static BackendConnectionState state;
    static unsigned long lastHeartbeatMs;
    static unsigned long lastCommandPollMs;
    static CommandHandler commandHandler;
    static const unsigned long HEARTBEAT_INTERVAL_MS = 60000;
    static const unsigned long COMMAND_POLL_INTERVAL_MS = 15000;

    static void setState(BackendConnectionState newState);
    static void pollAndDispatchCommands();
};

}  // namespace CarSentinel
