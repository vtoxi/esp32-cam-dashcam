# CarSentinel

A modular, configuration-driven, distributed ESP32 vehicle dashcam, security, telemetry
and black-box platform — built for a Peugeot 2008 prototype install, designed to run on
any vehicle.

> **Status: Phase 0 — Repository & Hardware Discovery.** No firmware exists yet. This
> repository currently contains architecture and hardware documentation only. See
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

Not yet applicable — no firmware exists. This section will be filled in starting Phase 1
(build/flash instructions) and Phase 2 (provisioning walkthrough).

## Adding / Removing / Replacing a Camera

Design target (implemented starting Phase 6): flash the generic node firmware once,
provision via BLE or temporary Wi-Fi AP, assign a name and hardware profile, and the
camera joins the ESP-NOW network — no source changes. See project specification Sections
70–73 for the full workflow this repo is building toward.

## Configuration

All normal operational settings (node identity, sensors enabled, Wi-Fi, security mode,
thresholds, storage retention, email, AI, OTA) are runtime-configurable — only
board-level pin definitions require a firmware rebuild. Configuration schema and
persistence land in Phase 1.

## OTA

Firmware updates are pushed from the gateway with checksum verification and a
post-update health check; details land in Phase 14.

## Security

Node authentication, message validation, and replay protection for ESP-NOW are designed
in from Phase 5 onward. Full details and known limitations will be documented in
`docs/SECURITY.md` once that work begins — not yet written.

## Development

No build system exists yet (Phase 1 introduces `platformio.ini`). This section will be
updated once firmware scaffolding lands.

## Wiring

Per-module wiring documentation (pin tables, voltage requirements, safety notes) lives in
[docs/wiring/](docs/wiring/). Most documents currently carry **TBD** placeholders pending
exact ESP32-S3 gateway board identification — see
[docs/wiring/README.md](docs/wiring/README.md) for status per module. Nothing should be
physically wired from a TBD document.

## Troubleshooting

Not yet applicable — no firmware exists to troubleshoot.
