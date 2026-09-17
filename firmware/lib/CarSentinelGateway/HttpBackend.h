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
    bool registerDevice(const String& jsonPayload, String& outResponsePayload) override;
    bool uploadEvidence(const String& incidentId, const String& nodeId, const String& eventId,
                         const uint8_t* data, size_t len) override;
    bool pollCommands(String& outCommandsJson) override;
    bool reportCommandResult(const String& commandId, const String& jsonResult) override;
    int lastStatusCode() override;

private:
    String baseUrl;
    String deviceId;
    String credential;
    bool tlsVerify = false;
    unsigned long lastSuccessMs = 0;
    int lastHttpStatus = 0;
    static const unsigned long CONNECTED_STALENESS_MS = 120000;  // 2x the default heartbeat interval

    // POSTs jsonPayload to baseUrl + path with the device credential as a Bearer
    // token. Bounded — HTTPClient's own default timeout applies, same as
    // OtaManager's HTTP calls; retry/backoff is RemoteSyncManager/BackendQueue's job
    // (Phase 21.3), not this class's. outResponsePayload, if non-null, receives the
    // response body (used by registerDevice(); every other caller passes nullptr and
    // ignores the body, since none of the send* methods need a response beyond
    // success/failure).
    bool post(const String& path, const String& jsonPayload, String* outResponsePayload = nullptr);

    // GETs baseUrl + path with the same auth headers post() uses, returning the
    // response body via outResponsePayload. Used only by pollCommands() today.
    bool get(const String& path, String& outResponsePayload);
};

}  // namespace CarSentinel
