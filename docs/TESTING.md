# CarSentinel — Testing

**Status: consolidates the per-phase bench-test checklist already tracked in
`docs/IMPLEMENTATION_PLAN.md` (each phase's "Not yet bench-tested" / "Next Step" notes)
with the new failure-scenario matrix required by the hybrid ESP-NOW/Wi-Fi networking
architecture (`docs/NETWORK.md`). This document doesn't replace the phase-by-phase
checklist — it adds the transport-specific matrix that checklist didn't cover.**

## 1. Networking failure-scenario matrix (A–F)

Mirrors `docs/NETWORK.md` Section 10. Each scenario needs an explicit bench test once
the transport architecture update (`docs/NETWORK.md`'s "Gap vs. current
implementation") is actually implemented — none of these are testable against the
current code today, since ESP-NOW is currently gated behind Wi-Fi/provisioning rather
than tried first.

| # | Scenario | Setup | Expected result | Verifies |
|---|---|---|---|---|
| A | Gateway + Wi-Fi both available | Gateway on Wi-Fi, node with both ESP-NOW and Wi-Fi fallback configured | Node uses ESP-NOW, not Wi-Fi, even though both would work | ESP-NOW is genuinely preferred, not just "tried first and then never re-checked" |
| B | Router unavailable, gateway still running | Router powered off, gateway and node both still powered | Node ↔ gateway over ESP-NOW is unaffected | ESP-NOW doesn't implicitly depend on the router being up |
| C | ESP-NOW temporarily unavailable, gateway reachable via Wi-Fi | Node's ESP-NOW radio disabled/jammed (or gateway ESP-NOW disabled), both on same Wi-Fi | Node falls back to Wi-Fi and reaches the gateway's own IP | Wi-Fi fallback engages correctly and doesn't try to reach the Internet |
| D | Gateway completely unavailable | Gateway powered off | Node continues local motion/capture/evidence/DHT/IMU; events queue | Standalone mode — Section 4 of `docs/NETWORK.md` |
| E | Gateway returns after D | Power the gateway back on | Node's standalone-queued events sync automatically once ESP-NOW reconnects | The queue (currently unimplemented — see `docs/NETWORK.md` Section 9, item 3) actually delivers, not just that reconnection happens |
| F | Node in Wi-Fi fallback, ESP-NOW gateway returns | Start from scenario C's state, then re-enable ESP-NOW | Node switches back to ESP-NOW automatically (optionally dropping Wi-Fi) | The system never permanently commits to the fallback transport |

## 2. Existing per-phase bench-test checklist (unchanged, summarized)

`docs/IMPLEMENTATION_PLAN.md` already tracks, per phase, what's been confirmed on real
hardware vs. "compiles clean, not yet bench-tested." As of this update:

- **Confirmed on real hardware**: Phases 1–3 (pending full verification), 4 (partial),
  5 (ESP-NOW discovery), 6 (zero-code device discovery, rename-sync), 8 (GPS boot/
  `NO_FIX` reporting), 10 (`PARKED (auto)` reporting).
- **Compiles clean, not yet bench-tested**: Phases 7, 9, 11, 12, 13, 14, 15, 16, 17,
  18, 19, plus the multi-WiFi/settings-page and live-stream/flash-control work done
  after Phase 20.
- **Phase 20** is a planning checklist with no firmware component — see
  `docs/wiring/VEHICLE_INSTALLATION.md`.

This document doesn't duplicate every phase's individual checklist — see
`docs/IMPLEMENTATION_PLAN.md`'s own "Next Step" sections for those. What's new here is
Section 1's matrix, which cuts across phases (it exercises Phase 5's ESP-NOW, Phase 2's
provisioning/Wi-Fi, and Phase 4/11's local capture pipeline together) and didn't have a
home until this networking architecture update.

## 3. What "done" looks like for the networking architecture update

Once `docs/NETWORK.md`'s gap items are implemented, this update is bench-verifiable
when:

1. A brand-new node, given no Wi-Fi credentials at all, pairs with a gateway and
   operates fully over ESP-NOW — no provisioning prompt for Wi-Fi ever appears unless
   Wi-Fi fallback is explicitly enabled for that device.
2. All six scenarios in Section 1's matrix pass.
3. `WIFILIST`/saved-network behavior (already implemented,
   `docs/IMPLEMENTATION_PLAN.md`'s "Multiple remembered Wi-Fi networks" entry) continues
   working unchanged — this update must not regress it.

## 4. Phase 21 — Remote Backend end-to-end test matrix (Phase 21.11)

Every backend flow below was verified by actually running `backend/` and issuing
real HTTP requests (not just `dotnet build`), sub-phase by sub-phase, as each one
shipped (see `docs/IMPLEMENTATION_PLAN.md`'s Phase 21.5–21.9 write-ups for the full
detail behind each row). This section consolidates those into one matrix and
separates "verified" from "hardware-gated" — the backend side of every flow has real
coverage; the firmware side of most of them does not, because no physical gateway has
been available to run against a live backend instance during this project's
development.

| # | Flow | Backend-side (curl) | Firmware-side (real hardware) |
|---|---|---|---|
| 1 | Fresh device registration (`POST /register`) | Verified — returns `deviceId`+`credential`, stored hashed | Not tested — `RemoteSyncManager::attemptRegistration()` build-verified only |
| 2 | Re-registration with existing credential | Verified — confirms match, doesn't reissue credential; wrong credential → 401 | Not tested |
| 3 | Heartbeat + `device.online`/`device.heartbeat` events | Verified — gap-based online detection, both events observed on the SSE stream | Not tested |
| 4 | Telemetry ingestion | Verified — stored, `telemetry` event published | Not tested |
| 5 | Event ingestion (`motion.detected`) | Verified | Not tested |
| 6 | Incident upsert by `incidentId` (create then update) | Verified — posted the same `incidentId` twice, confirmed one row, `firstReceivedAt` preserved, `incident.created` then `incident.updated` published | Not tested |
| 7 | Evidence upload (raw JPEG, byte-for-byte) | Verified — uploaded a fake JPEG, listed metadata, downloaded, `diff`'d identical to the original | Not tested — `fetchAndUploadEvidence()`'s node→gateway HTTP GET and gateway→backend raw POST are both build-verified only |
| 8 | SSE real-time stream | Verified — `curl -N`'d the stream in the background, confirmed a live envelope arrived for a real event | N/A (browser/external-client-facing, no firmware involvement) |
| 9 | Remote command issue → poll → result → history | Verified — issued with/without/wrong admin key (401/200/401), polled as the target device (delivered once, empty second poll), reported a result, confirmed history shows `EXECUTED`, confirmed a different device's poll is rejected (401) | Not tested — `handleRemoteCommand()`'s `SECURITY_MODE` dispatch and `RemoteSyncManager`'s 15s poll loop are build-verified only |
| 10 | Webhook subscription CRUD + delivery | Verified — created a subscription pointed at the backend's own `/health`, triggered a real heartbeat, confirmed two logged delivery attempts (initial + retry) with real HTTP status codes and a correctly computed HMAC signature | N/A (backend-only feature, no firmware involvement) |

**What "done" looks like for Phase 21.11 specifically**: every row above with a
firmware-side "Not tested" needs a real gateway, provisioned with `BACKENDCONFIG
CUSTOM_SERVER <backend URL>`, running against a live `backend/` instance, to move
from "build-verified" to "bench-verified." That hardware pairing hasn't been
available during this development window — this matrix exists so that gap is
explicit and actionable rather than silently assumed away. Nothing about the backend
implementation itself is blocked on it; the backend's own correctness is independently
covered by the curl-based verification in the middle column.

This closes out Phase 21's own sub-phase checklist (21.1–21.11). The project's
overall "not yet bench-tested on real hardware" gap — true for most phases, not
specific to Phase 21 — remains tracked the same way it always has: per-phase in
`docs/IMPLEMENTATION_PLAN.md`'s status table.
