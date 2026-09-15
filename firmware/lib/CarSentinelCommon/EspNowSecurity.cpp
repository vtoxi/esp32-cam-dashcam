#include "EspNowSecurity.h"
#include "Logger.h"

#include <LittleFS.h>

namespace CarSentinel {

static const char* TAG = "EspNowSecurity";
static const char* KEY_PATH = "/config/espnow_psk.bin";

// INSECURE DEFAULT — see header comment. 32 bytes, the ASCII text below padded/truncated
// to exactly 32.
static const uint8_t DEFAULT_INSECURE_KEY[EspNowSecurity::KEY_LEN] = {
    'C','a','r','S','e','n','t','i','n','e','l','-','I','N','S','E',
    'C','U','R','E','-','D','E','F','A','U','L','T','-','K','E','Y'
};

uint8_t EspNowSecurity::key[EspNowSecurity::KEY_LEN];

void EspNowSecurity::begin() {
    if (LittleFS.exists(KEY_PATH)) {
        File f = LittleFS.open(KEY_PATH, "r");
        if (f && f.size() == KEY_LEN) {
            f.read(key, KEY_LEN);
            f.close();
            bool isDefault = memcmp(key, DEFAULT_INSECURE_KEY, KEY_LEN) == 0;
            if (isDefault) {
                Logger::warn(TAG, "ESP-NOW key file exists but still holds the INSECURE "
                             "DEFAULT key — replace " + String(KEY_PATH) +
                             " with a private 32-byte key on every device before real use");
            } else {
                Logger::info(TAG, "Loaded custom ESP-NOW key from " + String(KEY_PATH));
            }
            return;
        }
        if (f) f.close();
        Logger::warn(TAG, String(KEY_PATH) + " exists but has wrong size — rewriting default");
    }

    memcpy(key, DEFAULT_INSECURE_KEY, KEY_LEN);
    File out = LittleFS.open(KEY_PATH, "w");
    if (out) {
        out.write(key, KEY_LEN);
        out.close();
    }
    Logger::warn(TAG, "Using INSECURE DEFAULT ESP-NOW key (Section 41) — every device "
                 "ships with the same key until you replace " + String(KEY_PATH) +
                 " with a private 32-byte key on every device in the network");
}

const uint8_t* EspNowSecurity::getKey() {
    return key;
}

size_t EspNowSecurity::getKeyLength() {
    return KEY_LEN;
}

}  // namespace CarSentinel
