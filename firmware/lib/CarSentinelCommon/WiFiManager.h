#pragma once

#include <Arduino.h>
#include "NetworkConfig.h"

// Wraps WiFi.h STA connect behind a bounded attempt (Section 10: retry a configurable
// number of times, then give up and let the caller start provisioning — never block
// normal operation indefinitely waiting for Wi-Fi). After a successful connect, loop()
// watches for drops and reconnects using the same saved credentials without re-entering
// provisioning.
namespace CarSentinel {

enum class WiFiConnState : uint8_t {
    DISCONNECTED = 0,
    CONNECTING,
    CONNECTED
};

class WiFiManager {
public:
    // Blocks for at most (maxRetries * (connectTimeoutMs + retryIntervalMs)) — bounded,
    // never infinite. Returns true only on an actual STA connection. Safe to call with
    // no saved credentials (returns false immediately).
    static bool connectBlocking(const NetworkConfigData& config);

    // Orchestrates connectBlocking() over multiple remembered networks ("multiple wifi
    // remember" — Section 49-style config-over-hardcoding, applied to which network to
    // use rather than which sensors exist): tries the current primary first, then each
    // other saved network in order, stopping at the first success. On success to a
    // non-primary network, persists it as the new primary via
    // NetworkConfig::setPrimary() so the next boot's fast path (connectBlocking() alone)
    // tries the right one first. Bounded — same as connectBlocking(), just repeated
    // per candidate, so still never blocks forever.
    static bool connectBestKnown();

    static bool isConnected();
    static WiFiConnState getState();
    static String localIP();

    // Non-blocking: call every loop() iteration once initially connected, to notice and
    // recover from drops without blocking camera/security logic. No-op if never
    // successfully connected via connectBlocking() first (provisioning mode owns
    // reconnect behavior differently — see ProvisioningPortal/BLEProvisioning).
    static void loop();

private:
    static WiFiConnState state;
    static NetworkConfigData activeConfig;
    static unsigned long lastReconnectAttempt;
    static const unsigned long RECONNECT_INTERVAL_MS = 30000;
};

}  // namespace CarSentinel
