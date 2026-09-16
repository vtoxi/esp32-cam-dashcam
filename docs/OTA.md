# CarSentinel — OTA (Over-the-Air Updates)

**Status: consolidates Phase 14's already-implemented, already-bench-verified-by-build
(not yet physically bench-tested) OTA system, and records how it maps onto the hybrid
ESP-NOW/Wi-Fi transport model (`docs/NETWORK.md`). The OTA mechanism itself is
unchanged by this networking update — see `docs/IMPLEMENTATION_PLAN.md` Phase 14 for
the full implementation history.**

## 1. What exists today

- `OtaManager` (`lib/CarSentinelCommon/OtaManager.h/.cpp`) — MANUAL-trigger only
  (`OTACHECK <manifestUrl>` / `OTAUPDATE <manifestUrl>` serial commands, also reachable
  from the gateway dashboard). No AUTO/STAGED mode — a human must trigger it, never a
  timer or an automatic reaction to a version check.
- `fetchManifest(url)` parses `{version, url, md5, hardwareProfile}` over `HTTPClient`.
  `isUpdateNeeded()` refuses a mismatched `hardwareProfile` (never flash the wrong
  board's binary) and a no-op version match.
- `performUpdate()` streams the image directly into `Update` (no full-image buffering
  — the ESP32 doesn't have RAM for that), verifies MD5 via `Update.setMD5()`, and
  restarts on success. The device is left unchanged on any failure along the way.
- **Gateway-only in practice.** Classic ESP32 camera nodes cannot fit `HTTPClient`/
  `Update.h` (whose flash-write path needs IRAM-resident code) alongside their
  existing WiFi/BLE/camera/SD footprint — a real, measured link failure during Phase
  14, not a guess (see `docs/IMPLEMENTATION_PLAN.md` Phase 14 for the exact trim
  history). `OtaManager`'s node build is a stub that reports "not supported on this
  hardware" rather than linking OTA machinery it can't fit.
- TLS uses `WiFiClientSecure::setInsecure()` — no certificate validation, a documented
  limitation, not hidden (see `docs/SECURITY.md` Section 5).

## 2. How OTA maps onto the hybrid transport model

`docs/NETWORK.md` Section 2 classifies traffic into ESP-NOW (small, control/event) and
Wi-Fi (large/continuous data). OTA has both halves:

- **Image transfer** — inherently large binary data. Stays on Wi-Fi/HTTP(S)
  unconditionally; ESP-NOW's payload size and design intent both rule it out. This
  doesn't change.
- **OTA control/status** — "is an update available," "start the update," "update
  succeeded/failed," progress reporting — are small, infrequent messages that fit
  ESP-NOW's traffic profile. Today these only exist as direct serial commands and
  gateway-dashboard HTTP calls; under the target architecture, a node without its own
  Wi-Fi fallback enabled should still be able to receive an OTA "check"/"trigger"
  command from the gateway over ESP-NOW, and report status back the same way — even
  though it fetches the actual image itself.

**This only matters where OTA is actually reachable.** Since node-side OTA is
currently unimplemented (Section 1's IRAM constraint) and node OTA remains out of
scope until node-side headroom improves, the ESP-NOW-control-channel idea above is
purely forward-looking for the gateway today (which already has direct Wi-Fi/HTTP
access, so it doesn't need an ESP-NOW control path to reach itself) and becomes
relevant only if/when node OTA is revisited.

## 3. What this update does not change

No code in `OtaManager`, the node's IRAM-driven OTA scoping decision, or the MD5/
hardware-profile safety checks changes as part of this document. This file exists so
OTA's place in the transport model is written down consistently with
`docs/NETWORK.md`/`docs/SECURITY.md`, not because OTA itself needed rework.
