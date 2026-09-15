// CarSentinel Gateway — Phase 1 (device foundation) + Phase 2 (BLE/Wi-Fi provisioning)
// + Phase 3 (hardware capability layer) + Phase 5 (ESP-NOW) + Phase 6 (dynamic node
// management) + Phase 7 (multi-camera correlation) + Phase 8 (GPS).
//
// Hardware capabilities (I2C buses for IMU/OLED, GPS UART) are initialized before
// Wi-Fi/provisioning for the same reason as the node: they shouldn't depend on network
// state. IMU/OLED driver logic (reading accel/gyro, rendering pages) is still
// Phase 9/13 — GPS is now fully implemented (GpsManager, real NMEA parsing), superseding
// Phase 3's byte-liveness-only GpsUart.

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
#include "EspNowManager.h"
#include "PeerRegistry.h"
#include "DeviceRegistry.h"
#include "MacAddress.h"
#include "IncidentCorrelator.h"

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

    DeviceRegistry::begin();
    IncidentCorrelator::begin();

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
    } else {
        Logger::warn(TAG, "NETWORK: not connected (no IP) — Wi-Fi will keep retrying in the background");
    }

    Logger::info(TAG, "Boot complete. Serial commands: STATUS, FACTORY_RESET, PROVISION, "
                 "DEVICES, RENAME <id> <name>, ENABLE <id>, DISABLE <id>, REMOVE <id>, "
                 "SETROLE <id> <role>, RESTART <id>, RESET <id>");
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
    }

    unsigned long now = millis();
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
    // Pumped every iteration, not interval-gated — NMEA sentences arrive continuously
    // and must be drained regularly to avoid losing data to a full UART buffer.
    if (caps.gps) {
        GpsManager::loop();
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
