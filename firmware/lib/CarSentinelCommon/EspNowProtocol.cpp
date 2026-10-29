#include "EspNowProtocol.h"
#include "EspNowSecurity.h"
#include "Logger.h"

#include <mbedtls/md.h>

namespace CarSentinel {

static const char* TAG = "EspNowProtocol";

const char* messageTypeToString(EspNowMessageType type) {
    switch (type) {
        case EspNowMessageType::HELLO: return "HELLO";
        case EspNowMessageType::DISCOVERY: return "DISCOVERY";
        case EspNowMessageType::PAIR_REQUEST: return "PAIR_REQUEST";
        case EspNowMessageType::PAIR_RESPONSE: return "PAIR_RESPONSE";
        case EspNowMessageType::HEARTBEAT: return "HEARTBEAT";
        case EspNowMessageType::NODE_STATUS: return "NODE_STATUS";
        case EspNowMessageType::MOTION_DETECTED: return "MOTION_DETECTED";
        case EspNowMessageType::INTRUSION_DETECTED: return "INTRUSION_DETECTED";
        case EspNowMessageType::IMPACT_DETECTED: return "IMPACT_DETECTED";
        case EspNowMessageType::TEMPERATURE_UPDATE: return "TEMPERATURE_UPDATE";
        case EspNowMessageType::HUMIDITY_UPDATE: return "HUMIDITY_UPDATE";
        case EspNowMessageType::GPS_UPDATE: return "GPS_UPDATE";
        case EspNowMessageType::IMU_UPDATE: return "IMU_UPDATE";
        case EspNowMessageType::CAPTURE_REQUEST: return "CAPTURE_REQUEST";
        case EspNowMessageType::CAPTURE_RESULT: return "CAPTURE_RESULT";
        case EspNowMessageType::INCIDENT_START: return "INCIDENT_START";
        case EspNowMessageType::INCIDENT_UPDATE: return "INCIDENT_UPDATE";
        case EspNowMessageType::INCIDENT_END: return "INCIDENT_END";
        case EspNowMessageType::CONFIG_REQUEST: return "CONFIG_REQUEST";
        case EspNowMessageType::CONFIG_UPDATE: return "CONFIG_UPDATE";
        case EspNowMessageType::TIME_SYNC: return "TIME_SYNC";
        case EspNowMessageType::OTA_COMMAND: return "OTA_COMMAND";
        case EspNowMessageType::ACK: return "ACK";
        case EspNowMessageType::ERROR_MSG: return "ERROR";
        default: return "UNKNOWN";
    }
}

bool messageTypeExpectsAck(EspNowMessageType type) {
    switch (type) {
        case EspNowMessageType::HELLO:
        case EspNowMessageType::DISCOVERY:
        case EspNowMessageType::HEARTBEAT:
        case EspNowMessageType::NODE_STATUS:
        case EspNowMessageType::ACK:
            return false;
        default:
            return true;
    }
}

static void computeHmac(const uint8_t* data, size_t len, uint8_t out8[ESPNOW_HMAC_LEN]) {
    uint8_t full[32];
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(info, EspNowSecurity::getKey(), EspNowSecurity::getKeyLength(), data, len, full);
    memcpy(out8, full, ESPNOW_HMAC_LEN);
}

bool EspNowProtocol::encode(const EspNowMessage& msg, uint8_t* outBuf, size_t bufCap, size_t& outLen) {
    size_t nodeIdLen = msg.senderNodeId.length();
    size_t payloadLen = msg.payload.length();

    if (nodeIdLen > ESPNOW_MAX_NODEID_LEN || payloadLen > ESPNOW_MAX_PAYLOAD_LEN) {
        Logger::error(TAG, "encode: nodeId or payload exceeds wire limits (nodeId=" +
                      String(nodeIdLen) + " payload=" + String(payloadLen) + ")");
        return false;
    }

    size_t needed = 1 + 1 + 4 + 1 + nodeIdLen + 1 + payloadLen + ESPNOW_HMAC_LEN;
    if (needed > bufCap) {
        Logger::error(TAG, "encode: output buffer too small");
        return false;
    }

    size_t offset = 0;
    outBuf[offset++] = msg.protocolVersion;
    outBuf[offset++] = (uint8_t)msg.type;
    outBuf[offset++] = (uint8_t)(msg.sequenceNumber & 0xFF);
    outBuf[offset++] = (uint8_t)((msg.sequenceNumber >> 8) & 0xFF);
    outBuf[offset++] = (uint8_t)((msg.sequenceNumber >> 16) & 0xFF);
    outBuf[offset++] = (uint8_t)((msg.sequenceNumber >> 24) & 0xFF);
    outBuf[offset++] = (uint8_t)nodeIdLen;
    memcpy(outBuf + offset, msg.senderNodeId.c_str(), nodeIdLen);
    offset += nodeIdLen;
    outBuf[offset++] = (uint8_t)payloadLen;
    memcpy(outBuf + offset, msg.payload.c_str(), payloadLen);
    offset += payloadLen;

    uint8_t hmac[ESPNOW_HMAC_LEN];
    computeHmac(outBuf, offset, hmac);
    memcpy(outBuf + offset, hmac, ESPNOW_HMAC_LEN);
    offset += ESPNOW_HMAC_LEN;

    outLen = offset;
    return true;
}

bool EspNowProtocol::decode(const uint8_t* buf, size_t len, EspNowMessage& outMsg) {
    // Minimum: 1+1+4+1+0+1+0+8 = 16 bytes (empty nodeId and payload)
    if (len < 16) {
        return false;
    }

    size_t offset = 0;
    uint8_t protocolVersion = buf[offset++];
    if (protocolVersion != ESPNOW_PROTOCOL_VERSION) {
        Logger::warn(TAG, "decode: protocol version mismatch (got " +
                     String(protocolVersion) + ", expected " + String(ESPNOW_PROTOCOL_VERSION) + ")");
        return false;
    }
    uint8_t typeByte = buf[offset++];

    uint32_t seq = (uint32_t)buf[offset] | ((uint32_t)buf[offset + 1] << 8) |
                   ((uint32_t)buf[offset + 2] << 16) | ((uint32_t)buf[offset + 3] << 24);
    offset += 4;

    if (offset >= len) return false;
    uint8_t nodeIdLen = buf[offset++];
    if (nodeIdLen > ESPNOW_MAX_NODEID_LEN || offset + nodeIdLen > len) return false;
    String nodeId;
    nodeId.reserve(nodeIdLen);
    for (uint8_t i = 0; i < nodeIdLen; i++) {
        nodeId += (char)buf[offset + i];
    }
    offset += nodeIdLen;

    if (offset >= len) return false;
    uint8_t payloadLen = buf[offset++];
    if (payloadLen > ESPNOW_MAX_PAYLOAD_LEN || offset + payloadLen > len) return false;
    String payload;
    payload.reserve(payloadLen);
    for (uint8_t i = 0; i < payloadLen; i++) {
        payload += (char)buf[offset + i];
    }
    offset += payloadLen;

    if (offset + ESPNOW_HMAC_LEN != len) {
        // Trailing/leading garbage — reject rather than silently accept a truncated frame.
        return false;
    }

    uint8_t expectedHmac[ESPNOW_HMAC_LEN];
    computeHmac(buf, offset, expectedHmac);
    if (memcmp(buf + offset, expectedHmac, ESPNOW_HMAC_LEN) != 0) {
        Logger::warn(TAG, "decode: HMAC verification failed — wrong key, corrupted, or "
                     "tampered packet from claimed sender \"" + nodeId + "\"");
        return false;
    }

    outMsg.protocolVersion = protocolVersion;
    outMsg.type = (EspNowMessageType)typeByte;
    outMsg.sequenceNumber = seq;
    outMsg.senderNodeId = nodeId;
    outMsg.payload = payload;
    return true;
}

}  // namespace CarSentinel
