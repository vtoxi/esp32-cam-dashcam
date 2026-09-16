# CarSentinel

A modular, configuration-driven, distributed ESP32 vehicle dashcam, security, telemetry
and black-box platform — built for a Peugeot 2008 prototype install, designed to run on
any vehicle.

> **Status: Phases 0–19 implemented** (Phase 20 Vehicle Installation is a physical
> install phase with no firmware component — see below). Boot/identity/config
> (Phase 1), BLE+AP provisioning (Phase 2), capability-gated
> hardware init (Phase 3), a working motion → confirm → capture → local-evidence
> pipeline (Phase 4), node-to-gateway ESP-NOW communication (Phase 5), a zero-code
> auto-populating gateway device registry with remote commands (Phase 6), cross-camera
> correlation (Phase 7), real NEO-6M NMEA parsing (Phase 8), MPU6050 accel/gyro reads
> with configurable, never-claims-a-crash impact-threshold detection (Phase 9),
> `DISARMED`/`DRIVING`/`PARKED`/`SERVICE` security modes auto-detected from GPS/IMU
> (Phase 10), real persisted incident records with full lifecycle and GPS/IMU/DHT
> association (Phase 11), email alerts tied to the incident engine (Phase 12),
> dual-SSD1306 OLED status pages (Phase 13), gateway self-update over HTTP(S) with
> MD5-verified streaming writes (Phase 14 — node self-update is a documented hardware
> limitation, see below), a pluggable heuristic threat-scoring framework that gates
> incident email alerts on assessed severity (Phases 15–16 — real on-device ML was
> evaluated and scoped out, see docs), Wi-Fi modem sleep while parked (Phase 17 — full
> deep-sleep deliberately scoped out to protect the verified motion-alert pipeline), a
> capability-gated ignition-sense input that drives DRIVING/PARKED directly when wired
> (Phase 18 — OBD-II/CAN blocked on not yet having a confirmed harness), and a real
> gateway-hosted dashboard: live device/incident data, a JSON companion API, and a live
> camera stream proxy (Phase 19). **Compiles clean for both targets, and both the node
> and gateway have now been flashed and tested together on real hardware** —
> zero-code device discovery (Phase 6) confirmed working end-to-end. Multi-camera
> correlation (Phase 7), GPS fix acquisition, IMU, mode auto-transitions, and every
> Phase 13–19 feature are still pending a physical bench test. Classic ESP32 camera
> nodes cannot fit OTA's flash-write code within their fixed IRAM budget alongside
> WiFi/BLE/camera — a real, measured link failure, not a guess — so OTA is gateway-only
> this phase. See [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) for full
> phase status and known limitations.

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

- Multi-camera coordination over ESP-NOW, each camera independently functional
- BLE and temporary-AP provisioning — add/remove/reassign a camera with no code changes
- RCWL-0516 motion detection with debounce/cooldown, DHT temperature/humidity logging
- GPS (NEO-6M) and IMU (MPU6050) telemetry, correlated into incident records
- Dual SSD1306 OLED status displays with configurable pages
- Local evidence storage on microSD with automatic retention cleanup
- Offline event queueing — incidents sync to the gateway when connectivity returns
- Email notifications (optional), AI-assisted classification (optional, never required)
- OTA firmware updates with checksum verification and health-check rollback
- Configurable security modes: `DISARMED`, `DRIVING`, `PARKED`, `SERVICE`

## Architecture

```text
Gateway (ESP32-S3)
  ↓ ESP-NOW
Camera / Sensor Nodes (ESP32-CAM, generic firmware)
```

Full diagrams and design rationale: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

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

On first boot (no saved Wi-Fi credentials), a device opens **both** BLE and a temporary
Wi-Fi AP (`CarSentinel-Setup-<id>`) for provisioning. Connect to the AP and browse to
`192.168.4.1`, or use a BLE GATT client, to set the Wi-Fi SSID/password, hostname,
display name, and role; the device reboots into normal operation once submitted. Send
`PROVISION` over serial at any time to clear saved Wi-Fi credentials and re-open
provisioning without a full factory reset.

Once a device successfully joins your Wi-Fi network, browse to its IP address (shown in
the boot log as `NETWORK: connected, IP=...`, or via the `STATUS` serial command) for a
read-only, auto-refreshing status page — identity, sensors, ESP-NOW state, and (on the
gateway) the live device registry. This is a minimal view-only page, not the full
configuration dashboard (that's a later phase).

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

Firmware updates are pushed from the gateway with checksum verification and a
post-update health check; details land in Phase 14.

## Security

Node authentication, message validation, and replay protection for ESP-NOW are designed
in from Phase 5 onward. Full details and known limitations will be documented in
`docs/SECURITY.md` once that work begins — not yet written.

## Development

`firmware/platformio.ini` defines two environments, `gateway` and `node`, both built
from one Arduino/PlatformIO project. Shared logic (device identity, persistent config,
diagnostics, watchdog, logging) lives in `firmware/lib/CarSentinelCommon/`; each
environment compiles only its own entry point (`firmware/src/gateway_main.cpp` or
`node_main.cpp`). See Quick Start above for build/flash commands.

## Wiring

Per-module wiring documentation (pin tables, voltage requirements, safety notes) lives in
[docs/wiring/](docs/wiring/). Most documents currently carry **TBD** placeholders pending
exact ESP32-S3 gateway board identification — see
[docs/wiring/README.md](docs/wiring/README.md) for status per module. Nothing should be
physically wired from a TBD document.

## Troubleshooting

Not yet applicable — no firmware exists to troubleshoot.
