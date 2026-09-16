// CarSentinel Gateway — Phase 1 (device foundation) + Phase 2 (BLE/Wi-Fi provisioning)
// + Phase 3 (hardware capability layer) + Phase 5 (ESP-NOW) + Phase 6 (dynamic node
// management) + Phase 7 (multi-camera correlation) + Phase 8 (GPS) + Phase 9 (MPU6050).
//
// Hardware capabilities (I2C buses for IMU/OLED, GPS UART) are initialized before
// Wi-Fi/provisioning for the same reason as the node: they shouldn't depend on network
// state. OLED page rendering is still Phase 13 — GPS (Phase 8) and IMU (Phase 9) are now
// fully implemented, superseding Phase 3's presence-detection-only stubs.

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
#include "GpsManager.h"
#include "ImuManager.h"
#include "EspNowManager.h"
#include "PeerRegistry.h"
#include "DeviceRegistry.h"
#include "MacAddress.h"
#include "IncidentCorrelator.h"
#include "StatusPage.h"
#include "SecurityModeConfig.h"

#include <ArduinoJson.h>

using namespace CarSentinel;

static const char* TAG = "Gateway";
static unsigned long lastDiagnosticsLog = 0;
static const unsigned long DIAGNOSTICS_INTERVAL_MS = 30000;
static bool provisioningMode = false;
static bool espNowActive = false;

// Section 6: every HELLO/HEARTBEAT auto-populates/refreshes the persistent device
// registry — this is what makes adding a node zero-code (the gateway learns about it
// just by hearing from it) and lets DEVICES/health tracking work off live data.
static void onEspNowHeartbeat(const EspNowMessage& msg, const uint8_t mac[6]) {
    JsonDocument doc;
    if (deserializeJson(doc, msg.payload) != DeserializationError::Ok) {
        return;
    }
    String role = doc["role"] | "";
    DeviceRegistry::upsertFromDiscovery(msg.senderNodeId, mac, role);
    DeviceRegistry::updateHealth(msg.senderNodeId, doc["freeHeap"] | 0, doc["uptimeMs"] | 0);
}

// Phase 5/6/7 scope: log receipt, respect the registry's enabled flag, and correlate
// multi-camera responses (Section 18) into a lightweight in-memory incident via
// IncidentCorrelator. The full persistent incident lifecycle/evidence association
// (Section 11) is Phase 11, built on top of this.
static void onEspNowMessage(const EspNowMessage& msg, const uint8_t mac[6]) {
    DeviceRegistryEntry* dev = DeviceRegistry::find(msg.senderNodeId);
    if (dev && !dev->enabled) {
        Logger::info(TAG, "Ignoring event from disabled device " + msg.senderNodeId +
                     " (" + String(messageTypeToString(msg.type)) + ") — re-enable with "
                     "ENABLE " + msg.senderNodeId);
        return;
    }
    Logger::info(TAG, "Security event from " + msg.senderNodeId + ": " +
                 String(messageTypeToString(msg.type)) + " " + msg.payload);

    if (msg.type == EspNowMessageType::MOTION_DETECTED) {
        JsonDocument doc;
        if (deserializeJson(doc, msg.payload) == DeserializationError::Ok) {
            String eventId = doc["eventId"] | "unsaved";
            String incidentId = IncidentCorrelator::startIncident(msg.senderNodeId, eventId);
            // Section 21 "incident location": the gateway is the only node with GPS, so
            // it's the natural place to attach a position to an incident. Logged
            // alongside the incident, not yet persisted into it — Phase 11 owns the
            // actual incident record this would get written into.
            Logger::info(TAG, incidentId + " location: " + GpsManager::toJson());
        }
    } else if (msg.type == EspNowMessageType::CAPTURE_RESULT) {
        JsonDocument doc;
        if (deserializeJson(doc, msg.payload) == DeserializationError::Ok) {
            String triggerNodeId = doc["triggerNodeId"] | "";
            String triggerEventId = doc["triggerEventId"] | "";
            if (!triggerNodeId.isEmpty()) {
                IncidentCorrelator::addRelated(triggerNodeId, triggerEventId, msg.senderNodeId);
            }
        }
    }
}

