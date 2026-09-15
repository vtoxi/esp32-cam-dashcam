#pragma once

#include <Arduino.h>

// Temporary Wi-Fi AP + minimal web form provisioning (Section 9's fallback when BLE is
// unavailable/inconvenient). Serves a single page at 192.168.4.1 to set Wi-Fi
// credentials, hostname, node display name, and role. On save, persists via
// NetworkConfig/DeviceConfig and reboots into normal (STA) boot. The AP is only active
// while provisioning is open — callers must call stop() once provisioning completes or
// a real Wi-Fi connection is desired, per Section 9 ("After configuration, disable
// provisioning AP").
namespace CarSentinel {

class ProvisioningPortal {
public:
    // apSsid should already include a unique suffix (e.g. derived from MAC) so multiple
    // unprovisioned devices don't collide. No AP password by default — see
    // docs/PROVISIONING.md (not yet written) for the accepted trade-off; physical/RF
    // proximity is the only access control during first-time setup.
    static void begin(const String& apSsid);
    static void stop();

    // Services the SoftAP's web server; call every loop() iteration while active.
    static void loop();

    static bool isActive();

    // True once a client has submitted valid config via the form; caller should then
    // stop() the portal and reboot.
    static bool isSubmitted();

private:
    static bool active;
    static bool submitted;
    static void handleRoot();
    static void handleSave();
    static void handleNotFound();
};

}  // namespace CarSentinel
