#pragma once

#include <Arduino.h>

// Phase 21.2 — persistent remote-backend configuration (docs/BACKEND.md Section 4).
// Same tier as NetworkConfig/EmailConfig (one file per concern, /config/backend.json),
// even though only the gateway ever populates or reads it — nodes never talk to a
// backend directly (docs/NETWORK.md Section 8 / docs/BACKEND.md Section 5).
//
// enabled=false (the default) means LOCAL_ONLY: RemoteSyncManager never attempts a
// single network call, never even constructs an HTTPClient — this is the local-first
// guarantee (docs/BACKEND.md Section 6), not just a UI toggle that happens to also
// stop traffic.
namespace CarSentinel {

enum class BackendMode : uint8_t {
    LOCAL_ONLY = 0,
    CAR_SENTINEL_CLOUD = 1,
    CUSTOM_SERVER = 2,
};

const char* backendModeToString(BackendMode mode);
BackendMode backendModeFromString(const String& value);

// docs/BACKEND.md Section 7 / Phase 21 brief Section 7 — per-category sync policy.
// Not yet consumed anywhere (Phase 21.2 is the config + connection abstraction only;
// which categories actually get sent, and how often, is decided by whatever calls
// RemoteSyncManager — a later sub-phase's job to wire up against real call sites).
enum class SyncPolicy : uint8_t {
    OFF = 0,
    METADATA_ONLY = 1,
    SELECTED = 2,
    FULL = 3,
};

const char* syncPolicyToString(SyncPolicy policy);
SyncPolicy syncPolicyFromString(const String& value);

struct BackendConfigData {
    bool enabled = false;
    BackendMode mode = BackendMode::LOCAL_ONLY;
    String baseUrl;             // e.g. "https://example.com/api/v1" — no trailing slash
    String deviceId;            // this gateway's backend-facing identity (docs/BACKEND.md Section 9's stable-identity note)
    String tenantId;            // optional — multi-tenant readiness (Section 27), unused by a single-install deployment
    String credential;          // device credential / API key. Never logged (Logger calls in BackendConfig.cpp deliberately omit it), never sent over ESP-NOW.
    bool tlsVerify = false;     // same documented posture as OtaManager/EmailProvider — no cert pinning yet, not hidden

    uint32_t telemetryIntervalMs = 30000;
    uint32_t gpsIntervalMs = 10000;
    SyncPolicy eventsPolicy = SyncPolicy::SELECTED;
    SyncPolicy incidentsPolicy = SyncPolicy::FULL;
    SyncPolicy evidencePolicy = SyncPolicy::METADATA_ONLY;
};

class BackendConfig {
public:
    static bool begin();
    static const BackendConfigData& get();
    static bool save(const BackendConfigData& data);

private:
    static BackendConfigData current;
};

}  // namespace CarSentinel
