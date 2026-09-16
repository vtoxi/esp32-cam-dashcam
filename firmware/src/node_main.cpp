// CarSentinel generic Node — Phase 1 (device foundation) + Phase 2 (BLE/Wi-Fi
// provisioning) + Phase 3 (hardware capability layer) + Phase 4 (single camera node)
// + Phase 5 (ESP-NOW) + Phase 6 (dynamic node management) + Phase 7 (multi-camera
// correlation) + Phase 10 (driving/parking modes).
//
// Boot order: hardware capabilities (camera/SD/RCWL/DHT) initialize BEFORE
// Wi-Fi/provisioning, and the motion→capture→evidence pipeline runs unconditionally in
// loop() — regardless of provisioning/Wi-Fi state (Section 5 node independence). RCWL
// trigger (Section 17 debounce/confirmation) → mode check (Section 16 — only PARKED
// alerts) → JPEG snapshot → local evidence file (Section 27/55) → ESP-NOW forward to
// the gateway, which owns the actual incident record (Phase 11).

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
#include "StatusPage.h"
#include "SecurityModeConfig.h"
#include "OtaManager.h"

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

static void streamCamera(WiFiClient client, const String&) {
    if (!CameraManager::isInitialized()) {
        client.print("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\nCamera unavailable");
        client.stop();
        return;
    }

    client.print("HTTP/1.1 200 OK\r\n"
                 "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                 "Cache-Control: no-cache\r\n"
                 "Connection: close\r\n\r\n");

    unsigned long startedMs = millis();
    while (client.connected() && millis() - startedMs < 30000) {
        Watchdog::feed();
        camera_fb_t* fb = CameraManager::captureJpeg();
        if (!fb) {
            delay(50);
            continue;
        }

        client.print("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ");
        client.print(fb->len);
        client.print("\r\n\r\n");
        client.write(fb->buf, fb->len);
        client.print("\r\n");
        CameraManager::returnFrame(fb);
        delay(120);
    }
    client.stop();
}

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

// Section 18: called when this node hears another camera's CAPTURE_REQUEST broadcast
// (a peer's confirmed motion event) — grabs its own synchronized snapshot and reports
// back to the gateway so it can correlate all responding cameras under one incident.
// Deliberately lighter than captureAndRecordEvent(): no MotionEventEngine involvement
// (this isn't this node's own motion trigger), no forwarding of a MOTION_DETECTED —
// just capture, save locally, and tell the gateway.
static void captureRelatedEvidence(const String& triggerNodeId, const String& triggerEventId) {
    const DeviceConfigData& dev = DeviceConfig::get();

    if (!CameraManager::isInitialized()) {
        Logger::info(TAG, "CAPTURE_REQUEST from " + triggerNodeId + " but no camera available — skipping");
        return;
    }

    EnvironmentReading env;
    env.valid = lastDhtReading.valid;
    env.temperatureC = lastDhtReading.temperatureC;
    env.humidityPercent = lastDhtReading.humidityPercent;

    String localEventId;
    if (EvidenceManager::isAvailable()) {
        localEventId = EvidenceManager::createEvent(dev.nodeId, "RELATED_CAPTURE", "INFO", env);
    }

    bool hasImage = false;
    camera_fb_t* fb = CameraManager::captureJpeg();
    if (fb) {
        if (!localEventId.isEmpty()) {
            EvidenceManager::attachImage(localEventId, fb->buf, fb->len);
        }
        hasImage = true;
        Logger::info(TAG, "Synchronized capture for " + triggerNodeId + "/" + triggerEventId +
                     ": " + String(fb->len) + " bytes");
        CameraManager::returnFrame(fb);
    }

    if (!espNowActive) return;

    JsonDocument doc;
    doc["triggerNodeId"] = triggerNodeId;
    doc["triggerEventId"] = triggerEventId;
    doc["localEventId"] = localEventId.isEmpty() ? "unsaved" : localEventId;
    doc["hasImage"] = hasImage;
    String payload;
    serializeJson(doc, payload);

    uint8_t gatewayMac[6];
    bool haveGateway = EspNowManager::findGatewayMac(gatewayMac);
    EspNowManager::sendMessage(EspNowMessageType::CAPTURE_RESULT, payload,
                                haveGateway ? gatewayMac : nullptr);
}

