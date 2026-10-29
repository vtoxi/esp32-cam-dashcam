#include "EspNowManager.h"
#include "Transport.h"
#include "EspNowTransport.h"
#include "EspNowSecurity.h"
#include "PeerRegistry.h"
#include "Logger.h"
#include "Diagnostics.h"
#include "DeviceConfig.h"
#include "WiFiManager.h"

#include <ArduinoJson.h>
#include <WiFi.h>

namespace CarSentinel {

static const char* TAG = "EspNowManager";

// The only Transport implementation today (Section 12) — accessed exclusively through
// the Transport* interface below, never esp_now_*() directly, from this point on.
static EspNowTransport espNowTransportImpl;
static Transport* transport = &espNowTransportImpl;

String EspNowManager::myNodeId;
String EspNowManager::myRole;
uint32_t EspNowManager::nextSequenceNumber = 1;
EspNowManager::PendingSend EspNowManager::pending[EspNowManager::MAX_PENDING];
unsigned long EspNowManager::lastHeartbeat = 0;
EspNowMessageHandler EspNowManager::onMessageHandler = nullptr;
EspNowMessageHandler EspNowManager::onPeerHeartbeatHandler = nullptr;

bool EspNowManager::begin(const String& nodeId, const String& role) {
    myNodeId = nodeId;
    myRole = role;

    EspNowSecurity::begin();

    // esp_now_init() needs the WiFi driver already brought up — even though ESP-NOW
    // never associates to an access point, it reuses the WiFi driver's radio/netif
    // plumbing internally. Since ESP-NOW now starts unconditionally, before any
    // Wi-Fi/provisioning code has ever touched the WiFi driver (docs/NETWORK.md:
    // "ESP-NOW starts unconditionally, regardless of Wi-Fi/provisioning state"),
    // this call can no longer assume something upstream already did it — a real
    // boot crash (esp_now_init() dereferencing uninitialized WiFi/netif state,
    // Guru Meditation LoadProhibited) was observed on ESP32-S3 gateway hardware
    // without this. WIFI_STA is idempotent/harmless to call again once real Wi-Fi
    // setup runs later — it does not connect to anything by itself.
    WiFi.mode(WIFI_STA);

    if (!transport->begin()) {
        Logger::error(TAG, "Transport init failed — ESP-NOW unavailable this boot");
        return false;
    }
    transport->setReceiveCallback(onReceive);

    Logger::info(TAG, "ESP-NOW manager started, nodeId=" + myNodeId + " role=" + myRole);
    sendHeartbeat();  // announce presence immediately rather than waiting a full interval
    lastHeartbeat = millis();
    return true;
}

void EspNowManager::sendHeartbeat() {
    JsonDocument doc;
    doc["role"] = myRole;
    doc["uptimeMs"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    if (WiFiManager::isConnected()) {
        doc["ip"] = WiFiManager::localIP();
    }
    // Carried so the gateway's DeviceRegistry can mirror a rename that happened locally
    // on this device (e.g. via BLE/AP provisioning) — RENAME's gateway-initiated path
    // already keeps the registry in sync, but until now a node-initiated rename had no
    // way back to the gateway. See DeviceRegistry::upsertFromDiscovery.
    doc["displayName"] = DeviceConfig::get().displayName;
    String payload;
    serializeJson(doc, payload);
    sendMessage(EspNowMessageType::HEARTBEAT, payload, nullptr);
}

bool EspNowManager::sendMessage(EspNowMessageType type, const String& jsonPayload, const uint8_t* targetMac) {
    EspNowMessage msg;
    msg.type = type;
    msg.sequenceNumber = nextSequenceNumber++;
    msg.senderNodeId = myNodeId;
    msg.payload = jsonPayload;

    uint8_t buf[EspNowProtocol::MAX_ENCODED_LEN];
    size_t len = 0;
    if (!EspNowProtocol::encode(msg, buf, sizeof(buf), len)) {
        return false;  // EspNowProtocol already logged why
    }

    bool broadcast = (targetMac == nullptr);
    bool sent = broadcast ? transport->broadcastMessage(buf, len) : transport->sendTo(targetMac, buf, len);
    if (!sent) {
        Logger::warn(TAG, "Send failed for " + String(messageTypeToString(type)));
        return false;
    }

    if (!broadcast && messageTypeExpectsAck(type)) {
        // Find a free (or the oldest) pending slot — MAX_PENDING is small and Phase 5's
        // traffic is low-rate (event-driven + a 20s heartbeat), so eviction under normal
        // use should be rare.
        uint8_t slot = 0;
        bool foundFree = false;
        for (uint8_t i = 0; i < MAX_PENDING; i++) {
            if (!pending[i].active) { slot = i; foundFree = true; break; }
        }
        if (!foundFree) {
            unsigned long oldest = pending[0].nextRetryAt;
            slot = 0;
            for (uint8_t i = 1; i < MAX_PENDING; i++) {
                if (pending[i].nextRetryAt < oldest) { oldest = pending[i].nextRetryAt; slot = i; }
            }
            Logger::warn(TAG, "Pending-ACK table full; evicting oldest unacknowledged send");
        }

        PendingSend& p = pending[slot];
        p.active = true;
        p.sequenceNumber = msg.sequenceNumber;
        memcpy(p.targetMac, targetMac, 6);
        memcpy(p.encoded, buf, len);
        p.encodedLen = len;
        p.attempts = 1;
        p.nextRetryAt = millis() + RETRY_INTERVAL_MS;
        p.type = type;
    }

    return true;
}

void EspNowManager::sendAckFor(uint32_t sequenceToAck, const uint8_t mac[6]) {
    JsonDocument doc;
    doc["ackSeq"] = sequenceToAck;
    String payload;
    serializeJson(doc, payload);
    sendMessage(EspNowMessageType::ACK, payload, mac);
}

void EspNowManager::resolvePending(uint32_t ackedSequence) {
    for (uint8_t i = 0; i < MAX_PENDING; i++) {
        if (pending[i].active && pending[i].sequenceNumber == ackedSequence) {
            pending[i].active = false;
            return;
        }
    }
}

void EspNowManager::onReceive(const uint8_t mac[6], const uint8_t* data, size_t len) {
    EspNowMessage msg;
    if (!EspNowProtocol::decode(data, len, msg)) {
        return;  // EspNowProtocol already logged the reason
    }

    String role;
    if (msg.type == EspNowMessageType::HEARTBEAT || msg.type == EspNowMessageType::HELLO) {
        JsonDocument doc;
        if (deserializeJson(doc, msg.payload) == DeserializationError::Ok) {
            role = doc["role"] | "";
        }
    }
    PeerInfo* peer = PeerRegistry::registerOrUpdate(mac, msg.senderNodeId, role);

    if (!PeerRegistry::acceptSequence(peer, msg.sequenceNumber)) {
        Logger::debug(TAG, "Dropping duplicate/replayed seq=" + String(msg.sequenceNumber) +
                      " from " + msg.senderNodeId);
        return;
    }

    if (msg.type == EspNowMessageType::ACK) {
        JsonDocument doc;
        if (deserializeJson(doc, msg.payload) == DeserializationError::Ok) {
            resolvePending(doc["ackSeq"] | 0);
        }
        return;
    }

    Logger::info(TAG, "RX " + String(messageTypeToString(msg.type)) + " from " +
                 msg.senderNodeId + " seq=" + String(msg.sequenceNumber) +
                 " payload=" + msg.payload);

    if (messageTypeExpectsAck(msg.type)) {
        sendAckFor(msg.sequenceNumber, mac);
    }

    bool isDiscoveryTraffic = (msg.type == EspNowMessageType::HELLO || msg.type == EspNowMessageType::HEARTBEAT);

    if (isDiscoveryTraffic && onPeerHeartbeatHandler) {
        onPeerHeartbeatHandler(msg, mac);
    }
    if (onMessageHandler && !isDiscoveryTraffic) {
        onMessageHandler(msg, mac);
    }
}

void EspNowManager::setOnMessageHandler(EspNowMessageHandler handler) {
    onMessageHandler = handler;
}

void EspNowManager::setOnPeerHeartbeatHandler(EspNowMessageHandler handler) {
    onPeerHeartbeatHandler = handler;
}

bool EspNowManager::findGatewayMac(uint8_t outMac[6]) {
    PeerInfo* gw = PeerRegistry::findByRole("GATEWAY");
    if (!gw) return false;
    memcpy(outMac, gw->mac, 6);
    return true;
}

void EspNowManager::loop() {
    unsigned long now = millis();

    if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeat = now;
        sendHeartbeat();
    }

    for (uint8_t i = 0; i < MAX_PENDING; i++) {
        PendingSend& p = pending[i];
        if (!p.active || now < p.nextRetryAt) continue;

        if (p.attempts >= MAX_RETRIES) {
            Logger::error(TAG, String(messageTypeToString(p.type)) + " seq=" +
                          String(p.sequenceNumber) + " undelivered after " +
                          String(p.attempts) + " attempts — giving up (no persistent "
                          "offline queue yet, Section 28/Phase 28)");
            p.active = false;
            continue;
        }

        transport->sendTo(p.targetMac, p.encoded, p.encodedLen);
        p.attempts++;
        p.nextRetryAt = now + (RETRY_INTERVAL_MS * p.attempts);
        Logger::warn(TAG, "Retrying " + String(messageTypeToString(p.type)) + " seq=" +
                     String(p.sequenceNumber) + " (attempt " + String(p.attempts) + ")");
    }
}

}  // namespace CarSentinel
