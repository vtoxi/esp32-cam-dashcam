#include "Watchdog.h"
#include "Logger.h"

#include <esp_task_wdt.h>

namespace CarSentinel {

static const char* TAG = "Watchdog";

void Watchdog::begin(uint32_t timeoutSeconds) {
    // Legacy (ESP-IDF 4.x-style) esp_task_wdt_init(timeout_s, panic) signature — this is
    // what the pinned platform-espressif32/arduino-esp32 core (3.20017.241212) actually
    // provides. The newer esp_task_wdt_config_t struct form (ESP-IDF 5.x) doesn't exist
    // in this toolchain; switch back if the pinned core is later upgraded past it.
    esp_task_wdt_init(timeoutSeconds, true);
    esp_task_wdt_add(NULL);  // register the calling task (loop task, if called from setup())
    Logger::info(TAG, "Watchdog armed, timeout=" + String(timeoutSeconds) + "s");
}

void Watchdog::feed() {
    esp_task_wdt_reset();
}

}  // namespace CarSentinel
