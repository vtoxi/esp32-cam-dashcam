// CarSentinel Gateway — Phase 1 (device foundation) + Phase 2 (BLE/Wi-Fi provisioning)
// + Phase 3 (hardware capability layer) + Phase 5 (ESP-NOW) + Phase 6 (dynamic node
// management) + Phase 7 (multi-camera correlation) + Phase 8 (GPS) + Phase 9 (MPU6050)
// + Phase 10 (driving/parking modes) + Phase 11 (incident & evidence engine).
//
// Hardware capabilities (I2C buses for IMU/OLED, GPS UART) are initialized before
// Wi-Fi/provisioning for the same reason as the node: they shouldn't depend on network
// state. OLED page rendering is still Phase 13 — GPS (Phase 8) and IMU (Phase 9) are now
// fully implemented, superseding Phase 3's presence-detection-only stubs. Incidents
// (Phase 11) persist to the gateway's own internal flash — see IncidentCorrelator.h for
// why (the gateway has no SD card).

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
#include "IgnitionSense.h"
#include "ImuManager.h"
#include "EspNowManager.h"
#include "PeerRegistry.h"
#include "DeviceRegistry.h"
#include "MacAddress.h"
#include "IncidentCorrelator.h"
#include "DashboardServer.h"
#include "SecurityModeConfig.h"
#include "PowerManager.h"
#include "NotificationManager.h"
#include "AIThreatFramework.h"
#include "EmailConfig.h"
#include "DisplayManager.h"
#include "OtaManager.h"
#include "BackendConfig.h"
#include "RemoteSyncManager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

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
    String displayName = doc["displayName"] | "";
    String ip = doc["ip"] | "";
    DeviceRegistry::upsertFromDiscovery(msg.senderNodeId, mac, role, displayName, ip);
    DeviceRegistry::updateHealth(msg.senderNodeId, doc["freeHeap"] | 0, doc["uptimeMs"] | 0);
}

// Lightest workable technique: rather than the gateway relaying every MJPEG byte
// through a second raw socket (doubles bandwidth/latency through a single-core-busy
// WebServer loop and is fragile under real network conditions — the earlier
// implementation here did exactly that and streaming never reliably worked), just
// redirect the browser straight to the node's own /stream endpoint. Nodes are on the
// same LAN as the gateway and the viewer's browser, so there's no reason to proxy.
static void streamCameraFromNode(WiFiClient client, const String& nodeId) {
    DeviceRegistryEntry* device = DeviceRegistry::find(nodeId);
    if (!device || device->ip.isEmpty()) {
        client.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\nCamera IP unavailable "
                      "(node hasn't sent a heartbeat with its IP yet)");
        client.stop();
        return;
    }
    client.print("HTTP/1.1 302 Found\r\nLocation: http://" + device->ip +
                 "/stream\r\nConnection: close\r\n\r\n");
    client.stop();
}

// Section 24: whatever GPS/IMU context the gateway currently has, gathered fresh at the
// moment an incident opens — this is what lets an incident record honestly claim GPS/
// IMU association without IncidentCorrelator itself depending on either sensor driver.
static IncidentTriggerInfo gatherAmbientInfo() {
    IncidentTriggerInfo info;
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
    if (caps.gps && GpsManager::hasFix()) {
        GpsFix fix = GpsManager::getFix();
        info.hasGps = true;
        info.gpsLat = fix.latitude;
        info.gpsLon = fix.longitude;
        info.gpsSpeedKmph = fix.speedKmph;
    }
    if (caps.imu && ImuManager::isInitialized()) {
        ImuReading r = ImuManager::read();
        if (r.valid) {
            info.hasImu = true;
            info.imuAccelG = r.accelMagnitudeG;
            info.imuGyroDps = r.gyroMagnitudeDps;
        }
    }
    return info;
}

