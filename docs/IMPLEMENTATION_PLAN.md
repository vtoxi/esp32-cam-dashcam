# CarSentinel — Implementation Plan

Phases are implemented strictly one at a time, per the project specification (Section
64/76). Each phase stops for review before the next begins. This file tracks status only
— see the project spec for full phase definitions.

## Status

| Phase | Name | Status |
|---|---|---|
| 0 | Repository & Hardware Discovery | Complete |
| 1 | Generic Device Foundation | Complete (pending bench verification) |
| 2 | BLE + Wi-Fi Provisioning | Complete (pending bench verification) |
| 3 | Hardware Capability Layer | Complete (pending bench verification) |
| 4 | Single Camera Node | **In progress** (this commit) |
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

## Phase 2 — BLE + Wi-Fi Provisioning

**Implemented:**
- `firmware/lib/CarSentinelCommon/NetworkConfig` — persistent Wi-Fi config on LittleFS
  (`/config/network.json`), kept in a **separate file** from `device.json` on purpose:
  factory reset / a bare "forget Wi-Fi" action can clear credentials without touching
  node identity, and device-config diagnostic dumps never risk leaking a password
  (Section 41 — no credentials in logs). Own `schemaVersion`/migrate seam, same pattern
  as `DeviceConfig`.
- `WiFiManager` — `connectBlocking()` tries saved credentials with a **bounded** retry
  budget (`maxRetries × (connectTimeoutMs + retryIntervalMs)`, defaults to 3×20s ≈ 60s
  worst case) — never blocks forever (Section 10). `loop()` does non-blocking drop
  detection/reconnect during normal operation, rate-limited to once per 30s so it can
  never stall time-critical logic added in later phases.
- `ProvisioningPortal` — temporary SoftAP (`CarSentinel-Setup-<id>`) + a minimal
  `WebServer` form (SSID/password/hostname/display name/role) at `192.168.4.1`, with a
  captive-portal-style redirect on unknown paths. Blank password field on resubmit does
  not overwrite a working saved password.
