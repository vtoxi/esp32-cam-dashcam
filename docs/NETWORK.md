# CarSentinel — Networking Architecture

**Status: architecture/design document. Describes the target hybrid transport model.
Where the current implementation doesn't yet match this document, that's called out
explicitly under "Gap vs. current implementation" — this file is not a claim that the
gap is already closed.** See `docs/IMPLEMENTATION_PLAN.md` for which phase is expected
to close it.

## 1. Core principle: ESP-NOW primary, Wi-Fi fallback, standalone always available

Every node (camera or sensor) follows this transport priority:

```text
ESP-NOW  →  Wi-Fi fallback  →  Standalone operation
```

- **ESP-NOW is the default, preferred transport** between a node and the gateway. A
  node's normal operating mode requires no router, no Internet, and no Wi-Fi
  credentials at all.
- **Wi-Fi is a fallback**, used only when the gateway can't be reached over ESP-NOW
  within a configurable timeout. Even then, the destination is the gateway's own IP on
  the local network — not the Internet.
- **Standalone operation is mandatory**, not a degraded edge case. If neither
  transport can reach the gateway, every node's local security/capture/telemetry
  pipeline (RCWL → capture → SD evidence, DHT, IMU, mode detection) keeps running
  exactly as if the gateway were present. Events queue locally and sync automatically
  once connectivity returns.

This inverts an assumption baked into the current implementation and current docs
(see "Gap vs. current implementation" below): today, a node with no saved Wi-Fi
credentials boots straight into Wi-Fi/BLE provisioning mode and **does not start
ESP-NOW at all** until it leaves provisioning. Under this architecture, ESP-NOW starts
immediately on every boot regardless of Wi-Fi state, and provisioning becomes optional
— needed only to name/role a device or to configure the Wi-Fi fallback, never a
prerequisite for a node to talk to its gateway.

## 2. What travels over which transport

**ESP-NOW** (small, time-sensitive, control/event traffic):
gateway discovery, node discovery, pairing/handshake, authentication, heartbeats, node
status, device configuration push, security state, RCWL motion events, DHT telemetry,
GPS telemetry, IMU telemetry/events, incident metadata, camera trigger commands
(`CAPTURE_REQUEST`), capture-complete notifications (`CAPTURE_RESULT`), synchronization
commands, time synchronization, OTA control/status messages (the small "check/trigger"
side — see `docs/OTA.md`).

