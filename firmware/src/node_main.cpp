// CarSentinel generic Node (camera/sensor/display) — Phase 1 generic boot flow.
//
// Same Phase 1 scope as gateway_main.cpp: identity, persistent versioned config,
// factory reset, diagnostics, watchdog, logging. No camera/SD/RCWL/DHT yet (Phase 3+) —
// this file proves a single generic node image boots and identifies itself without any
// per-camera-position source code, which every later phase builds on.

#include <Arduino.h>
#include "Logger.h"
#include "DeviceConfig.h"
#include "DeviceIdentity.h"
#include "Diagnostics.h"
#include "Watchdog.h"

using namespace CarSentinel;

static const char* TAG = "Node";
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
        DeviceConfig::factoryReset("NODE", DeviceRole::UNASSIGNED);
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
    Logger::info(TAG, "CarSentinel Node booting, firmware=" CARSENTINEL_FIRMWARE_VERSION);

    Diagnostics::begin();

    // Role starts UNASSIGNED, not CAMERA — a freshly flashed generic node has no
    // predetermined role until BLE/AP provisioning (Phase 2) or manual config assigns
    // one. This is the literal implementation of "hardware becomes configuration."
    if (!DeviceConfig::begin("NODE", DeviceRole::UNASSIGNED)) {
        Logger::error(TAG, "DeviceConfig::begin failed — halting boot");
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
