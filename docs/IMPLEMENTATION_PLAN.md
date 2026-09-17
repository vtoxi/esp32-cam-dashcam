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
| 4 | Single Camera Node | Compiles clean (both envs); node flashed and phase 4 code running on hardware — pending full functional bench test |
| 5 | ESP-NOW | Confirmed on real hardware — gateway↔node discovery working (see real-hardware update below) |
| 6 | Dynamic Node Management | Confirmed on real hardware — zero-code device discovery verified; rename-sync bug found and fixed |
| 7 | Multi-Camera Correlation | Compiles clean — needs 2+ camera nodes to bench-test, not yet done |
| 8 | GPS | Confirmed booting/reporting correctly (`NO_FIX`, as expected with no sky view) — fix acquisition itself not yet tested |
| 9 | MPU6050 | Compiles clean — no MPU6050 wired yet on the tested gateway, not bench-tested |
| 10 | Driving / Parking Modes | Confirmed `PARKED (auto)` reporting correctly on real hardware; mode transitions not yet tested |
| 11 | Incident & Evidence Engine | Compiles clean (both envs) — pending physical bench test |
| 12 | Email Notification | Compiles clean (both envs) — pending real SMTP bench test |
| 13 | OLED Displays | Compiles clean (both envs) — pending physical OLED bench test |
| 14 | OTA | Gateway: compiles clean, real MANUAL-mode HTTP(S) update flow. Node: scoped out — classic ESP32 IRAM budget can't fit it alongside camera/WiFi/BLE (real measured link failure, see below) |
| 15 | AI Framework | Compiles clean (gateway) — pluggable heuristic threat-scoring framework; real on-device ML deliberately scoped out (see below) |
| 16 | AI Security Assistance | Compiles clean (gateway) — heuristic analyzer gates email notification on assessed severity |
| 17 | Low-Power Parked Mode | Compiles clean (both envs) — Wi-Fi modem sleep in PARKED mode only; full deep-sleep deliberately scoped out (see below) |
| 18 | Vehicle Integration | Compiles clean (both envs) — capability-gated ignition-sense input drives DRIVING/PARKED when wired; OBD-II/CAN blocked on hardware, not yet started |
| 19 | Dashboard | Compiles clean (both envs) — gateway-hosted single-page dashboard + JSON API + live camera stream proxy; pending physical bench test |
| 20 | Vehicle Installation | Planning checklist documented (`docs/wiring/VEHICLE_INSTALLATION.md`) — no firmware component, no physical install has happened |
| 21 | Remote Backend, API & Hybrid Connectivity | 21.1–21.10 complete: architecture audit, gateway-side backend abstraction, persistent retry queue, device registration/auth, a real ASP.NET Core reference backend (`backend/`), SSE real-time stream, end-to-end evidence upload, a Backend→Gateway command flow (one real command, SECURITY_MODE, wired end to end), outbound webhooks (admin-managed subscriptions, HMAC-signed delivery, retry, logging), and a standalone API reference doc (`docs/API.md`). Gateway firmware itself still not bench-tested against a live server. 21.11 not started |

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

**Build status: compiles clean, both environments, with a real PlatformIO toolchain.**
The user installed PlatformIO locally and ran `pio run`, which caught three real bugs
that had been sitting undetected through Phases 1–4 (self-review and reading the code
is not a substitute for actually compiling it — this is the proof):

1. **`DeviceRole::DISPLAY` collided with `Arduino.h`'s `#define DISPLAY 0x1`** (a
   text-alignment constant). Renamed the enumerator to `DISPLAY_NODE` in
   `DeviceConfig.h`/`.cpp` — the persisted/wire string value stays `"DISPLAY"`, only the
   C++ identifier changed, so no schema migration is needed.
2. **`BLEProvisioning`'s NimBLE write-callback couldn't set the private static
   `committed` flag** — the callback class lives in the `.cpp` as a separate type, not a
   nested friend. Added a public `BLEProvisioning::markCommitted()` setter instead of
   granting broader access.
3. **`Watchdog.cpp` used the `esp_task_wdt_config_t` struct API (ESP-IDF 5.x)**, but the
   pinned `platform-espressif32`/`arduino-esp32` core (3.20017.241212) actually ships the
   older ESP-IDF 4.x-style `esp_task_wdt_init(timeout_s, panic)` signature — exactly the
   fallback flagged as a risk back in Phase 1. Switched to that signature.

Also corrected `platformio.ini`'s `gateway` environment: PlatformIO's default
`esp32-s3-devkitc-1` board JSON describes the **N8 variant (8MB flash, no PSRAM)**, not
the confirmed N16R8 (16MB flash, 8MB octal PSRAM) hardware. Added explicit overrides
(`board_upload.flash_size`, `default_16MB.csv` partitions, `-D BOARD_HAS_PSRAM`) matching
the override set PlatformIO's own N16R8 board definitions use, so flash size and PSRAM
are correctly recognized (build now reports a 6.5MB app partition, not the ~1.3MB the
wrong 8MB/no-PSRAM default would have left after OTA dual-partitioning).

Final result: `gateway` RAM 15.9%/Flash 15.9% (16MB), `node` RAM 18.5%/Flash 41.3% (4MB).

**Still not done:** flashing to real hardware and functional bench testing (camera
capture quality, motion trigger accuracy, SD write reliability, BLE/AP provisioning
end-to-end, Wi-Fi reconnect behavior). Compiling clean rules out toolchain/API mismatches
but proves nothing about runtime correctness on the actual boards.

