#include "DeviceIdentity.h"
#include <esp_mac.h>

namespace CarSentinel {

String DeviceIdentity::macAddress() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

String DeviceIdentity::generateDefaultNodeId(const char* prefix) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s-%02X%02X%02X", prefix, mac[3], mac[4], mac[5]);
    return String(buf);
}

}  // namespace CarSentinel
