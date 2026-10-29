#include "Diagnostics.h"
#include "Logger.h"

#include <esp_system.h>
#include <LittleFS.h>

namespace CarSentinel {

static const char* TAG = "Diagnostics";

// Minimum free heap (bytes) below which the device is considered unhealthy. Conservative
// placeholder for Phase 1 — will be tuned once camera/sensor memory use is measured in
// later phases.
static const uint32_t MIN_HEALTHY_FREE_HEAP = 20000;

void Diagnostics::begin() {
    // Nothing to initialize yet; present for symmetry with the other subsystems and so
    // later phases (e.g. persisting reboot-reason history) have an obvious place to add it.
}

String Diagnostics::resetReasonToString() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXTERNAL";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
        case ESP_RST_WDT: return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP_WAKE";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}

DiagnosticsSnapshot Diagnostics::snapshot() {
    DiagnosticsSnapshot snap;
    snap.uptimeMs = millis();
    snap.freeHeap = ESP.getFreeHeap();
    snap.minFreeHeap = ESP.getMinFreeHeap();
    snap.resetReason = resetReasonToString();
    return snap;
}

void Diagnostics::logSnapshot(const char* tag) {
    DiagnosticsSnapshot snap = snapshot();
    Logger::info(tag, "uptime=" + String(snap.uptimeMs / 1000) + "s freeHeap=" +
                 String(snap.freeHeap) + " minFreeHeap=" + String(snap.minFreeHeap) +
                 " resetReason=" + snap.resetReason);
}

bool Diagnostics::healthCheck() {
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < MIN_HEALTHY_FREE_HEAP) {
        Logger::warn(TAG, "healthCheck FAIL: freeHeap=" + String(freeHeap) +
                     " below threshold " + String(MIN_HEALTHY_FREE_HEAP));
        return false;
    }
    return true;
}

bool Diagnostics::selfTest() {
    bool ok = true;

    if (LittleFS.exists("/config/device.json")) {
        Logger::info(TAG, "selfTest: filesystem+config OK");
    } else {
        Logger::error(TAG, "selfTest: /config/device.json missing after DeviceConfig::begin()");
        ok = false;
    }

    if (!healthCheck()) {
        ok = false;
    }

    Logger::info(TAG, ok ? "selfTest PASSED" : "selfTest FAILED");
    return ok;
}

}  // namespace CarSentinel
