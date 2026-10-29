# CarSentinel — Architecture (Phase 0, networking model updated post-Phase 20)

## Overview

CarSentinel is a distributed ESP32 vehicle dashcam/security/telemetry platform. One
ESP32-S3 **Gateway** coordinates multiple ESP32-CAM **Camera Nodes** and vehicle-level
sensors (GPS, IMU, OLED displays) over a **hybrid transport**: ESP-NOW is the primary,
preferred transport between every node and the gateway; Wi-Fi is a fallback used only
when ESP-NOW can't reach the gateway; standalone local operation (no transport
reachable at all) is mandatory, not a degraded edge case. **A node's normal operation
requires no Wi-Fi credentials, no router, and no Internet access.** See
`docs/NETWORK.md` for the full transport architecture, state machine, and — important —
exactly where the current implementation doesn't yet match this model.

Every node must remain independently functional if the gateway, Wi-Fi, or Internet is
unavailable — this was already the stated principle in Phase 0 and is now the load
-bearing requirement the networking architecture is built around, not just an
aspiration.

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
              ESP-NOW (primary) / Wi-Fi (fallback,
              node → gateway's own IP — never required
              to reach the Internet)
          ┌─────────────────┼─────────────────┐
     ┌────▼────┐       ┌────▼────┐       ┌────▼────┐
     │ FRONT   │       │ REAR    │       │ LEFT/   │
     │ CAMERA  │       │ CAMERA  │       │ RIGHT   │
     │ (RCWL,  │       │ (RCWL,  │       │ CAMERA  │
     │  DHT,SD)│       │  DHT,SD)│       │ (...)   │
     └─────────┘       └─────────┘       └─────────┘
```

See `docs/NETWORK.md` for the transport state machine (ESP-NOW ↔ Wi-Fi fallback ↔
standalone), `docs/PROVISIONING.md` for how a device gets its identity and (optional)
Wi-Fi fallback credentials, `docs/SECURITY.md` for the ESP-NOW authentication model and
its known gaps, `docs/OTA.md` for how firmware updates map onto this transport split,
`docs/TESTING.md` for the failure-scenario test matrix this model requires, and
`docs/BACKEND.md`/`docs/REMOTE_ACCESS.md` (Phase 21, architecture-audit stage only —
nothing implemented yet) for the optional remote-backend layer this local-first system
is designed to extend to: `Node → ESP-NOW → Gateway → HTTPS → Backend`, with the
Gateway remaining fully functional with zero backend/Internet connectivity, exactly as
every tier below it already is.

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
delivery to the gateway (over ESP-NOW first, Wi-Fi fallback second — `docs/NETWORK.md`);
if neither is reachable, the node operates in standalone mode and events are meant to
queue locally (persisted, survives reboot) and sync when connectivity returns. This is
a hard requirement carried into every phase's design — see Sections 5, 28, 65 of the
project spec. **The persisted offline-event queue itself is not yet implemented** —
local capture/storage already works independent of network state, but automatic
sync-on-reconnect does not exist yet; see `docs/NETWORK.md` Section 9 for the tracked
gap.

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
│   ├── platformio.ini      (two envs: gateway, node — see below)
│   ├── src/
│   │   ├── gateway_main.cpp
│   │   └── node_main.cpp
│   └── lib/
│       └── CarSentinelCommon/  (shared: identity, config, diagnostics, watchdog, logging)
└── tests/                  (host-side simulation tests, created per phase)
```

**Note on `firmware/` layout (decided in Phase 1):** the originally sketched
`firmware/gateway/`, `firmware/node/`, `firmware/common/` split doesn't map cleanly onto
PlatformIO, which expects one `src/` per project. Instead, both entry points
(`gateway_main.cpp`, `node_main.cpp`) live in one `src/`, and each PlatformIO environment
compiles only its own file via `build_src_filter`; shared logic lives in `lib/`, which
PlatformIO's library finder picks up automatically for both environments. This keeps the
"one generic image, two build targets" property without fighting the build tool.

Only `docs/` and `configs/defaults/` were created in Phase 0/1. `configs/hardware/`
(hardware profile JSON files) is still empty pending Phase 3.

## What Phase 0 deliberately does NOT do

- No GPIO pin finalization for the ESP32-S3 gateway (exact board model unconfirmed —
  see `docs/HARDWARE.md` Open Questions).
- No ESP-NOW protocol implementation.
- No PlatformIO project scaffolding / `platformio.ini` (deferred to Phase 1, so the
  toolchain choice can still be revisited before code depends on it).
- No ESP32-CAM ↔ SD ↔ RCWL ↔ DHT final pin table — a well-known public AI-Thinker
  reference pinout is recorded in `docs/wiring/ESP32_CAM.md` but flagged for physical
  verification before any wiring is done, per the project's "never guess GPIOs" rule.
