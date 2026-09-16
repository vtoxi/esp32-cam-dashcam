#include "HttpBackend.h"
#include "Logger.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace CarSentinel {

static const char* TAG = "HttpBackend";

void HttpBackend::begin(const String& newBaseUrl, const String& newDeviceId,
                         const String& newCredential, bool newTlsVerify) {
    baseUrl = newBaseUrl;
    deviceId = newDeviceId;
    credential = newCredential;
    tlsVerify = newTlsVerify;
    Logger::info(TAG, "Configured: baseUrl=" + (baseUrl.isEmpty() ? String("(none)") : baseUrl) +
                 " deviceId=" + deviceId + " tlsVerify=" + String(tlsVerify));
    // credential is deliberately never logged — same posture as BackendConfig.cpp.
}

bool HttpBackend::isConnected() {
    return lastSuccessMs != 0 && (millis() - lastSuccessMs) < CONNECTED_STALENESS_MS;
}

bool HttpBackend::post(const String& path, const String& jsonPayload, String* outResponsePayload) {
    if (baseUrl.isEmpty()) {
        Logger::warn(TAG, "post() called with no baseUrl configured — refusing");
        lastHttpStatus = 0;
        return false;
    }

    String url = baseUrl + path;
    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    bool began;
    if (url.startsWith("https://")) {
        // Same documented TLS limitation as OtaManager/EmailProvider — no certificate
        // pinning yet. tlsVerify exists in BackendConfig as a forward-looking toggle;
        // it's not honored differently here yet (both paths are equally unverified),
        // which is itself a gap worth stating plainly rather than implying the toggle
        // already does something it doesn't.
        secureClient.setInsecure();
        began = http.begin(secureClient, url);
    } else {
        began = http.begin(plainClient, url);
    }
    if (!began) {
        Logger::error(TAG, "Failed to begin HTTP request to " + url);
        lastHttpStatus = 0;
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    if (!credential.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + credential);
    }
    if (!deviceId.isEmpty()) {
        http.addHeader("X-CarSentinel-Device-Id", deviceId);
    }

    int code = http.POST(jsonPayload);
    lastHttpStatus = code;
    if (outResponsePayload && code >= 200 && code < 300) {
        *outResponsePayload = http.getString();
    }
    http.end();

    if (code >= 200 && code < 300) {
        lastSuccessMs = millis();
        return true;
    }
    Logger::warn(TAG, "POST " + path + " failed, HTTP " + String(code));
    return false;
}

bool HttpBackend::registerDevice(const String& jsonPayload, String& outResponsePayload) {
    return post("/register", jsonPayload, &outResponsePayload);
}

int HttpBackend::lastStatusCode() {
    return lastHttpStatus;
}

bool HttpBackend::sendHeartbeat(const String& jsonPayload) {
    return post("/heartbeat", jsonPayload);
}

bool HttpBackend::sendTelemetry(const String& jsonPayload) {
    return post("/telemetry", jsonPayload);
}

bool HttpBackend::sendEvent(const String& jsonPayload) {
    return post("/events", jsonPayload);
}

bool HttpBackend::sendIncident(const String& jsonPayload) {
    return post("/incidents", jsonPayload);
}

}  // namespace CarSentinel
