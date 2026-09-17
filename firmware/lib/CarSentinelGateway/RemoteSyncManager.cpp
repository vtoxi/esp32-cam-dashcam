#include "RemoteSyncManager.h"
#include "HttpBackend.h"
#include "BackendConfig.h"
#include "BackendQueue.h"
#include "Logger.h"
#include "DeviceConfig.h"

#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "RemoteSyncManager";
static HttpBackend httpBackendInstance;
static bool registered = false;
static unsigned long lastRegistrationAttemptMs = 0;

RemoteBackend* RemoteSyncManager::backend = nullptr;
BackendConnectionState RemoteSyncManager::state = BackendConnectionState::LOCAL_ONLY;
unsigned long RemoteSyncManager::lastHeartbeatMs = 0;

// AUTH_FAILED vs. RETRY_BACKOFF (Phase 21.4 — deferred from 21.2, now possible since
// HttpBackend exposes the HTTP status code). 401/403 mean the credential itself is
// wrong, not a transient network/server problem — worth a visibly different state so
// BACKENDSTATUS/the dashboard don't just say "retrying" forever for a problem retrying
// will never fix.
static BackendConnectionState classifyFailure(RemoteBackend* backend) {
    int code = backend->lastStatusCode();
    if (code == 401 || code == 403) {
        return BackendConnectionState::AUTH_FAILED;
    }
    return BackendConnectionState::RETRY_BACKOFF;
}

// Phase 21.4 — device registration. Called once per boot (or once per begin(), if
// re-triggered by a config change) before the first heartbeat, only when no deviceId
// is configured yet — an operator who already assigned a deviceId via the dashboard/
// BACKENDCONFIG is treated as already registered, this never overwrites a
// deliberately-set value. On success, whatever the server returns for deviceId/
// credential is persisted via BackendConfig so it survives reboot. Never blocks
// startup or local operation either way (docs/BACKEND.md Section 10's "operate
// locally before successful registration" — already true, since this only runs from
// loop(), never setup()).
static void attemptRegistration(RemoteBackend* backend) {
    const DeviceConfigData& dev = DeviceConfig::get();
    JsonDocument doc;
    doc["hardwareProfile"] = dev.hardwareProfile;
    doc["firmwareVersion"] = dev.firmwareVersion;
    doc["nodeId"] = dev.nodeId;
    String payload;
    serializeJson(doc, payload);

    String responsePayload;
    if (!backend->registerDevice(payload, responsePayload)) {
        Logger::warn(TAG, "Backend registration failed (HTTP " + String(backend->lastStatusCode()) + ")");
        return;
    }

    JsonDocument respDoc;
    if (deserializeJson(respDoc, responsePayload) == DeserializationError::Ok) {
        BackendConfigData cfg = BackendConfig::get();
        bool changed = false;
        if (respDoc["deviceId"].is<const char*>()) {
            cfg.deviceId = respDoc["deviceId"].as<String>();
            changed = true;
        }
        if (respDoc["credential"].is<const char*>()) {
            cfg.credential = respDoc["credential"].as<String>();  // never logged
            changed = true;
        }
        if (changed) {
            BackendConfig::save(cfg);
            backend->begin(cfg.baseUrl, cfg.deviceId, cfg.credential, cfg.tlsVerify);
            Logger::info(TAG, "Backend registration assigned deviceId=" + cfg.deviceId);
        }
    }
    registered = true;
    Logger::info(TAG, "Backend registration complete");
}

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
    registered = !cfg.deviceId.isEmpty();  // an already-assigned deviceId counts as registered
    lastHeartbeatMs = 0;  // send a heartbeat on the very next loop(), not after a full interval
    lastRegistrationAttemptMs = 0;  // attempt registration on the very next loop() too
}

void RemoteSyncManager::loop() {
    if (state == BackendConnectionState::LOCAL_ONLY || backend == nullptr) {
        return;
    }

    unsigned long nowForRegistration = millis();
    if (!registered && nowForRegistration - lastRegistrationAttemptMs >= HEARTBEAT_INTERVAL_MS) {
        lastRegistrationAttemptMs = nowForRegistration;
        attemptRegistration(backend);
        // Whether it succeeded or not, fall through to the heartbeat below — a failed
        // attempt is naturally retried at the next interval since `registered` stays
        // false, same cadence as the heartbeat rather than a tighter dedicated timer
        // (no need to hammer a down server any faster than we'd check in on it anyway).
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
    // A missed heartbeat isn't queued — it's a liveness signal, not data; a stale one
    // delivered minutes late (once the backoff timer allows a retry) would carry a
    // wrong uptimeMs and add no value the *next* on-time heartbeat won't already
    // provide. Telemetry/events/incidents (below) are queued because losing those is
    // losing real data, not just a missed liveness check.
    setState(ok ? BackendConnectionState::CONNECTED : classifyFailure(backend));
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

bool RemoteSyncManager::uploadEvidence(const String& incidentId, const String& nodeId,
                                        const String& eventId, const uint8_t* data, size_t len) {
    if (state == BackendConnectionState::LOCAL_ONLY || !backend) return false;
    return backend->uploadEvidence(incidentId, nodeId, eventId, data, len);
}

}  // namespace CarSentinel
