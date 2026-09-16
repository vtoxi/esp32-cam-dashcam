# Vehicle Installation (Phase 20)

**Status: Planning checklist only — no physical install has happened yet.** This is
not a wiring reference like the other `docs/wiring/` files (there's no new hardware
module here); it's the sequence Phase 20 needs once every other phase is bench-verified
on the actual Peugeot 2008. Nothing in this file has been executed or confirmed against
the real vehicle.

## Prerequisites (must be true before starting)

- Every phase's "Not yet bench-tested" items in `docs/IMPLEMENTATION_PLAN.md` are
  resolved on the bench first — an install is the wrong place to debug firmware.
- A real ignition-sense circuit (Phase 18) has been built and tested on the bench
  against a simulated 12V source before it ever sees the vehicle's actual wiring —
  `docs/wiring/POWER.md` is explicit that no GPIO connects to vehicle 12V directly.
- A decision on permanent power: the bench powerbank (`docs/wiring/POWER.md`) is not a
  permanent install solution. A vehicle install needs a proper regulated 12V-to-5V
  buck converter per node, fused, on a switched (not always-hot) circuit unless
  Phase 17's low-power PARKED mode's actual current draw has been measured and judged
  acceptable for a permanently-hot connection — not yet done (Phase 17's own "known
  limitations").

## Planned sequence

1. **Mounting locations** — gateway (ESP32-S3) somewhere central with clear line of
   sight (RF) to every camera node; camera nodes per the coverage the Peugeot 2008
   prototype actually needs (not yet decided — depends on which blind spots/entry
   points matter most for this vehicle).
2. **Power taps** — one switched (ignition-on) and, if a permanently-monitoring PARKED
   mode is wanted, one always-hot circuit per the decision above. Fused at the tap,
   not just at the node.
3. **Ignition-sense tap** — a single ignition-switched signal wire (not a power feed)
   into the voltage-divider/optocoupler circuit feeding one node's `ignitionGpio`
   (only one device needs this; it broadcasts `SET_MODE` to the rest — see Phase 18).
4. **GPS antenna placement** — needs sky visibility; Phase 8's fix-acquisition testing
   hasn't happened yet, so antenna placement should be validated on the bench (or with
   the module simply set on a dashboard/windshield area with the vehicle outside)
   before committing to a permanent mounting position.
5. **Camera aiming and SD card access** — each node's SD card should stay physically
   reachable without fully removing the node, for evidence retrieval until OTA (Phase
   14) or a remote-download API exists (neither ships one yet — OTA is gateway-only,
   and evidence images were never intended to leave a node except via its own SD card
   per `IncidentCorrelator.h`'s design).
6. **Post-install verification** — repeat the Phase 13/14/15/16/17/18/19 bench checks
   (`docs/IMPLEMENTATION_PLAN.md`'s "Next Step" after Phase 18) with the system
   actually running in the vehicle: mode auto-transitions on real ignition-on/off and
   real driving, dashboard reachable over the vehicle's own Wi-Fi, incidents/evidence
   captured correctly with the vehicle parked in a realistic location.

## Explicitly out of scope for this install

- OBD-II/CAN bus integration (Phase 18's own limitation — no confirmed harness/PID
  knowledge yet).
- Any permanent modification to the vehicle's factory wiring beyond the fused taps
  described above.