// Section 6: applies administrative commands the gateway sends via CONFIG_UPDATE
// (see gateway_main.cpp's sendNodeCommand()). Payload is {"cmd":..., "value":...}.
// EspNowManager has already auto-ACKed this message before the handler runs.
//
// Also handles Section 18's multi-camera correlation: a CAPTURE_REQUEST broadcast from
// another camera that just confirmed motion.
static void onEspNowMessage(const EspNowMessage& msg, const uint8_t mac[6]) {
    if (msg.type == EspNowMessageType::CAPTURE_REQUEST) {
        JsonDocument doc;
        if (deserializeJson(doc, msg.payload) != DeserializationError::Ok) {
            Logger::warn(TAG, "CAPTURE_REQUEST payload not valid JSON: " + msg.payload);
            return;
        }
        String triggerNodeId = doc["triggerNodeId"] | "";
        String triggerEventId = doc["triggerEventId"] | "";
        if (triggerNodeId.isEmpty() || triggerNodeId == DeviceConfig::get().nodeId) {
            return;  // malformed, or (shouldn't happen) our own broadcast
        }
        captureRelatedEvidence(triggerNodeId, triggerEventId);
        return;
    }

    if (msg.type != EspNowMessageType::CONFIG_UPDATE) {
        return;  // other types not yet handled on nodes
    }

    JsonDocument doc;
    if (deserializeJson(doc, msg.payload) != DeserializationError::Ok) {
        Logger::warn(TAG, "CONFIG_UPDATE payload not valid JSON: " + msg.payload);
        return;
    }
    String cmd = doc["cmd"] | "";
    String value = doc["value"] | "";

    if (cmd == "RENAME") {
        DeviceConfigData dev = DeviceConfig::get();
        dev.displayName = value;
        DeviceConfig::save(dev);
        Logger::info(TAG, "Remote RENAME applied: displayName=" + value);
    } else if (cmd == "SETROLE") {
        DeviceConfigData dev = DeviceConfig::get();
        dev.role = roleFromString(value);
        DeviceConfig::save(dev);
        Logger::info(TAG, "Remote SETROLE applied: role=" + String(roleToString(dev.role)));
    } else if (cmd == "SET_MODE") {
        // Section 16: the gateway is the authority on the current security mode
        // (it's the one running Section 44's auto-detection); nodes just store and
        // obey whatever it last broadcast. "manual" is meaningless on a node itself —
        // stored as false since a node never runs its own auto-detection loop to defer to.
        SecurityMode newMode = securityModeFromString(value);
        SecurityModeConfig::setMode(newMode, false);
        Logger::info(TAG, "Security mode set by gateway: " + String(securityModeToString(newMode)));
    } else if (cmd == "RESTART") {
        restartInto("remote RESTART command from gateway");
    } else if (cmd == "FACTORY_RESET") {
        Logger::warn(TAG, "Remote FACTORY_RESET command from gateway");
        DeviceConfig::factoryReset("NODE", DeviceRole::UNASSIGNED);
        NetworkConfig::clearCredentials();
        restartInto("remote factory reset complete");
    } else {
        Logger::warn(TAG, "Unknown remote command: " + cmd);
    }
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

            // Section 18: tell other cameras to grab a synchronized snapshot too, so the
            // gateway can correlate multiple angles under one incident. Broadcast (not
            // targeted at any specific peer) and best-effort — no ACK/retry tracking,
            // consistent with how HELLO/HEARTBEAT broadcasts already work.
            JsonDocument reqDoc;
            reqDoc["triggerNodeId"] = dev.nodeId;
            reqDoc["triggerEventId"] = eventId.isEmpty() ? "unsaved" : eventId;
            String reqPayload;
            serializeJson(reqDoc, reqPayload);
            EspNowManager::sendMessage(EspNowMessageType::CAPTURE_REQUEST, reqPayload, nullptr);
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
        Logger::info(TAG, "securityMode=" + String(securityModeToString(SecurityModeConfig::getMode())));
        Diagnostics::logSnapshot(TAG);
    } else if (line.startsWith("OTACHECK ")) {
        String url = line.substring(9);
        url.trim();
        if (!WiFiManager::isConnected()) {
            Logger::warn(TAG, "OTACHECK requires Wi-Fi; not connected");
        } else {
            OtaManifest m;
            if (OtaManager::fetchManifest(url, m)) {
                bool needed = OtaManager::isUpdateNeeded(m);
                Logger::info(TAG, "Manifest version=" + m.version + " — " +
                             (needed ? "update available" : "already up to date or incompatible"));
            }
        }
    } else if (line.startsWith("OTAUPDATE ")) {
        String url = line.substring(10);
        url.trim();
        if (!WiFiManager::isConnected()) {
            Logger::warn(TAG, "OTAUPDATE requires Wi-Fi; not connected");
        } else {
            OtaManifest m;
            if (OtaManager::fetchManifest(url, m) && OtaManager::performUpdate(m)) {
                // performUpdate() restarts the device on success; reaching here means it failed.
                Logger::error(TAG, "OTA update did not complete — device unchanged");
            }
        }
    }
}

