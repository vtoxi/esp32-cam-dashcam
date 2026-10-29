#include "PowerManager.h"
#include "Logger.h"

#include <WiFi.h>

namespace CarSentinel {

static const char* TAG = "PowerManager";

void PowerManager::applyModeChange(SecurityMode mode) {
    bool lowPower = (mode == SecurityMode::PARKED);
    bool applied = WiFi.setSleep(lowPower);
    Logger::info(TAG, String("Wi-Fi modem sleep ") + (lowPower ? "enabled" : "disabled") +
                 " for mode " + securityModeToString(mode) +
                 (applied ? "" : " (WiFi.setSleep() reported failure — radio state unchanged)"));
}

}  // namespace CarSentinel
