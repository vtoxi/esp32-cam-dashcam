// CarSentinel Gateway — Phase 1 (device foundation) + Phase 2 (BLE/Wi-Fi provisioning).
//
// Boot flow: load identity/config (Phase 1), then try the saved Wi-Fi credentials with
// a bounded retry (Section 10 — never block forever). If that fails or no credentials
// exist yet, open BLE provisioning and a temporary AP web form simultaneously (Section
// 9) and wait for either to receive a valid configuration, then reboot into normal
// (STA) operation. No ESP-NOW/sensors/camera yet (Phase 3+).

#include <Arduino.h>
#include "Logger.h"
#include "DeviceConfig.h"
#include "DeviceIdentity.h"
#include "Diagnostics.h"
#include "Watchdog.h"
#include "NetworkConfig.h"
#include "WiFiManager.h"
#include "ProvisioningPortal.h"
#include "BLEProvisioning.h"

using namespace CarSentinel;

static const char* TAG = "Gateway";
static unsigned long lastDiagnosticsLog = 0;
static const unsigned long DIAGNOSTICS_INTERVAL_MS = 30000;
static bool provisioningMode = false;

static void enterProvisioningMode() {
    provisioningMode = true;
    const DeviceConfigData& dev = DeviceConfig::get();
    String suffix = dev.nodeId.substring(dev.nodeId.length() >= 6 ? dev.nodeId.length() - 6 : 0);
    String apSsid = "CarSentinel-Setup-" + suffix;

    Logger::info(TAG, "Entering provisioning mode (BLE + AP): " + apSsid);
    ProvisioningPortal::begin(apSsid);
    BLEProvisioning::begin(apSsid);
}

static void restartInto(const char* reason) {
    Logger::warn(TAG, String("Restarting: ") + reason);
    delay(200);
    ESP.restart();
}

static void handleSerialCommands() {
    if (!Serial.available()) {
        return;
    }
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line == "FACTORY_RESET") {
        Logger::warn(TAG, "FACTORY_RESET command received via serial");
        DeviceConfig::factoryReset("GATEWAY", DeviceRole::GATEWAY);
        NetworkConfig::clearCredentials();
        restartInto("factory reset complete");
    } else if (line == "PROVISION") {
        Logger::warn(TAG, "PROVISION command received via serial — clearing Wi-Fi credentials");
        NetworkConfig::clearCredentials();
        restartInto("re-entering provisioning");
    } else if (line == "STATUS") {
        const DeviceConfigData& cfg = DeviceConfig::get();
        const NetworkConfigData& net = NetworkConfig::get();
        Logger::info(TAG, "nodeId=" + cfg.nodeId + " displayName=" + cfg.displayName +
                     " role=" + String(roleToString(cfg.role)) +
                     " hardwareProfile=" + cfg.hardwareProfile +
                     " firmwareVersion=" + cfg.firmwareVersion +
                     " mac=" + DeviceIdentity::macAddress());
        Logger::info(TAG, "wifi: hasCredentials=" + String(NetworkConfig::hasCredentials() ? "true" : "false") +
                     " connected=" + String(WiFiManager::isConnected() ? "true" : "false") +
                     " ip=" + (WiFiManager::isConnected() ? WiFiManager::localIP() : String("-")) +
                     " ssid=" + net.ssid);
        Diagnostics::logSnapshot(TAG);
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Logger::begin(LogLevel::INFO);
    Logger::info(TAG, "CarSentinel Gateway booting, firmware=" CARSENTINEL_FIRMWARE_VERSION);

    Diagnostics::begin();

    if (!DeviceConfig::begin("GATEWAY", DeviceRole::GATEWAY)) {
        Logger::error(TAG, "DeviceConfig::begin failed — halting boot");
        while (true) { delay(1000); }
    }

    const DeviceConfigData& cfg = DeviceConfig::get();
    Logger::info(TAG, "Identity: nodeId=" + cfg.nodeId + " role=" +
                 String(roleToString(cfg.role)) + " mac=" + DeviceIdentity::macAddress());

    if (!NetworkConfig::begin(cfg.nodeId)) {
        Logger::error(TAG, "NetworkConfig::begin failed — continuing without persisted Wi-Fi config");
    }

    Diagnostics::selfTest();
    Watchdog::begin(10);

    if (NetworkConfig::hasCredentials()) {
        bool connected = WiFiManager::connectBlocking(NetworkConfig::get());
        if (!connected) {
            Logger::warn(TAG, "Saved Wi-Fi credentials failed to connect; opening provisioning");
            enterProvisioningMode();
        }
    } else {
        Logger::info(TAG, "No saved Wi-Fi credentials; opening provisioning");
        enterProvisioningMode();
    }

    Logger::info(TAG, "Boot complete. Serial commands: STATUS, FACTORY_RESET, PROVISION");
    Diagnostics::logSnapshot(TAG);
}

void loop() {
    Watchdog::feed();
    handleSerialCommands();

    if (provisioningMode) {
        ProvisioningPortal::loop();
        if (ProvisioningPortal::isSubmitted() || BLEProvisioning::isCommitted()) {
            ProvisioningPortal::stop();
            BLEProvisioning::stop();
            restartInto("provisioning complete");
        }
    } else {
        WiFiManager::loop();
    }

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
