#pragma once

#include <Arduino.h>

// Section 12: application code (EspNowManager and above) must depend on this interface,
// never directly on esp_now_*() calls — that keeps the door open for a future transport
// (e.g. a mesh library, or Wi-Fi UDP for gateway-to-cloud) without touching the protocol
// or application layers. EspNowTransport is the only implementation today.
namespace CarSentinel {

class Transport {
public:
    virtual ~Transport() {}

    virtual bool begin() = 0;

    virtual bool sendTo(const uint8_t mac[6], const uint8_t* data, size_t len) = 0;
    virtual bool broadcastMessage(const uint8_t* data, size_t len) = 0;

    virtual bool registerPeer(const uint8_t mac[6]) = 0;
    virtual bool removePeer(const uint8_t mac[6]) = 0;

    typedef void (*ReceiveCallback)(const uint8_t mac[6], const uint8_t* data, size_t len);
    virtual void setReceiveCallback(ReceiveCallback cb) = 0;
};

}  // namespace CarSentinel
