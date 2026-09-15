#include "MacAddress.h"

namespace CarSentinel {

String macToString(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

bool macFromString(const String& str, uint8_t outMac[6]) {
    if (str.length() != 17) return false;
    unsigned int bytes[6];
    int n = sscanf(str.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
                    &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]);
    if (n != 6) return false;
    for (int i = 0; i < 6; i++) outMac[i] = (uint8_t)bytes[i];
    return true;
}

}  // namespace CarSentinel
