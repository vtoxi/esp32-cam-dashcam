#include "Watchdog.h"
#include "Logger.h"

#include <esp_task_wdt.h>

namespace CarSentinel {

static const char* TAG = "Watchdog";

void Watchdog::begin(uint32_t timeoutSeconds) {
    // esp_task_wdt_config_t is the arduino-esp32 3.x / ESP-IDF 5.x task-watchdog API.
    // If the project toolchain pins an older arduino-esp32 core, this call signature
    // will need updating to the legacy esp_task_wdt_init(timeout_s, panic) form.
    esp_task_wdt_config_t config = {
        .timeout_ms = timeoutSeconds * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&config);
    esp_task_wdt_add(NULL);  // register the calling task (loop task, if called from setup())
    Logger::info(TAG, "Watchdog armed, timeout=" + String(timeoutSeconds) + "s");
}

void Watchdog::feed() {
    esp_task_wdt_reset();
}

}  // namespace CarSentinel
