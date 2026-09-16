#pragma once

#include <Arduino.h>
#include <WiFiClient.h>

// Minimal read-only status page served at the device's normal Wi-Fi IP once connected
// (distinct from ProvisioningPortal, which only serves the temporary setup AP at
// 192.168.4.1 — the two never run at the same time, since a device is either in
// provisioning mode or normally connected, never both). This is NOT Phase 19's
// dashboard: no config forms, no device-management actions, no multi-page navigation —
// just "hit the IP, see what's going on," view-only. Auto-refreshes so it's useful
// without the visitor needing to manually reload.
namespace CarSentinel {

// Returns an HTML fragment (a series of <p>/<table> etc.) to place inside the page
// body — caller-provided so node_main.cpp and gateway_main.cpp can each report their
// own relevant state without this shared class knowing about either.
typedef String (*StatusContentProvider)();
typedef void (*StatusStreamProvider)(WiFiClient client, const String& nodeId);
// Called with the requested on/off state; returns the resulting actual state (so the
// caller can't be lied to by a request that silently failed) — e.g. CameraManager's
// setFlash()+isFlashOn() on a node with a flash LED wired.
typedef bool (*StatusFlashToggleProvider)(bool on);

class StatusPage {
public:
    static void begin(const String& deviceTitle, StatusContentProvider provider,
                      StatusStreamProvider streamProvider = nullptr,
                      StatusFlashToggleProvider flashProvider = nullptr);
    static void loop();
    static bool isActive();

private:
    static void handleRoot();
    static void handleStream();
    static void handleFlash();
    static StatusContentProvider contentProvider;
    static StatusStreamProvider streamProvider;
    static StatusFlashToggleProvider flashProvider;
    static String title;
    static bool active;
};

}  // namespace CarSentinel