// Phase 5/6/7/11 scope: log receipt, respect the registry's enabled flag, and correlate
// multi-camera responses (Section 18) into a persisted incident record (Section 11/24)
// via IncidentCorrelator — full lifecycle, GPS/IMU/DHT association, evidence
// references, survives reboot.
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
            IncidentTriggerInfo info = gatherAmbientInfo();
            info.triggerType = "MOTION_DETECTED";
            info.severity = doc["severity"] | "SUSPICIOUS";
            info.hasImage = doc["hasImage"] | false;
            if (!doc["temperatureC"].isNull()) {
                info.hasEnv = true;
                info.envTempC = doc["temperatureC"] | 0.0f;
                info.envHumidity = doc["humidityPercent"] | 0.0f;
            }
            IncidentCorrelator::startIncident(msg.senderNodeId, eventId, info);
        }
    } else if (msg.type == EspNowMessageType::CAPTURE_RESULT) {
        JsonDocument doc;
        if (deserializeJson(doc, msg.payload) == DeserializationError::Ok) {
            String triggerNodeId = doc["triggerNodeId"] | "";
            String triggerEventId = doc["triggerEventId"] | "";
            String localEventId = doc["localEventId"] | "";
            bool hasImage = doc["hasImage"] | false;
            if (!triggerNodeId.isEmpty()) {
                IncidentCorrelator::addRelated(triggerNodeId, triggerEventId, msg.senderNodeId,
                                                localEventId, hasImage);
            }
        }
    }
}

// Phase 16 — AI Security Assistance: runs the (heuristic, honestly documented as such —
// see AIThreatFramework.h) threat framework on every closed incident before deciding
// whether to actually email about it. This is the "assistance" part: a LOW-confidence,
// uncorroborated single motion blip no longer generates the same alert as a
// multi-camera-confirmed event or a real impact — reduces notification noise without
// silently dropping anything (still logged either way, and still visible on the
// dashboard's incident list regardless of this decision).
static void assistedIncidentNotify(const IncidentRecord& inc) {
    ThreatAssessment assessment = AIThreatFramework::assess(inc.trigger, inc.evidenceCount);
    Logger::info(TAG, "AI assessment for " + inc.incidentId + ": " +
                 String(threatSeverityToString(assessment.severity)) + " (" +
                 String(assessment.confidencePercent) + "% confidence) — " + assessment.reasoning);

    if (assessment.severity == ThreatSeverity::THREAT_LOW) {
        Logger::info(TAG, inc.incidentId + " assessed LOW — skipping email notification "
                     "(still recorded and visible on the dashboard)");
        return;
    }
    NotificationManager::onIncidentReady(inc);
}

static const uint8_t MPU6050_I2C_ADDR = 0x68;
static const uint8_t SSD1306_I2C_ADDR = 0x3C;
static uint32_t nextImuEventNumber = 1;

