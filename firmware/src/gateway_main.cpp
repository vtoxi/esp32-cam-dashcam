// CarSentinel Gateway — Phase 1 generic boot flow.
//
// Phase 1 scope only: device identity, persistent versioned config, factory reset,
// diagnostics, watchdog, structured logging. No Wi-Fi/BLE (Phase 2), no ESP-NOW
// (Phase 5), no sensors/camera (Phase 3+). This file and node_main.cpp intentionally
// share almost all logic — the "generic firmware, configuration decides role" principle
// starts here: only the compiled-in default role/prefix differ per target.

#include <Arduino.h>
#include "Logger.h"
#include "DeviceConfig.h"
#include "DeviceIdentity.h"
#include "Diagnostics.h"
#include "Watchdog.h"

using namespace CarSentinel;

static const char* TAG = "Gateway";
static unsigned long lastDiagnosticsLog = 0;
static const unsigned long DIAGNOSTICS_INTERVAL_MS = 30000;

static void handleSerialCommands() {
    if (!Serial.available()) {
        return;
    }
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "FACTORY_RESET") {
        Logger::warn(TAG, "FACTORY_RESET command received via serial");
        DeviceConfig::factoryReset("GATEWAY", DeviceRole::GATEWAY);
        Logger::info(TAG, "Factory reset complete, restarting");
        delay(200);
        ESP.restart();
    } else if (line == "STATUS") {
        const DeviceConfigData& cfg = DeviceConfig::get();
        Logger::info(TAG, "nodeId=" + cfg.nodeId + " displayName=" + cfg.displayName +
                     " role=" + String(roleToString(cfg.role)) +
                     " hardwareProfile=" + cfg.hardwareProfile +
                     " firmwareVersion=" + cfg.firmwareVersion +
                     " mac=" + DeviceIdentity::macAddress());
        Diagnostics::logSnapshot(TAG);
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);  // let USB-serial settle before first log line

    Logger::begin(LogLevel::INFO);
    Logger::info(TAG, "CarSentinel Gateway booting, firmware=" CARSENTINEL_FIRMWARE_VERSION);

    Diagnostics::begin();

    if (!DeviceConfig::begin("GATEWAY", DeviceRole::GATEWAY)) {
        Logger::error(TAG, "DeviceConfig::begin failed — halting boot");
        // No SD/camera dependency yet to fail gracefully around; a config subsystem
        // failure at this phase is unrecoverable without the watchdog forcing a retry.
        while (true) {
            delay(1000);
        }
    }

    const DeviceConfigData& cfg = DeviceConfig::get();
    Logger::info(TAG, "Identity: nodeId=" + cfg.nodeId + " role=" +
                 String(roleToString(cfg.role)) + " mac=" + DeviceIdentity::macAddress());

    Diagnostics::selfTest();

    Watchdog::begin(10);

    Logger::info(TAG, "Boot complete. Serial commands: STATUS, FACTORY_RESET");
    Diagnostics::logSnapshot(TAG);
}

void loop() {
    Watchdog::feed();
    handleSerialCommands();

    unsigned long now = millis();
    if (now - lastDiagnosticsLog >= DIAGNOSTICS_INTERVAL_MS) {
        lastDiagnosticsLog = now;
        Diagnostics::logSnapshot(TAG);
        if (!Diagnostics::healthCheck()) {
            Logger::warn(TAG, "Periodic health check failed");
        }
    }

    delay(50);
}
