#include "TransportManager.h"
#include "EspNowManager.h"
#include "PeerRegistry.h"
#include "WiFiManager.h"
#include "OfflineQueue.h"
#include "Logger.h"

namespace CarSentinel {

static const char* TAG = "TransportManager";

TransportState TransportManager::state = TransportState::DISCONNECTED;
unsigned long TransportManager::stateEnteredMs = 0;
unsigned long TransportManager::lastEspNowRetryMs = 0;

const char* transportStateToString(TransportState state) {
    switch (state) {
        case TransportState::DISCONNECTED: return "DISCONNECTED";
        case TransportState::ESPNOW_CONNECTING: return "ESPNOW_CONNECTING";
        case TransportState::ESPNOW_CONNECTED: return "ESPNOW_CONNECTED";
        case TransportState::WIFI_FALLBACK_CONNECTING: return "WIFI_FALLBACK_CONNECTING";
        case TransportState::WIFI_CONNECTED: return "WIFI_CONNECTED";
        default: return "STANDALONE";
    }
}

// A HELLO/HEARTBEAT heard recently from a peer with role GATEWAY — the same signal
// EspNowManager::findGatewayMac() uses to locate the gateway's MAC, checked here
// against the configured heartbeat timeout to decide if that peer still counts as
// "reachable" (PeerRegistry itself has no expiry — Phase 5 scope, see PeerRegistry.h).
static bool gatewayReachableViaEspNow() {
    PeerInfo* gw = PeerRegistry::findByRole("GATEWAY");
    if (!gw) {
        return false;
    }
    uint32_t timeout = NetworkConfig::get().espNowHeartbeatTimeoutMs;
    return (millis() - gw->lastSeenMs) < timeout;
}

void TransportManager::setState(TransportState newState) {
    if (newState == state) {
        return;
    }
    Logger::info(TAG, "Transport: " + String(transportStateToString(state)) + " -> " +
                 String(transportStateToString(newState)));
    state = newState;
    stateEnteredMs = millis();
    if (newState == TransportState::ESPNOW_CONNECTED) {
        OfflineQueue::flush();
    }
}

void TransportManager::begin() {
    state = TransportState::DISCONNECTED;
    stateEnteredMs = millis();
    lastEspNowRetryMs = millis();
    OfflineQueue::begin();
}

void TransportManager::loop() {
    const NetworkConfigData& cfg = NetworkConfig::get();
    bool espNowUp = gatewayReachableViaEspNow();
    bool wifiUp = cfg.wifiFallbackEnabled && WiFiManager::isConnected();
    unsigned long now = millis();

    switch (state) {
        case TransportState::DISCONNECTED:
            setState(TransportState::ESPNOW_CONNECTING);
            break;

        case TransportState::ESPNOW_CONNECTING:
            if (espNowUp) {
                setState(TransportState::ESPNOW_CONNECTED);
            } else if (now - stateEnteredMs >= cfg.espNowDiscoveryTimeoutMs) {
                setState(cfg.wifiFallbackEnabled ? TransportState::WIFI_FALLBACK_CONNECTING
                                                  : TransportState::STANDALONE);
            }
            break;

        case TransportState::ESPNOW_CONNECTED:
            // ESP-NOW stays preferred even once connected — this state is only left if
            // the gateway's heartbeat actually times out (docs/NETWORK.md: "ESP-NOW
            // remains the preferred mode").
            if (!espNowUp) {
                setState(cfg.wifiFallbackEnabled ? TransportState::WIFI_FALLBACK_CONNECTING
                                                  : TransportState::STANDALONE);
            }
            break;

        case TransportState::WIFI_FALLBACK_CONNECTING:
            if (espNowUp) {
                setState(TransportState::ESPNOW_CONNECTED);
            } else if (wifiUp) {
                setState(TransportState::WIFI_CONNECTED);
            } else if (now - stateEnteredMs >= cfg.wifiFallbackDelayMs) {
                // Wi-Fi itself is handled by WiFiManager (connectBestKnown() already
                // ran during setup(), bounded); if it's still not connected by now,
                // there's nothing left to wait for here.
                setState(TransportState::STANDALONE);
            }
            break;

        case TransportState::WIFI_CONNECTED:
            if (espNowUp) {
                setState(TransportState::ESPNOW_CONNECTED);
            } else if (!wifiUp) {
                setState(cfg.wifiFallbackEnabled ? TransportState::WIFI_FALLBACK_CONNECTING
                                                  : TransportState::STANDALONE);
            }
            lastEspNowRetryMs = now;  // passively re-checked every loop() already
            break;

        case TransportState::STANDALONE:
            if (espNowUp) {
                setState(TransportState::ESPNOW_CONNECTED);
            } else if (wifiUp) {
                setState(TransportState::WIFI_CONNECTED);
            }
            // No explicit retry timer needed beyond this — gatewayReachableViaEspNow()
            // and WiFiManager::isConnected() are both checked every loop() already, so
            // reconnection is detected as soon as it happens, not on a poll interval.
            break;
    }
}

TransportState TransportManager::getState() {
    return state;
}

bool TransportManager::isGatewayReachable() {
    return state == TransportState::ESPNOW_CONNECTED || state == TransportState::WIFI_CONNECTED;
}

bool TransportManager::trySendEspNow(EspNowMessageType type, const String& payload,
                                      const uint8_t* targetMac) {
    if (targetMac) {
        return EspNowManager::sendMessage(type, payload, targetMac);
    }
    uint8_t gatewayMac[6];
    if (EspNowManager::findGatewayMac(gatewayMac)) {
        return EspNowManager::sendMessage(type, payload, gatewayMac);
    }
    // No known gateway MAC yet — broadcast (no ACK/retry tracking, same as
    // EspNowManager's own documented behavior for targetMac == nullptr).
    return EspNowManager::sendMessage(type, payload, nullptr);
}

// All four generic ops share the same policy: try ESP-NOW now (works whenever the
// gateway is reachable over ESP-NOW, regardless of transport state, since state only
// updates once per loop() and shouldn't gate an opportunistic send); queue it if that
// fails, rather than silently dropping it or falsely claiming Wi-Fi delivery that
// isn't implemented yet (see TransportManager.h's class comment).
bool TransportManager::sendEvent(EspNowMessageType type, const String& jsonPayload) {
    if (trySendEspNow(type, jsonPayload, nullptr)) return true;
    OfflineQueue::enqueue(type, jsonPayload);
    return false;
}

bool TransportManager::sendTelemetry(EspNowMessageType type, const String& jsonPayload) {
    if (trySendEspNow(type, jsonPayload, nullptr)) return true;
    OfflineQueue::enqueue(type, jsonPayload);
    return false;
}

bool TransportManager::sendStatus(EspNowMessageType type, const String& jsonPayload) {
    if (trySendEspNow(type, jsonPayload, nullptr)) return true;
    OfflineQueue::enqueue(type, jsonPayload);
    return false;
}

bool TransportManager::sendCommand(EspNowMessageType type, const String& jsonPayload,
                                    const uint8_t* targetMac) {
    if (trySendEspNow(type, jsonPayload, targetMac)) return true;
    OfflineQueue::enqueue(type, jsonPayload);
    return false;
}

}  // namespace CarSentinel
