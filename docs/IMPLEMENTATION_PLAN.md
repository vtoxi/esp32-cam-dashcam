# CarSentinel — Implementation Plan

Phases are implemented strictly one at a time, per the project specification (Section
64/76). Each phase stops for review before the next begins. This file tracks status only
— see the project spec for full phase definitions.

## Status

| Phase | Name | Status |
|---|---|---|
| 0 | Repository & Hardware Discovery | Complete |
| 1 | Generic Device Foundation | **In progress** (this commit) |
| 2 | BLE + Wi-Fi Provisioning | Not started |
| 3 | Hardware Capability Layer | Not started |
| 4 | Single Camera Node | Not started |
| 5 | ESP-NOW | Not started |
| 6 | Dynamic Node Management | Not started |
| 7 | Multi-Camera Correlation | Not started |
| 8 | GPS | Not started |
| 9 | MPU6050 | Not started |
| 10 | Driving / Parking Modes | Not started |
| 11 | Incident & Evidence Engine | Not started |
| 12 | Email Notification | Not started |
| 13 | OLED Displays | Not started |
| 14 | OTA | Not started |
| 15 | AI Framework | Not started |
| 16 | AI Security Assistance | Not started |
| 17 | Low-Power Parked Mode | Not started |
| 18 | Vehicle Integration | Not started |
| 19 | Dashboard | Not started |
| 20 | Vehicle Installation | Not started |

## Phase 0 — Repository & Hardware Discovery

**Findings:**
- Repository was empty (no framework, no code, not a git repo) — initialized fresh.
- Toolchain decided: Arduino core for ESP32 via PlatformIO (two environments: `gateway`,
  `node`).
- Hardware confirmed: AI-Thinker ESP32-CAM (camera nodes), generic ESP32-S3-WROOM dev
  board (gateway — exact model still unconfirmed), RCWL-0516, DHT11, 0.96" SSD1306 I2C,
  GY-NEO6MV2 (NEO-6M), GY-521 (MPU6050).
- Known GPIO conflict area: AI-Thinker ESP32-CAM has very limited free GPIOs once camera
  + SD are active — RCWL + DHT must be fit into the remaining pins, to be verified in
  Phase 3 against the physical board (not assumed here).
- Gateway exact pin table is **TBD** pending exact ESP32-S3 board identification.

Full detail in `docs/HARDWARE.md`.

**Deliverables produced this phase:**
- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/HARDWARE.md`
- `docs/IMPLEMENTATION_PLAN.md` (this file)
- `docs/wiring/README.md` + module wiring docs (ESP32_CAM, ESP32_S3_GATEWAY, RCWL_0516,
  DHT11, NEO_6M, MPU6050_HW123, SSD1306, MICROSD, POWER)

**Explicitly not done:** no firmware, no `platformio.ini`, no ESP-NOW code, no final
gateway GPIO assignments.

## Phase 1 — Generic Device Foundation

**Implemented:**
- `firmware/platformio.ini` — two environments (`gateway`: esp32-s3-devkitc-1 board def
  as closest match to the confirmed ESP32-S3-N16R8; `node`: esp32cam / AI-Thinker),
  sharing one Arduino/PlatformIO project via per-environment `build_src_filter`.
- `firmware/lib/CarSentinelCommon/`:
  - `Logger` — ERROR/WARN/INFO/DEBUG/TRACE structured logging to Serial, runtime level.
  - `DeviceIdentity` — MAC-derived default node ID (`GATEWAY-XXXXXX` / `NODE-XXXXXX`)
    so an unprovisioned device is identifiable before Phase 2 BLE/AP provisioning exists.
  - `DeviceConfig` — persistent config on LittleFS (`/config/device.json`), fields
    `schemaVersion, nodeId, displayName, role, hardwareProfile, firmwareVersion`, with a
    `migrate()` seam for future schema bumps (Section 39 — firmware updates must not
    silently invalidate existing config).
  - `Diagnostics` — uptime, free/min-heap, reset reason; `healthCheck()` and `selfTest()`
    stubs each later phase's own checks plug into (Section 58).
  - `Watchdog` — ESP-IDF task watchdog wrapper, 10s default timeout, fed from `loop()`.
- `firmware/src/gateway_main.cpp`, `firmware/src/node_main.cpp` — near-identical generic
  boot flow (only default role/ID-prefix differ), plus `STATUS` and `FACTORY_RESET`
  serial commands (Section 40 — software factory reset).
- `configs/defaults/device_config.example.json` — example of the on-disk schema.

**Not implemented (by design, later phases):** Wi-Fi/BLE (Phase 2), any sensor/camera
code (Phase 3+), ESP-NOW (Phase 5), physical-button factory reset (no GPIO assignment
exists yet for it — deferred with the rest of Phase 3's GPIO work).

**Build status: not compiled.** No PlatformIO/Python toolchain is installed in this
environment (`pio`/`python3`/`pip` all unavailable), so this code has been written and
reviewed but **not built or flashed**. Before trusting it, install PlatformIO
(`pip install platformio` or the VS Code extension) and run:
```
cd firmware
pio run -e gateway
pio run -e node
```
and flash+serial-monitor each target to confirm boot, `STATUS`, and `FACTORY_RESET`
behave as documented. Report any compile errors back — the `esp_task_wdt_config_t` API
in `Watchdog.cpp` in particular assumes a recent arduino-esp32 core (3.x / ESP-IDF 5.x)
and may need adjusting for an older pinned platform version.

## Next Step

Compile and bench-test Phase 1 on real hardware (see Build status above) before starting
Phase 2 (BLE + Wi-Fi Provisioning).