**Real-hardware update:** the node was flashed via a CH340 USB-serial programmer board
(RST/IO0 buttons, no DTR/RTS auto-reset). Uploads initially failed with a consistent
"Invalid head of packet (0x08)" — traced (via esptool's verbose trace, not guesswork) to
what looked like a TX/RX loopback signature, but turned out to be resolved by the user's
own environment troubleshooting (closing whatever else held the port / a clean
unplug-replug) rather than any code or wiring change. Also lowered `node`'s
`upload_speed` to 115200 in `platformio.ini` as a standing reliability margin for this
adapter. The naming fix in the section below (role-prefixed provisioning names) was
applied and reflashed successfully after that.

**Provisioning naming fix (applied after initial Phase 4 flash):** the AP/BLE setup name
was `CarSentinel-Setup-<6-hex-chars>`, which doesn't indicate device type — a gateway and
a node could produce visually similar names during a Wi-Fi/BLE scan. Changed to use the
full role-prefixed nodeId (`CarSentinel-NODE-A1B2C3` / `CarSentinel-GATEWAY-A1B2C3`) in
both `enterProvisioningMode()` functions.

## Phase 5 — ESP-NOW

**Implemented:**
- `Transport` (Section 12) — abstract interface (`begin`, `sendTo`, `broadcastMessage`,
  `registerPeer`, `removePeer`, `setReceiveCallback`); `EspNowManager` depends only on
  this interface, never on `esp_now_*()` directly, so a future transport can be swapped
  in without touching protocol/application code.
- `EspNowTransport` — the (only, today) `Transport` implementation. Callback signatures
  were taken directly from this toolchain's installed `esp_now.h`
  (`framework-arduinoespressif32 3.20017.241212`), not assumed from newer ESP-IDF 5.x
  docs — the older `void(*)(const uint8_t *mac_addr, const uint8_t *data, int data_len)`
  form, consistent with the Watchdog API lesson from Phase 4's build-fix pass. Registers
  the broadcast address as a peer at `begin()` (required before `esp_now_send()` can
  target it).
- `EspNowProtocol` (Section 11) — the full message-type vocabulary (`HELLO` through
  `ERROR_MSG` — deliberately not named bare `ERROR`, having already been bitten once by
  `Arduino.h`'s `#define DISPLAY`), a manually byte-serialized wire format (not a raw
  struct cast, so ESP32 vs ESP32-S3 struct-padding differences can never cause a silent
  mismatch), protocol versioning (a version byte, `decode()` rejects mismatches), and
  HMAC-SHA256 (truncated to 8 bytes) message authentication via `EspNowSecurity`.
- `EspNowSecurity` — a 32-byte pre-shared key persisted at `/config/espnow_psk.bin`,
  defaulting to a **documented-insecure** compiled-in placeholder on first boot. The key
  itself is never transmitted — only its HMAC output travels over the air (Section 41).
  Known limitation, stated loudly in logs and code comments: every device ships with the
  *same* default key until someone manually replaces that file on every device — there is
  no key-distribution mechanism yet.
- `PeerRegistry` — in-memory (not persisted — resets on reboot) tracking of peers heard
  from, used for (a) monotonic-sequence duplicate/replay rejection and (b) discovering
  the gateway's MAC by role from its heartbeat payload. Explicitly not Section 6's
  persistent dynamic device registry (enable/disable/rename) — that's Phase 6, built on
  top of this.
- `EspNowManager` — sends a `HELLO`-equivalent `HEARTBEAT` broadcast immediately at boot
  and every 20s carrying `{role, uptimeMs, freeHeap}`; `sendMessage()` auto-assigns
  sequence numbers and, for unicast sends of ack-expecting types, tracks the send in a
  small (4-slot) in-memory pending table with bounded retries (3 attempts, linear
  backoff) — giving up and logging after that (no persistent offline queue yet, that's
  Section 28/Phase 28). Received messages are deduplicated via `PeerRegistry`, auto-ACKed
  if their type expects one, and dispatched to an app-registered handler.
- `node_main.cpp`: a confirmed `MOTION_DETECTED` event now forwards to the gateway (by
  MAC if already discovered via a heartbeat, otherwise broadcast as a best-effort
  fallback) — `CAPTURE`'s manual test events stay local, matching that command's own
  "local test" intent. ESP-NOW starts after Wi-Fi/provisioning settles (deferred while in
  AP-mode provisioning — a known scope boundary, not attempted this phase) and runs
  unconditionally in `loop()` once started.
- `gateway_main.cpp`: registers a message handler that logs every received security event
  clearly; `STATUS` now lists discovered peers.
- Both `STATUS` commands report ESP-NOW active state, peer count, and (node only)
  whether the gateway has been discovered yet.

**Defined but not yet handled this phase** (vocabulary exists, behavior is later
phases'): `PAIR_REQUEST`/`PAIR_RESPONSE` (Phase 6), `CONFIG_REQUEST`/`CONFIG_UPDATE`
(Phase 6/19), `INCIDENT_START`/`UPDATE`/`END` (Phase 11), `TIME_SYNC` (Phase 57),
`OTA_COMMAND` (Phase 14), `GPS_UPDATE`/`IMU_UPDATE`/`TEMPERATURE_UPDATE`/
`HUMIDITY_UPDATE` (Phase 8/9 — the gateway has no sensors bench-verified yet to source
these from). Received instances of these types are still ACKed (if applicable) and
logged by the generic RX path, just not acted on.

**Build status: compiles clean, both environments** (verified directly — RAM/Flash usage
barely moved: node 19.1%/42.2%, gateway similar). **Not yet bench-tested with two
physical devices actually talking to each other.**

**Known limitations / risks to verify on hardware:**
- ESP-NOW requires the STA/AP radio to be on a settled channel; once the gateway
  connects to a real Wi-Fi router, ESP-NOW peers must be on that same channel. This
  phase does not implement channel synchronization — if the gateway's router-assigned
  channel differs from a node's, they may fail to hear each other. Untested; likely the
  first real-world issue to surface.
  ESP-NOW during provisioning (AP mode) is out of scope this phase (see above) — a
  node stuck in provisioning never forwards events, by design, not by oversight.
- Two-device delivery (send → receive → auto-ACK → pending-table resolution) has not
  been observed end-to-end; the retry/backoff logic is logic-reviewed, not
  bench-verified.
- The default ESP-NOW PSK is identical across every device until manually changed —
  acceptable for bench testing, not for any real deployment.

**Also this session (not a phase, quality-of-life):** both firmware targets now log an
explicit `NETWORK: connected, IP=...` line at the end of `setup()` (or the provisioning
AP's address if unconfigured yet), and `WiFiManager::loop()` logs `Reconnected, IP=...`
on recovery from a dropped connection — previously the IP was only visible via the
`STATUS` command or by scrolling back through the boot log.

## Phase 6 — Dynamic Node Management

**Implemented:**
- `DeviceRegistry` (gateway-only, Section 6) — persistent (`/config/device_registry.json`)
  record of every node the gateway has heard from, distinct from Phase 5's
  `PeerRegistry` (in-memory-only radio dedup state): this is the durable "devices I
  manage" list. Auto-populated — a node's first HELLO/HEARTBEAT creates its entry
  (`enabled=true`, `displayName` defaulting to `nodeId`) with **no code change and no
  manual registration step**, which is the literal Section 70 "add a camera" workflow
  minus the BLE/AP provisioning step (already done in Phase 2) and the physical
  wiring/mounting.
- `MacAddress` — tiny shared `macToString`/`macFromString` helpers (registry persistence
  and remote-command dispatch both need to convert between the 6-byte form ESP-NOW uses
  and the string form JSON/serial commands use).
- Gateway `EspNowManager` gained a second callback, `setOnPeerHeartbeatHandler` —
  separate from Phase 5's `setOnMessageHandler`, because that one deliberately skips
  HELLO/HEARTBEAT (they're not "security events"), but the registry needs exactly those
  to learn about new devices and their health (`freeHeap`, `uptimeMs` from the heartbeat
  payload).
- New gateway serial commands implementing Section 6's action list: `DEVICES` (list,
  with live health from the last heartbeat), `RENAME <id> <name>`, `ENABLE <id>`,
  `DISABLE <id>`, `REMOVE <id>`, `SETROLE <id> <role>`, `RESTART <id>`, `RESET <id>`.
  `RENAME`/`SETROLE`/`RESTART`/`RESET` are forwarded to the node itself over ESP-NOW as a
  `CONFIG_UPDATE` message (`{"cmd":..., "value":...}`) — a generic administrative-command
  envelope, since Section 11's message vocabulary has no dedicated message type for
  "restart" or "factory reset" specifically.
- Node-side `CONFIG_UPDATE` handler (`node_main.cpp`): applies `RENAME` (updates its own
  `displayName`), `SETROLE` (updates `role`), `RESTART` (`ESP.restart()`), and
  `FACTORY_RESET` (same as the local `FACTORY_RESET` serial command, just triggered
  remotely). The message is already auto-ACKed by `EspNowManager` before this handler
  runs, so the gateway knows the command was delivered (though not necessarily that it
  was *applied* successfully — no result-reporting message type exists yet for that).
- **Disable is application-level, not radio-level**, and this is a real, documented
  limitation, not an oversight: `DISABLE` sets a registry flag the gateway checks before
  treating a node's events as significant (`onEspNowMessage` logs "ignoring event from
  disabled device" and returns). It does **not** stop the node's radio transmissions or
  remove it from ESP-NOW's peer list — broadcast heartbeats can't be selectively blocked
  at the radio layer without abandoning the discovery mechanism. A disabled node keeps
  running and keeps being heard; the gateway just stops caring about what it says.

**Not implemented (by design, later phases):** "change hardware profile" and "update
firmware" from Section 6's action list — the former doesn't make sense to change
remotely (it describes the physical board, not a preference), the latter is Phase 14
(OTA) outright. No result/acknowledgement message type exists for "did the remote
command actually get applied" (vs. just "was the packet delivered," which ESP-NOW's ACK
already confirms) — Phase 11's incident engine or a future CONFIG_RESULT message type
would be the natural place to add that.

**Build status: compiles clean, both environments** (verified directly — node
19.1%/42.3%, essentially unchanged from Phase 5, since this phase mostly reused existing
mechanisms). **Not yet bench-tested.**

**Known limitations / risks to verify on hardware:**
- Every `sendNodeCommand()` call requires the node to already be in the registry (i.e.
  have sent at least one heartbeat) — there is no way to command a node the gateway
  hasn't heard from yet, which is the correct behavior but worth confirming produces a
  clear "unknown nodeId" message rather than a confusing failure.
- `RENAME` updates the gateway's registry immediately but only *requests* the node update
  its own `displayName` — if that ESP-NOW send fails (no retries beyond
  `EspNowManager`'s existing bounded retry), the two can drift out of sync until the next
  successful rename attempt. Not persisted/retried beyond what Phase 5 already does.
- `REMOVE` only forgets the device gateway-side; because discovery is automatic, a
  removed device reappears on its next heartbeat. This is almost certainly the right
  behavior (matches Section 71's "disable/remove is a gateway action, the node keeps
  operating independently") but is worth confirming matches actual expectations once
  used for real, rather than assumed correct from re-reading the spec.

## Next Step

Flash both images and bench-test Phases 5–6 together with two physical devices in
range: confirm auto-discovery populates `DEVICES` on the gateway, exercise every new
serial command (`RENAME`/`ENABLE`/`DISABLE`/`REMOVE`/`SETROLE`/`RESTART`/`RESET`) against
a real node and confirm the expected effect on each side, and confirm a `DISABLE`d
device's events are ignored while its heartbeats keep the registry entry's `lastSeenMs`
fresh.

## Phase 7 — Multi-Camera Correlation

**Implemented:**
- Node: when its own RCWL trigger produces a confirmed `MOTION_DETECTED` (after
  forwarding to the gateway as before), it now also broadcasts a `CAPTURE_REQUEST`
  (`{"triggerNodeId":..., "triggerEventId":...}`) — Section 18's "FRONT → SECURITY_EVENT
  → ESP-NOW broadcast → REAR, LEFT, RIGHT, INTERIOR."
- Node: a new `captureRelatedEvidence()` handles a received `CAPTURE_REQUEST` from
  another camera — captures its own JPEG (if it has a camera and one is initialized),
  saves it locally as a `RELATED_CAPTURE` evidence event (Section 27 layout, same as any
  other local event — this node's own SD gets its own record of participating), and
  reports back to the gateway via `CAPTURE_RESULT`
  (`{"triggerNodeId", "triggerEventId", "localEventId", "hasImage"}`). Deliberately
  lighter-weight than the RCWL-trigger path: no `MotionEventEngine` involvement (this
  isn't *this* node's own motion decision), no re-broadcast (no risk of a request storm).
- Gateway: `IncidentCorrelator` (new, gateway-only, in-memory) — `MOTION_DETECTED` opens
  an incident (`INCIDENT-000123`-style ID, sequential, not persisted), `CAPTURE_RESULT`
  messages matching that trigger get appended to its `relatedNodeIds` list, and after an
  8-second correlation window with no further activity the incident is closed and logged
  as one consolidated summary line (`trigger=... related=[...]`) — the Section 18 "one
  incident" behavior, without Phase 11's full lifecycle/persistence machinery.
- Disabled-device gating (Phase 6) still applies before any of this — a `DISABLE`d
  node's `MOTION_DETECTED`/`CAPTURE_RESULT` never reaches the correlator.

**Deliberately NOT Phase 11:** no persisted incident records, no lifecycle states
(DETECTED→CONFIRMING→ACTIVE→...→CLOSED), no GPS/IMU/DHT association beyond what
`MOTION_DETECTED`'s payload already carried since Phase 4 (temperature/humidity from the
*triggering* node only — related nodes' own DHT readings aren't currently attached, a
gap Phase 11 could close), no storage retention/cleanup. `IncidentCorrelator`'s job is
narrowly "prove multiple cameras' responses get grouped," which it does.

**Known limitations / risks to verify on hardware:**
- A `CAPTURE_RESULT` arriving after its incident's 8-second correlation window already
  closed is logged as "no open incident found" and dropped — this is a real
  Phase-7-scope limitation (a slow node, or one whose camera capture takes unusually
  long, could miss the window), not a bug to silently paper over. Worth observing actual
  timing with real hardware to see if 8 seconds is generous enough.
- A node responds to a peer's `CAPTURE_REQUEST` immediately (no debounce needed — it's a
  direct instruction, not a re-detection), while the *triggering* node's own event only
  fires after Section 17's full RCWL debounce/confirmation window. So a related capture
  can show up in the gateway's log before the trigger's own follow-up traffic settles —
  intentional, but worth knowing when reading logs out of order.
- Only `MOTION_DETECTED` triggers correlation right now; other event types in Section
  11's vocabulary (`INTRUSION_DETECTED`, `IMPACT_DETECTED`) don't yet, since nothing
  produces them yet (no IMU/GPS bench-verified — Phase 8/9).

## Phase 8 — GPS

**Implemented:**
- `GpsManager` (gateway-only) — full replacement for Phase 3's `GpsUart` (which was
  explicitly scoped to a raw byte-liveness check only, full parsing deferred here).
  Built on `TinyGPS++`: feeds every received UART byte into the parser and, whenever a
  complete NMEA sentence updates the fix, refreshes latitude/longitude/altitude/speed/
  course/satellite-count from whichever fields that sentence carried (GPRMC, GPGGA, etc.
  — `TinyGPS++` classifies fields, not whole sentences, so this doesn't need to
  special-case sentence types itself).
- `GpsFixStatus`: `NO_FIX` / `FIX` — Section 21's hard requirement that GPS absence is a
  normal, continuing state, never a blocking error. A fix is actively downgraded back to
  `NO_FIX` if nothing refreshes it for 10 seconds (antenna disconnected, moved indoors,
  satellites lost) rather than silently reporting an increasingly stale position as
  current.
- `GpsManager::toJson()` — compact JSON (`{"lat","lon","altM","speedKmph","courseDeg",
  "sats"}` when fixed, `{"status":"NO_FIX"}` otherwise) for logging and future payload
  embedding.
- `gateway_main.cpp`: `GpsManager::loop()` now pumps every loop iteration (not
  interval-gated like the old byte-count check — NMEA data arrives continuously and a
  full UART buffer would drop sentences), `STATUS` reports the current fix, and — a
  light Section 21 "incident location" integration — when `IncidentCorrelator` opens an
  incident from a `MOTION_DETECTED`, the gateway's current GPS fix is logged alongside
  it. Not yet persisted into the incident record itself (Phase 11's job); this proves
  the data is available at the right moment, not that it's stored yet.
- New dependency: `mikalhart/TinyGPSPlus@^1.0.3`, added to the shared `[env]` lib_deps —
  confirmed (the hard way, back in earlier phases) that everything in
  `lib/CarSentinelCommon/` compiles into *both* environments regardless of which
  target's `main.cpp` actually uses it, so GPS-only dependencies still need to resolve
  cleanly for the node build too. It does.

**Not implemented (by design, later phases):** trip recording, geofencing, and
"movement detection from GPS speed" (Sections 44/45) — those are dedicated later
features, not part of "implement the GPS subsystem itself." GPS data is not yet attached
to node-originated event payloads (`MOTION_DETECTED`'s JSON) since only the gateway has
GPS — Section 24's full GPS+IMU+camera+RCWL+DHT correlation record is Phase 11 territory.

**Build status: compiles clean, both environments** (verified directly — node
19.1%/42.4%, gateway similar; TinyGPS++ resolved without issue). **Not yet bench-tested
with a physical GPS module** — no code here has seen a real NMEA stream.

**Known limitations / risks to verify on hardware:**
- The 10-second stale-fix timeout is a guess, not tuned against real NEO-6M behavior
  (cold-start time to first fix, typical re-acquisition time after a brief signal loss).
- `GpsManager` reuses the same gateway UART1 pins (`rx=17, tx=18`) proposed — not
  bench-verified — back in Phase 3/5; this phase doesn't change that risk, just adds
  real parsing on top of it.
- Indoor/bench testing may never produce a fix at all (GPS needs sky visibility) — a
  `{"status":"NO_FIX"}` result during bench testing doesn't necessarily mean the code is
  broken; testing outdoors or near a window is the documented fallback (per
  `docs/wiring/NEO_6M.md`).

## Next Step

Flash all firmware and bench-test Phases 5–8 together: two camera nodes triggering
correlated motion events (Phase 5/6/7), and the gateway's GPS module outdoors or near a
window to confirm `GpsManager` actually acquires a fix and `STATUS`/incident-location
logs show real coordinates, not just `{"status":"NO_FIX"}`.

## Phase 9 — MPU6050

**Implemented:**
- `ImuManager` — direct MPU6050 register access via new `I2CBusManager::writeRegister`/
  `readRegisters` primitives, **no external MPU6050 library**. Deliberate choice: the
  register map (wake via `PWR_MGMT_1`, burst-read 14 bytes from `ACCEL_XOUT_H` covering
  accel+temp+gyro) is small and stable, and every earlier phase that pulled in a new
  third-party library needed at least one guessed-API compile-fix pass — this sidesteps
  that risk entirely for a driver this simple. Confirmed the exact `Wire.h`
  `endTransmission(bool)`/`requestFrom(int,int)` signatures against this toolchain's
  installed header before writing `I2CBusManager`'s new methods, same discipline as
  Phase 5's ESP-NOW work.
- Computes acceleration magnitude and angular velocity magnitude (vector magnitude of
  the three axes, in g and deg/s respectively) — Section 22's explicit deliverable.
  Default scale factors assume the MPU6050's power-on-default full-scale ranges
  (±2g, ±250°/s); the driver doesn't reconfigure them.
- `ImuThresholdsData` — persisted (`/config/imu_thresholds.json`), not hardcoded, per
  Section 23's explicit instruction that thresholds need real-world tuning. New
  `IMUTHRESHOLDS <accelG> <gyroDps>` gateway serial command to tune them without
  hand-editing JSON or reflashing.
- `ImuManager::checkImpact()` — Section 23's hard requirement respected exactly: this
  reports **`IMPACT_EVENT` with raw measurements attached, never a crash determination**.
  A 5-second (configurable) cooldown after a threshold crossing prevents one sustained
  event from spamming incidents.
- `gateway_main.cpp`: polls the IMU every 100ms (frequent enough to catch a sharp,
  short-duration impact) once `ImuManager` confirms the device is actually present
  (skips init entirely if the Phase 3 presence probe found nothing — never touches
  absent hardware). A threshold crossing opens an `IncidentCorrelator` incident the same
  way a node's `MOTION_DETECTED` does, but locally — the gateway is both sensor source
  and incident-opener here, no ESP-NOW round trip needed. `STATUS` reports the live
  accel/gyro magnitudes and current thresholds.

**Not implemented (by design, later phases):** sending IMU readings to nodes or
persisting them into an actual incident *record* (Phase 11 owns the real incident
schema — this phase only proves detection + threshold logic + logging). Driving-mode
behaviors (hard braking, sharp turning as distinct classified events, not just a raw
magnitude threshold) are Section 22's "Driving" use-case bullet, not this phase's
"implement the IMU subsystem" scope.

**Build status: compiles clean, both environments** (verified directly — first attempt,
no fixes needed, node 19.1%/42.4%). **Not yet bench-tested with a physical MPU6050** —
register-level I2C code has not been run against real silicon.

**Known limitations / risks to verify on hardware:**
- Scale factor constants (16384 LSB/g, 131 LSB/°/s) assume default full-scale range
  registers — if the specific MPU6050/HW-123 unit doesn't power up at those defaults
  (unlikely per datasheet, but unverified on this physical unit), readings would be
  scaled wrong without any error being raised.
- Threshold defaults (2.5g, 200°/s, 5s cooldown) are placeholders per Section 23's own
  instruction — expect to need real tuning once mounted in a vehicle.
- `checkImpact()`'s "at rest" baseline is the raw magnitude (~1g from gravity alone,
  not gravity-subtracted) — this is a coarse approximation appropriate for a first pass,
  not a calibrated inertial measurement.

## Next Step

Flash the gateway and bench-test Phases 8–9 together: confirm `STATUS` shows a plausible
resting accel magnitude (~1.0g) and near-zero gyro, then physically tap/shake the board
to confirm an `IMPACT_EVENT` fires, opens an incident, and respects its cooldown.
Combined with GPS, confirm the incident's location line shows real coordinates outdoors.

## Out-of-sequence addition — minimal read-only status web page

Not a numbered phase; requested directly (user: "does the gateway and node contain some
UI when user hits their IP?"). Before this, hitting a device's IP over normal Wi-Fi got
nothing — only the temporary provisioning AP at `192.168.4.1` served a web page, and it
stops once Wi-Fi is configured. The real multi-page dashboard with config forms and
device-management actions is still Phase 19 as originally planned; this is a much
smaller, view-only addition on top of what already existed.

**Implemented:**
- `StatusPage` (shared, `lib/CarSentinelCommon`) — wraps a `WebServer` on port 80,
  serves a single auto-refreshing (5s) HTML page at `/` built from a caller-provided
  content function. Separate `WebServer` instance from `ProvisioningPortal`'s — safe
  because a device is either in provisioning mode (AP) or normally connected (STA),
  never both, so the two never actually run concurrently despite both binding port 80.
- Both `node_main.cpp` and `gateway_main.cpp` now build a status HTML fragment
  mirroring their existing `STATUS` serial command's fields exactly (identity, Wi-Fi,
  capabilities/sensor readings, ESP-NOW state) — kept as one source of truth per field
  rather than letting the web version and serial version drift apart. Gateway's page
  also lists the live device registry (Section 6), the same data `DEVICES` prints.
- Starts automatically once Wi-Fi actually connects (at boot, or later if
  `WiFiManager` reconnects after starting in provisioning/disconnected).

**Deliberately not included:** any configuration form, any action button, any
authentication — this is read-only and unauthenticated, same trust model as the serial
console (physical/network access to the device already implies that level of trust at
this stage of the project; Section 41's broader security posture hasn't reached this
surface yet). No page beyond `/` — no navigation, no per-sensor detail pages.

**Build status: compiles clean, both environments, first attempt** (verified directly —
node RAM 19.2%/Flash 42.5%). **Not yet bench-tested** — same as everything else since
Phase 5, this has never been loaded onto a physical, Wi-Fi-connected board.

## Real-hardware findings and fixes (node, live serial log)

The node was flashed and connected to real Wi-Fi ("CodeRunner"). Two genuine bugs
surfaced from the actual boot log, not guessed:

1. **Task watchdog panic ~21s into boot, during `WiFiManager: Connecting to "CodeRunner"
   (attempt 1/3)`.** Root cause: `WiFiManager::connectBlocking()` runs synchronously
   inside `setup()` for up to `connectTimeoutMs` (15s default) per attempt, times
   `maxRetries`, using bare `delay()` calls that never fed the watchdog `Watchdog::begin(10)`
   had already armed with a 10s timeout. Any attempt slower than 10s guaranteed a panic
   and reboot. **Fixed**: the connect-wait loop and the inter-attempt retry delay now
   call `Watchdog::feed()` every 250ms.
2. **Camera failed to reinit after that watchdog reboot** (`SCCB_Write Failed... Camera
   probe failed`), even though it had initialized fine moments earlier. This is a known
   ESP32-CAM quirk — a soft/watchdog reset doesn't fully power-cycle the OV2640, which
   can be left in a bad SCCB (I2C) state. **Fixed**: `CameraManager::begin()` now
   toggles `PWDN` (active-high power-down) before every init attempt, and retries once
   with a longer toggle if the first attempt still fails.
3. **SD_MMC mount failed (`0x107`)** — confirmed **not a bug**: no card was inserted
   (intentional). The existing graceful-degradation path already handled it correctly —
   confirmed directly from the same log: `SD unavailable — continuing without local
   evidence storage` was followed by normal continuation into RCWL/DHT checks and
   Wi-Fi connect, no crash or halt. `EvidenceManager::isAvailable()` gates every SD
   write, so camera capture and ESP-NOW motion forwarding both continue working with
   `hasImage:true` in the payload even with nothing persisted. No code change needed;
   confirmed working as designed, not just assumed.

Both fixes rebuilt clean on both environments. Not yet reflashed/reverified on hardware
as of this writing — that's the immediate next step regardless of which phase follows.

## Phase 10 — Driving / Parking Modes

**Implemented:**
- `SecurityModeConfig` (shared, persisted at `/config/security_mode.json`) — the four
  Section 16 modes (`DISARMED`, `DRIVING`, `PARKED`, `SERVICE`), plus a `manualOverride`
  flag distinguishing an explicit `MODE` command from the gateway's own auto-detection.
  `securityModeAllowsMotionAlerts()` centralizes the one rule every caller needs: only
  `PARKED` runs full motion alerting.
- Gateway-side Section 44 auto-detection: every 2s (while not manually overridden),
  checks GPS speed (`>5 km/h`) and IMU movement (accel deviation `>0.15g` from the ~1g
  at-rest reading, or gyro `>15°/s`) — either signals "moving." Requires 5 consecutive
  matching checks (~10s) before actually switching modes, to avoid flapping on a single
  noisy reading. Switches only ever land on `DRIVING` or `PARKED` automatically —
  `DISARMED`/`SERVICE` are always explicit, never auto-entered.
- On any mode change, the gateway broadcasts it to every **enabled** registry device via
  the same `CONFIG_UPDATE` envelope Phase 6 already uses for remote commands
  (`{"cmd":"SET_MODE","value":"DRIVING"}`) — no new message type needed.
- New gateway serial commands: `MODE` (show current), `MODE <mode>` (manual override),
  `AUTOMODE` (clears the override, resumes Section 44 detection on the next sustained
  reading rather than forcing an immediate switch).
- IMU impact detection (Phase 9) is now gated by mode: skipped entirely while
  `DISARMED` (Section 16: "security alerts disabled" — impact detection counts as one),
  active in every other mode, including `DRIVING` where it matters most (Section 22's
  hard-braking/impact use case).
- Node: stores whatever mode the gateway last broadcast (persisted, survives reboot —
  though a node that reboots before hearing a fresh broadcast starts from its last
  known mode, not necessarily what's currently true; it'll catch up on the gateway's
  next mode change or periodic re-broadcast, though there is no periodic re-broadcast
  today, only broadcast-on-change — a node that misses the one `CONFIG_UPDATE` for a
  mode change, e.g. it was unreachable at that moment, has no way to learn the current
  mode until the next change. Known gap, not solved this phase). A confirmed RCWL
  trigger is still always fed through `MotionEventEngine` (debounce/cooldown state stays
  consistent regardless of mode) — only the *alerting* (capture + ESP-NOW forward) is
  gated by `securityModeAllowsMotionAlerts()`.
- `STATUS` and the status page (both node and gateway) now show the current mode.

**Not implemented (by design):** any actual behavior change for `DRIVING` beyond
suppressing motion alerts — Section 15's "Dashcam Mode" (periodic snapshots, rolling
storage, GPS/IMU-tagged event clips) isn't a numbered phase in the master plan at all
and is out of scope here; Phase 10's own text only asks for the four modes to exist and
gate *existing* behavior (motion alerts, impact detection), which this does. No
ignition-signal integration (Section 44 explicitly defers that: "do not connect
directly to vehicle electronics without appropriate electrical isolation").

**Build status: compiles clean, both environments, first attempt** (verified directly —
node RAM 19.2%/Flash 42.7%). **Not yet bench-tested** — mode auto-detection in
particular needs an actual drive (or a convincing simulation: moving the GPS module,
shaking the IMU) to verify the hysteresis behaves sensibly rather than flapping.

**Known limitations / risks to verify on hardware:**
- Movement thresholds (5 km/h, 0.15g, 15°/s) and the 10s hysteresis window are
  first-pass guesses, not tuned against a real drive — expect adjustment once tested.
- The "node missed the mode-change broadcast" gap above — a real risk if ESP-NOW
  delivery to a given node is spotty at the moment a mode change happens.
- `applyModeChange()` broadcasts to every *enabled* device in the registry regardless
  of whether it's actually reachable right now; the underlying `sendMessage()` still
  gets bounded-retried per Phase 5, but there's no confirmation loop specifically for
  mode delivery.

## Next Step

Reflash the node with the watchdog/camera fixes above and confirm a clean boot with no
crash loop. Then flash the gateway and bench-test Phase 10: manually cycle through all
four modes via `MODE`, confirm a node suppresses/allows motion alerts correctly per
mode, and (outdoors, with GPS fix) confirm `AUTOMODE` correctly detects a real DRIVING
transition.

## Phase 11 — Incident & Evidence Engine

**Implemented:**
- `IncidentCorrelator` (gateway-only) grew from Phase 7's pure in-memory correlator
  into a real incident engine: full Section 19 lifecycle (`DETECTED → ACTIVE →
  EVIDENCE_COLLECTION → NOTIFICATION → CLOSED` — `CONFIRMING` exists in the enum for
  schema completeness but isn't independently timed; by the time the gateway hears
  about any trigger, the originating node/sensor has already finished its own
  confirmation, so there's nothing left to wait for on the gateway side), GPS/IMU/DHT
  association (Section 24), evidence references, and persistence.
- **Where records live**: the gateway has no SD card (`docs/HARDWARE.md` — its hardware
  profile has `sd=false`), so incident *records* (metadata, associations, evidence
  references) persist to the gateway's own internal flash (LittleFS) under
  `/incidents/<INCIDENT-NNNNNN>.json`. Actual JPEG evidence stays exactly where it
  always has — on each contributing node's own SD card (`EvidenceManager`, Phase 4) —
  referenced here by `(nodeId, localEventId)`, never copied onto the gateway. A
  persisted counter (`/incidents/.next_incident_number`, same pattern as
  `EvidenceManager`'s) keeps IDs unique across reboots.
- `IncidentTriggerInfo` — deliberately keeps `IncidentCorrelator` sensor-agnostic (no
  dependency on `GpsManager`/`ImuManager`). `gatherAmbientInfo()` in `gateway_main.cpp`
  is the one place that actually reads GPS/IMU state, at the moment an incident opens.
- Both incident-opening paths now build real association data instead of just logging
  it alongside: `MOTION_DETECTED` (from a node) captures GPS position + speed, the
  gateway's own current IMU reading as ambient context, and the DHT reading already
  present in the node's payload (temperature/humidity — Phase 4's environment field,
  now actually stored, not just logged). `IMPACT_EVENT` (Phase 9's local IMU trigger)
  captures GPS + the actual triggering IMU reading (not just an ambient snapshot).
- **New for this phase**: an IMU impact now also broadcasts `CAPTURE_REQUEST` (Section
  18) so nearby cameras contribute footage of the impact, the same mechanism Phase 7
  built for motion triggers — a vehicle impact is exactly the case multi-camera
  evidence matters most for, and this was a one-line gap, not a new subsystem.
- Storage retention (Section 27, applied to the gateway's own flash — there's no SD to
  apply it to here): once persisted incidents exceed `MAX_STORED_INCIDENTS` (100), the
  oldest is deleted on the next incident close.
- New gateway serial command `INCIDENTS` lists every persisted incident file (parity
  with `DEVICES`).

**Not implemented (by design, later phases):** any actual notification action — the
`NOTIFICATION` state is reached and logged ("would trigger email/alert pipeline here")
but nothing sends anything; that's Phase 12 (Email) consuming this state, not this
phase producing it. No dashboard/UI presentation of incident records beyond the raw
`INCIDENTS` file listing (Phase 19). No cross-node time synchronization — incident
`createdAtMs` is gateway uptime, not wall-clock (NTP is Phase 57, not yet reached).

**Build status: compiles clean, both environments, first attempt** (verified directly —
node RAM 19.2%/Flash 42.7%, unchanged from Phase 10 since this phase's changes are
entirely gateway-side). **Not yet bench-tested** — nothing about persistence, retention,
or the richer association data has touched a real filesystem yet.

**Known limitations / risks to verify on hardware:**
- `LittleFS` directory-listing behavior (`openNextFile()`) for `enforceRetention()` and
  the new `INCIDENTS` command is standard Arduino-ESP32 API but hasn't been exercised
  with real files on this project's actual flash yet.
- A `CAPTURE_RESULT` that arrives after its incident already closed (Phase 7's existing
  limitation) now also means that evidence reference is lost from the *persisted*
  record too, not just the in-memory one — slightly higher stakes than before, same
  underlying gap.
- `gatherAmbientInfo()`'s IMU snapshot for a `MOTION_DETECTED`-triggered incident is
  whatever the gateway's IMU reads *at that instant*, which could be a stale/default
  reading if `ImuManager` hasn't been polled recently relative to when the event
  arrived — worth checking real timing rather than assuming it's always fresh.

## Real-hardware update: first gateway test (screenshots, both status pages)

The user flashed and tested the gateway for the first time, sharing screenshots of both
status pages. Genuinely good news: uptime 1202s with no crash, `ESP-NOW: active`,
`Peers seen: 1`, and — the best evidence yet that Phase 6 actually works — the gateway's
`DEVICES`/status page correctly listed the node (`NODE-2C3A30`, role `CAMERA`, `enabled:
yes`, `14s ago`) with **zero manual registration**, exactly the zero-code discovery
Phase 6 promised. GPS correctly reported `{"status":"NO_FIX"}` (no antenna view yet, not
an error) and Security Mode showed `PARKED (auto)`. This is the first real confirmation
that Phases 5–10's ESP-NOW/registry/mode-broadcast machinery functions on physical
hardware, not just in review.

Two real bugs surfaced from the screenshots themselves:

1. **Mojibake** (`â€"` where an em dash should render) on both status pages — a missing
   charset declaration. `StatusPage`'s HTML had no `<meta charset>` and the response was
   sent as plain `text/html` with no charset in the `Content-Type` header either, so
   browsers fell back to guessing (usually Latin-1/Windows-1252) against UTF-8-encoded
   em-dash bytes. **Fixed**: added `<meta charset='UTF-8'>` and
   `text/html; charset=utf-8` on the response.
2. **Rename doesn't flow node → gateway.** The node's own status page showed Display
   Name `CAM01`, but the gateway's registry still showed `NODE-2C3A30` for the same
   device. Root cause: `RENAME` only ever pushed a name from gateway to node (via
   `CONFIG_UPDATE`); a name set locally on the node (e.g. through BLE/AP provisioning,
   which does let you set a display name directly) had no path back to the gateway's
   registry. **Fixed**: the HEARTBEAT payload now carries the sender's own
   `displayName`, and `DeviceRegistry::upsertFromDiscovery` adopts it whenever
   non-empty and different — making a node's own name authoritative and keeping both
   `RENAME` (gateway → node → next heartbeat → registry) and local provisioning
   (node → next heartbeat → registry) consistent through the same mechanism, rather
   than needing two different sync paths.

**Not yet resolved, needs the user's input rather than a code fix:** the node's status
page reported `firmware=0.4.0-phase4`, but the page itself displays ESP-NOW/Security
Mode fields that don't exist in any build before Phase 10 — the running binary is
clearly much newer than the version string it's reporting. Given several `pio run`
(build-only, no upload) calls happened between phases without an accompanying reflash,
this is most likely just a stale flash from a version string that got bumped in source
after the last actual upload — but that's a guess, not confirmed. Worth clarifying when
next reflashing: check what `pio run -e node -t upload` actually deploys and whether the
version string it reports afterward matches `platformio.ini`.

Both fixes above rebuild clean on both environments (verified directly).

## Next Step

Flash the gateway and bench-test Phase 11 specifically: trigger a motion event, confirm
an `/incidents/INCIDENT-*.json` file actually appears on the gateway's flash (readable
via a small test sketch or a future OTA/file-access tool — there's no direct way to pull
files off LittleFS remotely yet), confirm its GPS/IMU/env fields are populated
correctly, and confirm `INCIDENTS` and retention behave as expected after enough events
to exceed `MAX_STORED_INCIDENTS`. Also reflash both devices with this update to confirm
the mojibake and rename-sync fixes, and to get both devices onto a firmware version
string that actually matches what's running.

## Firmware version display bug — resolved (root cause found, not just worked around)

Last session's open question (node reporting `0.4.0-phase4` while clearly running much
newer code) is now explained and fixed: `DeviceConfig::loadFromDisk()` was reading
`firmwareVersion` from the *persisted* `device.json` and keeping that value forever —
once a config file existed, the field never refreshed to match whatever binary actually
booted, no matter how many times the device was reflashed with different source. Not a
stale-build guess after all; a real, deterministic bug. Fixed: `loadFromDisk()` now
always sets `current.firmwareVersion` from the compile-time `CARSENTINEL_FIRMWARE_VERSION`
constant, logs when it differs from what was stored, and re-persists so the file stays
in sync too.

## Phase 12 — Email Notification

**Implemented:**
- `NotificationProvider` — abstract interface (Section 30); `IncidentCorrelator` and
  `NotificationManager` depend only on this, not on email specifically, so a future
  provider (Telegram, webhook, push) is a new class, not a rewrite.
- `EmailProvider` — a minimal SMTP client written directly over `WiFiClientSecure`
  (implicit TLS, e.g. `smtp.gmail.com:465` with an app password) rather than a
  third-party mail library. The EHLO/AUTH LOGIN/MAIL FROM/RCPT TO/DATA dialogue is a
  short, stable, well-documented text protocol; every earlier phase that added a new
  external library needed at least one guessed-API compile-fix pass, not worth that
  risk here. `base64::encode()` and `WiFiClientSecure::connect()` were both confirmed
  against this toolchain's actual installed headers before writing any of it.
- **Documented, not hidden, security limitation**: TLS certificate validation is
  currently disabled (`setInsecure()`), not pinned to a CA bundle. Accepts whatever
  certificate the server presents. Fine for a first working version talking to a known
  provider; not real certificate pinning (Section 41).
- `EmailConfig` — persisted SMTP settings (host/port/username/password/sender/
  recipient/cooldown) in their own file, same pattern as `NetworkConfig`/
  `EspNowSecurity` — never logged, never mixed into a diagnostic dump of other config.
- `NotificationManager` — the glue: registered as `IncidentCorrelator`'s notification
  handler (called whenever an incident reaches the `NOTIFICATION` state), builds a
  subject/body from the incident's full association data (GPS/IMU/env/evidence list),
  and applies Section 29's cooldown — but **not** full aggregation. A notification
  arriving within `cooldownSeconds` of the last one is simply suppressed and logged,
  not queued or combined into one richer email; real Section 29 aggregation (batching
  several events into one message) is a further step this phase doesn't take.
- No image attachments — Section 29 lists them as optional, and the gateway has no
  direct access to image bytes (they live on each node's own SD card, not the
  gateway); implementing that would need an ESP-NOW file-transfer mechanism that
  doesn't exist. Text-only email lists which nodes have images available and where.
- New gateway serial commands: `EMAILCONFIG <host> <port> <user> <pass> <sender>
  <recipient>` (also enables), `EMAILENABLE`, `EMAILDISABLE`, `TESTEMAIL` (sends
  immediately, bypassing the cooldown, to verify configuration). `STATUS` and the
  status page both show whether email is enabled (not the credentials).

**Not implemented (by design):** real Section 29 aggregation (see above), image
attachments (see above), any other notification channel (Section 30 lists Telegram/
WhatsApp/push/webhook/Home Assistant as future work built on the same
`NotificationProvider` interface — not this phase's job to build all of them).

**Build status: compiles clean, both environments** (verified directly, one real bug
caught and fixed along the way: `IncidentNotifyHandler`'s typedef referenced
`IncidentRecord` before it was declared — reordered). **Not yet bench-tested against a
real SMTP server** — nothing about the EHLO/AUTH/DATA dialogue has been exercised
against real credentials or a real mail provider yet.

**Known limitations / risks to verify on hardware:**
- The disabled certificate validation, stated above, is worth revisiting before this
  is anything more than a bench/development configuration.
- `EMAILCONFIG`'s parser has no quoting support — none of host/port/username/sender/
  recipient are expected to contain spaces, and a password containing one isn't
  supported by this command (documented, not silently mishandled, but worth knowing
  before choosing an app-password with spaces in it).
- The SMTP response reader blocks for up to 10s per protocol step (`send()` is only ever
  called from inside `loop()`, via `IncidentCorrelator::closeIncident()` →
  `NotificationManager::onIncidentReady()`), so a slow/hung SMTP server **will** stall
  the gateway's main loop for potentially several multiples of 10s during a real
  notification attempt. Applying Phase 1/10's lesson directly: `readSmtpResponse()`'s
  wait loop calls `Watchdog::feed()` every iteration, so this won't repeat the earlier
  watchdog-panic bug — but the loop is still genuinely blocked for that duration (ESP-NOW,
  GPS parsing, IMU polling, everything else in `loop()` pauses too), which is a real
  responsiveness cost worth confirming is acceptable once tested against a real server.

## Next Step

Flash the gateway and bench-test Phases 11–12 together: trigger an incident, confirm an
`/incidents/*.json` file appears with populated association data, configure real SMTP
credentials via `EMAILCONFIG`, run `TESTEMAIL`, and confirm a real email arrives —
watching specifically for whether the blocking SMTP call causes any watchdog issue on
real hardware.

## Phase 13 — OLED Displays

**Implemented:**
- `DisplayManager` (gateway-only) — drives up to two SSD1306 OLEDs via
  `adafruit/Adafruit SSD1306` + `Adafruit GFX Library` (well-established, stable public
  APIs unchanged for years; not verified against this toolchain's installed headers the
  way core/ESP-IDF APIs have been in every prior phase, since these are external
  libraries not present in the local install to grep — a deliberate, lower-risk
  exception to that discipline given how mature and unchanging this specific API is).
  Display 2 lives on the second I2C bus (`Wire1`), guarded by `#if SOC_I2C_NUM > 1`
  exactly like `I2CBusManager` already does, since `Wire1` doesn't exist on the node's
  classic ESP32 target and this file compiles into both environments.
- Never touches hardware that wasn't already confirmed present by Phase 3/9's I2C
  presence probe (Section 2.2) — `DisplayManager::begin(present0, present1)` takes
  those results directly, doesn't re-probe.
- **Configurable page assignment, not hardcoded** (Section 25's explicit requirement):
  each display cycles through a persisted, ordered list of pages
  (`/config/display_config.json`), with sensible compiled-in defaults (display 0:
  HOME/SECURITY/GPS, display 1: NETWORK/DEVICES/SYSTEM) that a `DISPLAYPAGES <0|1>
  <PAGE,PAGE,...>` serial command can override per Section 48's "no raw JSON to normal
  users" spirit — an admin command, not a hand-edited config file. `DISPLAYINTERVAL
  <ms>` tunes the page-cycle timing.
- Seven pages implemented (Section 26): HOME, NETWORK, GPS, IMU, SECURITY, DEVICES,
  SYSTEM — each a terse ~4–5 line summary (128×64 at text size 1 fits ~8 lines) mirroring
  the same fields `STATUS`/the status page already report, kept as one source of truth
  per field rather than a third place these could drift apart.
- Sensor-agnostic by design, same pattern as `IncidentCorrelator`/`StatusPage`:
  `DisplayManager` owns the Adafruit_SSD1306 objects and the page-cycling timer only;
  `gateway_main.cpp`'s `renderDisplayPage()` callback does the actual drawing using
  whatever managers it already has (`GpsManager`, `ImuManager`, `DeviceRegistry`,
  `SecurityModeConfig`, etc.) — `DisplayManager` itself has no dependency on any of them.

**Not implemented (by design):** any interactive input (no buttons/encoder wired —
these are read-only status displays); animations/graphics beyond text (Adafruit GFX
supports them, nothing in Section 25/26 asks for them); a page reachable from BLE/AP
provisioning's initial setup (display config is gateway-serial-only for now, matching
where every other runtime config command already lives).

**Build status: compiles clean, both environments, first attempt** (verified directly —
node RAM 19.2%/Flash 42.7%, unchanged since node never touches `DisplayManager`; the two
new external libraries resolved without a compile-fix pass despite not being pre-verified
against local headers). **Not yet bench-tested** — no physical SSD1306 has rendered
anything from this code yet.

**Known limitations / risks to verify on hardware:**
- `Adafruit_SSD1306::begin(..., periphBegin=false)` assumes `I2CBusManager` already
  called `Wire.begin()`/`Wire1.begin()` with the confirmed pins before `DisplayManager`
  starts — order-dependent, and `gateway_main.cpp`'s `initHardwareCapabilities()` does
  call them in that order, but this is a real coupling worth remembering if that
  function is ever refactored.
- Both physical SSD1306 units are confirmed at the same address (`0x3C`) with no
  jumper — this phase's dual-bus approach depends entirely on that being resolved
  correctly at the `I2CBusManager`/pin level (Phase 9), not re-verified here.
- Page content generation (`renderDisplayPage()`) has never been visually checked for
  layout/readability on an actual 128×64 screen — line lengths were estimated from the
  font's nominal character width, not measured against a real render.

## Phase 14 — OTA

**Implemented (gateway):**
- `OtaManager` — `fetchManifest(url)` parses `{version, url, md5, hardwareProfile}` over
  `HTTPClient`; `isUpdateNeeded()` refuses a mismatched `hardwareProfile` (Section 37 —
  never flash the wrong board's binary) and a no-op version match; `performUpdate()`
  streams the image directly into `Update` (no full-image buffering — the ESP32 doesn't
  have RAM for that), verifies MD5 via `Update.setMD5()`, and restarts on success —
  device is left unchanged on any failure along the way (short write, MD5 mismatch,
  low free heap). MANUAL trigger only (`OTACHECK <url>` / `OTAUPDATE <url>` serial
  commands) — Section 36 explicitly warns against blind AUTO/STAGED updates, deferred
  rather than rushed.
- `Update.onProgress()` calls `Watchdog::feed()` every callback — same lesson as every
  other multi-second blocking call this project has hit (Wi-Fi connect, SMTP dialogue).
- `confirmHealthyBoot()` (both roles) calls `esp_ota_mark_app_valid_cancel_rollback()`
  near the end of `setup()`; harmless no-op if the bootloader wasn't built with rollback
  support (not verified either way in this toolchain — documented as such, not claimed
  as guaranteed).
- `board_build.partitions = min_spiffs.csv` on `[env:node]` — the `esp32cam` board's
  default `huge_app.csv` has only one app partition, making standard `Update.h` OTA
  physically impossible regardless of application code. `min_spiffs.csv` gives each
  slot ~1.92MB (current build ~1.3MB, comfortable) and a 128KB LittleFS partition
  (configs only — evidence images stay on SD).

**Not implemented on the node — real hardware constraint, not a choice deferred for
later convenience:** linking `HTTPClient`/`Update.h` (whose flash-write path needs
IRAM-resident code) alongside the node's existing WiFi/BLE/camera/SD_MMC footprint
overflowed classic ESP32's fixed IRAM region — a genuine `ld.exe` link failure, not a
guess. Fixed in stages, each independently confirmed by rebuilding:
1. `-flto` on `[env:node]`: 512 → 408 bytes over.
2. Disabling NimBLE's central/observer roles (`BLEProvisioning.cpp` only ever acts as a
   peripheral/server, never scans or connects as a client —
   `CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED` / `..._OBSERVER_DISABLED`): 408 → 52 bytes.
3. Dropping `WiFiClientSecure`/TLS from the node's own OTA path (node OTA now only
   accepts plain `http://`, refusing `https://` with a clear log message) and moving
   `Adafruit SSD1306`/`GFX` out of the global `lib_deps` into `[env:gateway]`-only
   (they were being force-linked into both environments regardless of use — moving
   `DisplayManager.cpp` to a separate `lib/CarSentinelGateway/` folder alone didn't
   help, since PlatformIO's LDF still force-links explicitly declared `lib_deps`
   regardless of `#include` chains): no further change — confirmed neither was
   actually being pulled into the node's IRAM at link time to begin with.
4. Every further NimBLE trim tried (`CONFIG_BT_NIMBLE_LOG_LEVEL`,
   `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`, `-fipa-icf`) made no further difference — the
   remaining ~52 bytes are baked into Espressif's prebuilt WiFi/BT SDK archives, not
   reconfigurable from application code or PlatformIO's `platformio.ini` without
   rebuilding the toolchain's own components.

Given that hard floor, `OtaManager.cpp`'s node build (`#if !CARSENTINEL_ROLE_GATEWAY`)
is now a stub that logs "not supported on this hardware profile" and returns `false` —
`HTTPClient.h`/`Update.h`/`WiFiClientSecure.h` are not even included on that path, so
none of their object code is pulled into the node link at all. `confirmHealthyBoot()`
(cheap, `esp_ota_ops.h` only) still runs on both roles.

**Build status: compiles clean, both environments** (verified directly after each of
the fixes above — node RAM 19.0%/Flash 64.6%, gateway RAM 18.1%/Flash 20.0%).

**Known limitations:**
- Node camera firmware currently has no self-update path. Revisit if node-side IRAM
  headroom improves (a future board swap off classic ESP32, or an Espressif toolchain
  change) — tracked here, not silently dropped from the spec.
- Node OTA (if ever re-enabled) would be `http://`-only; TLS was the first thing traded
  away for IRAM headroom.
- `esp_ota_mark_app_valid_cancel_rollback()`'s real effect depends on bootloader
  rollback support that isn't independently confirmed in this build.

## Phase 19 — Dashboard

**Implemented (gateway-only):**
- `DashboardServer` (`lib/CarSentinelGateway/`) replaces `StatusPage` on the gateway
  (node keeps `StatusPage` — its minimal read-only page is enough for a device with no
  rich data to show and doesn't need a second hardware profile to support). Same
  sensor-agnostic callback pattern as `StatusPage`/`IncidentCorrelator`: it knows
  nothing about `DeviceRegistry`/`GpsManager`/`IncidentCorrelator` directly —
  `gateway_main.cpp` supplies JSON-string-producing callbacks
  (`buildStatusJson`/`buildDevicesJson`/`buildIncidentsJson`) and a stream provider.
- Routes: `GET /` (embedded single-page dashboard — dark theme, no CDN/build step, must
  work fully offline in a parked vehicle), `GET /api/status`, `GET /api/devices`,
  `GET /api/incidents` (the "companion API" — usable on its own by a future phone app or
  external tool, not just the bundled page), and `GET /stream?node=<id>` which proxies
  a live MJPEG feed from the named camera node's own `StatusPage`-hosted `/stream`
  endpoint (added to `StatusPage`/`CameraManager` alongside this phase) back through the
  gateway, so the dashboard can show live camera video without exposing each node's IP
  directly.
- Dashboard page polls the three JSON endpoints every 3s and patches the DOM in place
  (no full-page reload/flicker, unlike `StatusPage`'s old meta-refresh) — device cards
  (firmware, uptime, heap, Wi-Fi, GPS, IMU, security mode, ESP-NOW), a live devices
  table (Section 6 registry — auto-populated, zero-code), a live camera selector/feed,
  and a recent-incidents table.
- `IncidentCorrelator::listRecentJson(maxCount)` — new read model over the already
  -persisted `/incidents/*.json` records (newest first), so the dashboard/API doesn't
  need to re-implement IncidentCorrelator's on-disk format itself.
- `DeviceRegistryEntry` gained a persisted `ip` field (populated from each node's
  heartbeat) so the gateway can address a node directly for the camera stream proxy.

**Not implemented:** authentication/access control on the dashboard or API (matches
every other HTTP surface in this project so far — trusted local network only, not
stated as anything more); historical charts/trends (only live + recent-incidents data);
a JSON API for controlling the gateway (RENAME/ENABLE/etc. remain serial-only for now).

**Build status: compiles clean, both environments** (verified directly — node
unaffected since `DashboardServer` lives in the gateway-only `lib/CarSentinelGateway/`
library and node's own `StatusPage` build only gained the small `/stream` addition).
**Not yet bench-tested** — no physical device has served this page yet.

**Known limitations / risks to verify on hardware:**
- The camera stream proxy (`streamCameraFromNode`) is a raw byte relay over a second
  `WiFiClient` connection to the node's IP — not yet confirmed under real network
  conditions (packet loss, a node reconnecting mid-stream, multiple dashboard viewers
  at once).
- Dashboard JSON payload sizes (especially `/api/incidents`) haven't been checked
  against the gateway's actual free heap under load with many devices/incidents.

## Phase 15 — AI Framework

**Implemented (gateway-only):** `AIThreatFramework` (`lib/CarSentinelGateway/`) — a
pluggable analyzer slot (`ThreatAnalyzer` function pointer, swappable at runtime via
`setAnalyzer()`) that turns an `IncidentTriggerInfo` + corroborating-node count into a
`ThreatAssessment` (severity, confidence 0-100%, plain-English reasoning).

**Deliberately not real ML — a scoping decision, not a shortcut:** this toolchain does
ship esp-dl's face/human detection (`libhuman_face_detect.a`/`libcat_face_detect.a`,
confirmed present), but Phase 14 already showed classic ESP32's IRAM budget is
razor-thin even without a model loaded — adding real inference to a camera node would
very likely reopen that exact link failure. The gateway has real headroom (ESP32-S3,
8MB PSRAM), but nodes don't transmit full images to it today (evidence stays local to
each node's SD card by design). So this phase ships the *framework* — a real,
swappable interface — with a transparent heuristic as its only analyzer today, honestly
documented as such rather than dressed up as more.

**Build status: compiles clean** (gateway only; node build untouched since this lives
in `lib/CarSentinelGateway/`).

## Phase 16 — AI Security Assistance

**Implemented:** `AIThreatFramework::heuristicAnalyzer()` — combines IMU impact
magnitude vs. configured thresholds, GPS movement detected while the vehicle should be
stationary (an incident only opens in PARKED mode — Section 16), and multi-camera
corroboration (`IncidentRecord::evidenceCount`) into a score, then a severity band
(LOW/SUSPICIOUS/HIGH — not named `LOW`/`HIGH` in code, since those collide with
Arduino's `#define LOW 0x0`/`HIGH 0x1` pin-state macros, a real compile failure caught
building this). `gateway_main.cpp`'s `assistedIncidentNotify()` replaces the direct
`NotificationManager::onIncidentReady` registration: it runs the assessment on every
closed incident, logs it, and **skips the email** for a LOW-confidence,
uncorroborated single trigger — reducing notification noise without silently dropping
anything (still persisted and visible on the Phase 19 dashboard's incident list either
way).

**Not implemented:** no per-user-configurable scoring weights (thresholds are the same
`ImuThresholdsData`/GPS constants already used elsewhere); no historical learning
(purely stateless per-incident).

**Build status: compiles clean.** **Not yet bench-tested** — no real incident has been
scored by this code on hardware yet.

## Phase 17 — Low-Power Parked Mode

**Implemented (both roles):** `PowerManager::applyModeChange(mode)` — toggles
`WiFi.setSleep()` (modem sleep: radio duty-cycles between beacon intervals instead of
staying fully receive-active) on whenever the current `SecurityMode` is `PARKED`, off
for every other mode. Called once at boot with the current mode and again every time
`SecurityModeConfig`'s mode actually changes (gateway's `applyModeChange()` /
auto-detection loop; node's `SET_MODE` remote-command handler).

**Deliberately not full deep-sleep — a real trade-off, not an oversight:** true
ESP32 deep sleep between RCWL motion events was considered and rejected for this phase.
It would tear down the Wi-Fi/ESP-NOW connection state that Phase 4/5's already-verified
motion → capture → evidence pipeline depends on being immediately available, and
re-establishing an ESP-NOW link from cold on every wake risks missing the very motion
event this device exists to catch. Modem sleep keeps the connection alive (some
latency added to packet delivery, not measured on hardware) while still reducing power
draw during the long stretches a parked vehicle sits idle — the safer, smaller win.

**Not implemented:** CPU frequency scaling (`setCpuFrequencyMhz()`) — considered, but
skipped to avoid any risk to the camera capture/ESP-NOW timing paths already verified
working on real hardware; a global CPU slowdown during PARKED would also slow down
motion-triggered capture itself, which is the one time responsiveness matters most.

**Build status: compiles clean, both environments** (node Flash 64.7%, +0.1% —
negligible, `WiFi.h` was already linked).

**Known limitations:** actual power draw reduction not yet measured on hardware; modem
sleep's added ESP-NOW/motion-alert latency not yet measured either.

## Phase 18 — Vehicle Integration

**Implemented (gateway; capability-gated, so also usable by a node if ever wired
there):** `IgnitionSense` — reads a debounced digital ignition-sense line (a
voltage-divider/optocoupler-stepped-down vehicle ignition-switched 12V signal, never a
direct connection — see `CapabilitiesConfig.h`'s new `ignition`/`ignitionGpio` fields).
When configured, the Section 44 auto-mode-detection loop uses it *instead of* the
GPS-speed/IMU-movement heuristic (a wired ignition signal is unambiguous; GPS/IMU are
inferring from noisy proxies) — ignition ON drives the same DRIVING transition, OFF
drives PARKED, through the same existing hysteresis/streak logic. Off by default on
every hardware profile (`GPIO_UNCONFIGURED`) until real vehicle wiring exists.

**Not implemented — genuinely blocked on hardware, not a scope choice:** OBD-II/CAN
bus integration (vehicle-specific PID knowledge and a confirmed physical harness are
both prerequisites this project doesn't have yet); any other vehicle-bus signal
(door/alarm state, speed from the vehicle itself rather than GPS). These remain
tracked, not silently dropped from the spec — revisit once the Peugeot 2008's OBD-II
port and pinout are actually characterized.

**Build status: compiles clean, both environments** (node unaffected in practice —
`IgnitionSense` is capability-gated off by default and adds negligible code even
compiled in). **Not yet bench-tested** — no physical ignition-sense circuit has been
wired or read from yet.

## Phase 20 — Vehicle Installation

**No firmware component.** `docs/wiring/VEHICLE_INSTALLATION.md` documents the planned
install sequence (mounting, power taps, ignition-sense tap, GPS antenna placement,
SD-card accessibility, post-install re-verification of every prior phase) and its
prerequisites — chiefly that every phase's "Not yet bench-tested" item gets resolved on
the bench first, and that a real (not simulated) ignition-sense circuit is built and
tested before it ever touches the vehicle's actual wiring. This is a checklist, not a
completed install: nothing in it has been executed against the real Peugeot 2008 yet.

## Next Step (superseded for networking — see the Architecture Update entry below)

All 20 phases now have either working firmware or (Phase 20) a documented plan. The
project's own "one phase at a time, stop for review" discipline (Section 64/76) has
been running ahead of physical bench verification for a while now — Phases 4/5/6/8/10
have some real-hardware confirmation, but 7/9/11/12/13/14/15/16/17/18/19 are all still
"compiles clean, not yet bench-tested." The actual next step was going to be hardware
bench verification — but a networking architecture change (below) landed first and
needs to be implemented before some of that bench testing (especially anything
touching Phase 2/5/6) is worth doing against the current code.

## Architecture Update — Hybrid ESP-NOW/Wi-Fi Networking

Full design: [docs/NETWORK.md](NETWORK.md), [docs/PROVISIONING.md](PROVISIONING.md),
[docs/SECURITY.md](SECURITY.md), [docs/OTA.md](OTA.md), [docs/TESTING.md](TESTING.md).
`docs/ARCHITECTURE.md` and `README.md` were also updated for consistency. Originally
landed as a planning-only entry; **implementation followed in two commits** (below) —
this section now records both the original plan and what's actually been built.

**Implemented:**
1. **ESP-NOW starts unconditionally on every boot**, both roles — no longer gated
   behind `!provisioningMode`. Closes gap item 1.
2. **`NetworkConfig.wifiFallbackEnabled`** (schema v2→v3, default `true`) — when
   `false`, a device never touches Wi-Fi at all: no `WiFi.begin()`, no AP-mode
   provisioning, no credentials prompt. `WIFIFALLBACK ON|OFF` serial command (both
   roles) + a toggle on the gateway dashboard's Settings page. Closes half of gap
   item 5 (the toggle existing at all — `ProvisioningPortal`'s form still always shows
   a Wi-Fi field with no in-flow skip option, so that half remains open).
3. **`TransportManager`** (`lib/CarSentinelCommon/`, node-focused) — the real state
   machine from `docs/NETWORK.md` Section 3 (`DISCONNECTED` / `ESPNOW_CONNECTING` /
   `ESPNOW_CONNECTED` / `WIFI_FALLBACK_CONNECTING` / `WIFI_CONNECTED` /
   `STANDALONE`), using `PeerRegistry`'s gateway-role peer heartbeat age against a new
   configurable `espNowHeartbeatTimeoutMs` (schema v3→v4, also adds
   `espNowDiscoveryTimeoutMs`/`espNowRetryIntervalMs`/`wifiFallbackDelayMs`). Exposes
   `sendEvent()`/`sendTelemetry()`/`sendStatus()`/`sendCommand()`; node's
   `MOTION_DETECTED` forward now goes through it instead of calling `EspNowManager`
   directly. Closes most of gap item 2 — the low-level `Transport`/`EspNowTransport`
   interface is unchanged, as planned.
4. **`OfflineQueue`** — bounded (20), LittleFS-persisted, flushed automatically on
   transition into `ESPNOW_CONNECTED`. Closes gap item 3.

**Still open (unchanged from the original plan):**
- **`WiFiTransport`/Wi-Fi-fallback message *delivery*.** The state machine correctly
  tracks `WIFI_CONNECTED` (real Wi-Fi connectivity), but `TransportManager` has no
  actual delivery path over it — no gateway-side HTTP ingestion endpoint exists, and a
  node has no way to discover the gateway's Wi-Fi IP. An event that can't go over
  ESP-NOW is queued (`OfflineQueue`), not delivered over Wi-Fi and not dropped. This is
  gap item 2's remaining half.
- **Gateway identity validation / formal pairing handshake** (gap item 4) — not
  started. Zero-code auto-discovery (Phase 6) still means "the first device that
  HELLOs claiming role GATEWAY, with a valid HMAC" is trusted as the gateway.
- **`ProvisioningPortal`'s Wi-Fi field can't be skipped in-flow** (remaining half of
  gap item 5) — a device can be configured ESP-NOW-only via `WIFIFALLBACK OFF`
  afterward, but first-time provisioning doesn't yet ask "do you want Wi-Fi fallback?"
  before showing the SSID field.

**What changed:** the project's networking model is now explicitly ESP-NOW-primary /
Wi-Fi-fallback / standalone-mandatory, with Wi-Fi credentials optional per device
(requested only if Wi-Fi fallback is enabled), rather than the implicit
Wi-Fi-first/only model the docs previously read as. This wasn't a contradiction
introduced now — it's the same "node independence" principle stated since Phase 0
(`docs/ARCHITECTURE.md`), made explicit and load-bearing rather than aspirational, and
made consistent across every doc that touches networking (some of which, notably
`README.md`'s provisioning section, previously read as if Wi-Fi were required for every
device).

**Concrete gap vs. the current implementation** (full detail in `docs/NETWORK.md`
Section 9 and `docs/PROVISIONING.md` Section 4 — summarized here for the plan record):

1. `node_main.cpp`'s `setup()` currently gates ESP-NOW behind Wi-Fi/provisioning
   state — a node with no saved Wi-Fi credentials enters `enterProvisioningMode()`
   and **does not start ESP-NOW at all** until provisioning ends. Target: ESP-NOW
   starts unconditionally and early; Wi-Fi/provisioning becomes independent of it.
2. No `TransportManager` or `WiFiTransport` exist — application code calls
   `EspNowManager::sendMessage()` directly everywhere, and transport state is tracked
   via separate booleans rather than a real state machine. The existing `Transport`/
   `EspNowTransport` interface (Phase 5) is a low-level, MAC-addressed radio
   abstraction and stays as-is; the new `TransportManager` is a higher-level layer
   above it, addressed by logical device identity.
3. No persisted offline-event queue — already an acknowledged gap in `EspNowManager.h`
   itself ("the persistent offline queue is Section 28 / Phase 28, out of scope
   here"). Standalone local capture/storage already works; sync-on-reconnect doesn't.
4. No formal gateway discovery/pairing handshake or gateway-identity validation —
   today's zero-code auto-discovery (Phase 6) is a real, worth-keeping convenience for
   the common case, but has no cryptographic proof that a device claiming to be the
   gateway actually is the paired one.
5. `ProvisioningPortal`'s form always shows a Wi-Fi SSID/password field with no way to
   skip it for an ESP-NOW-only device, and `NetworkConfig` has no explicit "Wi-Fi
   fallback enabled" toggle distinct from "credentials happen to be saved."

**Still not implemented / open decisions:** `WiFiTransport` itself (Section above);
`requestConfiguration()` was scoped out of `TransportManager`'s generic-ops set for now
— no two-way gateway config-push API to route it through yet, and it wasn't needed by
the one call site (`MOTION_DETECTED`) this pass actually rewired; no new phase number
was assigned (whether this becomes "Phase 21" or a rework folded into Phases 2/5/6 is
still an open decision). The multi-WiFi list feature and Settings page built earlier in
this session (`NetworkConfig::saved[]`, `connectBestKnown()`, the gateway dashboard's
`/settings` page) are unaffected and remain in place — they solve "remember multiple
Wi-Fi networks for the fallback path," which is still exactly what's needed, just no
longer the *first* thing a node tries.

## Next Step (superseded — see Phase 21 below)

This entry's original "decide how the gap gets implemented" question was resolved:
`TransportManager`/`OfflineQueue`/the boot-order fix were implemented directly (see
this entry's own "Implemented" list above), not folded into a Phase 2/5/6 rework or
given a separate phase number of their own. Phase 21 (below) is a distinct, larger
follow-on — a remote backend/API layer — not a continuation of closing this entry's
remaining gaps (gateway identity validation, `WiFiTransport`, provisioning's in-flow
Wi-Fi-skip option all remain open, tracked above, independent of Phase 21).

# Phase 21 — Remote Backend, API & Hybrid Connectivity

Full design: [docs/BACKEND.md](BACKEND.md), [docs/REMOTE_ACCESS.md](REMOTE_ACCESS.md).
`docs/ARCHITECTURE.md` updated to reference the optional backend layer.

## Phase 21.1 — Architecture Audit (complete)

**Scope: inspection and documentation only — no firmware or backend code was written.**

**Findings** (full detail in `docs/BACKEND.md`/`docs/REMOTE_ACCESS.md`):

1. **Current networking architecture**: ESP-NOW primary / Wi-Fi fallback /
   standalone-mandatory, implemented (`docs/NETWORK.md`, `TransportManager`,
   `OfflineQueue` — the entry directly above this one). Unaffected by Phase 21;
   the backend sits *above* the Gateway, never between a node and its gateway.
2. **ESP-NOW implementation**: `EspNowManager`/`EspNowTransport`/`EspNowProtocol` —
   HMAC-signed, sequence-numbered, HELLO/HEARTBEAT discovery, ACK/retry for
   ACK-expecting message types. Unaffected by Phase 21.
3. **Wi-Fi fallback**: `WiFiManager` (connect/reconnect) + `TransportManager` (state
   machine deciding when to use it). Unaffected by Phase 21 — the Gateway's *own*
   Wi-Fi/Internet connection (for the backend) is a separate, pre-existing thing
   (`WiFiManager` on the gateway already connects for the dashboard/email/OTA; the
   backend just becomes one more thing that connection is used for).
4. **Gateway capabilities**: device registry (`DeviceRegistry`), incident engine
   (`IncidentCorrelator`), AI threat scoring (`AIThreatFramework`), email
   (`NotificationManager`/`EmailProvider`), OTA (`OtaManager`, gateway-only), OLED
   displays (`DisplayManager`), local dashboard (`DashboardServer`) — all confirmed
   present and are exactly what `RemoteSyncManager` will read from to populate
   telemetry/events/incidents sent to a backend.
5. **Existing local API**: `DashboardServer`'s `/api/status`, `/api/devices`,
   `/api/incidents`, `/api/settings/*` — unauthenticated, LAN-only, unversioned. Stays
   exactly as-is (Section 18 of the Phase 21 spec explicitly asks to keep a lightweight
   local API); the remote API is a separate, new, versioned, authenticated surface.
6. **Event/incident queue**: `OfflineQueue` (Node↔Gateway, ESP-NOW-only delivery
   today) and `IncidentCorrelator`'s own persisted records — neither currently syncs
   anywhere off-device. A Gateway↔Backend queue (Phase 21.3) is new, analogous in
   design (bounded, persisted, retry/backoff) but a distinct queue serving a distinct
   hop.
7. **Configuration system**: versioned JSON-per-concern
   (`NetworkConfig`/`EmailConfig`/`CapabilitiesConfig`/`DeviceConfig`, each with
   `schemaVersion` + a `migrate()` seam) — the exact pattern `BackendConfig`
   (Phase 21.2) should follow, not a new configuration mechanism.
8. **Security/authentication**: ESP-NOW HMAC (shared key, documented
   single-default-key limitation — `docs/SECURITY.md`), no gateway-identity
   validation, no per-device backend credential of any kind (none needed yet — no
   backend exists). Phase 21.4 adds an entirely new authentication boundary
   (Gateway↔Backend) layered on top, not a replacement for the ESP-NOW one.
9. **Storage/evidence handling**: images live on each node's own SD card
   (`EvidenceManager`), referenced (not copied) by incident records
   (`IncidentEvidenceRef`). **No existing path exists for the Gateway to pull an
   image off a node's SD on demand** — this is a real gap Phase 21.7 (evidence upload)
   needs to close before "upload evidence to the backend" can work at all, since the
   Gateway doesn't have the bytes today.

**Exact files needing modification** (Phase 21.2+, none touched in this audit):
`gateway_main.cpp` (wire `RemoteSyncManager`), `DashboardServer.cpp`/`.h` (Settings
page gains a Backend section).

**New files** (Phase 21.2+): `lib/CarSentinelGateway/RemoteBackend.h`,
`HttpBackend.h/.cpp`, `RemoteSyncManager.h/.cpp`; `lib/CarSentinelCommon/
BackendConfig.h/.cpp` (same tier as `NetworkConfig`, even though gateway-only in
practice, for consistency with every other `*Config` class's location).

**Architectural conflicts with this plan: none identified.** The existing provider
-abstraction pattern (`AIThreatFramework`'s `ThreatAnalyzer`,
`NotificationManager`'s `EmailProvider`) and the `Transport`/manager-above-it split
(`EspNowTransport`/`EspNowManager`, `docs/NETWORK.md` Section 5) both directly
support the `RemoteBackend`/`RemoteSyncManager` shape Phase 21 asks for — no
competing system to reconcile, no rework of completed phases required.

**Recommended implementation order**: as specified in the Phase 21 brief itself
(21.2 Backend Abstraction → 21.3 Persistent Remote Queue → 21.4 Device Registration &
Authentication → 21.5 Backend Server → 21.6 Real-Time API → 21.7 Evidence Upload →
21.8 Remote Commands → 21.9 Webhooks → 21.10 API Documentation → 21.11 End-to-End
Testing) — no reordering recommended; 21.2–21.4 build the Gateway-side abstraction and
can be verified by build alone (no server to talk to yet, same "compiles clean, not
yet bench-tested" pattern used throughout this project), while 21.5 is the first
sub-phase requiring an actual backend deployment to test against.

## Phase 21.2 — Backend Abstraction (complete)

**Implemented** (gateway-only): `RemoteBackend` (interface — `begin`/`isConnected`/
`sendHeartbeat`/`sendTelemetry`/`sendEvent`/`sendIncident`), `HttpBackend` (the one
implementation — REST/HTTPS, `HTTPClient`+`WiFiClientSecure`, Bearer-token auth),
`RemoteSyncManager` (`LOCAL_ONLY`/`CONNECTING`/`CONNECTED`/`AUTH_FAILED`/
`RETRY_BACKOFF` state machine, periodic heartbeat, pass-through send methods), and
`BackendConfig` (`lib/CarSentinelCommon/`, same versioned-JSON-per-concern tier as
`NetworkConfig`/`EmailConfig` — `mode`, `baseUrl`, `deviceId`, `tenantId`,
`credential`, per-category sync-policy fields not yet consumed by any call site).
`BACKENDCONFIG`/`BACKENDENABLE`/`BACKENDDISABLE`/`BACKENDSTATUS` serial commands and a
new Remote Backend section on the dashboard's Settings page
(`GET`/`POST /api/settings/backend`).

**Local-first guarantee enforced, not just configured**: `BackendConfig.enabled`
defaults `false`; `RemoteSyncManager::loop()` returns immediately (no state check, no
network call, no CPU cost beyond the one comparison) whenever disabled.

**Not implemented in this sub-phase** (tracked for later sub-phases, not an
oversight): no retry/backoff/persisted queue — a failed send is currently just
dropped (Phase 21.3 is exactly this); no automatic call sites feeding real
telemetry/events/incidents into `RemoteSyncManager` (deciding how `BackendConfig`'s
`SyncPolicy` filters what gets sent is deferred until there's a real backend, Phase
21.5, to verify payloads against); no `AUTH_FAILED` detection (`HttpBackend::post()`
doesn't expose the HTTP status code yet, so "wrong credential" and "server
unreachable" both currently land in `RETRY_BACKOFF`).

**Build status: compiles clean, both environments.** Node build unaffected —
`BackendConfig` is gateway-only in practice; `--gc-sections` strips it since nothing
in the node's call graph references it (confirmed: node flash size unchanged,
65.2%). **Not bench-tested** — there is no backend server yet for `HttpBackend` to
actually talk to; `RemoteSyncManager` has only been verified to stay correctly inert
when `LOCAL_ONLY` (build-level reasoning, not a live test).

## Phase 21.3 — Persistent Remote Queue (complete)

**Implemented** (gateway-only, `lib/CarSentinelGateway/BackendQueue.h/.cpp`): a
bounded (50 items), LittleFS-persisted queue mirroring `OfflineQueue`'s
design (`docs/IMPLEMENTATION_PLAN.md` Phase 21.2's own "not implemented" list),
extended with what the Gateway↔Backend hop specifically needs beyond the Node↔Gateway
one: a category tag (`HEARTBEAT`/`TELEMETRY`/`EVENT`/`INCIDENT`, matching
`RemoteBackend`'s per-category send methods) and per-item exponential backoff
(5s base, doubling, capped at 5 minutes — a local ESP-NOW hop and an Internet link to
a possibly-down server fail very differently, so retrying every `loop()` the way
`OfflineQueue`'s flush does wasn't appropriate here).

`RemoteSyncManager`'s `sendTelemetry()`/`sendEvent()`/`sendIncident()` now enqueue on
delivery failure instead of just returning `false` and discarding the payload;
`loop()` calls `BackendQueue::flush()` every iteration (cheap when nothing is due —
each item's own backoff timer gates whether `flush()` actually attempts it).
Heartbeats are deliberately **not** queued — a stale liveness signal delivered
minutes late carries a wrong `uptimeMs` and adds nothing the next on-time heartbeat
won't already provide; only real data (telemetry/events/incidents) goes through the
queue. `BACKENDSTATUS` (serial) and the dashboard Settings page's backend status line
now report the pending queue count.

**Not implemented in this sub-phase** (documented gap, not an oversight): no
deduplication by event ID — would need to parse each JSON payload for an `id` field,
deferred until a real backend (Phase 21.5) exists to verify the payload shape against
first; no per-category retention policy — every category shares one bounded queue and
one eviction rule (oldest evicted first when full), not differentiated by importance
(e.g. an `INCIDENT` isn't protected from eviction by a flood of `TELEMETRY` items —
worth revisiting once there's a real usage pattern to design against, not guessed at
here).

**Build status: compiles clean, both environments.** Node build unaffected —
`BackendQueue` lives in the gateway-only library, confirmed by an unchanged node
flash size (65.2%). **Not bench-tested** — same reason as Phase 21.2: no backend
server exists yet to actually exercise retry/backoff against.

## Phase 21.4 — Device Registration & Authentication (complete)

**Implemented** (gateway-only): `RemoteBackend` gains `registerDevice(payload,
outResponsePayload)` and `lastStatusCode()`; `HttpBackend` implements both (`POST
/register`, response body captured via `HTTPClient::getString()`, status code tracked
on every request). `RemoteSyncManager` attempts registration once per boot (retried on
the same cadence as the heartbeat if it fails, never a tighter loop that would hammer
a down server) — skipped entirely if `BackendConfig.deviceId` is already set (an
operator-assigned ID is never overwritten). A successful registration response's
`deviceId`/`credential` fields (if present) are persisted via `BackendConfig::save()`
and applied to the live `HttpBackend` immediately, no reboot required. Registration
payload carries `hardwareProfile`/`firmwareVersion`/`nodeId` from the existing
`DeviceConfig` — no new identity fields invented for this.

**`AUTH_FAILED` detection** (deferred from 21.2, now possible): any failed backend
call is classified by `HttpBackend::lastStatusCode()` — HTTP 401/403 sets
`AUTH_FAILED` instead of `RETRY_BACKOFF`, so `BACKENDSTATUS`/the dashboard can show
"the credential is wrong" as a visibly different, more actionable state than "still
trying."

**Not implemented in this sub-phase**: no credential rotation/revocation flow (the
brief's Section 11 requirement) — `BACKENDCONFIG`/the dashboard already let an
operator manually replace a credential at any time, but there's no
automatic-refresh-before-expiry mechanism, since there's no real backend yet to define
what "expiry" even looks like. No secure hardware-backed credential storage (it's a
plain field in `/config/backend.json`, same posture as every other credential in this
project — `EmailConfig`'s SMTP password, `EspNowSecurity`'s key — documented, not
hidden).

**Build status: compiles clean, both environments.** Node build unaffected (all of
this lives in `lib/CarSentinelGateway/`). **Not bench-tested** — no real `/register`
endpoint exists anywhere to register against; the registration flow's JSON parsing and
state transitions are build-verified only.

## Phase 21.5 — Backend Server (complete)

**Technology: ASP.NET Core 8** — user's explicit choice (asked via the pending
decision this entry used to record). New `backend/` directory at the repo root,
**separate from `firmware/`** — a .NET solution, not embedded firmware; see
`backend/README.md` for how to run it.

**Implemented** (`backend/src/CarSentinel.Backend/`): minimal-API ASP.NET Core app,
EF Core + SQLite (`EnsureCreated()`, no migrations — documented scope cut, not an
oversight), matching exactly what the firmware side already sends:

- `POST /api/v1/register` — anonymous (a device has no credential on first contact);
  re-registration with an already-known `deviceId` requires the existing credential to
  match, so this can't be used to hijack a device's identity. Server-assigns
  `deviceId`/`credential` on first registration; the credential is stored only as a
  SHA-256 hash (stricter than most credentials elsewhere in this project, since a
  backend holding many devices' credentials is a higher-value target than any single
  device's own config file).
- `POST /api/v1/heartbeat` / `/telemetry` / `/events` / `/incidents` — device-credential
  authenticated (`Authorization: Bearer <credential>` + `X-CarSentinel-Device-Id`,
  constant-time comparison against the stored hash). Incidents are **upserted by
  `incidentId`**, not appended — one row holds the latest state as
  `IncidentCorrelator`'s state machine progresses, the idempotency behavior Section 13
  of the Phase 21 brief asked for.
- `GET /api/v1/devices`, `/devices/{id}`, `/devices/{id}/telemetry`, `/events`,
  `/incidents`, `/incidents/{id}`, `/health` — anonymous reads (no user-account model
  yet, documented gap).
- Swagger UI at `/` (Swashbuckle) — OpenAPI 3.x document with the device-credential
  auth scheme described, per Section 16 of the Phase 21 brief.

**Bug found and fixed by actually running it, not just building it** (the same
discipline this project has followed for firmware all along):
`ORDER BY`-ing a `DateTimeOffset` column isn't supported by EF Core's SQLite provider
(`System.NotSupportedException` at request time, not build time) — every timestamp
field was `DateTimeOffset`, which is what the query endpoints order by. Switched every
timestamp to `DateTime` (UTC) throughout. Caught by actually registering a device,
sending a heartbeat/telemetry/incident, and querying them back — real
requests, not just `dotnet build` succeeding — including confirming the incident
upsert behavior works (posted the same `incidentId` twice with different `state`
values, confirmed one row with the latest state and the original `firstReceivedAt`
preserved) and that a wrong credential gets a real 401.

**Not implemented in this sub-phase** (see `backend/README.md`'s own list): EF
migrations, user-facing authentication on the read API, WebSocket/SSE (Phase 21.6),
evidence/image upload (Phase 21.7), remote-command relay (Phase 21.8), webhooks
(Phase 21.9).

**Build/test status: builds clean (`dotnet build` on the solution) and was manually
exercised end-to-end against a running instance** (register → heartbeat → telemetry →
incident upsert → query → auth rejection), unlike every other Phase 21 sub-phase so
far, which had no server to test against. The Gateway↔Backend integration itself
(pointing a real `HttpBackend` at this running server) is still unverified — that
needs real hardware, not just two processes on one machine.

## Phase 21.6 — Real-Time API (complete)

**Implemented** (`backend/src/CarSentinel.Backend/`): Server-Sent Events, not
WebSocket — one-directional (server → browser) is all `docs/REMOTE_ACCESS.md`
Section 5's flow needs, and SSE is a plain streamed HTTP response, no extra package,
no upgrade handshake. `EventBroadcaster` (in-memory, single-process pub/sub via
bounded `Channel<string>` per subscriber) is published to from every ingest endpoint;
`GET /api/v1/stream` subscribes and streams `data: {...}\n\n` lines. Event envelope:
`{schemaVersion, type, deviceId, timestamp, payload}` — `type` is one of
`device.online`, `device.heartbeat`, `telemetry`, `motion.detected`,
`incident.created`, `incident.updated` (matching Section 15 of the Phase 21 brief's
webhook event names, so the same event taxonomy will serve Phase 21.9 too).
`device.online` fires when a heartbeat arrives after a >120s gap (the same staleness
window `/health` already uses); `device.offline` is **not** detected this pass — that
needs a background sweep for heartbeats that stop arriving, not just a reaction to
one arriving, and is deferred (see Known gaps below).

**Verified by actually running it**, not just building: started the server, opened a
real SSE connection (`curl -N`), registered a device and posted a `motion.detected`
event from a separate request, and confirmed the connected stream received the
envelope in real time — the same "run it, don't just compile it" discipline Phase
21.5 established.

**Real bug found in the process, unrelated to SSE itself**: a stray
`Properties/launchSettings.json` (auto-generated by the .NET tooling at some point,
not intentionally created) was silently overriding `ASPNETCORE_URLS` during `dotnet
run`, sending the server to random ports instead of the requested one. Deleted and
gitignored — `backend/README.md`'s run instructions now use `dotnet run --urls
<url>` explicitly rather than relying on environment-variable behavior that turned
out to be overridden.

**Not implemented**: `device.offline` detection (needs a background timer sweeping
for stale devices, not just event-reactive — natural to build alongside Phase 21.9's
webhook dispatcher, which needs the same sweep), multi-instance pub/sub (this is
single-process in-memory; a real multi-instance deployment needs a shared broker —
explicitly out of scope per `docs/BACKEND.md`'s "avoid unnecessary infrastructure"
until there's a second instance to justify it).

## Phase 21.7 — Evidence Upload (complete)

**Closes the exact gap `docs/REMOTE_ACCESS.md` Section 4 identified during the Phase
21.1 audit**: "no existing path for the Gateway to pull an image off a node's SD card
on demand." Three-hop chain, each hop new:

1. **Node** (`EvidenceManager::imagePath(eventId)`, `lib/CarSentinelCommon/`) — resolves
   an already-captured event's image path on SD if it exists. Served over HTTP via a
   new `StatusPage` route, `GET /evidence?eventId=...` (raw response, same style as
   the existing `/stream` route, CORS-enabled like `/flash`) — `node_main.cpp`'s
   `serveEvidence()`.
2. **Gateway** (`gateway_main.cpp`'s `fetchAndUploadEvidence()`) — GETs the image from
   the originating node's IP (`DeviceRegistry`, already tracked), bounded to 300KB
   (comfortably above what this project's JPEG settings ever produce) so a malformed
   `Content-Length` can't exhaust the gateway's heap, then hands the bytes to
   `RemoteSyncManager::uploadEvidence()`. Wired into `assistedIncidentNotify()`
   (Phase 16) — every evidenced node's image is fetched and uploaded whenever the
   backend is enabled, run alongside (not gated by) the AI severity check that
   decides whether to *email* about the incident. `incidentToJson()` mirrors
   `IncidentCorrelator::persist()`'s on-disk shape so the backend's incident schema
   matches the local one (`docs/REMOTE_ACCESS.md` Section 4's "same shape, not a
   redesign").
3. **`RemoteBackend`/`HttpBackend`** gain `uploadEvidence()` — raw-bytes `POST`
   (`HTTPClient::POST(uint8_t*, size_t)`, not the JSON-string overload the other
   methods use), `Content-Type: image/jpeg`. **Not queued on failure** (unlike the
   JSON `send*` methods) — `BackendQueue`'s persisted-JSON-array-on-LittleFS design
   was never sized for binary blobs; a real design for durable evidence retry would
   probably re-fetch from the node later rather than buffer image bytes, and is
   deferred, documented in `RemoteSyncManager.h`.
4. **Backend** (`backend/`) — `EvidenceRecord` (metadata) + `EvidenceStorage`
   (`Services/`, local-disk stand-in for real object storage per `docs/BACKEND.md`
   Section 23, path components sanitized against traversal — "local doesn't mean
   trusted," extended to the Gateway↔Backend boundary). `POST
   /api/v1/incidents/{incidentId}/evidence`, `GET .../evidence` (metadata list), `GET
   /api/v1/evidence/{id}/file` (the actual bytes). Publishes `evidence.uploaded` on
   the Phase 21.6 SSE stream too.

**Verified byte-for-byte, not just building**: registered a device, created an
incident, uploaded a fake JPEG via the exact multipart-free raw-POST shape
`HttpBackend.cpp` uses, listed its metadata, downloaded it back, and diffed the
downloaded bytes against the original — identical.

**Not implemented**: evidence-policy filtering (`docs/BACKEND.md` Section 7's
`SyncPolicy` — `METADATA_ONLY`/`INCIDENT_ONLY`/etc. — every evidenced image is
uploaded whenever the backend is enabled at all, no filtering by policy yet); durable
retry for a failed evidence upload (documented gap above); an on-demand `/snapshot`
route (only already-captured evidence is retrievable, not a fresh live frame).

**Build status: firmware compiles clean, both environments** (node Flash 65.4%, +0.2%
from the new `/evidence` route — comfortable margin, no IRAM regression). **Backend
manually verified end-to-end** (upload → list → download → byte-diff), same "actually
run it" discipline as every other Phase 21 sub-phase since 21.5.

## Phase 21.8 — Remote Commands (complete)

**Backend → Gateway command flow, polling (not push)** — `RemoteSyncManager` already
polls for registration/heartbeat on its own timer; adding a persistent connection
just for commands would be new infrastructure for a use case a short poll interval
already serves well enough.

**Backend** (`backend/`): `CommandRecord` (`PENDING`/`DELIVERED`/`EXECUTED`/`FAILED`/
`EXPIRED`/`REJECTED`), three endpoints —
`POST /api/v1/devices/{id}/commands` (issue; gated by a shared `X-Admin-Key` header
against `Admin:ApiKey` config, a deliberate placeholder for real user auth per
Section 11's "never allow arbitrary unauthenticated remote commands" — **locked by
default**: an unset `Admin:ApiKey` refuses every issue attempt with 503 rather than
silently accepting unauthenticated commands), `GET .../commands/pending`
(device-credential authenticated, and the route's `{id}` must equal the
authenticated device's own ID — a gateway can only ever see/claim its own commands,
never another device's), `POST .../commands/{commandId}/result` (same per-device
check). Expired-but-still-`PENDING` commands are swept to `EXPIRED` on each poll.

**Firmware**: `RemoteBackend`/`HttpBackend` gain `pollCommands()`/
`reportCommandResult()`. `RemoteSyncManager` polls every 15s (more responsive than
the 60s heartbeat — a command's whole point is getting acted on promptly) once
registered, parses the returned array, and dispatches each to a registered
`CommandHandler` — a gateway that never registers one still polls and logs
"no handler registered" rather than silently never checking. `gateway_main.cpp`
registers `handleRemoteCommand()`, which wires up **one real command end to end**:
`SECURITY_MODE` (validates the mode string, calls the existing `applyModeChange()`,
which already broadcasts to every node — Phase 10). Every other command the Phase 21
brief's Section 20 lists (snapshot request, node restart, telemetry request, OTA
trigger, enable/disable) has an existing internal function it could map onto the same
way; deliberately not wired up this pass — adding another `else if` in
`handleRemoteCommand()` is how a future one gets added, not a redesign.

**Verified end-to-end** (backend running, curl only — no gateway hardware
available to actually receive a real `SECURITY_MODE` command): issued a command with
the admin key (401 without one/with the wrong one), polled it as the target device
(delivered once, empty on a second poll), reported a result, confirmed the command
history shows `EXECUTED` with the reported result payload, and confirmed a
different device's poll attempt against the same command is rejected (401). The
firmware side (`handleRemoteCommand()`'s `applyModeChange()` call,
`RemoteSyncManager`'s dispatch loop) is build-verified only — untestable without
real hardware polling a real backend.

**Build status: both environments compile clean** (gateway Flash 21.1%, node
unaffected — all Phase 21.8 code is gateway-only).

## Phase 21.9 — Webhooks (complete)

**External event integrations** — outbound HTTP push to third-party subscribers,
reusing the exact event taxonomy `EventBroadcaster`'s SSE stream already established
in Phase 21.6 (`device.heartbeat`, `device.online`, `telemetry`, `motion.detected`,
`incident.created`/`incident.updated`, `evidence.uploaded`, `command.issued`/
`command.executed`/`command.failed`).

**`EventBroadcaster`** gained a plain C# event, `OnEvent`, invoked at the end of
`Publish()` right after the existing SSE fan-out — `WebhookDispatcher` attaches to
it once at startup (`Program.cs`) rather than any endpoint code calling it directly,
so none of the four existing `Publish()` call sites in `IngestEndpoints`/
`CommandEndpoints` needed to change.

**`WebhookSubscription`** (`Url`, comma-separated `EventTypes` or `"*"`, a
server-generated plaintext `Secret`, `Enabled`) and **`WebhookDelivery`** (one row
per delivery attempt: `SubscriptionId`, `EventType`, `Success`, `StatusCode`,
`AttemptedAt`) — new models, new `DbSet`s. `Secret` is deliberately plaintext, not
hashed like `Device.CredentialHash`: this backend is the party *proving* authenticity
to the receiving webhook endpoint (computing an outgoing HMAC needs the plaintext
secret), the reverse of verifying an incoming device credential.

**`WebhookDispatcher`**: for every event, finds enabled subscriptions whose
`EventTypes` match (`"*"` or a literal match), and for each one POSTs the same JSON
envelope SSE clients receive, signed with `X-CarSentinel-Signature: sha256=<hex
HMAC-SHA256 over the raw body, keyed by the subscription's secret>` — the same
GitHub/Stripe-style signature convention a receiver can verify by recomputing it.
One retry (1s delay) on failure/non-2xx/timeout (10s timeout per attempt); every
attempt, success or failure, is logged to `WebhookDelivery`. Runs on its own
fire-and-forget `Task` per event (own DI scope) so `Publish()` itself stays
synchronous.

**`WebhookEndpoints`** (`/api/v1/webhooks`), admin-key gated exactly like
`CommandEndpoints`' issue route (`X-Admin-Key` vs. `Admin:ApiKey` config, locked by
default): `POST /` creates a subscription and returns the generated secret exactly
once (never returned again — the caller must store it), `GET /` lists subscriptions
(secret omitted), `DELETE /{id}` removes one, `GET /{id}/deliveries` returns recent
delivery attempts for debugging.

**Verified by actually running it**: started the backend with `Admin:ApiKey` set,
created a subscription pointed at the backend's own `GET /api/v1/health` endpoint
(no external webhook receiver available in this environment), then registered a
device and sent a heartbeat to trigger a real `device.heartbeat` publish. Confirmed
via `GET /{id}/deliveries`: two logged attempts (initial + the one retry), each a
real HTTP POST that reached `/api/v1/health` and got a real `405` back (that route
only accepts GET) — proving the dispatch, signature computation, and delivery
logging all actually executed, not just built. Also verified: unauthenticated
`POST /` → 401, list/delete work, and deleting the subscription stops further
delivery attempts.

**Build status: both environments unaffected** (all Phase 21.9 code is
backend-only, no firmware changes this sub-phase).

## Phase 21.10 — API Documentation (complete)

**`docs/API.md`** (new) — a standalone reference for every HTTP endpoint shipped
across 21.2–21.9, independent of the live Swagger UI (which already existed from
21.5 and documents the same surface interactively): auth conventions (device
credential vs. admin key vs. anonymous), and a worked `curl` example for every
endpoint — registration/heartbeat/telemetry/events/incidents/evidence upload, the
read/query endpoints, the SSE stream, remote commands, and webhooks — plus an
explicit "what's intentionally not here" section matching `backend/README.md`'s.

No new Swagger annotations were needed beyond what Phases 21.5–21.9 already added
incrementally (`WithTags`, the `DeviceCredential` security scheme, per-request
`ExcludeFromDescription()` on the raw-stream/raw-body routes that don't fit
OpenAPI's request/response model) — this sub-phase's job was writing the example-
driven reference doc, not restructuring the generated spec.

`README.md` and `backend/README.md` updated to point at it.

**Build status:** no code changes this sub-phase.

## Next Step

Phase 21.11 (End-to-End Testing) — document/execute whatever test matrix is
achievable without physical ESP32 hardware (every backend flow has already been
curl-verified sub-phase by sub-phase; this pass consolidates that into a single
test-matrix document and identifies exactly what remains hardware-gated), updating
`docs/TESTING.md`. This closes out Phase 21. Everything else in the project remains
independent of Phase 21 and can still be bench-tested in the meantime — Phase 21
stays purely additive.
