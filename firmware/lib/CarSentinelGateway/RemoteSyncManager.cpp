#include "RemoteSyncManager.h"
#include "HttpBackend.h"
#include "BackendConfig.h"
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
    setState(ok ? BackendConnectionState::CONNECTED : BackendConnectionState::RETRY_BACKOFF);
}

BackendConnectionState RemoteSyncManager::getState() {
    return state;
}

bool RemoteSyncManager::sendHeartbeat(const String& jsonPayload) {
    if (state != BackendConnectionState::CONNECTED || !backend) return false;
    return backend->sendHeartbeat(jsonPayload);
}

bool RemoteSyncManager::sendTelemetry(const String& jsonPayload) {
    if (state != BackendConnectionState::CONNECTED || !backend) return false;
    return backend->sendTelemetry(jsonPayload);
}

bool RemoteSyncManager::sendEvent(const String& jsonPayload) {
    if (state != BackendConnectionState::CONNECTED || !backend) return false;
    return backend->sendEvent(jsonPayload);
}

bool RemoteSyncManager::sendIncident(const String& jsonPayload) {
    if (state != BackendConnectionState::CONNECTED || !backend) return false;
    return backend->sendIncident(jsonPayload);
}

}  // namespace CarSentinel
