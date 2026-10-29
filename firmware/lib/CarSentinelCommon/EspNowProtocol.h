#pragma once

#include <Arduino.h>

// Wire protocol for ESP-NOW messages (Section 11). Manually byte-serialized (not a raw
// struct cast) so the layout is explicit and doesn't depend on compiler padding/alignment
// matching between the ESP32 (node) and ESP32-S3 (gateway) targets. Every message is
// versioned and HMAC-signed (Section 41) — see EspNowSecurity for key handling.
//
// Wire layout (all little-endian, ESP32 is native little-endian so no swapping needed):
//   [0]       protocolVersion
//   [1]       messageType
//   [2..5]    sequenceNumber (4 bytes)
//   [6]       senderNodeId length (N)
//   [7..7+N)  senderNodeId bytes (no null terminator on wire)
//   [+1]      payload length (M)
//   [+M]      payload bytes (compact JSON)
//   [+8]      HMAC-SHA256 truncated to 8 bytes, over every byte before this field
namespace CarSentinel {

enum class EspNowMessageType : uint8_t {
    HELLO = 1,
    DISCOVERY = 2,
    PAIR_REQUEST = 3,
    PAIR_RESPONSE = 4,
    HEARTBEAT = 5,
    NODE_STATUS = 6,
    MOTION_DETECTED = 7,
    INTRUSION_DETECTED = 8,
    IMPACT_DETECTED = 9,
    TEMPERATURE_UPDATE = 10,
    HUMIDITY_UPDATE = 11,
    GPS_UPDATE = 12,
    IMU_UPDATE = 13,
    CAPTURE_REQUEST = 14,
    CAPTURE_RESULT = 15,
    INCIDENT_START = 16,
    INCIDENT_UPDATE = 17,
    INCIDENT_END = 18,
    CONFIG_REQUEST = 19,
    CONFIG_UPDATE = 20,
    TIME_SYNC = 21,
    OTA_COMMAND = 22,
    ACK = 23,
    // Not named ERROR: some toolchain headers define ERROR as a macro (the DISPLAY
    // enumerator collision in DeviceConfig.h was exactly this class of bug) — avoided
    // preemptively here rather than found the hard way again.
    ERROR_MSG = 24
};

const char* messageTypeToString(EspNowMessageType type);

// True for message types that expect a unicast ACK reply from the recipient
// (EspNowManager retries these on timeout). Discovery/heartbeat/ACK traffic itself does
// not — see EspNowManager.cpp for the retry state machine that uses this.
bool messageTypeExpectsAck(EspNowMessageType type);

constexpr uint8_t ESPNOW_PROTOCOL_VERSION = 1;
constexpr size_t ESPNOW_MAX_NODEID_LEN = 20;
constexpr size_t ESPNOW_MAX_PAYLOAD_LEN = 180;
constexpr size_t ESPNOW_HMAC_LEN = 8;

struct EspNowMessage {
    uint8_t protocolVersion = ESPNOW_PROTOCOL_VERSION;
    EspNowMessageType type = EspNowMessageType::HELLO;
    uint32_t sequenceNumber = 0;
    String senderNodeId;
    String payload;
};

class EspNowProtocol {
public:
    // 1 + 1 + 4 + 1 + nodeId + 1 + payload + hmac
    static constexpr size_t MAX_ENCODED_LEN =
        1 + 1 + 4 + 1 + ESPNOW_MAX_NODEID_LEN + 1 + ESPNOW_MAX_PAYLOAD_LEN + ESPNOW_HMAC_LEN;

    // Returns false if senderNodeId/payload exceed the bounds above — caller should treat
    // that as a bug (a message too large to fit ESP-NOW's ~250-byte limit), not retry.
    static bool encode(const EspNowMessage& msg, uint8_t* outBuf, size_t bufCap, size_t& outLen);

    // Returns false if malformed, version-mismatched, or HMAC verification fails (wrong
    // key, corrupted/tampered packet, or replay of a packet signed with a different key).
    static bool decode(const uint8_t* buf, size_t len, EspNowMessage& outMsg);
};

}  // namespace CarSentinel
