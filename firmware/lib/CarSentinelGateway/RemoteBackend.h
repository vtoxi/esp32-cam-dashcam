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
};

}  // namespace CarSentinel
