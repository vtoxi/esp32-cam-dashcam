#include "EspNowTransport.h"
#include "Logger.h"

#include <WiFi.h>
#include <esp_wifi.h>

namespace CarSentinel {

static const char* TAG = "EspNowTransport";

const uint8_t ESPNOW_BROADCAST_ADDR[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

Transport::ReceiveCallback EspNowTransport::receiveCallback = nullptr;

void EspNowTransport::onDataRecvTrampoline(const uint8_t* macAddr, const uint8_t* data, int dataLen) {
    if (receiveCallback && dataLen > 0) {
        receiveCallback(macAddr, data, (size_t)dataLen);
    }
}

void EspNowTransport::onDataSentTrampoline(const uint8_t* macAddr, esp_now_send_status_t status) {
    // Link-layer delivery confirmation only (radio ACK, not application ACK) — logged at
    // TRACE-equivalent verbosity via DEBUG since app-level reliability is handled by
    // EspNowManager's own ACK/retry, not this callback.
    if (status != ESP_NOW_SEND_SUCCESS) {
        Logger::debug(TAG, "Link-layer send report: FAIL");
    }
}

bool EspNowTransport::begin() {
    if (esp_now_init() != ESP_OK) {
        Logger::error(TAG, "esp_now_init failed");
        return false;
    }
    esp_now_register_recv_cb(onDataRecvTrampoline);
    esp_now_register_send_cb(onDataSentTrampoline);

    // Broadcast address must be registered as a peer before esp_now_send() can target it,
    // same as any unicast peer.
    if (!registerPeer(ESPNOW_BROADCAST_ADDR)) {
        Logger::error(TAG, "Failed to register broadcast peer");
        return false;
    }

    Logger::info(TAG, "ESP-NOW transport initialized");
    return true;
}

bool EspNowTransport::sendTo(const uint8_t mac[6], const uint8_t* data, size_t len) {
    esp_err_t err = esp_now_send(mac, data, len);
    if (err != ESP_OK) {
        Logger::warn(TAG, "esp_now_send failed: 0x" + String(err, HEX));
        return false;
    }
    return true;
}

bool EspNowTransport::broadcastMessage(const uint8_t* data, size_t len) {
    return sendTo(ESPNOW_BROADCAST_ADDR, data, len);
}

bool EspNowTransport::registerPeer(const uint8_t mac[6]) {
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0;  // use whatever channel the STA/AP interface is currently on
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = false;  // app-layer HMAC (EspNowProtocol) provides authenticity;
                                // ESP-NOW's own peer-key encryption is not enabled this
                                // phase — see docs/IMPLEMENTATION_PLAN.md Phase 5 known
                                // limitations.
    esp_err_t err = esp_now_add_peer(&peerInfo);
    if (err != ESP_OK) {
        Logger::warn(TAG, "esp_now_add_peer failed: 0x" + String(err, HEX));
        return false;
    }
    return true;
}

bool EspNowTransport::removePeer(const uint8_t mac[6]) {
    if (!esp_now_is_peer_exist(mac)) {
        return true;
    }
    return esp_now_del_peer(mac) == ESP_OK;
}

void EspNowTransport::setReceiveCallback(ReceiveCallback cb) {
    receiveCallback = cb;
}

}  // namespace CarSentinel
