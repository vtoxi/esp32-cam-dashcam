// CarSentinel generic Node — Phase 1 (device foundation) + Phase 2 (BLE/Wi-Fi
// provisioning) + Phase 3 (hardware capability layer).
//
// Boot order matters here: hardware capabilities (SD/RCWL/DHT) are initialized BEFORE
// Wi-Fi/provisioning, and polled in loop() unconditionally — regardless of
// provisioning/Wi-Fi state — because Section 5 requires motion sensing and local
// evidence capture to keep working even while a node is unprovisioned or mid-setup.
// Camera presence is capability-flagged here but actual esp32-camera init is Phase 4.

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
#include "SdStorage.h"
#include "MotionSensor.h"
#include "TemperatureHumiditySensor.h"

using namespace CarSentinel;

static const char* TAG = "Node";
static unsigned long lastDiagnosticsLog = 0;
static const unsigned long DIAGNOSTICS_INTERVAL_MS = 30000;
static const unsigned long DHT_READ_INTERVAL_MS = 5000;  // DHT11 min ~1s; 5s is comfortably above it
static unsigned long lastDhtRead = 0;
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

static void initHardwareCapabilities() {
    DeviceConfigData dev = DeviceConfig::get();
    if (dev.hardwareProfile == "UNKNOWN") {
        dev.hardwareProfile = PROFILE_ESP32_CAM_AI_THINKER;
        DeviceConfig::save(dev);
        Logger::info(TAG, "hardwareProfile defaulted to " + dev.hardwareProfile);
    }

    CapabilitiesConfigData defaults = defaultCapabilitiesForProfile(dev.hardwareProfile);
    CapabilitiesConfig::begin(defaults);
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();

    Logger::info(TAG, "Capabilities: camera=" + String(caps.camera) + " sd=" + String(caps.sd) +
                 " rcwl=" + String(caps.rcwl) + "(gpio=" + String(caps.rcwlGpio) + ")" +
                 " dht=" + String(caps.dht) + "(gpio=" + String(caps.dhtGpio) + ")");

    if (caps.camera) {
        Logger::info(TAG, "Camera capability present — esp32-camera init deferred to Phase 4");
    }

    if (caps.sd) {
        if (!SdStorage::begin()) {
            Logger::warn(TAG, "SD unavailable — continuing without local evidence storage (Section 59)");
        }
    } else {
        Logger::info(TAG, "SD capability disabled; skipping mount");
    }

    if (caps.rcwl && caps.rcwlGpio != GPIO_UNCONFIGURED) {
        MotionSensor::begin(caps.rcwlGpio);
    } else {
        Logger::info(TAG, "RCWL capability disabled or GPIO unconfigured; skipping init "
                     "(enable via capabilities.json once a bench test confirms a free GPIO — "
                     "see docs/HARDWARE.md open questions)");
    }

    if (caps.dht && caps.dhtGpio != GPIO_UNCONFIGURED) {
        TemperatureHumiditySensor::begin(caps.dhtGpio);
    } else {
        Logger::info(TAG, "DHT capability disabled or GPIO unconfigured; skipping init");
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
        DeviceConfig::factoryReset("NODE", DeviceRole::UNASSIGNED);
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
        Logger::info(TAG, "sd.mounted=" + String(SdStorage::status().mounted) +
                     " rcwl.enabled=" + String(caps.rcwl) + " dht.enabled=" + String(caps.dht));
        Diagnostics::logSnapshot(TAG);
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Logger::begin(LogLevel::INFO);
    Logger::info(TAG, "CarSentinel Node booting, firmware=" CARSENTINEL_FIRMWARE_VERSION);

    Diagnostics::begin();

    if (!DeviceConfig::begin("NODE", DeviceRole::UNASSIGNED)) {
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

    // Hardware capabilities before Wi-Fi/provisioning: local sensing must not depend on
    // network state (Section 5).
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

    // Sensor polling runs unconditionally — independent of provisioning/Wi-Fi state
    // (Section 5). Full debounce/cooldown/event pipeline lands in Phase 4; this is just
    // proof the capability layer reads live hardware.
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
    if (caps.rcwl && caps.rcwlGpio != GPIO_UNCONFIGURED && MotionSensor::isTriggered()) {
        Logger::info(TAG, "RCWL: motion signal HIGH");
    }
    unsigned long now = millis();
    if (caps.dht && caps.dhtGpio != GPIO_UNCONFIGURED && now - lastDhtRead >= DHT_READ_INTERVAL_MS) {
        lastDhtRead = now;
        TemperatureHumidityReading r = TemperatureHumiditySensor::read();
        if (r.valid) {
            Logger::info(TAG, "DHT: temp=" + String(r.temperatureC, 1) + "C humidity=" +
                         String(r.humidityPercent, 1) + "%");
        }
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