- `BLEProvisioning` — NimBLE GATT service with **write-only** SSID/password
  characteristics (never readable back over BLE — Section 9), read/write display
  name/role, and a Commit characteristic that stages values and only persists them once
  written (so a dropped connection mid-entry can't leave a half-configured device).
- Both `gateway_main.cpp` and `node_main.cpp`: on boot, try saved credentials once
  (bounded); on failure or no credentials, open **both** BLE and AP provisioning
  concurrently and wait for either to receive a valid submission, then reboot. Added
  `PROVISION` serial command (clears Wi-Fi credentials, restarts into provisioning) and
  extended `FACTORY_RESET` to also clear network credentials (Section 40).
- `configs/defaults/network_config.example.json`.

**Not implemented (by design, later phases):** actual use of the Wi-Fi connection for
anything (no NTP, no gateway sync, no email — those are Phase 5+ and later); BLE/AP
provisioning of sensor-enable flags or hardware profile selection (Phase 3, once
hardware profiles exist); static-IP field validation beyond a basic parse check.

**Build status: not compiled.** Same toolchain gap as Phase 1 (no PlatformIO/Python
available in this environment) — this code is written and self-reviewed, not built or
flashed. Specific risks to check first when a toolchain is available:
- `NimBLE-Arduino@^1.4.3` pinned in `platformio.ini`; `BLEProvisioning.cpp`'s
  characteristic-callback code targets that version's `std::string`-based
  `getValue()`/`setValue()` API. NimBLE-Arduino 2.x changed some of these signatures —
  if PlatformIO resolves a newer version despite the pin, this is the first place to fix.
- Running Wi-Fi AP + BLE advertising simultaneously on the **AI-Thinker ESP32-CAM**
  (classic ESP32, shared radio, limited heap) is untested — it may be less stable there
  than on the ESP32-S3 gateway. If bench testing shows problems, the fallback is to
  offer BLE and AP sequentially (BLE first, AP only if BLE also fails) rather than
  concurrently, on the node target specifically.
- `WebServer.h`'s captive-portal redirect (`handleNotFound` → 302 to `/`) is a minimal
  approach and won't trigger every OS's captive-portal auto-popup; manually browsing to
  `192.168.4.1` always works as the documented fallback.

## Phase 3 — Hardware Capability Layer

**Implemented:**
- `firmware/lib/CarSentinelCommon/HardwareProfiles` — compiled-in pin tables for the two
  confirmed boards (`ESP32_CAM_AI_THINKER`, `ESP32_S3_N16R8_GATEWAY`). Board-level pin
  data is explicitly allowed to require a firmware rebuild (Section 49's closing line);
  what stays runtime-configurable is which capabilities are *enabled*.
- `CapabilitiesConfig` — persistent, versioned (`/config/capabilities.json`) per-device
  enable flags + GPIO assignments, seeded from the hardware profile on first boot, then
  the sole source of truth (editing the file — no UI for this yet, that's Phase 19 — is
  how a device's sensor set is changed without reflashing).
- `I2CBusManager` — wraps `Wire`/`Wire1` for the gateway's two I2C buses (needed because
  both SSD1306 units are confirmed at address `0x3C` with no jumper — see
  `docs/wiring/SSD1306.md`); presence-probes a device by address.
- `MotionSensor` (RCWL), `TemperatureHumiditySensor` (DHT11, via Adafruit DHT library),
  `GpsUart` (raw UART open + byte-liveness check), `SdStorage` (SD_MMC 1-bit mount) —
  each Phase-3-scoped to init/detect only; full driver behavior (debounce, NMEA parsing,
  accel/gyro reads, page rendering) is Phase 4/8/9/13.
- Both `gateway_main.cpp`/`node_main.cpp`: hardware capability init now runs **before**
  Wi-Fi/provisioning, and sensor polling in `loop()` is unconditional — local sensing
  must never depend on network/provisioning state (Section 5). `STATUS` now reports
  capability/sensor state too.
- `configs/hardware/*.json` — human-readable mirrors of the compiled profile tables (not
  loaded by firmware, exist so the pin data is reviewable without reading C++).
  `configs/defaults/capabilities_config.example.json`.

**Deliberately conservative:** on the ESP32-CAM node, RCWL and DHT stay
`enabled: false, gpio: -1` by default — their GPIOs are still not bench-verified (see
`docs/HARDWARE.md`), so the capability layer exists but nothing guesses a pin. On the
gateway, GPS/IMU/display default to *enabled* with the *proposed* (untested) GPIOs from
`docs/wiring/ESP32_S3_GATEWAY.md`, since that's the best current information — every log
line touching them says "proposed, not bench-verified."

**Not implemented (by design, later phases):** actual camera capture (Phase 4), NMEA
parsing (Phase 8), IMU accel/gyro reads (Phase 9), OLED rendering (Phase 13), evidence
file layout on SD (Section 27, Phase 4/11).

**Build status: not compiled.** Same toolchain gap as Phases 1–2. New risks this phase
adds to the bench-test list:
- `adafruit/DHT sensor library@^1.4.6` + its `Adafruit Unified Sensor` dependency,
  newly added to `platformio.ini` — unverified to resolve/compile cleanly.
- `Wire1` on the ESP32-S3 gateway (`I2CBusManager` bus index 1) — guarded by
  `#if SOC_I2C_NUM > 1`, but the actual Arduino-ESP32 core's `Wire1` global/constructor
  behavior on this specific board is unverified.
- `SD_MMC.begin("/sdcard", true)` 1-bit-mode default pins are assumed to match
  AI-Thinker's fixed SD slot wiring per public reference — not yet confirmed against the
  physical unit (see `docs/wiring/MICROSD.md`).

## Phase 4 — Single Camera Node

**Implemented:**
- `CameraManager` — wraps `esp_camera` for the AI-Thinker OV2640 using the standard,
  widely-published `CAMERA_MODEL_AI_THINKER` pin set (now documented per-signal in
  `docs/wiring/ESP32_CAM.md`). PSRAM-aware: SVGA/2-buffer with PSRAM, VGA/1-buffer
  without. Snapshot capture only (`captureJpeg()`/`returnFrame()`) — no continuous video,
  per Section 14.
- `MotionEventEngine` — Section 17's debounce/confirmation-window/cooldown state machine
  on top of Phase 3's raw `MotionSensor` read: counts rising edges within a
  `confirmationWindowMs` window, fires once `minimumEvents` is reached, then holds a
  `cooldownSeconds` cooldown. Defaults match Section 17 exactly (3000ms/2/30s).
- `EvidenceManager` — local evidence storage on SD per Section 27's directory layout
  (`/security/events/<EVENT_ID>/event.json` + `image_001.jpg`) and a simplified Section
  55 event schema (no GPS/IMU — camera nodes don't have those; no `relatedNodes` — no
  ESP-NOW yet; timestamp is `uptimeMsAtEvent`, not wall-clock, since NTP is Phase 57).
  Event IDs are a persisted sequential counter on SD, not time-based.
- `node_main.cpp`: motion → confirm → capture JPEG → save event + image pipeline, running
  unconditionally in `loop()` regardless of Wi-Fi/provisioning state. New `CAPTURE`
  serial command for a manual test snapshot without needing a live motion trigger.

**Not implemented (by design, later phases):** ESP-NOW event forwarding to the gateway
(Phase 5+, `relatedNodes` stays empty and events stay purely local until then), storage
retention/cleanup (Section 27's quota fields, Phase 11), full incident lifecycle states
(Phase 11), burst capture (Section 14 mentions it; single-frame-per-event is enough to
prove the pipeline).

**Build status: not compiled.** Same toolchain gap as prior phases. New risks:
- `esp_camera_init` with the AI-Thinker pin set — extremely standard, used by nearly
  every public ESP32-CAM example, but still unverified on *this* physical unit.
- PSRAM detection (`psramFound()`) determines frame size/buffer count — if this specific
  board's PSRAM isn't enabled in the build config, camera init may fail outright rather
  than gracefully degrade; worth an explicit bench check.
- `SD_MMC` file API calls (`mkdir`, `open(..., FILE_WRITE)`, `parseInt()`) in
  `EvidenceManager` are standard but untested here.

## Next Step

Compile and bench-test Phases 1–4 together on real hardware — this is the first phase
that actually needs a physical camera + SD card to validate meaningfully, so it's a
natural point to stop and get a toolchain running before Phase 5 (ESP-NOW).
