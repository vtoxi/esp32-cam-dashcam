#pragma once

#include "RemoteBackend.h"

// Phase 21.2 — the one concrete RemoteBackend implementation this sub-phase needs:
// REST over HTTPS, reusing the same HTTPClient/WiFiClientSecure combination already
// verified working on the gateway build for OtaManager/EmailProvider (docs/OTA.md,
// docs/BACKEND.md Section 3). CAR_SENTINEL_CLOUD vs. CUSTOM_SERVER (BackendConfig.h)
// are both just a different baseUrl/credential fed into this same class — no protocol
// difference between them today.
namespace CarSentinel {

class HttpBackend : public RemoteBackend {
public:
    void begin(const String& baseUrl, const String& deviceId, const String& credential,
               bool tlsVerify) override;
    bool isConnected() override;

    bool sendHeartbeat(const String& jsonPayload) override;
    bool sendTelemetry(const String& jsonPayload) override;
    bool sendEvent(const String& jsonPayload) override;
    bool sendIncident(const String& jsonPayload) override;

private:
    String baseUrl;
    String deviceId;
    String credential;
    bool tlsVerify = false;
    unsigned long lastSuccessMs = 0;
    static const unsigned long CONNECTED_STALENESS_MS = 120000;  // 2x the default heartbeat interval

    // POSTs jsonPayload to baseUrl + path with the device credential as a Bearer
    // token. Bounded — HTTPClient's own default timeout applies, same as
    // OtaManager's HTTP calls; never retried here (that's RemoteSyncManager's job,
    // Phase 21.3).
    bool post(const String& path, const String& jsonPayload);
};

}  // namespace CarSentinel
