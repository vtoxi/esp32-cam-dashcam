#pragma once

#include <Arduino.h>

// Phase 21.2 — the interface RemoteSyncManager talks to, never a concrete transport
// directly. Same shape as Transport.h's ESP-NOW abstraction (docs/NETWORK.md Section
// 5): application code (RemoteSyncManager) depends on this interface only, so a future
// second implementation (MQTT, a CarSentinel-cloud-specific protocol) doesn't touch
// RemoteSyncManager or anything above it. HttpBackend is the only implementation today.
namespace CarSentinel {

class RemoteBackend {
public:
    virtual ~RemoteBackend() {}

    // (Re)configures the backend connection. Safe to call again if config changes
    // (e.g. from the dashboard Settings page) — implementations should just update
    // their stored base URL/credential, not require a reboot.
    virtual void begin(const String& baseUrl, const String& deviceId,
                       const String& credential, bool tlsVerify) = 0;

    // Best-effort reachability — implementations decide what "connected" means (e.g.
    // "the last request succeeded recently"), not a persistent socket the way ESP-NOW
    // peer tracking is (HTTP is inherently connectionless per-request).
    virtual bool isConnected() = 0;

    // One JSON payload per call — RemoteSyncManager decides *when* to call these
    // (sync policy, Phase 21.2 doesn't wire this up yet — see BackendConfig.h),
    // this interface only decides *how* a payload actually reaches the backend.
    // Returns false on any failure (network, HTTP status, auth) — never throws,
    // never blocks longer than a bounded timeout (same "never block forever"
    // discipline as WiFiManager::connectBlocking()).
    virtual bool sendHeartbeat(const String& jsonPayload) = 0;
    virtual bool sendTelemetry(const String& jsonPayload) = 0;
    virtual bool sendEvent(const String& jsonPayload) = 0;
    virtual bool sendIncident(const String& jsonPayload) = 0;

    // Phase 21.4 — device registration/pairing (docs/BACKEND.md Section: "the Gateway
    // should be able to operate locally before successful backend registration,"
    // already true since RemoteSyncManager never calls this outside CONNECTING).
    // jsonPayload carries whatever identifies this device (hardwareProfile,
    // firmwareVersion, protocolVersion, its current deviceId if any); on success,
    // outResponsePayload carries the raw server response body so the caller
    // (RemoteSyncManager) can pull out a server-assigned deviceId/credential without
    // this interface needing to know the exact response schema.
    virtual bool registerDevice(const String& jsonPayload, String& outResponsePayload) = 0;

    // Phase 21.7 — uploads one already-fetched evidence image (the Gateway has
    // already pulled the bytes off the originating node's SD card over its local
    // /evidence route — docs/REMOTE_ACCESS.md Section 4's identified gap, closed in
    // this phase). Not a JSON payload like the methods above; raw bytes with a
    // handful of identifying fields the backend needs to file it under the right
    // incident/node/event.
    virtual bool uploadEvidence(const String& incidentId, const String& nodeId,
                                 const String& eventId, const uint8_t* data, size_t len) = 0;

    // Phase 21.8 — Backend -> Gateway command flow. Polling, not push (see
    // RemoteSyncManager.h's own note on why). outCommandsJson receives a raw JSON
    // array (possibly empty, "[]") of {commandId, commandType, payload, expiresAt} —
    // this interface hands the array back unparsed, same "don't assume the caller's
    // schema" posture as registerDevice()'s response payload.
    virtual bool pollCommands(String& outCommandsJson) = 0;

    // Reports what happened after RemoteSyncManager (via its registered
    // CommandHandler) actually executed a command. Best-effort — if this fails, the
    // command was still executed locally, it's only the backend's record of it that's
    // now stale; not retried/queued (same reasoning as uploadEvidence() — a small,
    // deliberate list of things this phase doesn't durably retry, see
    // docs/IMPLEMENTATION_PLAN.md's Phase 21.8 entry).
    virtual bool reportCommandResult(const String& commandId, const String& jsonResult) = 0;

    // The last HTTP-ish status code from any of the calls above — lets
    // RemoteSyncManager distinguish "wrong credential" (its own state, AUTH_FAILED)
    // from "server unreachable" (RETRY_BACKOFF), which was an explicitly deferred gap
    // in Phase 21.2. 0 means "no request has completed yet" (not the same as a real
    // failure code).
    virtual int lastStatusCode() = 0;
};

}  // namespace CarSentinel