// Section 23: the gateway is both the sensor source and the incident-opener here (no
// ESP-NOW round trip needed — it's the gateway's own IMU), so this mirrors the
// MOTION_DETECTED handling in onEspNowMessage() but triggers locally. Reports
// IMPACT_EVENT with raw measurements; never claims a crash occurred. Also broadcasts a
// CAPTURE_REQUEST (Section 18/24) so nearby cameras contribute footage of the impact
// too, the same as a motion trigger does — a vehicle impact is exactly the kind of
// event multi-camera evidence matters most for.
static void reportImuImpact(const ImuReading& reading) {
    char idBuf[24];
    snprintf(idBuf, sizeof(idBuf), "IMU-%06u", (unsigned int)(nextImuEventNumber++));
    String eventId = String(idBuf);
    const DeviceConfigData& cfg = DeviceConfig::get();

    IncidentTriggerInfo info = gatherAmbientInfo();
    info.triggerType = "IMPACT_EVENT";
    info.severity = "SUSPICIOUS";
    info.hasImage = false;  // the gateway itself has no camera
    info.hasImu = true;     // override gatherAmbientInfo()'s snapshot with the actual triggering reading
    info.imuAccelG = reading.accelMagnitudeG;
    info.imuGyroDps = reading.gyroMagnitudeDps;

    String incidentId = IncidentCorrelator::startIncident(cfg.nodeId, eventId, info);
    Logger::warn(TAG, incidentId + " IMPACT_EVENT " + eventId +
                 " accelMagnitudeG=" + String(reading.accelMagnitudeG, 2) +
                 " gyroMagnitudeDps=" + String(reading.gyroMagnitudeDps, 1) +
                 " raw accel(g)=[" + String(reading.accelXg, 2) + "," + String(reading.accelYg, 2) +
                 "," + String(reading.accelZg, 2) + "] gyro(dps)=[" + String(reading.gyroXdps, 1) +
                 "," + String(reading.gyroYdps, 1) + "," + String(reading.gyroZdps, 1) + "]");

    if (espNowActive) {
        JsonDocument reqDoc;
        reqDoc["triggerNodeId"] = cfg.nodeId;
        reqDoc["triggerEventId"] = eventId;
        String reqPayload;
        serializeJson(reqDoc, reqPayload);
        EspNowManager::sendMessage(EspNowMessageType::CAPTURE_REQUEST, reqPayload, nullptr);
    }
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

// Splits "COMMAND a b c ..." into up to maxTokens space-separated tokens after the
// command word — used by EMAILCONFIG, which needs more fields than splitArgs handles.
// No quoting support: none of these fields (SMTP host/port/username/sender/recipient)
// are expected to contain spaces. A password containing a space isn't supported by
// this command; documented, not silently mishandled.
static uint8_t splitTokens(const String& line, String tokens[], uint8_t maxTokens) {
    int firstSpace = line.indexOf(' ');
    if (firstSpace < 0) return 0;
    String rest = line.substring(firstSpace + 1);
    rest.trim();
    uint8_t count = 0;
    while (rest.length() > 0 && count < maxTokens) {
        int sp = rest.indexOf(' ');
        if (sp < 0) {
            tokens[count++] = rest;
            break;
        }
        tokens[count++] = rest.substring(0, sp);
        rest = rest.substring(sp + 1);
        rest.trim();
    }
    return count;
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
    PowerManager::applyModeChange(mode);
    broadcastModeToAllDevices(mode);
}

// Section 25/26: draws one OLED page's content. Deliberately terse (128x64 at text
// size 1 fits ~8 lines of ~21 chars) — this mirrors buildStatusHtml()/the STATUS
// command's fields at a glance, not a full replica of either.
static void renderDisplayPage(uint8_t displayIndex, DisplayPageId page, Adafruit_SSD1306& d) {
    const DeviceConfigData& cfg = DeviceConfig::get();
    switch (page) {
        case DisplayPageId::HOME: {
            d.println("CarSentinel");
            d.println(cfg.nodeId);
            d.print("Mode: ");
            d.println(securityModeToString(SecurityModeConfig::getMode()));
            d.print("ESP-NOW: ");
            d.println(espNowActive ? "active" : "off");
            break;
        }
        case DisplayPageId::NETWORK: {
            d.println("NETWORK");
            d.println(WiFiManager::isConnected() ? NetworkConfig::get().ssid : "not connected");
            d.println(WiFiManager::isConnected() ? WiFiManager::localIP() : "-");
            break;
        }
        case DisplayPageId::GPS_PAGE: {
            d.println("GPS");
            GpsFix fix = GpsManager::getFix();
            if (fix.status == GpsFixStatus::FIX) {
                d.print("Sat: "); d.println(fix.satellites);
                d.print("Spd: "); d.print(fix.speedKmph, 1); d.println(" km/h");
                d.println(String(fix.latitude, 5));
                d.println(String(fix.longitude, 5));
            } else {
                d.println("NO FIX");
            }
            break;
        }
        case DisplayPageId::IMU_PAGE: {
            d.println("IMU");
            if (ImuManager::isInitialized()) {
                ImuReading r = ImuManager::read();
                d.print("Accel: "); d.print(r.accelMagnitudeG, 2); d.println("g");
                d.print("Gyro: "); d.print(r.gyroMagnitudeDps, 1); d.println(" dps");
            } else {
                d.println("not present");
            }
            break;
        }
        case DisplayPageId::SECURITY: {
            d.println("SECURITY");
            d.print("Mode: "); d.println(securityModeToString(SecurityModeConfig::getMode()));
            d.print("Devices: "); d.println(DeviceRegistry::count());
            d.print("Email: "); d.println(EmailConfig::get().enabled ? "on" : "off");
            break;
        }
        case DisplayPageId::DEVICES: {
            d.println("DEVICES");
            uint8_t shown = 0;
            for (uint8_t i = 0; i < DeviceRegistry::count() && shown < 5; i++) {
                DeviceRegistryEntry* dv = DeviceRegistry::get(i);
                d.println(dv->displayName + (dv->enabled ? "" : " (off)"));
                shown++;
            }
            if (DeviceRegistry::count() == 0) d.println("(none yet)");
            break;
        }
        default: {  // SYSTEM
            DiagnosticsSnapshot diag = Diagnostics::snapshot();
            d.println("SYSTEM");
            d.println(cfg.firmwareVersion);
            d.print("Up: "); d.print(diag.uptimeMs / 1000); d.println("s");
            d.print("Heap: "); d.println(diag.freeHeap);
            break;
        }
    }
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
    bool display0Present = false, display1Present = false;
    if (caps.display && caps.displayCount >= 1) {
        display0Present = I2CBusManager::isPresent(0, SSD1306_I2C_ADDR);
        Logger::info(TAG, String("SSD1306 #1 (0x3C) on bus 0: ") + (display0Present ? "PRESENT" : "NOT FOUND"));
    }
    if (caps.display && caps.displayCount >= 2) {
        display1Present = I2CBusManager::isPresent(1, SSD1306_I2C_ADDR);
        Logger::info(TAG, String("SSD1306 #2 (0x3C) on bus 1: ") + (display1Present ? "PRESENT" : "NOT FOUND"));
    }
    if (display0Present || display1Present) {
        DisplayManager::setRenderCallback(renderDisplayPage);
        DisplayManager::begin(display0Present, display1Present);
    }

    if (caps.gps) {
        GpsManager::begin(caps.gpsRxGpio, caps.gpsTxGpio);
    } else {
        Logger::info(TAG, "GPS capability disabled; skipping UART init");
    }

    if (caps.ignition && caps.ignitionGpio != GPIO_UNCONFIGURED) {
        IgnitionSense::begin(caps.ignitionGpio);
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
        Logger::info(TAG, "email.enabled=" + String(EmailConfig::get().enabled) +
                     " email.host=" + EmailConfig::get().smtpHost);
        Logger::info(TAG, "display0.present=" + String(DisplayManager::isPresent(0)) +
                     " display1.present=" + String(DisplayManager::isPresent(1)));
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
                         " ip=" + (d->ip.isEmpty() ? String("-") : d->ip) +
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
    } else if (line == "INCIDENTS") {
        // Section 11: lists every persisted incident record on the gateway's own flash
        // (not SD — the gateway has none; see IncidentCorrelator.h). Direct LittleFS
        // scan rather than a new IncidentCorrelator API, since this is read-only
        // reporting the class itself doesn't need to own.
        File dir = LittleFS.open("/incidents");
        uint16_t count = 0;
        if (dir && dir.isDirectory()) {
            File entry = dir.openNextFile();
            while (entry) {
                String name = String(entry.name());
                if (name.endsWith(".json")) {
                    count++;
                    Logger::info(TAG, "  " + name);
                }
                entry = dir.openNextFile();
            }
        }
        Logger::info(TAG, "Persisted incidents: " + String(count));
    } else if (line.startsWith("EMAILCONFIG")) {
        // Section 29: SMTP settings, persisted, never logged (the password is written
        // to /config/email_config.json but never echoed back in any log line).
        String tokens[6];
        uint8_t n = splitTokens(line, tokens, 6);
        if (n < 6) {
            Logger::warn(TAG, "Usage: EMAILCONFIG <smtpHost> <smtpPort> <username> <password> <sender> <recipient>");
        } else {
            EmailConfigData cfg = EmailConfig::get();
            cfg.smtpHost = tokens[0];
            cfg.smtpPort = (uint16_t)tokens[1].toInt();
            cfg.username = tokens[2];
            cfg.password = tokens[3];
            cfg.sender = tokens[4];
            cfg.recipient = tokens[5];
            cfg.enabled = true;
            EmailConfig::save(cfg);
            Logger::info(TAG, "Email configured and enabled: host=" + cfg.smtpHost +
                         ":" + String(cfg.smtpPort) + " sender=" + cfg.sender +
                         " recipient=" + cfg.recipient + " (password not logged)");
        }
    } else if (line == "EMAILENABLE") {
        EmailConfigData cfg = EmailConfig::get();
        cfg.enabled = true;
        EmailConfig::save(cfg);
        Logger::info(TAG, "Email notifications enabled");
    } else if (line == "EMAILDISABLE") {
        EmailConfigData cfg = EmailConfig::get();
        cfg.enabled = false;
        EmailConfig::save(cfg);
        Logger::info(TAG, "Email notifications disabled");
    } else if (line == "TESTEMAIL") {
        Logger::info(TAG, "Sending test email...");
        bool sent = NotificationManager::sendTest();
        Logger::info(TAG, sent ? "Test email sent" : "Test email failed — check EMAILCONFIG and serial log above");
    } else if (line.startsWith("BACKENDCONFIG")) {
        // Phase 21.2: remote backend settings, persisted, credential never logged —
        // same posture as EMAILCONFIG's password above.
        String tokens[4];
        uint8_t n = splitTokens(line, tokens, 4);
        if (n < 4) {
            Logger::warn(TAG, "Usage: BACKENDCONFIG <LOCAL_ONLY|CAR_SENTINEL_CLOUD|CUSTOM_SERVER> "
                         "<baseUrl> <deviceId> <credential>");
        } else {
            BackendConfigData cfg = BackendConfig::get();
            cfg.mode = backendModeFromString(tokens[0]);
            cfg.baseUrl = tokens[1];
            cfg.deviceId = tokens[2];
            cfg.credential = tokens[3];
            cfg.enabled = cfg.mode != BackendMode::LOCAL_ONLY;
            BackendConfig::save(cfg);
            RemoteSyncManager::begin();
            Logger::info(TAG, "Backend configured: mode=" + String(backendModeToString(cfg.mode)) +
                         " baseUrl=" + cfg.baseUrl + " deviceId=" + cfg.deviceId +
                         " (credential not logged)");
        }
    } else if (line == "BACKENDENABLE") {
        BackendConfigData cfg = BackendConfig::get();
        cfg.enabled = true;
        BackendConfig::save(cfg);
        RemoteSyncManager::begin();
        Logger::info(TAG, "Backend sync enabled");
    } else if (line == "BACKENDDISABLE") {
        BackendConfigData cfg = BackendConfig::get();
        cfg.enabled = false;
        BackendConfig::save(cfg);
        RemoteSyncManager::begin();
        Logger::info(TAG, "Backend sync disabled — LOCAL_ONLY");
    } else if (line == "BACKENDSTATUS") {
        Logger::info(TAG, "Backend: " + String(backendConnectionStateToString(RemoteSyncManager::getState())));
    } else if (line == "WIFILIST") {
        const NetworkConfigData& net = NetworkConfig::get();
        Logger::info(TAG, "Saved Wi-Fi networks (" + String(net.savedCount) + "/" +
                     String(MAX_SAVED_NETWORKS) + "), primary=\"" + net.ssid + "\":");
        for (uint8_t i = 0; i < net.savedCount; i++) {
            Logger::info(TAG, "  " + net.saved[i].ssid);
        }
    } else if (line.startsWith("WIFIADD ")) {
        String ssid, password;
        splitArgs(line, ssid, password);
        bool ok = NetworkConfig::addNetwork(ssid, password);
        Logger::info(TAG, ok ? ("Saved network \"" + ssid + "\"") : "Usage: WIFIADD <ssid> <password>");
    } else if (line.startsWith("WIFIREMOVE ")) {
        String ssid = line.substring(11);
        ssid.trim();
        bool ok = NetworkConfig::removeNetwork(ssid);
        Logger::info(TAG, ok ? ("Removed network \"" + ssid + "\"") : "No saved network named \"" + ssid + "\"");
    } else if (line == "WIFIFALLBACK ON") {
        NetworkConfig::setWifiFallbackEnabled(true);
        Logger::info(TAG, "Wi-Fi fallback enabled — restart to take effect on the transport boot order");
    } else if (line == "WIFIFALLBACK OFF") {
        NetworkConfig::setWifiFallbackEnabled(false);
        Logger::info(TAG, "Wi-Fi fallback disabled — restart to run ESP-NOW only");
    } else if (line.startsWith("DISPLAYPAGES ")) {
        // Section 25: "configuration should determine display content, do not hardcode
        // a display's purpose." <index> is 0 or 1; <pages> is a comma-separated list
        // like HOME,SECURITY,GPS.
        String indexStr, pagesStr;
        splitArgs(line, indexStr, pagesStr);
        int idx = indexStr.toInt();
        if ((indexStr != "0" && indexStr != "1") || pagesStr.isEmpty()) {
            Logger::warn(TAG, "Usage: DISPLAYPAGES <0|1> <PAGE,PAGE,...>  (pages: HOME, "
                         "NETWORK, GPS, IMU, SECURITY, DEVICES, SYSTEM)");
        } else {
            DisplayConfigData cfg = DisplayManager::getConfig();
            uint8_t count = 0;
            DisplayPageId parsed[DisplayConfigData::MAX_PAGES];
            String remaining = pagesStr;
            while (remaining.length() > 0 && count < DisplayConfigData::MAX_PAGES) {
                int comma = remaining.indexOf(',');
                String token = comma < 0 ? remaining : remaining.substring(0, comma);
                token.trim();
                if (token.length() > 0) parsed[count++] = displayPageIdFromString(token);
                if (comma < 0) break;
                remaining = remaining.substring(comma + 1);
            }
            if (idx == 0) {
                cfg.display0PageCount = count;
                for (uint8_t i = 0; i < count; i++) cfg.display0Pages[i] = parsed[i];
            } else {
                cfg.display1PageCount = count;
                for (uint8_t i = 0; i < count; i++) cfg.display1Pages[i] = parsed[i];
            }
            DisplayManager::setConfig(cfg);
            Logger::info(TAG, "Display " + String(idx) + " pages updated (" + String(count) + " page(s))");
        }
    } else if (line.startsWith("DISPLAYINTERVAL ")) {
        String msStr, unused;
        splitArgs(line, msStr, unused);
        DisplayConfigData cfg = DisplayManager::getConfig();
        cfg.pageIntervalMs = (unsigned long)msStr.toInt();
        DisplayManager::setConfig(cfg);
        Logger::info(TAG, "Display page interval set to " + String(cfg.pageIntervalMs) + "ms");
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

// Phase 19 dashboard JSON providers (see DashboardServer.h) — mirrors the STATUS/DEVICES
// serial commands' fields so they never drift into showing different things.
static String buildStatusJson() {
    const DeviceConfigData& cfg = DeviceConfig::get();
    const NetworkConfigData& net = NetworkConfig::get();
    const CapabilitiesConfigData& caps = CapabilitiesConfig::get();
    DiagnosticsSnapshot diag = Diagnostics::snapshot();

    JsonDocument doc;
    doc["nodeId"] = cfg.nodeId;
    doc["displayName"] = cfg.displayName;
    doc["hardwareProfile"] = cfg.hardwareProfile;
    doc["firmwareVersion"] = cfg.firmwareVersion;
    doc["mac"] = DeviceIdentity::macAddress();
    doc["wifiSsid"] = net.ssid;
    doc["wifiIp"] = WiFiManager::localIP();
    doc["uptimeS"] = diag.uptimeMs / 1000;
    doc["freeHeap"] = diag.freeHeap;

    if (caps.gps) {
        GpsFix fix = GpsManager::getFix();
        JsonObject gps = doc["gps"].to<JsonObject>();
        gps["status"] = fix.status == GpsFixStatus::NO_FIX ? "NO_FIX" : "FIX";
        gps["lat"] = fix.latitude;
        gps["lon"] = fix.longitude;
        gps["speedKmph"] = fix.speedKmph;
        gps["satellites"] = fix.satellites;
    }
    if (caps.imu && ImuManager::isInitialized()) {
        ImuReading r = ImuManager::read();
        JsonObject imu = doc["imu"].to<JsonObject>();
        imu["accelG"] = r.accelMagnitudeG;
        imu["gyroDps"] = r.gyroMagnitudeDps;
    }

    doc["securityMode"] = String(securityModeToString(SecurityModeConfig::getMode())) +
                           (SecurityModeConfig::isManualOverride() ? " (manual)" : " (auto)");
    doc["emailEnabled"] = EmailConfig::get().enabled;
    doc["espNowActive"] = espNowActive;
    doc["peersSeen"] = PeerRegistry::count();
    doc["deviceCount"] = DeviceRegistry::count();

    String out;
    serializeJson(doc, out);
    return out;
}

// Section 6 registry — auto-populated, zero-code.
static String buildDevicesJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < DeviceRegistry::count(); i++) {
        DeviceRegistryEntry* d = DeviceRegistry::get(i);
        JsonObject o = arr.add<JsonObject>();
        o["nodeId"] = d->nodeId;
        o["displayName"] = d->displayName;
        o["role"] = d->role;
        o["enabled"] = d->enabled;
        o["ip"] = d->ip;
        o["lastSeenAgoMs"] = d->lastSeenMs == 0 ? 0 : (millis() - d->lastSeenMs);
    }
    String out;
    serializeJson(arr, out);
    return out;
}

// Delegates straight to IncidentCorrelator, which already owns the persisted record
// format — this just picks how many to show on the dashboard.
static String buildIncidentsJson() {
    return IncidentCorrelator::listRecentJson(20);
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

    // Section 37: "report healthy, mark update successful" — the half of that this
    // project can honestly deliver (see OtaManager.h for what depends on the
    // bootloader's own build configuration, not guaranteed here).
    OtaManager::confirmHealthyBoot();

    initHardwareCapabilities();

    DeviceRegistry::begin();
    IncidentCorrelator::begin();
    SecurityModeConfig::begin();
    PowerManager::applyModeChange(SecurityModeConfig::getMode());
    NotificationManager::begin();
    IncidentCorrelator::setNotificationHandler(assistedIncidentNotify);

    // Phase 21.2 — optional remote backend sync. BackendConfig defaults disabled
    // (LOCAL_ONLY); RemoteSyncManager::begin() with that config is a no-op beyond
    // logging, matching docs/BACKEND.md Section 6's local-first guarantee.
    BackendConfig::begin();
    RemoteSyncManager::begin();

    // docs/NETWORK.md: ESP-NOW is the primary transport and starts unconditionally,
    // regardless of Wi-Fi/provisioning state — this used to be gated behind
    // `!provisioningMode`, meaning a gateway with no saved Wi-Fi credentials never
    // started ESP-NOW at all until someone finished Wi-Fi provisioning. That was
    // backwards: ESP-NOW must work even if Wi-Fi/Internet never gets configured.
    espNowActive = EspNowManager::begin(cfg.nodeId, roleToString(cfg.role));
    if (espNowActive) {
        EspNowManager::setOnMessageHandler(onEspNowMessage);
        EspNowManager::setOnPeerHeartbeatHandler(onEspNowHeartbeat);
    }

    // Wi-Fi is independent of ESP-NOW's state — it's the gateway's fallback path for
    // reaching nodes over IP, and its own path to the router/Internet (email,
    // dashboard, OTA image downloads). wifiFallbackEnabled defaults true so an
    // existing gateway keeps its current behavior; set false only for a gateway
    // deliberately run ESP-NOW-only (unusual, but the config model allows it —
    // docs/NETWORK.md Section 6).
    if (NetworkConfig::get().wifiFallbackEnabled) {
        if (NetworkConfig::hasCredentials()) {
            bool connected = WiFiManager::connectBestKnown();
            if (!connected) {
                Logger::warn(TAG, "Saved Wi-Fi credentials failed to connect; opening provisioning");
                enterProvisioningMode();
            }
        } else {
            Logger::info(TAG, "No saved Wi-Fi credentials; opening provisioning");
            enterProvisioningMode();
        }
    } else {
        Logger::info(TAG, "Wi-Fi fallback disabled (WIFIFALLBACK OFF) — running ESP-NOW only");
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
        DashboardServer::begin("CarSentinel Gateway " + cfg.nodeId, buildStatusJson, buildDevicesJson,
                       buildIncidentsJson, streamCameraFromNode);
    } else {
        Logger::warn(TAG, "NETWORK: not connected (no IP) — Wi-Fi will keep retrying in the background");
    }

    Logger::info(TAG, "Boot complete. Serial commands: STATUS, FACTORY_RESET, PROVISION, "
                 "DEVICES, RENAME <id> <name>, ENABLE <id>, DISABLE <id>, REMOVE <id>, "
                 "SETROLE <id> <role>, RESTART <id>, RESET <id>, IMUTHRESHOLDS <accelG> <gyroDps>, "
                 "MODE, MODE <DISARMED|DRIVING|PARKED|SERVICE>, AUTOMODE, INCIDENTS, "
                 "EMAILCONFIG <host> <port> <user> <pass> <sender> <recipient>, "
                 "EMAILENABLE, EMAILDISABLE, TESTEMAIL, DISPLAYPAGES <0|1> <PAGE,...>, "
                 "DISPLAYINTERVAL <ms>, OTACHECK <manifestUrl>, OTAUPDATE <manifestUrl>, "
                 "WIFILIST, WIFIADD <ssid> <password>, WIFIREMOVE <ssid>, "
                 "WIFIFALLBACK <ON|OFF>, "
                 "BACKENDCONFIG <mode> <baseUrl> <deviceId> <credential>, "
                 "BACKENDENABLE, BACKENDDISABLE, BACKENDSTATUS "
                 "(also available on the dashboard's Settings page)");
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
        if (!DashboardServer::isActive() && WiFiManager::isConnected()) {
            DashboardServer::begin("CarSentinel Gateway " + DeviceConfig::get().nodeId, buildStatusJson,
                                    buildDevicesJson, buildIncidentsJson, streamCameraFromNode);
        }
        DashboardServer::loop();
        RemoteSyncManager::loop();  // no-op cost when LOCAL_ONLY — see RemoteSyncManager.h
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
        // Phase 18: a wired ignition-sense line is a more reliable signal than
        // inferring "moving" from GPS speed/IMU jostling — use it exclusively when
        // configured rather than blending it with the heuristic below.
        if (caps.ignition && IgnitionSense::isConfigured()) {
            moving = IgnitionSense::isOn();
        } else {
            if (caps.gps && GpsManager::hasFix() && GpsManager::getFix().speedKmph > MOVEMENT_SPEED_KMPH) {
                moving = true;
            }
            if (haveImuReading &&
                (fabsf(lastImuReading.accelMagnitudeG - 1.0f) > MOVEMENT_ACCEL_DELTA_G ||
                 lastImuReading.gyroMagnitudeDps > MOVEMENT_GYRO_DPS)) {
                moving = true;
            }
        }
        if (moving) { movingStreak++; stationaryStreak = 0; }
        else { stationaryStreak++; movingStreak = 0; }

        if (movingStreak >= MODE_SWITCH_STREAK && SecurityModeConfig::getMode() != SecurityMode::DRIVING) {
            applyModeChange(SecurityMode::DRIVING, false);
        } else if (stationaryStreak >= MODE_SWITCH_STREAK && SecurityModeConfig::getMode() != SecurityMode::PARKED) {
            applyModeChange(SecurityMode::PARKED, false);
        }
    }

    if (caps.display) {
        DisplayManager::loop();
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
