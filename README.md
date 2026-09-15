# CarSentinel

A modular, configuration-driven, distributed ESP32 vehicle dashcam, security, telemetry
and black-box platform — built for a Peugeot 2008 prototype install, designed to run on
any vehicle.

> **Status: Phase 4 — Single Camera Node.** Boot/identity/config (Phase 1), BLE+AP
> provisioning (Phase 2), capability-gated hardware init (Phase 3), and now a working
> motion → confirm → capture → local-evidence pipeline make one ESP32-CAM node a
> standalone security device with no gateway required. **Not yet compiled/flashed** — no
> PlatformIO toolchain is available in the environment this was written in. This is the
> first phase that genuinely needs physical hardware (camera + SD card) to validate. See
> [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) for phase status.

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