static const uint8_t MPU6050_I2C_ADDR = 0x68;
static const uint8_t SSD1306_I2C_ADDR = 0x3C;
static uint32_t nextImuEventNumber = 1;

// Section 23: the gateway is both the sensor source and the incident-opener here (no
// ESP-NOW round trip needed — it's the gateway's own IMU), so this mirrors the
// MOTION_DETECTED handling in onEspNowMessage() but triggers locally. Reports
// IMPACT_EVENT with raw measurements; never claims a crash occurred.
static void reportImuImpact(const ImuReading& reading) {
    char idBuf[24];
    snprintf(idBuf, sizeof(idBuf), "IMU-%06u", (unsigned int)(nextImuEventNumber++));
    String eventId = String(idBuf);
    const DeviceConfigData& cfg = DeviceConfig::get();

    String incidentId = IncidentCorrelator::startIncident(cfg.nodeId, eventId);
    Logger::warn(TAG, incidentId + " IMPACT_EVENT " + eventId +
                 " accelMagnitudeG=" + String(reading.accelMagnitudeG, 2) +
                 " gyroMagnitudeDps=" + String(reading.gyroMagnitudeDps, 1) +
                 " raw accel(g)=[" + String(reading.accelXg, 2) + "," + String(reading.accelYg, 2) +
                 "," + String(reading.accelZg, 2) + "] gyro(dps)=[" + String(reading.gyroXdps, 1) +
                 "," + String(reading.gyroYdps, 1) + "," + String(reading.gyroZdps, 1) + "]");
    Logger::info(TAG, incidentId + " location: " + GpsManager::toJson());
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

// Splits "COMMAND arg1 arg2..." into up to 3 space-separated tokens after the command
// word itself. Good enough for this phase's serial commands; a real CLI parser isn't
// warranted for a handful of admin commands.
static void splitArgs(const String& line, String& arg1, String& arg2) {
    int firstSpace = line.indexOf(' ');
    if (firstSpace < 0) return;
    String rest = line.substring(firstSpace + 1);
    rest.trim();
    int secondSpace = rest.indexOf(' ');
    if (secondSpace < 0) {
        arg1 = rest;
    } else {
        arg1 = rest.substring(0, secondSpace);
        arg2 = rest.substring(secondSpace + 1);
        arg2.trim();
    }
}

// Sends a Section 6 administrative command to a node over ESP-NOW as a CONFIG_UPDATE
// message ({"cmd":..., "value":...}) — the node applies it in its own onEspNowMessage
// handler (see node_main.cpp). Requires the node to have been heard from at least once
// (its MAC is only known via ESP-NOW discovery, never guessed).
static bool sendNodeCommand(const String& nodeId, const String& cmd, const String& value) {
    DeviceRegistryEntry* dev = DeviceRegistry::find(nodeId);
    if (!dev) {
        Logger::warn(TAG, "Unknown nodeId: " + nodeId + " (has it sent a heartbeat yet? see DEVICES)");
        return false;
    }
    uint8_t mac[6];
    if (!macFromString(dev->mac, mac)) {
        Logger::error(TAG, "Stored MAC for " + nodeId + " is malformed: " + dev->mac);
        return false;
    }
    JsonDocument doc;
    doc["cmd"] = cmd;
    if (!value.isEmpty()) doc["value"] = value;
    String payload;
    serializeJson(doc, payload);
    bool sent = EspNowManager::sendMessage(EspNowMessageType::CONFIG_UPDATE, payload, mac);
    Logger::info(TAG, (sent ? "Sent " : "Failed to send ") + cmd + " to " + nodeId);
    return sent;
}

// Section 16: every known device needs to agree on the current mode, since it's what
// gates a node's own motion-alert behavior (securityModeAllowsMotionAlerts). Reuses the
// same CONFIG_UPDATE envelope as every other remote command (Section 6) rather than
// inventing a dedicated message type for this one case.
static void broadcastModeToAllDevices(SecurityMode mode) {
    String value = securityModeToString(mode);
    for (uint8_t i = 0; i < DeviceRegistry::count(); i++) {
        DeviceRegistryEntry* d = DeviceRegistry::get(i);
        if (d->enabled) {
            sendNodeCommand(d->nodeId, "SET_MODE", value);
        }
    }
}

static void applyModeChange(SecurityMode mode, bool manual) {
    if (mode == SecurityModeConfig::getMode() && manual == SecurityModeConfig::isManualOverride()) {
        return;  // no actual change — don't spam a redundant broadcast
    }
    SecurityModeConfig::setMode(mode, manual);
    Logger::warn(TAG, "Security mode -> " + String(securityModeToString(mode)) +
                 (manual ? " (manual)" : " (auto-detected: GPS speed / IMU movement — Section 44)"));
    broadcastModeToAllDevices(mode);
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
        if (present) {
            ImuManager::begin(0, MPU6050_I2C_ADDR);
        } else {
            Logger::info(TAG, "Skipping ImuManager init — device not present");
        }
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
        GpsManager::begin(caps.gpsRxGpio, caps.gpsTxGpio);
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
        if (caps.gps) {
            Logger::info(TAG, "gps: " + GpsManager::toJson());
        }
        if (caps.imu && ImuManager::isInitialized()) {
            ImuReading r = ImuManager::read();
            const ImuThresholdsData& t = ImuManager::getThresholds();
            Logger::info(TAG, "imu: accelMagnitudeG=" + String(r.accelMagnitudeG, 2) +
                         " gyroMagnitudeDps=" + String(r.gyroMagnitudeDps, 1) +
                         " thresholds(accelG=" + String(t.accelMagnitudeG, 2) +
                         ", gyroDps=" + String(t.gyroMagnitudeDps, 1) +
                         ", cooldownMs=" + String(t.cooldownMs) + ")");
        }
        Logger::info(TAG, "securityMode=" + String(securityModeToString(SecurityModeConfig::getMode())) +
                     " manualOverride=" + String(SecurityModeConfig::isManualOverride()));
        Logger::info(TAG, "espnow.active=" + String(espNowActive) + " peers=" + String(PeerRegistry::count()));
        for (uint8_t i = 0; i < PeerRegistry::count(); i++) {
            PeerInfo* p = PeerRegistry::get(i);
            Logger::info(TAG, "  peer[" + String(i) + "]: " + p->nodeId + " role=" + p->role +
                         " lastSeenMsAgo=" + String(millis() - p->lastSeenMs));
        }
        Diagnostics::logSnapshot(TAG);
    } else if (line == "DEVICES") {
        // Section 6: the zero-code device list — every entry here got here by the
        // gateway hearing an ESP-NOW heartbeat, not by editing a config file or
        // reflashing anything.
        Logger::info(TAG, "Device registry (" + String(DeviceRegistry::count()) + "):");
        for (uint8_t i = 0; i < DeviceRegistry::count(); i++) {
            DeviceRegistryEntry* d = DeviceRegistry::get(i);
            unsigned long agoMs = d->lastSeenMs == 0 ? 0 : millis() - d->lastSeenMs;
            Logger::info(TAG, "  " + d->nodeId + " \"" + d->displayName + "\" role=" + d->role +
                         " mac=" + d->mac + " enabled=" + String(d->enabled) +
                         " lastSeenMsAgo=" + String(agoMs) +
                         " freeHeap=" + String(d->lastFreeHeap) +
                         " uptimeMs=" + String(d->lastUptimeMs));
        }
    } else if (line.startsWith("RENAME ")) {
        String nodeId, newName;
        splitArgs(line, nodeId, newName);
        if (nodeId.isEmpty() || newName.isEmpty()) {
            Logger::warn(TAG, "Usage: RENAME <nodeId> <newDisplayName>");
        } else {
            bool ok = DeviceRegistry::rename(nodeId, newName);
            Logger::info(TAG, ok ? "Renamed " + nodeId + " to \"" + newName + "\" (gateway-side)"
                                  : "Unknown nodeId: " + nodeId);
            if (ok) sendNodeCommand(nodeId, "RENAME", newName);  // keep the node's own displayName in sync
        }
    } else if (line.startsWith("ENABLE ")) {
        String nodeId, unused;
        splitArgs(line, nodeId, unused);
        bool ok = DeviceRegistry::setEnabled(nodeId, true);
        Logger::info(TAG, ok ? "Enabled " + nodeId : "Unknown nodeId: " + nodeId);
    } else if (line.startsWith("DISABLE ")) {
        String nodeId, unused;
        splitArgs(line, nodeId, unused);
        bool ok = DeviceRegistry::setEnabled(nodeId, false);
        Logger::info(TAG, ok ? "Disabled " + nodeId + " — its events will be ignored, not its radio traffic"
                              : "Unknown nodeId: " + nodeId);
    } else if (line.startsWith("REMOVE ")) {
        String nodeId, unused;
        splitArgs(line, nodeId, unused);
        bool ok = DeviceRegistry::remove(nodeId);
        Logger::info(TAG, ok ? "Removed " + nodeId + " from the registry (the node itself keeps "
                               "running independently — Section 5 — and will reappear here if it "
                               "sends another heartbeat)"
                              : "Unknown nodeId: " + nodeId);
    } else if (line.startsWith("SETROLE ")) {
        String nodeId, role;
        splitArgs(line, nodeId, role);
        if (nodeId.isEmpty() || role.isEmpty()) {
            Logger::warn(TAG, "Usage: SETROLE <nodeId> <GATEWAY|CAMERA|SENSOR|DISPLAY|VEHICLE_CONTROLLER|UNASSIGNED>");
        } else {
            sendNodeCommand(nodeId, "SETROLE", role);
        }
    } else if (line.startsWith("RESTART ")) {
        String nodeId, unused;
        splitArgs(line, nodeId, unused);
        sendNodeCommand(nodeId, "RESTART", "");
    } else if (line.startsWith("RESET ")) {
        String nodeId, unused;
        splitArgs(line, nodeId, unused);
        sendNodeCommand(nodeId, "FACTORY_RESET", "");
    } else if (line.startsWith("IMUTHRESHOLDS")) {
        // Section 23: thresholds are placeholder defaults meant to be tuned against real
        // driving data — this is that tuning knob, without needing to hand-edit
        // /config/imu_thresholds.json or reflash.
        String accelStr, gyroStr;
        splitArgs(line, accelStr, gyroStr);
        if (accelStr.isEmpty() || gyroStr.isEmpty()) {
            ImuThresholdsData t = ImuManager::getThresholds();
            Logger::info(TAG, "Usage: IMUTHRESHOLDS <accelG> <gyroDps>  (current: accelG=" +
                         String(t.accelMagnitudeG, 2) + " gyroDps=" + String(t.gyroMagnitudeDps, 1) + ")");
        } else {
            ImuThresholdsData t = ImuManager::getThresholds();
            t.accelMagnitudeG = accelStr.toFloat();
            t.gyroMagnitudeDps = gyroStr.toFloat();
            ImuManager::setThresholds(t);
            Logger::info(TAG, "IMU thresholds updated: accelG=" + String(t.accelMagnitudeG, 2) +
                         " gyroDps=" + String(t.gyroMagnitudeDps, 1));
        }
    } else if (line == "MODE") {
        Logger::info(TAG, "Security mode: " + String(securityModeToString(SecurityModeConfig::getMode())) +
                     (SecurityModeConfig::isManualOverride() ? " (manual)" : " (auto)"));
    } else if (line.startsWith("MODE ")) {
        String modeStr, unused;
        splitArgs(line, modeStr, unused);
        if (modeStr != "DISARMED" && modeStr != "DRIVING" && modeStr != "PARKED" && modeStr != "SERVICE") {
            Logger::warn(TAG, "Usage: MODE <DISARMED|DRIVING|PARKED|SERVICE>");
        } else {
            applyModeChange(securityModeFromString(modeStr), true);
        }
    } else if (line == "AUTOMODE") {
        // Section 44: resumes GPS-speed/IMU-movement auto-detection. Doesn't force an
        // immediate mode change — the detection loop's own hysteresis (see loop())
        // picks it up on the next sustained reading.
        SecurityModeConfig::setMode(SecurityModeConfig::getMode(), false);
        Logger::info(TAG, "Resumed automatic DRIVING/PARKED detection");
    }
}