// Minimal read-only status page content (see StatusPage.h) — mirrors the STATUS serial
// command's fields so the two never drift into showing different things.
static String buildStatusHtml() {
    const DeviceConfigData& cfg = DeviceConfig::get();
    const NetworkConfigData& net = NetworkConfig::get();
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
    DiagnosticsSnapshot diag = Diagnostics::snapshot();

    String html = "<table>";
    html += "<tr><td class=k>Node ID</td><td>" + cfg.nodeId + "</td></tr>";
    html += "<tr><td class=k>Display Name</td><td>" + cfg.displayName + "</td></tr>";
    html += "<tr><td class=k>Role</td><td>" + String(roleToString(cfg.role)) + "</td></tr>";
    html += "<tr><td class=k>Hardware Profile</td><td>" + cfg.hardwareProfile + "</td></tr>";
    html += "<tr><td class=k>Firmware</td><td>" + cfg.firmwareVersion + "</td></tr>";
    html += "<tr><td class=k>MAC</td><td>" + DeviceIdentity::macAddress() + "</td></tr>";
    html += "<tr><td class=k>Wi-Fi</td><td>" + net.ssid + " (" + WiFiManager::localIP() + ")</td></tr>";
    html += "<tr><td class=k>Uptime</td><td>" + String(diag.uptimeMs / 1000) + "s</td></tr>";
    html += "<tr><td class=k>Free Heap</td><td>" + String(diag.freeHeap) + " bytes</td></tr>";
    html += "</table>";

    html += "<table>";
    html += "<tr><td class=k>Camera</td><td>" + String(CameraManager::isInitialized() ? "initialized" : "off") + "</td></tr>";
    html += "<tr><td class=k>SD</td><td>" + String(SdStorage::status().mounted ? "mounted" : "not mounted") + "</td></tr>";
    html += "<tr><td class=k>Evidence storage</td><td>" + String(EvidenceManager::isAvailable() ? "available" : "unavailable") + "</td></tr>";
    html += "<tr><td class=k>RCWL</td><td>" + String(caps.rcwl ? ("enabled, gpio=" + String(caps.rcwlGpio)) : "disabled") + "</td></tr>";
    html += "<tr><td class=k>DHT</td><td>" + String(caps.dht
        ? (lastDhtReading.valid ? String(lastDhtReading.temperatureC, 1) + "C / " + String(lastDhtReading.humidityPercent, 1) + "%"
                                 : String("enabled, no reading yet"))
        : String("disabled")) + "</td></tr>";
    html += "</table>";

    uint8_t gwMac[6];
    bool haveGw = EspNowManager::findGatewayMac(gwMac);
    html += "<table>";
    html += "<tr><td class=k>Security Mode</td><td>" + String(securityModeToString(SecurityModeConfig::getMode())) + "</td></tr>";
    html += "<tr><td class=k>ESP-NOW</td><td>" + String(espNowActive ? "active" : "inactive") + "</td></tr>";
    html += "<tr><td class=k>Known peers</td><td>" + String(PeerRegistry::count()) + "</td></tr>";
    html += "<tr><td class=k>Gateway discovered</td><td>" + String(haveGw ? "yes" : "no") + "</td></tr>";
    html += "</table>";

    return html;
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

    // Section 37: "report healthy, mark update successful" — the half of that this
    // project can honestly deliver (see OtaManager.h for what depends on the
    // bootloader's own build configuration, not guaranteed here).
    OtaManager::confirmHealthyBoot();

    SecurityModeConfig::begin();

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
        if (espNowActive) {
            EspNowManager::setOnMessageHandler(onEspNowMessage);
        }
    }

    // Boot-summary line: the one thing worth grepping for in a serial log when you just
    // want "what IP did this thing get" without hunting through the rest of the boot
    // sequence.
    if (provisioningMode) {
        Logger::info(TAG, "NETWORK: provisioning AP active, connect to \"CarSentinel-" +
                     cfg.nodeId + "\" and browse to 192.168.4.1 to configure Wi-Fi");
    } else if (WiFiManager::isConnected()) {
        Logger::info(TAG, "NETWORK: connected, IP=" + WiFiManager::localIP() +
                     " ssid=" + NetworkConfig::get().ssid);
        StatusPage::begin("CarSentinel Node " + cfg.nodeId, buildStatusHtml, streamCamera);
    } else {
        Logger::warn(TAG, "NETWORK: not connected (no IP) — Wi-Fi will keep retrying in the background");
    }

    Logger::info(TAG, "Boot complete. Serial commands: STATUS, FACTORY_RESET, PROVISION, CAPTURE, "
                 "OTACHECK <manifestUrl>, OTAUPDATE <manifestUrl>");
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
        // Covers the case where Wi-Fi wasn't connected yet at boot (setup() only starts
        // the page immediately on a successful connect) but WiFiManager reconnects later.
        if (!StatusPage::isActive() && WiFiManager::isConnected()) {
            StatusPage::begin("CarSentinel Node " + DeviceConfig::get().nodeId, buildStatusHtml, streamCamera);
        }
        StatusPage::loop();
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
            // Section 16: only PARKED runs full motion alerting — DRIVING/DISARMED/
            // SERVICE all suppress it (road vibration during DRIVING isn't a security
            // event; DISARMED/SERVICE are explicit "don't alert me" states). The
            // confirmed-motion signal is still consumed (MotionEventEngine::update()
            // already ran above) so the cooldown/debounce state stays consistent
            // regardless of mode.
            if (securityModeAllowsMotionAlerts(SecurityModeConfig::getMode())) {
                captureAndRecordEvent("MOTION_DETECTED", "SUSPICIOUS");
            } else {
                Logger::info(TAG, "Motion confirmed but suppressed (mode=" +
                             String(securityModeToString(SecurityModeConfig::getMode())) + ")");
            }
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
