# CarSentinel

A modular, configuration-driven, distributed ESP32 vehicle dashcam, security, telemetry
and black-box platform — built for a Peugeot 2008 prototype install, designed to run on
any vehicle.

> **Status: all 20 core phases have working firmware or a documented plan, plus
> Phase 21 (optional remote backend) underway.** Quick orientation:
>
> - **Phases 1–19**: full local system — provisioning, capability-gated hardware
>   init, motion → capture → local evidence, ESP-NOW mesh, zero-code device
>   discovery, multi-camera correlation, GPS/IMU, auto security modes, incident
>   engine, email alerts, OLED displays, gateway OTA, a heuristic AI threat-scoring
>   layer, Wi-Fi modem sleep while parked, ignition-sense input, and a gateway-hosted
>   dashboard with live camera streaming, flash control, and a Settings page (Wi-Fi
>   networks + SMTP). **Compiles clean for both targets; node and gateway have been
>   flashed and tested together on real hardware** (zero-code device discovery
>   confirmed end-to-end) — most individual features are still pending their own
>   physical bench test (tracked per-phase in `docs/IMPLEMENTATION_PLAN.md`).
> - **Phase 20**: vehicle install — a planning checklist
>   (`docs/wiring/VEHICLE_INSTALLATION.md`), no firmware component, no physical
>   install done yet.
> - **Networking architecture**: reworked to be **ESP-NOW-primary / Wi-Fi-fallback /
>   standalone-mandatory** — a node's normal operation needs no Wi-Fi credentials, no
>   router, no Internet. ESP-NOW now starts unconditionally on every boot; Wi-Fi is
>   an opt-out fallback (`WIFIFALLBACK ON|OFF`); a `TransportManager` state machine
>   and a persisted `OfflineQueue` mean an event generated while the gateway is
>   unreachable is queued, not lost. Full design and exactly what's implemented vs.
>   still open: [docs/NETWORK.md](docs/NETWORK.md).
> - **Phase 21 (Remote Backend, API & Hybrid Connectivity)** — optional, off by
>   default, never a dependency for anything above. Gateway-side abstraction
>   (`RemoteSyncManager`/`RemoteBackend`/`HttpBackend`/`BackendQueue`, device
>   registration + auth, configurable via the dashboard's Settings page or
>   `BACKENDCONFIG`) is done and compiles clean. A real reference backend now exists
>   too — [`backend/`](backend/), ASP.NET Core 8, manually verified end-to-end
>   (register/heartbeat/telemetry/incident-upsert/query/auth-rejection all confirmed
>   working against a running instance) — though the Gateway↔Backend integration
>   itself (real firmware talking to this server) is still unverified. See
>   [docs/BACKEND.md](docs/BACKEND.md), [docs/REMOTE_ACCESS.md](docs/REMOTE_ACCESS.md),
>   and [backend/README.md](backend/README.md) for how to run it.
> - Classic ESP32 camera nodes cannot fit OTA's flash-write code within their fixed
>   IRAM budget alongside WiFi/BLE/camera — a real, measured link failure, not a
>   guess — so OTA is gateway-only.
>
> See [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) for the full
> phase-by-phase status, every known limitation, and what's next.

## Overview

CarSentinel is a network of ESP32-based nodes — one **Gateway** (ESP32-S3) and several
**Camera Nodes** (ESP32-CAM) — that together form a vehicle security and dashcam system.
The defining property of the design: **hardware becomes configuration, not code**. One
generic firmware image runs on every camera node; which sensors exist, what role a node
plays, and how it behaves are all set at runtime via provisioning, not by writing a new
`.ino` file per camera position.

Every node is designed to keep working on its own — motion detection, evidence capture,
and local storage do not depend on the gateway, Wi-Fi, or Internet being available.

## Features (planned — see phase status)

- Multi-camera coordination over ESP-NOW (primary transport — no Wi-Fi required for
  normal node operation), each camera independently functional
- Wi-Fi fallback (opt-out per device) + mandatory standalone operation when neither
  transport reaches the gateway, with a persisted offline event queue that syncs
  automatically on reconnect (`TransportManager`/`OfflineQueue`) — see
  [docs/NETWORK.md](docs/NETWORK.md)
- Multiple remembered Wi-Fi networks per device, tried in order at boot
- BLE and temporary-AP provisioning — add/remove/reassign a camera with no code changes
- RCWL-0516 motion detection with debounce/cooldown, DHT temperature/humidity logging
- GPS (NEO-6M) and IMU (MPU6050) telemetry, correlated into incident records
- A pluggable heuristic AI threat-scoring layer that gates incident email alerts on
  assessed severity (real on-device ML evaluated and scoped out — see
  `docs/IMPLEMENTATION_PLAN.md` Phase 15/16)
- Dual SSD1306 OLED status displays with configurable pages
- Local evidence storage on microSD with automatic retention cleanup
- Email notifications (optional), capability-gated ignition-sense input for
  DRIVING/PARKED detection
