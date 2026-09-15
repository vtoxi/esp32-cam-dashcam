// CarSentinel Gateway — Phase 1 (device foundation) + Phase 2 (BLE/Wi-Fi provisioning)
// + Phase 3 (hardware capability layer).
//
// Hardware capabilities (I2C buses for IMU/OLED, GPS UART) are initialized before
// Wi-Fi/provisioning for the same reason as the node: they shouldn't depend on network
// state. Full driver logic (rendering pages, parsing NMEA, reading accel/gyro) is
// Phase 8/9/13 — Phase 3 only proves presence detection on the proposed (not yet
// bench-verified) gateway GPIOs from docs/wiring/ESP32_S3_GATEWAY.md.

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
#include "HardwareProfiles.h"
#include "CapabilitiesConfig.h"
#include "I2CBusManager.h"
#include "GpsUart.h"

using namespace CarSentinel;

static const char* TAG = "Gateway";
static unsigned long lastDiagnosticsLog = 0;
static const unsigned long DIAGNOSTICS_INTERVAL_MS = 30000;
static const unsigned long GPS_CHECK_INTERVAL_MS = 10000;
static unsigned long lastGpsCheck = 0;
static bool provisioningMode = false;

static const uint8_t MPU6050_I2C_ADDR = 0x68;
static const uint8_t SSD1306_I2C_ADDR = 0x3C;

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

static void initHardwareCapabilities() {
    DeviceConfigData dev = DeviceConfig::get();
    if (dev.hardwareProfile == "UNKNOWN") {
        dev.hardwareProfile = PROFILE_ESP32_S3_N16R8_GATEWAY;
        DeviceConfig::save(dev);
        Logger::info(TAG, "hardwareProfile defaulted to " + dev.hardwareProfile);
    }

    CapabilitiesConfigData defaults = defaultCapabilitiesForProfile(dev.hardwareProfile);
    CapabilitiesConfig::begin(defaults);
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();

    Logger::warn(TAG, "Gateway GPIO assignments are PROPOSED, not yet bench-verified — "
                 "see docs/wiring/ESP32_S3_GATEWAY.md Section 11 for the required test");

    if (caps.imu || (caps.display && caps.displayCount >= 1)) {
        I2CBusManager::begin(0, caps.imuSdaGpio, caps.imuSclGpio);
    }
    if (caps.display && caps.displayCount >= 2) {
        I2CBusManager::begin(1, caps.display2SdaGpio, caps.display2SclGpio);
    }

    if (caps.imu) {
        bool present = I2CBusManager::isPresent(0, MPU6050_I2C_ADDR);
        Logger::info(TAG, String("MPU6050 (0x68) on bus 0: ") + (present ? "PRESENT" : "NOT FOUND"));
    }
    if (caps.display && caps.displayCount >= 1) {
        bool present = I2CBusManager::isPresent(0, SSD1306_I2C_ADDR);
        Logger::info(TAG, String("SSD1306 #1 (0x3C) on bus 0: ") + (present ? "PRESENT" : "NOT FOUND"));
    }
    if (caps.display && caps.displayCount >= 2) {
        bool present = I2CBusManager::isPresent(1, SSD1306_I2C_ADDR);
        Logger::info(TAG, String("SSD1306 #2 (0x3C) on bus 1: ") + (present ? "PRESENT" : "NOT FOUND"));
    }

    if (caps.gps) {
        GpsUart::begin(caps.gpsRxGpio, caps.gpsTxGpio);
    } else {
        Logger::info(TAG, "GPS capability disabled; skipping UART init");
    }
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
        const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
        Logger::info(TAG, "nodeId=" + cfg.nodeId + " displayName=" + cfg.displayName +
                     " role=" + String(roleToString(cfg.role)) +
                     " hardwareProfile=" + cfg.hardwareProfile +
                     " firmwareVersion=" + cfg.firmwareVersion +
                     " mac=" + DeviceIdentity::macAddress());
        Logger::info(TAG, "wifi: hasCredentials=" + String(NetworkConfig::hasCredentials() ? "true" : "false") +
                     " connected=" + String(WiFiManager::isConnected() ? "true" : "false") +
                     " ip=" + (WiFiManager::isConnected() ? WiFiManager::localIP() : String("-")) +
                     " ssid=" + net.ssid);
        Logger::info(TAG, "capabilities: gps=" + String(caps.gps) + " imu=" + String(caps.imu) +
                     " display=" + String(caps.display) + "(" + String(caps.displayCount) + ")");
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

    initHardwareCapabilities();

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
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
    if (caps.gps && now - lastGpsCheck >= GPS_CHECK_INTERVAL_MS) {
        lastGpsCheck = now;
        int drained = GpsUart::drain();
        Logger::info(TAG, "GPS: " + String(drained) + " bytes received in last check window");
    }

    if (now - lastDiagnosticsLog >= DIAGNOSTICS_INTERVAL_MS) {
        lastDiagnosticsLog = now;
        Diagnostics::logSnapshot(TAG);
        if (!Diagnostics::healthCheck()) {
            Logger::warn(TAG, "Periodic health check failed");
        }
    }

    delay(50);
}
