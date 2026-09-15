# CarSentinel — Architecture (Phase 0)

## Overview

CarSentinel is a distributed ESP32 vehicle dashcam/security/telemetry platform. One
ESP32-S3 **Gateway** coordinates multiple ESP32-CAM **Camera Nodes** and vehicle-level
sensors (GPS, IMU, OLED displays) over ESP-NOW. Every node must remain independently
functional if the gateway, Wi-Fi, or Internet is unavailable.

```text
                         INTERNET (optional)
                            │
                         Wi-Fi
                            │
                 ┌──────────▼──────────┐
                 │   ESP32-S3 Gateway  │
                 │ Wi-Fi / BLE / OTA   │
                 │ ESP-NOW coordinator │
                 │ Incident engine     │
                 │ Email / AI (opt.)   │
                 │ GPS + IMU + OLEDs   │
                 └──────────┬──────────┘
                            │
                         ESP-NOW
          ┌─────────────────┼─────────────────┐
     ┌────▼────┐       ┌────▼────┐       ┌────▼────┐
     │ FRONT   │       │ REAR    │       │ LEFT/   │
     │ CAMERA  │       │ CAMERA  │       │ RIGHT   │
     │ (RCWL,  │       │ (RCWL,  │       │ CAMERA  │
     │  DHT,SD)│       │  DHT,SD)│       │ (...)   │
     └─────────┘       └─────────┘       └─────────┘
```

## Toolchain (decided for Phase 1+)

- **Framework**: Arduino core for ESP32, built via **PlatformIO**.
- **Rationale**: mature libraries exist for every target peripheral (esp32-camera,
  Adafruit SSD1306/MPU6050, TinyGPS++, ESP-NOW, ArduinoJson) and PlatformIO gives
  per-board environments (gateway vs. camera node) from one repo without duplicating
  project files.
- Two PlatformIO environments planned: `gateway` (ESP32-S3) and `node` (ESP32-CAM,
  AI-Thinker). A shared `lib/` or `src/common` tree holds code used by both.

## Core Design Principle: Configuration Over Hard-Coding

No `front_camera.ino` / `rear_camera.ino`. One generic node firmware; node identity,
role, and enabled capabilities are runtime configuration (persisted on-device, set via
BLE/AP provisioning, no recompilation to add/remove/reassign a camera). See the main
project specification (Sections 1–2) for the full rationale — this document does not
repeat it, only records how the repo implements it.

## Node Independence

Every camera node runs its own local security/capture/storage logic and only *attempts*
ESP-NOW delivery to the gateway; if the gateway or network is unreachable, events queue
locally (persisted, survives reboot) and sync when connectivity returns. This is a hard
requirement carried into every phase's design — see Sections 5, 28, 65 of the project
spec.

## Repository Layout (target — created incrementally per phase)

```text
dashcam_setup/
├── README.md
├── docs/
│   ├── ARCHITECTURE.md          (this file)
│   ├── HARDWARE.md
│   ├── IMPLEMENTATION_PLAN.md
│   └── wiring/
│       ├── README.md
│       ├── ESP32_CAM.md
│       ├── ESP32_S3_GATEWAY.md
│       ├── RCWL_0516.md
│       ├── DHT11.md
│       ├── NEO_6M.md
│       ├── MPU6050_HW123.md
│       ├── SSD1306.md
│       ├── MICROSD.md
│       └── POWER.md
├── configs/
│   ├── defaults/         (created Phase 1: default config schema)
│   ├── hardware/          (hardware profiles, e.g. ESP32_CAM_AI_THINKER.json)
│   └── examples/
├── firmware/
│   ├── gateway/           (PlatformIO env, created Phase 1)
│   ├── node/               (PlatformIO env, created Phase 1)
│   └── common/             (shared lib code)
└── tests/                  (host-side simulation tests, created per phase)
```

Only `docs/`, `configs/hardware/` (as data, not code) are created in Phase 0. No
firmware source is written yet, per the project's phase discipline (Section 64: "STOP
after Phase 0").

## What Phase 0 deliberately does NOT do

- No GPIO pin finalization for the ESP32-S3 gateway (exact board model unconfirmed —
  see `docs/HARDWARE.md` Open Questions).
- No ESP-NOW protocol implementation.
- No PlatformIO project scaffolding / `platformio.ini` (deferred to Phase 1, so the
  toolchain choice can still be revisited before code depends on it).
- No ESP32-CAM ↔ SD ↔ RCWL ↔ DHT final pin table — a well-known public AI-Thinker
  reference pinout is recorded in `docs/wiring/ESP32_CAM.md` but flagged for physical
  verification before any wiring is done, per the project's "never guess GPIOs" rule.
