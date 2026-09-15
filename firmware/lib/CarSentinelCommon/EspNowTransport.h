#pragma once

#include "Transport.h"
#include <esp_now.h>

// ESP-NOW implementation of Transport. Callback signatures here target this toolchain's
// actual bundled esp_now.h (confirmed by reading the installed header, not assumed):
// esp_now_recv_cb_t is `void(*)(const uint8_t *mac_addr, const uint8_t *data, int data_len)`
// — the older/simpler form, not the newer esp_now_recv_info_t-based signature some ESP-IDF
// 5.x releases use. If a future toolchain upgrade changes this, the trampoline functions
// in EspNowTransport.cpp are the first place to fix.
namespace CarSentinel {

class EspNowTransport : public Transport {
public:
    bool begin() override;
    bool sendTo(const uint8_t mac[6], const uint8_t* data, size_t len) override;
    bool broadcastMessage(const uint8_t* data, size_t len) override;
    bool registerPeer(const uint8_t mac[6]) override;
    bool removePeer(const uint8_t mac[6]) override;
    void setReceiveCallback(ReceiveCallback cb) override;

private:
    static ReceiveCallback receiveCallback;
    static void onDataRecvTrampoline(const uint8_t* macAddr, const uint8_t* data, int dataLen);
    static void onDataSentTrampoline(const uint8_t* macAddr, esp_now_send_status_t status);
};

extern const uint8_t ESPNOW_BROADCAST_ADDR[6];

}  // namespace CarSentinel
