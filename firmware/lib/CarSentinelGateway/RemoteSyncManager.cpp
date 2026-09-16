#include "RemoteSyncManager.h"
#include "HttpBackend.h"
#include "BackendConfig.h"
#include "BackendQueue.h"
#include "Logger.h"

#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "RemoteSyncManager";
static HttpBackend httpBackendInstance;

RemoteBackend* RemoteSyncManager::backend = nullptr;
BackendConnectionState RemoteSyncManager::state = BackendConnectionState::LOCAL_ONLY;
unsigned long RemoteSyncManager::lastHeartbeatMs = 0;

const char* backendConnectionStateToString(BackendConnectionState state) {
    switch (state) {
        case BackendConnectionState::CONNECTING: return "CONNECTING";
        case BackendConnectionState::CONNECTED: return "CONNECTED";
        case BackendConnectionState::AUTH_FAILED: return "AUTH_FAILED";
        case BackendConnectionState::RETRY_BACKOFF: return "RETRY_BACKOFF";
        default: return "LOCAL_ONLY";
    }
}

void RemoteSyncManager::setState(BackendConnectionState newState) {
    if (newState == state) {
        return;
    }
    Logger::info(TAG, "Backend: " + String(backendConnectionStateToString(state)) + " -> " +
                 String(backendConnectionStateToString(newState)));
    state = newState;
}

void RemoteSyncManager::begin() {
    BackendQueue::begin();
    const BackendConfigData& cfg = BackendConfig::get();
    if (!cfg.enabled || cfg.mode == BackendMode::LOCAL_ONLY) {
        backend = nullptr;
        setState(BackendConnectionState::LOCAL_ONLY);
        Logger::info(TAG, "Backend disabled (LOCAL_ONLY) — no network activity");
        return;
    }
    if (cfg.baseUrl.isEmpty()) {
        Logger::warn(TAG, "Backend enabled but no baseUrl configured — staying LOCAL_ONLY");
        backend = nullptr;
        setState(BackendConnectionState::LOCAL_ONLY);
        return;
    }

    backend = &httpBackendInstance;
    backend->begin(cfg.baseUrl, cfg.deviceId, cfg.credential, cfg.tlsVerify);
    setState(BackendConnectionState::CONNECTING);
    lastHeartbeatMs = 0;  // send a heartbeat on the very next loop(), not after a full interval
}

void RemoteSyncManager::loop() {
    if (state == BackendConnectionState::LOCAL_ONLY || backend == nullptr) {
        return;
    }

    // Phase 21.3: give queued items (from earlier failed/offline sends) a chance to
    // flush every loop() — flush() itself only actually attempts delivery for items
    // whose backoff timer has elapsed, so this is cheap when nothing is due.
    BackendQueue::flush(backend);

    unsigned long now = millis();
    if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) {
        return;
    }
    lastHeartbeatMs = now;

    JsonDocument doc;
    doc["uptimeMs"] = now;
    String payload;
    serializeJson(doc, payload);

    bool ok = backend->sendHeartbeat(payload);
    // AUTH_FAILED is intentionally never entered yet — distinguishing "wrong
    // credential" from "network/server unreachable" needs the backend to expose the
    // HTTP status code, which RemoteBackend's interface doesn't yet (a deliberate
    // scope cut for this sub-phase, not an oversight — see this file's header
    // comment and docs/IMPLEMENTATION_PLAN.md's Phase 21.2 entry).
    // A missed heartbeat isn't queued — it's a liveness signal, not data; a stale one
    // delivered minutes late (once the backoff timer allows a retry) would carry a
    // wrong uptimeMs and add no value the *next* on-time heartbeat won't already
    // provide. Telemetry/events/incidents (below) are queued because losing those is
    // losing real data, not just a missed liveness check.
    setState(ok ? BackendConnectionState::CONNECTED : BackendConnectionState::RETRY_BACKOFF);
}

BackendConnectionState RemoteSyncManager::getState() {
    return state;
}

// docs/IMPLEMENTATION_PLAN.md Phase 21.3: a call that can't be delivered right now
// (either not connected at all, or the immediate attempt itself fails) is queued
// rather than dropped — BackendQueue::flush() (called every loop(), see above) keeps
// retrying it with exponential backoff until it succeeds or the queue's bounded size
// forces it out for something newer. LOCAL_ONLY is the one exception: queueing while
// backend sync is entirely disabled would just grow a queue nothing will ever flush
// (BackendQueue::flush() is only ever called when state != LOCAL_ONLY), so those
// calls are simply no-ops, matching the local-first guarantee (docs/BACKEND.md
// Section 6) rather than silently accumulating unbounded local state for a feature
// the user turned off.
bool RemoteSyncManager::sendHeartbeat(const String& jsonPayload) {
    if (state == BackendConnectionState::LOCAL_ONLY || !backend) return false;
    if (backend->sendHeartbeat(jsonPayload)) return true;
    return false;  // heartbeats aren't queued — see loop()'s comment on the same point
}

bool RemoteSyncManager::sendTelemetry(const String& jsonPayload) {
    if (state == BackendConnectionState::LOCAL_ONLY || !backend) return false;
    if (backend->sendTelemetry(jsonPayload)) return true;
    BackendQueue::enqueue(BackendCategory::TELEMETRY, jsonPayload);
    return false;
}

bool RemoteSyncManager::sendEvent(const String& jsonPayload) {
    if (state == BackendConnectionState::LOCAL_ONLY || !backend) return false;
    if (backend->sendEvent(jsonPayload)) return true;
    BackendQueue::enqueue(BackendCategory::EVENT, jsonPayload);
    return false;
}

bool RemoteSyncManager::sendIncident(const String& jsonPayload) {
    if (state == BackendConnectionState::LOCAL_ONLY || !backend) return false;
    if (backend->sendIncident(jsonPayload)) return true;
    BackendQueue::enqueue(BackendCategory::INCIDENT, jsonPayload);
    return false;
}

}  // namespace CarSentinel