// Minimal read-only status page content (see StatusPage.h) — mirrors the STATUS/DEVICES
// serial commands' fields so they never drift into showing different things.
static String buildStatusHtml() {
    const DeviceConfigData& cfg = DeviceConfig::get();
    const NetworkConfigData& net = NetworkConfig::get();
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
    DiagnosticsSnapshot diag = Diagnostics::snapshot();

    String html = "<table>";
    html += "<tr><td class=k>Node ID</td><td>" + cfg.nodeId + "</td></tr>";
    html += "<tr><td class=k>Display Name</td><td>" + cfg.displayName + "</td></tr>";
    html += "<tr><td class=k>Hardware Profile</td><td>" + cfg.hardwareProfile + "</td></tr>";
    html += "<tr><td class=k>Firmware</td><td>" + cfg.firmwareVersion + "</td></tr>";
    html += "<tr><td class=k>MAC</td><td>" + DeviceIdentity::macAddress() + "</td></tr>";
    html += "<tr><td class=k>Wi-Fi</td><td>" + net.ssid + " (" + WiFiManager::localIP() + ")</td></tr>";
    html += "<tr><td class=k>Uptime</td><td>" + String(diag.uptimeMs / 1000) + "s</td></tr>";
    html += "<tr><td class=k>Free Heap</td><td>" + String(diag.freeHeap) + " bytes</td></tr>";
    html += "</table>";

    html += "<table>";
    if (caps.gps) {
        html += "<tr><td class=k>GPS</td><td>" + GpsManager::toJson() + "</td></tr>";
    }
    if (caps.imu && ImuManager::isInitialized()) {
        ImuReading r = ImuManager::read();
        html += "<tr><td class=k>IMU accel</td><td>" + String(r.accelMagnitudeG, 2) + " g</td></tr>";
        html += "<tr><td class=k>IMU gyro</td><td>" + String(r.gyroMagnitudeDps, 1) + " deg/s</td></tr>";
    }
    html += "</table>";

    html += "<table>";
    html += "<tr><td class=k>Security Mode</td><td>" + String(securityModeToString(SecurityModeConfig::getMode())) +
            (SecurityModeConfig::isManualOverride() ? " (manual)" : " (auto)") + "</td></tr>";
    html += "<tr><td class=k>ESP-NOW</td><td>" + String(espNowActive ? "active" : "inactive") + "</td></tr>";
    html += "<tr><td class=k>Peers seen</td><td>" + String(PeerRegistry::count()) + "</td></tr>";
    html += "</table>";

    html += "<p class=sub>Devices (Section 6 registry — auto-populated, zero-code):</p><table>";
    html += "<tr><td class=k>Node</td><td class=k>Name</td><td class=k>Role</td><td class=k>Enabled</td><td class=k>Last seen</td></tr>";
    for (uint8_t i = 0; i < DeviceRegistry::count(); i++) {
        DeviceRegistryEntry* d = DeviceRegistry::get(i);
        unsigned long agoS = d->lastSeenMs == 0 ? 0 : (millis() - d->lastSeenMs) / 1000;
        html += "<tr><td>" + d->nodeId + "</td><td>" + d->displayName + "</td><td>" + d->role +
                "</td><td>" + String(d->enabled ? "yes" : "no") + "</td><td>" + String(agoS) + "s ago</td></tr>";
    }
    html += "</table>";

    return html;
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

    DeviceRegistry::begin();
    IncidentCorrelator::begin();
    SecurityModeConfig::begin();

    if (!provisioningMode) {
        espNowActive = EspNowManager::begin(cfg.nodeId, roleToString(cfg.role));
        if (espNowActive) {
            EspNowManager::setOnMessageHandler(onEspNowMessage);
            EspNowManager::setOnPeerHeartbeatHandler(onEspNowHeartbeat);
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
        StatusPage::begin("CarSentinel Gateway " + cfg.nodeId, buildStatusHtml);
    } else {
        Logger::warn(TAG, "NETWORK: not connected (no IP) — Wi-Fi will keep retrying in the background");
    }

    Logger::info(TAG, "Boot complete. Serial commands: STATUS, FACTORY_RESET, PROVISION, "
                 "DEVICES, RENAME <id> <name>, ENABLE <id>, DISABLE <id>, REMOVE <id>, "
                 "SETROLE <id> <role>, RESTART <id>, RESET <id>, IMUTHRESHOLDS <accelG> <gyroDps>, "
                 "MODE, MODE <DISARMED|DRIVING|PARKED|SERVICE>, AUTOMODE");
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
            IncidentCorrelator::loop();
        }
        // Covers the case where Wi-Fi wasn't connected yet at boot (setup() only starts
        // the page immediately on a successful connect) but WiFiManager reconnects later.
        if (!StatusPage::isActive() && WiFiManager::isConnected()) {
            StatusPage::begin("CarSentinel Gateway " + DeviceConfig::get().nodeId, buildStatusHtml);
        }
        StatusPage::loop();
    }

    unsigned long now = millis();
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
    // Pumped every iteration, not interval-gated — NMEA sentences arrive continuously
    // and must be drained regularly to avoid losing data to a full UART buffer.
    if (caps.gps) {
        GpsManager::loop();
    }

    static unsigned long lastImuRead = 0;
    const unsigned long IMU_READ_INTERVAL_MS = 100;  // frequent enough to catch a sharp impact
    ImuReading lastImuReading;  // reused below for mode auto-detection, avoids a second I2C read
    bool haveImuReading = false;
    if (caps.imu && ImuManager::isInitialized() && now - lastImuRead >= IMU_READ_INTERVAL_MS) {
        lastImuRead = now;
        lastImuReading = ImuManager::read();
        haveImuReading = lastImuReading.valid;
        // Section 16: DISARMED means "security alerts disabled" — impact detection
        // counts as one, so it's skipped entirely while disarmed rather than just
        // suppressing the report (keeps the cooldown from being consumed by an event
        // nobody will hear about anyway).
        if (SecurityModeConfig::getMode() != SecurityMode::DISARMED &&
            ImuManager::checkImpact(lastImuReading)) {
            reportImuImpact(lastImuReading);
        }
    }

    // Section 44: automatic DRIVING/PARKED detection from GPS speed + IMU movement.
    // Paused while a manual MODE command is in effect (DISARMED/SERVICE, or a forced
    // DRIVING/PARKED) — resumed with AUTOMODE.
    static unsigned long lastModeCheck = 0;
    static uint8_t movingStreak = 0, stationaryStreak = 0;
    const unsigned long MODE_CHECK_INTERVAL_MS = 2000;
    const uint8_t MODE_SWITCH_STREAK = 5;  // ~10s of sustained state before switching
    const float MOVEMENT_ACCEL_DELTA_G = 0.15f;  // deviation from the ~1g at-rest reading
    const float MOVEMENT_GYRO_DPS = 15.0f;
    const float MOVEMENT_SPEED_KMPH = 5.0f;
    if (!SecurityModeConfig::isManualOverride() && now - lastModeCheck >= MODE_CHECK_INTERVAL_MS) {
        lastModeCheck = now;
        bool moving = false;
        if (caps.gps && GpsManager::hasFix() && GpsManager::getFix().speedKmph > MOVEMENT_SPEED_KMPH) {
            moving = true;
        }
        if (haveImuReading &&
            (fabsf(lastImuReading.accelMagnitudeG - 1.0f) > MOVEMENT_ACCEL_DELTA_G ||
             lastImuReading.gyroMagnitudeDps > MOVEMENT_GYRO_DPS)) {
            moving = true;
        }
        if (moving) { movingStreak++; stationaryStreak = 0; }
        else { stationaryStreak++; movingStreak = 0; }

        if (movingStreak >= MODE_SWITCH_STREAK && SecurityModeConfig::getMode() != SecurityMode::DRIVING) {
            applyModeChange(SecurityMode::DRIVING, false);
        } else if (stationaryStreak >= MODE_SWITCH_STREAK && SecurityModeConfig::getMode() != SecurityMode::PARKED) {
            applyModeChange(SecurityMode::PARKED, false);
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