- Gateway-hosted dashboard: live device/incident data, a JSON companion API, live
  camera streaming + flash control, and a Settings page (Wi-Fi networks, SMTP,
  optional remote backend)
- OTA firmware updates with checksum verification and health-check rollback
  (gateway-only — classic ESP32 camera nodes can't fit it in their IRAM budget)
- Configurable security modes: `DISARMED`, `DRIVING`, `PARKED`, `SERVICE`, with
  Wi-Fi modem sleep while parked
- Optional remote backend sync (off by default, never a dependency for anything
  above) — see [docs/BACKEND.md](docs/BACKEND.md)

## Architecture

```text
Gateway (ESP32-S3)
  ↓ ESP-NOW (primary) / Wi-Fi fallback (node → gateway, not the Internet)
Camera / Sensor Nodes (ESP32-CAM, generic firmware)
```

A node's normal operation requires no Wi-Fi credentials, no router, and no Internet —
ESP-NOW is the default transport (starts unconditionally on every boot), Wi-Fi is an
opt-out fallback (`WIFIFALLBACK ON|OFF`), and full standalone local operation
(motion/capture/evidence/telemetry) is mandatory when neither is reachable, with a
`TransportManager` state machine and a persisted `OfflineQueue` so nothing generated
while unreachable is lost. See [docs/NETWORK.md](docs/NETWORK.md) for the full design
and exactly what's implemented vs. still open (gateway-identity validation and
Wi-Fi-fallback message *delivery* remain open gaps), plus
[docs/PROVISIONING.md](docs/PROVISIONING.md),
[docs/SECURITY.md](docs/SECURITY.md), [docs/OTA.md](docs/OTA.md),
[docs/TESTING.md](docs/TESTING.md), and — for the optional remote backend layer sitting
above the gateway — [docs/BACKEND.md](docs/BACKEND.md) and
[docs/REMOTE_ACCESS.md](docs/REMOTE_ACCESS.md). Full diagrams and design rationale:
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Hardware

| Role | Confirmed Hardware |
|---|---|
| Camera node | AI-Thinker ESP32-CAM (OV2640 + microSD) |
| Gateway | ESP32-S3-N16R8 dev board (16MB flash / 8MB PSRAM, dual USB-C) |
| Motion | RCWL-0516 |
| Environment | DHT11 |
| GPS | NEO-6M (GY-NEO6MV2) |
| IMU | MPU6050 (HW-123 breakout) |
| Display | SSD1306 0.96" 128x64 I2C ×2 (both fixed at address `0x3C` — no jumper; resolved via second I2C bus on the gateway) |

Full inventory, open questions, and known GPIO conflicts:
[docs/HARDWARE.md](docs/HARDWARE.md).

## Toolchain

Arduino core for ESP32, built via PlatformIO (two environments: `gateway`, `node`). See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#toolchain-decided-for-phase-1) for rationale.

## Quick Start

```
1. Install PlatformIO (pip install platformio, or the VS Code extension)
2. cd firmware
3. pio run -e gateway     # build the ESP32-S3 gateway image
4. pio run -e node        # build the generic ESP32-CAM node image
5. pio run -e gateway -t upload   # flash the gateway (adjust port if needed)
6. pio run -e node -t upload      # flash a camera node (via FTDI, GPIO0 to GND while flashing)
7. pio device monitor             # watch boot logs; try STATUS, FACTORY_RESET, PROVISION
```

ESP-NOW starts immediately on every boot, independent of Wi-Fi — a node with no saved
Wi-Fi credentials and Wi-Fi fallback disabled (`WIFIFALLBACK OFF`) never opens
provisioning for Wi-Fi at all and talks to its gateway over ESP-NOW right away. With
Wi-Fi fallback enabled (the default) and no saved credentials, a device opens **both**
BLE and a temporary Wi-Fi AP (`CarSentinel-Setup-<id>`) for provisioning — connect to
the AP and browse to `192.168.4.1`, or use a BLE GATT client, to set identity (display
name, role) and the Wi-Fi SSID/password/hostname; the device reboots into normal
operation once submitted. Send `PROVISION` over serial at any time to clear saved
Wi-Fi credentials and re-open provisioning without a full factory reset.
`ProvisioningPortal`'s form doesn't yet let you opt out of the Wi-Fi field *during*
first-time provisioning itself (`WIFIFALLBACK OFF` right after still works) — see
[docs/PROVISIONING.md](docs/PROVISIONING.md) for that remaining gap.

