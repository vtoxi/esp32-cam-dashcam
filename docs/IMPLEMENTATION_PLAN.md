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
| 5 | ESP-NOW | Compiles clean (both envs) — pending physical two-device bench test |
| 6 | Dynamic Node Management | Compiles clean (both envs) — pending physical two-device bench test |
| 7 | Multi-Camera Correlation | Compiles clean (both envs) — pending multi-device bench test |
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

## Next Step

Flash all firmware and bench-test Phase 7 with at least two camera nodes plus the
gateway: trigger motion on one node's RCWL, confirm the other node's `CAPTURE_REQUEST`
handling fires (check its own log for "Synchronized capture..." and its SD card for a
new `RELATED_CAPTURE` event), and confirm the gateway logs a single consolidated
`INCIDENT-...` summary naming both nodes. Once that's solid, continue with Phase 8
(GPS).
