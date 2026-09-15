// CarSentinel generic Node — Phase 1 (device foundation) + Phase 2 (BLE/Wi-Fi
// provisioning) + Phase 3 (hardware capability layer) + Phase 4 (single camera node).
//
// Boot order: hardware capabilities (camera/SD/RCWL/DHT) initialize BEFORE
// Wi-Fi/provisioning, and the motion→capture→evidence pipeline runs unconditionally in
// loop() — regardless of provisioning/Wi-Fi state (Section 5 node independence). This
// phase makes a single node a complete standalone security device: RCWL trigger (via
// Phase 3's debounce-free raw read, now behind Phase 17's confirmation/cooldown state
// machine) → JPEG snapshot → local evidence file (Section 27/55), no gateway required.

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
#include "CameraManager.h"
#include "MotionEventEngine.h"
#include "EvidenceManager.h"
#include "EspNowManager.h"
#include "PeerRegistry.h"

#include <ArduinoJson.h>

using namespace CarSentinel;

static const char* TAG = "Node";
static unsigned long lastDiagnosticsLog = 0;
static const unsigned long DIAGNOSTICS_INTERVAL_MS = 30000;
static const unsigned long DHT_READ_INTERVAL_MS = 5000;  // DHT11 min ~1s; 5s is comfortably above it
static unsigned long lastDhtRead = 0;
static bool provisioningMode = false;
static bool espNowActive = false;
static TemperatureHumidityReading lastDhtReading;  // used as event environment context

static void enterProvisioningMode() {
    provisioningMode = true;
    const DeviceConfigData& dev = DeviceConfig::get();
    // Use the full role-prefixed nodeId (e.g. "NODE-A1B2C3" / "GATEWAY-A1B2C3"), not just
    // a MAC-derived hex suffix — a bare hex suffix doesn't say what kind of device it is,
    // making a gateway and a node indistinguishable in a Wi-Fi/BLE scan during setup.
    // Fits within the 32-byte Wi-Fi SSID limit for both current prefixes.
    String apSsid = "CarSentinel-" + dev.nodeId;

    Logger::info(TAG, "Entering provisioning mode (BLE + AP): " + apSsid);
    ProvisioningPortal::begin(apSsid);
    BLEProvisioning::begin(apSsid);
}

static void restartInto(const char* reason) {
    Logger::warn(TAG, String("Restarting: ") + reason);
    delay(200);
    ESP.restart();
}