Once a device successfully joins your Wi-Fi network (fallback transport, or the
gateway's own Internet-facing connection — see [docs/NETWORK.md](docs/NETWORK.md)),
browse to its IP address (shown in the boot log as `NETWORK: connected, IP=...`, or via
the `STATUS` serial command) for a read-only status page — identity, sensors, ESP-NOW
state, and (on the gateway) the live device registry, dashboard, and settings.

## Adding / Removing / Replacing a Camera

Design target (implemented starting Phase 6): flash the generic node firmware once,
provision via BLE or temporary Wi-Fi AP, assign a name and hardware profile, and the
camera joins the ESP-NOW network — no source changes. See project specification Sections
70–73 for the full workflow this repo is building toward.

## Configuration

All normal operational settings (node identity, sensors enabled, Wi-Fi, security mode,
thresholds, storage retention, email, AI, OTA) are runtime-configurable — only
board-level pin definitions require a firmware rebuild. Device identity/role persists in
`/config/device.json`; Wi-Fi credentials persist separately in `/config/network.json` so
they can be cleared independently (Phase 1/2). Both are versioned with a `schemaVersion`
field and a migration seam for future firmware updates (Section 39).

## OTA

Manual-trigger firmware updates over HTTP(S) with MD5 verification and a post-update
health check (`OTACHECK`/`OTAUPDATE <manifestUrl>`, or the dashboard). **Gateway-only**
— classic ESP32 camera nodes can't fit `HTTPClient`/`Update.h`'s flash-write code
alongside their existing WiFi/BLE/camera footprint within their fixed IRAM budget, a
real measured link failure documented in `docs/IMPLEMENTATION_PLAN.md` Phase 14 and
`docs/OTA.md`, not a guess.

## Remote Backend (optional, Phase 21)

The gateway can optionally sync to a remote backend over HTTPS — off by default
(`LOCAL_ONLY`), and never a dependency for anything else in this project: every local
feature above works identically with it disabled. Configure via the dashboard's
Settings page or `BACKENDCONFIG <mode> <baseUrl> <deviceId> <credential>` over serial.

A real reference backend exists at [`backend/`](backend/) — ASP.NET Core 8 + SQLite,
device registration with per-device credentials (SHA-256 hashed, never a shared global
key), telemetry/event/incident ingestion (incidents upserted by ID, not appended),
Server-Sent Events real-time stream, end-to-end evidence upload (node → gateway →
backend), a Backend→Gateway remote command flow, and admin-managed webhooks
(HMAC-signed outbound delivery), Swagger UI, all manually verified end-to-end. Run it
(`cd backend/src/CarSentinel.Backend && dotnet run`) and point a gateway at it — see
[backend/README.md](backend/README.md). Persisted retry queue (`BackendQueue`) and
exponential backoff exist for the Gateway↔Backend hop. Phase 21 (21.1–21.11) is
complete — API documented in [docs/API.md](docs/API.md), test matrix in
[docs/TESTING.md](docs/TESTING.md). Every backend flow is curl-verified end to end;
the one remaining gap is bench-testing the gateway firmware side against a live
backend, since no physical hardware pairing has been available during development
(tracked explicitly, not hidden — see `docs/TESTING.md` Section 4). See
[docs/BACKEND.md](docs/BACKEND.md), [docs/REMOTE_ACCESS.md](docs/REMOTE_ACCESS.md),
and [docs/API.md](docs/API.md).

## Security

ESP-NOW messages are HMAC-SHA256 signed and sequence-numbered (bounded replay
protection, not hardened against a sophisticated attacker — documented as such, not
overstated). Known gaps: every device ships the same compiled-in default HMAC key
until manually rotated (no key-distribution mechanism yet), and there's no
cryptographic gateway-identity validation — a device is trusted as "the gateway" on
the first HELLO claiming that role with a valid HMAC. Full details, the Gateway↔Backend
TLS/credential boundary Phase 21 adds, and the complete gap list:
[docs/SECURITY.md](docs/SECURITY.md).

## Development

`firmware/platformio.ini` defines two environments, `gateway` and `node`, both built
from one Arduino/PlatformIO project. Logic shared by both roles (device identity,
persistent config, diagnostics, watchdog, logging, transport/queue, sensors) lives in
`firmware/lib/CarSentinelCommon/`; gateway-only code (dashboard, AI threat scoring,
remote backend, OLED displays) lives in `firmware/lib/CarSentinelGateway/` — kept
separate specifically so PlatformIO's dependency finder doesn't pull gateway-only
libraries into the node build, which matters on classic ESP32's tight IRAM budget (see
`docs/OTA.md`'s Phase 14 history for why this separation exists). Each environment
compiles only its own entry point (`firmware/src/gateway_main.cpp` or `node_main.cpp`).
See Quick Start above for build/flash commands.

## Wiring

Per-module wiring documentation (pin tables, voltage requirements, safety notes) lives in
[docs/wiring/](docs/wiring/), including the confirmed hardware table above (ESP32-S3-N16R8
gateway, AI-Thinker ESP32-CAM nodes) and the Phase 20 vehicle-install checklist
([docs/wiring/VEHICLE_INSTALLATION.md](docs/wiring/VEHICLE_INSTALLATION.md)). Check each
module's own doc for its current confirmation status before wiring anything.

## Troubleshooting

No dedicated troubleshooting doc yet. `docs/IMPLEMENTATION_PLAN.md` documents every real
bug found and fixed so far (watchdog timing, camera reinit after a soft reset, a
firmware-version display bug, an OTA-vs-IRAM link failure, and others) per-phase, with
root cause and fix — worth searching before assuming something is a new issue.
