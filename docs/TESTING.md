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
