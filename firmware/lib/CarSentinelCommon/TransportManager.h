#pragma once

#include <Arduino.h>
#include "EspNowProtocol.h"
#include "NetworkConfig.h"

// docs/NETWORK.md Section 3/5 — the transport state machine and generic send API sitting
// above EspNowManager. Node-focused (the gateway is the thing nodes fall back *to*; it
// doesn't need this same "which transport am I using right now" decision), but lives in
// the shared library like everything else here.
//
// What this is: a real state machine (one variable, explicit transitions) replacing the
// scattered booleans (espNowActive, provisioningMode, WiFiManager::isConnected() checked
// ad hoc) that node_main.cpp used before this. What this is NOT yet: a working Wi-Fi
// fallback *delivery* path. sendEvent()/etc. always go over ESP-NOW; when ESP-NOW can't
// reach the gateway, the state machine still tracks WIFI_FALLBACK_CONNECTING/
// WIFI_CONNECTED correctly (Wi-Fi's own connectivity, via WiFiManager, is real), but
// there is no gateway-side HTTP ingestion endpoint yet for this class to actually hand
// an event to over Wi-Fi — see docs/NETWORK.md Section 9 / docs/IMPLEMENTATION_PLAN.md
// for that tracked gap. Until it exists, an event that can't go over ESP-NOW is queued
// (OfflineQueue) rather than silently dropped or falsely claimed delivered over Wi-Fi.
namespace CarSentinel {

enum class TransportState : uint8_t {
    DISCONNECTED = 0,
    ESPNOW_CONNECTING,
    ESPNOW_CONNECTED,
    WIFI_FALLBACK_CONNECTING,
    WIFI_CONNECTED,
    STANDALONE
};

const char* transportStateToString(TransportState state);

class TransportManager {
public:
    // Call once in setup(), after EspNowManager::begin() and WiFiManager are ready to
    // be driven (doesn't itself start either — just begins tracking their state).
    static void begin();

    // Call every loop() iteration — non-blocking, drives the state machine using the
    // configured timeouts (NetworkConfig's espNowDiscoveryTimeoutMs/
    // espNowRetryIntervalMs/espNowHeartbeatTimeoutMs/wifiFallbackDelayMs).
    static void loop();

    static TransportState getState();
    static bool isGatewayReachable();  // ESPNOW_CONNECTED || WIFI_CONNECTED

    // Generic send operations (docs/NETWORK.md Section 5) — callers pick a category,
    // not a transport. All currently route through ESP-NOW (the only implemented
    // delivery path); see the class-level comment above for the Wi-Fi-fallback gap.
    // Falls back to OfflineQueue::enqueue() when the gateway isn't reachable at all
    // (STANDALONE), so the event isn't lost even though this call still returns false
    // in that case (queued, not delivered — callers that care about the distinction
    // can check getState() first).
    static bool sendEvent(EspNowMessageType type, const String& jsonPayload);
    static bool sendTelemetry(EspNowMessageType type, const String& jsonPayload);
    static bool sendStatus(EspNowMessageType type, const String& jsonPayload);
    static bool sendCommand(EspNowMessageType type, const String& jsonPayload,
                             const uint8_t* targetMac = nullptr);

private:
    static TransportState state;
    static unsigned long stateEnteredMs;
    static unsigned long lastEspNowRetryMs;

    static void setState(TransportState newState);
    static bool trySendEspNow(EspNowMessageType type, const String& payload, const uint8_t* targetMac);
};

}  // namespace CarSentinel
