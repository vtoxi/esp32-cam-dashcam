# CarSentinel — Implementation Plan

Phases are implemented strictly one at a time, per the project specification (Section
64/76). Each phase stops for review before the next begins. This file tracks status only
— see the project spec for full phase definitions.

## Status

| Phase | Name | Status |
|---|---|---|
| 0 | Repository & Hardware Discovery | **In progress** (this commit) |
| 1 | Generic Device Foundation | Not started |
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

## Next Step

Before Phase 1 can begin meaningfully, resolve the open hardware questions in
`docs/HARDWARE.md` (exact ESP32-S3 board model, SSD1306 address-jumper availability,
confirmed unit counts). Phase 1 itself (Generic Device Foundation: boot, device identity,
persistent config, factory reset, diagnostics, watchdog, logging) does not strictly
require the gateway pin table to start, so it can begin in parallel with hardware
verification if desired.
