#pragma once

#include <Arduino.h>
#include <WiFiClient.h>

// Phase 19 — gateway-only. Replaces StatusPage on the gateway (node keeps the plain
// StatusPage; this needs Adafruit-free, always-available gateway data, not a second
// hardware profile) with a real single-page dashboard: live device cards, incident
// timeline, GPS/IMU/health widgets, auto-polled from a small JSON API rather than the
// old meta-refresh full-page reload. The API is intentionally also documented/usable on
// its own (Section 19's "companion API") for a future phone app or external tool.
//
// Sensor-agnostic by the same pattern as StatusPage/IncidentCorrelator: this class knows
// nothing about DeviceRegistry/GpsManager/ImuManager — gateway_main.cpp supplies three
// JSON-string-producing callbacks and this class only serves them over HTTP.
namespace CarSentinel {

typedef String (*JsonContentProvider)();
typedef void (*DashboardStreamProvider)(WiFiClient client, const String& nodeId);

class DashboardServer {
public:
    static void begin(const String& deviceTitle, JsonContentProvider statusProvider,
                       JsonContentProvider devicesProvider, JsonContentProvider incidentsProvider,
                       DashboardStreamProvider streamProvider = nullptr);
    static void loop();
    static bool isActive();

private:
    static void handleRoot();
    static void handleApiStatus();
    static void handleApiDevices();
    static void handleApiIncidents();
    static void handleStream();
    // Settings (WiFi networks + SMTP) talk to NetworkConfig/EmailConfig directly
    // rather than through callbacks — unlike the sensor-facing providers above, these
    // are both already-shared config classes (same ones gateway_main.cpp itself calls
    // directly), not device drivers this class needs to stay decoupled from.
    static void handleSettingsPage();
    static void handleApiTransportGet();
    static void handleApiTransportSave();
    static void handleApiWifiList();
    static void handleApiWifiAdd();
    static void handleApiWifiRemove();
    static void handleApiEmailGet();
    static void handleApiEmailSave();
    static void handleApiBackendGet();
    static void handleApiBackendSave();

    static JsonContentProvider statusProvider;
    static JsonContentProvider devicesProvider;
    static JsonContentProvider incidentsProvider;
    static DashboardStreamProvider streamProvider;
    static String title;
    static bool active;
};

}  // namespace CarSentinel
