#pragma once

#include <Arduino.h>
#include "EspNowProtocol.h"

// High-level ESP-NOW orchestration: owns the Transport, assigns/tracks sequence numbers,
// sends periodic HELLO/HEARTBEAT broadcasts (Section 11 discovery), and retries unicast
// messages that expect an ACK (bounded attempts, in-memory only — no persistence across
// reboot; the persistent offline queue is Section 28 / Phase 28, out of scope here).
namespace CarSentinel {

typedef void (*EspNowMessageHandler)(const EspNowMessage& msg, const uint8_t mac[6]);

class EspNowManager {
public:
    // nodeId/role identify this device in every outgoing message and heartbeat payload.
    static bool begin(const String& nodeId, const String& role);

    // Call every loop() iteration — services heartbeat timing and pending-ACK retries.
    // Non-blocking (no delay() calls), safe to call alongside security-critical logic.
    static void loop();

    // targetMac == nullptr broadcasts (no ACK/retry tracking — see
    // messageTypeExpectsAck()). Otherwise sends unicast; if the type expects an ACK and
    // none arrives, retries up to maxRetries with a linear backoff, then gives up and
    // logs — the caller finds out only via logs in this phase, not a callback (kept
    // simple; Phase 11's incident engine is where "did this actually get delivered"
    // starts to matter enough to wire up properly).
    static bool sendMessage(EspNowMessageType type, const String& jsonPayload,
                             const uint8_t* targetMac = nullptr);

    // Registers a handler for messages that aren't handled internally (HELLO/HEARTBEAT/
    // ACK are). Called after this device has ACKed (if applicable) and updated the peer
    // registry.
    static void setOnMessageHandler(EspNowMessageHandler handler);

    // Convenience lookup used by callers that want to target the gateway specifically
    // (falls back to broadcast if not yet discovered via a HELLO/HEARTBEAT).
    static bool findGatewayMac(uint8_t outMac[6]);

private:
    static String myNodeId;
    static String myRole;
    static uint32_t nextSequenceNumber;

    static const uint8_t MAX_PENDING = 4;
    struct PendingSend {
        bool active = false;
        uint32_t sequenceNumber = 0;
        uint8_t targetMac[6] = {0};
        uint8_t encoded[EspNowProtocol::MAX_ENCODED_LEN] = {0};
        size_t encodedLen = 0;
        uint8_t attempts = 0;
        unsigned long nextRetryAt = 0;
        EspNowMessageType type = EspNowMessageType::HELLO;
    };
    static PendingSend pending[MAX_PENDING];
    static const uint8_t MAX_RETRIES = 3;
    static const unsigned long RETRY_INTERVAL_MS = 1500;

    static const unsigned long HEARTBEAT_INTERVAL_MS = 20000;
    static unsigned long lastHeartbeat;

    static EspNowMessageHandler onMessageHandler;

    static void onReceive(const uint8_t mac[6], const uint8_t* data, size_t len);
    static void sendHeartbeat();
    static void sendAckFor(uint32_t sequenceToAck, const uint8_t mac[6]);
    static void resolvePending(uint32_t ackedSequence);
};

}  // namespace CarSentinel