**Wi-Fi** (large or continuous data, and gateway ↔ Internet):
JPEG images and live MJPEG video (Phase 4/19 — already Wi-Fi-only:
`CameraManager`'s stream endpoint and `EvidenceManager`'s stored JPEGs never go over
ESP-NOW), firmware binaries (Phase 14's `OtaManager`, already HTTP(S)), the gateway's
web dashboard (Phase 19), AI/cloud uploads if ever added, and the gateway's own
Internet connectivity (email — Phase 12, future cloud/API integration).

ESP-NOW is never the transport for continuous video or firmware images — this is
already true today (the camera stream and OTA both use `WiFiClient`/`HTTPClient`
exclusively) and stays true going forward.

## 3. Transport state machine

```text
                    ┌────────────────────┐
                    │      NODE BOOT      │
                    └──────────┬──────────┘
                               ↓
                        Try ESP-NOW
                               ↓
                      Gateway available?
                       /              \
                     YES               NO
                      ↓                 ↓
                ESPNOW_CONNECTED   Wi-Fi fallback enabled?
                      ↑              /          \
                      │            YES           NO
                      │             ↓             ↓
                      │      Try Wi-Fi        STANDALONE
                      │             ↓             ↑
                      │     Gateway available?     │
                      │       /          \         │
                      │     YES           NO ──────┘
                      │      ↓
                      │  WIFI_CONNECTED
                      │      │
                      │      │ (periodic ESP-NOW retry while in Wi-Fi mode)
                      └──────┘
```

States (`TransportManager`'s state machine — see Section 5):

```text
DISCONNECTED
ESPNOW_CONNECTING
ESPNOW_CONNECTED
WIFI_FALLBACK_CONNECTING
WIFI_CONNECTED
STANDALONE
```

Rules:
- ESP-NOW is retried periodically (`espnow.retryIntervalMs`) even while in
  `WIFI_CONNECTED` or `STANDALONE` — the system never permanently commits to a
  fallback. On success, it switches back to `ESPNOW_CONNECTED` (optionally dropping
  the Wi-Fi connection, configurable — see Section 6).
- All four configurable timings (ESP-NOW discovery timeout, Wi-Fi fallback delay,
  ESP-NOW retry interval while in Wi-Fi mode, gateway heartbeat timeout) live in
  per-device config, not hardcoded constants scattered through application code — see
  Section 6.
- This is a genuine state machine (one current-state variable, explicit transitions),
  not scattered booleans (`espNowActive`, `provisioningMode`, `WiFiManager::isConnected()`
  checked ad hoc) the way the current code tracks state today.

## 4. Standalone mode

When neither transport can reach the gateway, a node keeps doing everything it does
normally:

- RCWL motion detection, camera capture, local SD evidence storage, DHT/IMU readings,
  local mode detection — none of this reads the transport state before running
  (already true today: `node_main.cpp`'s capture pipeline runs unconditionally in
  `loop()` regardless of `espNowActive`/Wi-Fi state — see Section 5 of the project
  spec, "node independence").
- What's genuinely new: **events generated while standalone must be queued locally**
  and delivered once the gateway becomes reachable again (over whichever transport
  reconnects first), rather than simply being lost because
  `EspNowManager::sendMessage()` had nothing to send to. This queue does not exist
  yet — see Section 9's gap note.

No security functionality may be written to assume the gateway is reachable.

## 5. Transport abstraction — two layers, not one

The existing `Transport` interface (`lib/CarSentinelCommon/Transport.h`,
implemented by `EspNowTransport`) is a **low-level radio abstraction**: `sendTo(mac,
...)`, `broadcastMessage(...)`, `registerPeer(mac)`. It's ESP-NOW-shaped by design —
addressed by MAC, no concept of "gateway" or "fallback." That's correct for what it
is and doesn't need to change.

What this architecture adds is a **new, higher-level `TransportManager`** that sits
above `EspNowManager` (which itself already wraps `Transport`/`EspNowTransport`) and a
new `WiFiTransport` (gateway-reachable-over-IP, not a general Wi-Fi radio wrapper —
`WiFiManager` already owns raw STA connect/reconnect and keeps doing that job).
`TransportManager` is addressed by logical identity (gateway/node ID), not MAC or IP,
and owns the state machine in Section 3:

```text
                     Application code
                    (gateway_main.cpp /
                     node_main.cpp)
                            │
                   sendEvent() / sendCommand() /
                sendTelemetry() / sendStatus() /
                     requestConfiguration()
                            │
                    ┌───────▼────────┐
                    │ TransportManager│  ← owns the state machine (Section 3)
                    └───────┬────────┘
                     ┌──────┴───────┐
                     ↓              ↓
              EspNowManager    WiFiTransport
              (existing)       (new — gateway-reachable-over-IP,
                    │           HTTP(S) to the gateway's own
              Transport /       dashboard API, not a Wi-Fi
              EspNowTransport   radio wrapper)
              (existing)
```

Application code should call `TransportManager`'s generic operations
(`sendEvent()`, `sendCommand()`, `sendTelemetry()`, `sendStatus()`,
`requestConfiguration()`) instead of calling `EspNowManager::sendMessage()` directly
everywhere the way `gateway_main.cpp`/`node_main.cpp` do today. `TransportManager`
picks the active channel per the state machine and falls back automatically; callers
don't choose a transport.

## 6. Configuration (per-device, no recompilation)

Extends `NetworkConfig` (already versioned/migrated — see `NetworkConfig.h`'s schema
v1→v2 history) rather than inventing a parallel config file. Conceptual shape (not a
final schema — see `docs/IMPLEMENTATION_PLAN.md`'s architecture-update entry for what
actually needs deciding before implementation):

```json
{
  "schemaVersion": 3,
  "network": {
    "primaryTransport": "espnow",
    "fallbackTransport": "wifi",
    "espnow": {
      "enabled": true,
      "discoveryTimeoutMs": 15000,
      "retryIntervalMs": 30000,
      "heartbeatTimeoutMs": 60000
    },
    "wifi": {
      "enabled": true,
      "fallbackDelayMs": 5000,
      "gatewayHost": "",
      "gatewayPort": 80
    }
  }
}
```

`primaryTransport`/`fallbackTransport` must support all three configurations without
recompiling:

- **ESP-NOW only**: `primaryTransport: "espnow"`, `fallbackTransport` absent/disabled.
  No Wi-Fi credentials required or requested.
- **ESP-NOW + Wi-Fi fallback**: the default described throughout this document.
- **Wi-Fi only**: `primaryTransport: "wifi"` — for a device with no ESP-NOW peer at
  all (unusual, but the config model shouldn't forbid it).

This is configured per device (gateway and each node independently), consistent with
"configuration over hard-coding" (`docs/ARCHITECTURE.md`'s core design principle).

## 7. Wi-Fi credentials are optional

An ESP-NOW-only node must never be forced to enter Wi-Fi credentials. Wi-Fi
credentials are only requested/stored when Wi-Fi fallback is enabled for that device.
See `docs/PROVISIONING.md` for how this changes the BLE/AP provisioning flow, and
`NetworkConfig`'s existing multi-network support (`docs/IMPLEMENTATION_PLAN.md`, the
"Multiple remembered Wi-Fi networks" entry) for where saved credentials already live —
that mechanism doesn't change, only when a node is required to have any credentials at
all.

## 8. Gateway networking

The gateway's own role doesn't change: Wi-Fi for router/Internet (email, future cloud,
OTA image downloads, the web dashboard) and ESP-NOW as the coordinator for every node.
The gateway is not itself expected to have an ESP-NOW fallback story the way a node
does — it's already the thing nodes fall back *to*.

```text
                    Internet
                       │
                     Wi-Fi
                       │
              ┌────────▼────────┐
              │ ESP32-S3 Gateway│
              └────────┬────────┘
                       │
                    ESP-NOW (primary)
                    Wi-Fi (fallback, node → gateway's own IP, not the Internet)
          ┌────────────┼────────────┐
          ↓            ↓            ↓
      ESP32-CAM     ESP32-CAM    Sensor Node
```

## 9. Gap vs. current implementation

Concrete, code-referenced differences between this document and what's actually
running today (all of these are planning notes — nothing here has been implemented as
part of this update):

1. **ESP-NOW is gated behind Wi-Fi/provisioning, not the other way around.**
   `node_main.cpp`'s `setup()`: if `NetworkConfig::hasCredentials()` is false, the node
   calls `enterProvisioningMode()` and ESP-NOW is not started
   (`if (!provisioningMode) { espNowActive = EspNowManager::begin(...); }`). Under this
   architecture, ESP-NOW starts unconditionally and early; provisioning/Wi-Fi becomes
   independent of it.
2. **No `TransportManager` or `WiFiTransport` exist.** Application code
   (`gateway_main.cpp`, `node_main.cpp`) calls `EspNowManager::sendMessage()` and
   ESP-NOW-specific APIs directly; there's no generic `sendEvent()`/`sendCommand()`/
   `sendTelemetry()`/`sendStatus()`/`requestConfiguration()` layer, and no transport
   state machine — state is tracked via separate booleans (`espNowActive`,
   `provisioningMode`) and `WiFiManager::isConnected()` checked ad hoc.
3. **No offline event queue.** `EspNowManager`'s own header already documents this gap
   (`"the persistent offline queue is Section 28 / Phase 28, out of scope here"`) — a
   message sent while the gateway is unreachable over ESP-NOW is simply not delivered;
   nothing queues it for later. Standalone mode's local capture/storage already works
   (Section 4), but the *sync-when-reconnected* half doesn't exist yet.
4. **No formal gateway discovery/pairing handshake.** What exists today
   (`EspNowManager`'s HELLO/HEARTBEAT + `DeviceRegistry::upsertFromDiscovery()`) is
   zero-code auto-discovery, not the identify → validate → authenticate → register →
   receive-configuration sequence Section 10 describes — see `docs/SECURITY.md` for
   the security-specific gaps in this same area.
5. **README/ARCHITECTURE currently describe Wi-Fi as if every device needs it.**
   Being corrected as part of this same doc update — see those files' current text vs.
   this document.

None of these are contradictions to "fix silently" — each is called out here
specifically so the implementation phase that closes this gap has a concrete,
line-referenced starting checklist rather than a vague "make it hybrid."

## 10. Failure scenarios (design must handle all of these)

See `docs/TESTING.md` for the corresponding test matrix. Scenario labels (A–F) are
shared between the two documents.

| # | Scenario | Expected behavior |
|---|---|---|
| A | Gateway + Wi-Fi both available | Node → ESP-NOW → Gateway (ESP-NOW preferred even when Wi-Fi would also work) |
| B | Router unavailable, gateway still running | Node → ESP-NOW → Gateway (unaffected — ESP-NOW doesn't need the router) |
| C | ESP-NOW temporarily unavailable, gateway reachable via Wi-Fi | Node → Wi-Fi → Gateway (fallback engages) |
| D | Gateway completely unavailable | Node → Standalone (local capture/security continues; events queue) |
| E | Gateway returns after being unavailable | Standalone → ESP-NOW discovery → reconnect → queued events sync |
| F | Node in Wi-Fi fallback, ESP-NOW gateway returns | Wi-Fi fallback → ESP-NOW gateway detected → switch back to ESP-NOW (optionally drop Wi-Fi) |