static void captureAndRecordEvent(const String& eventType, const String& severity) {
    const DeviceConfigData& dev = DeviceConfig::get();

    EnvironmentReading env;
    env.valid = lastDhtReading.valid;
    env.temperatureC = lastDhtReading.temperatureC;
    env.humidityPercent = lastDhtReading.humidityPercent;

    String eventId;
    if (EvidenceManager::isAvailable()) {
        eventId = EvidenceManager::createEvent(dev.nodeId, eventType, severity, env);
    } else {
        Logger::warn(TAG, "Evidence storage unavailable — event will only be logged, not saved");
    }

    bool hasImage = false;
    if (CameraManager::isInitialized()) {
        camera_fb_t* fb = CameraManager::captureJpeg();
        if (fb) {
            if (!eventId.isEmpty()) {
                EvidenceManager::attachImage(eventId, fb->buf, fb->len);
            }
            Logger::info(TAG, "Captured JPEG: " + String(fb->len) + " bytes");
            hasImage = true;
            CameraManager::returnFrame(fb);
        }
    } else {
        Logger::warn(TAG, "Camera not initialized — event recorded without image");
    }

    Logger::info(TAG, "Event " + (eventId.isEmpty() ? String("(unsaved)") : eventId) +
                 " type=" + eventType);

    // Only real security events get forwarded — the CAPTURE serial command's
    // MANUAL_TEST events stay local, matching its own "manual test" intent.
    if (eventType == "MOTION_DETECTED") {
        if (!espNowActive) {
            Logger::info(TAG, "ESP-NOW inactive (provisioning mode) — event remains local only");
        } else {
            JsonDocument doc;
            doc["eventId"] = eventId.isEmpty() ? "unsaved" : eventId;
            doc["severity"] = severity;
            doc["hasImage"] = hasImage;
            if (env.valid) {
                doc["temperatureC"] = env.temperatureC;
                doc["humidityPercent"] = env.humidityPercent;
            }
            String payload;
            serializeJson(doc, payload);

            uint8_t gatewayMac[6];
            bool haveGateway = EspNowManager::findGatewayMac(gatewayMac);
            bool sent = EspNowManager::sendMessage(EspNowMessageType::MOTION_DETECTED, payload,
                                                    haveGateway ? gatewayMac : nullptr);
            Logger::info(TAG, sent
                ? (haveGateway ? "Forwarded event to gateway via ESP-NOW (awaiting ACK)"
                                : "Gateway not yet discovered — broadcast event, no ACK tracked")
                : "ESP-NOW forward failed — event remains local only (no persistent offline "
                  "queue yet, Section 28/Phase 28)");
        }
    }
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
        if (!CameraManager::begin()) {
            Logger::error(TAG, "Camera init failed — node continues without capture (Section 59)");
        }
    }

    if (caps.sd) {
        if (SdStorage::begin()) {
            EvidenceManager::begin();
        } else {
            Logger::warn(TAG, "SD unavailable — continuing without local evidence storage (Section 59)");
        }
    } else {
        Logger::info(TAG, "SD capability disabled; skipping mount");
    }

    if (caps.rcwl && caps.rcwlGpio != GPIO_UNCONFIGURED) {
        MotionSensor::begin(caps.rcwlGpio);
        MotionEventEngine::begin(MotionEventConfig());  // Section 17 defaults
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
    } else if (line == "CAPTURE") {
        Logger::info(TAG, "CAPTURE command received via serial — manual test snapshot");
        captureAndRecordEvent("MANUAL_TEST", "INFO");
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
        Logger::info(TAG, "camera.initialized=" + String(CameraManager::isInitialized()) +
                     " sd.mounted=" + String(SdStorage::status().mounted) +
                     " evidence.available=" + String(EvidenceManager::isAvailable()) +
                     " rcwl.enabled=" + String(caps.rcwl) + " dht.enabled=" + String(caps.dht));
        uint8_t gwMac[6];
        bool haveGw = EspNowManager::findGatewayMac(gwMac);
        Logger::info(TAG, "espnow.active=" + String(espNowActive) + " peers=" +
                     String(PeerRegistry::count()) + " gatewayDiscovered=" + String(haveGw));
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

    // Hardware capabilities before Wi-Fi/provisioning: local sensing/capture must not
    // depend on network state (Section 5).
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

    // ESP-NOW needs a settled Wi-Fi radio mode (STA), which provisioning's AP mode
    // conflicts with — deferred until provisioning mode isn't active. A node that stays
    // in provisioning mode simply doesn't forward events over ESP-NOW yet; its local
    // capture pipeline is unaffected either way (Section 5).
    if (!provisioningMode) {
        espNowActive = EspNowManager::begin(cfg.nodeId, roleToString(cfg.role));
    }

    Logger::info(TAG, "Boot complete. Serial commands: STATUS, FACTORY_RESET, PROVISION, CAPTURE");
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
        if (espNowActive) {
            EspNowManager::loop();
        }
    }

    // Security/capture pipeline runs unconditionally — independent of
    // provisioning/Wi-Fi state (Section 5).
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
    unsigned long now = millis();

    if (caps.dht && caps.dhtGpio != GPIO_UNCONFIGURED && now - lastDhtRead >= DHT_READ_INTERVAL_MS) {
        lastDhtRead = now;
        lastDhtReading = TemperatureHumiditySensor::read();
        if (lastDhtReading.valid) {
            Logger::info(TAG, "DHT: temp=" + String(lastDhtReading.temperatureC, 1) + "C humidity=" +
                         String(lastDhtReading.humidityPercent, 1) + "%");
        }
    }

    if (caps.rcwl && caps.rcwlGpio != GPIO_UNCONFIGURED) {
        bool raw = MotionSensor::isTriggered();
        if (MotionEventEngine::update(raw)) {
            captureAndRecordEvent("MOTION_DETECTED", "SUSPICIOUS");
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
