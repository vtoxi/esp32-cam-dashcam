# CarSentinel — Provisioning

**Status: architecture/design document, describing the target flow under the hybrid
ESP-NOW/Wi-Fi model (`docs/NETWORK.md`). See "Gap vs. current implementation" below
for exactly what the current BLE/AP portal (Phase 2) does differently today.**

## 1. Principle: Wi-Fi credentials are requested only when needed

Provisioning's job is identity + role assignment + (optionally) Wi-Fi fallback
credentials + (implicitly, via ESP-NOW discovery) pairing with a gateway. Under
`docs/NETWORK.md`'s model, a node that only ever needs ESP-NOW should never be asked
for a Wi-Fi SSID/password:

```text
New Node
   ↓
BLE provisioning
   ↓
Select device name / role / hardware profile
   ↓
Pair with Gateway (over ESP-NOW)
   ↓
ESP-NOW becomes primary transport
```

Wi-Fi credentials are requested/stored only if:

```text
Wi-Fi fallback = enabled  (docs/NETWORK.md Section 6's per-device config)
```

## 2. Provisioning surfaces

Two provisioning surfaces already exist and both stay:

- **BLE** (`BLEProvisioning`) — the preferred surface under this architecture, since it
  requires no Wi-Fi radio state at all and works even for an ESP-NOW-only device.
- **Temporary Wi-Fi AP** (`ProvisioningPortal`, `CarSentinel-<nodeId>` /
  `192.168.4.1`) — remains available as a fallback to BLE (e.g. for a client device
  with no BLE support), not the primary path.

Neither surface should be entered unconditionally just because Wi-Fi credentials are
absent — see the gap note below, since that's exactly what happens today.

## 3. What provisioning sets

- Device identity: display name, role, hardware profile (`DeviceConfig`) — always
  requested, independent of transport configuration.
- Wi-Fi fallback credentials (`NetworkConfig`) — requested only when Wi-Fi fallback is
  enabled for this device (a toggle, not inferred from whether the user happened to
  fill in the SSID field). Already supports multiple remembered networks
  (`NetworkConfig::addNetwork()` — see `docs/IMPLEMENTATION_PLAN.md`'s "Multiple
  remembered Wi-Fi networks" entry); provisioning should call that same API rather than
  only ever setting one primary network, which it already does today.
- Gateway pairing — under this architecture, provisioning should trigger (or
  hand off to) the ESP-NOW discovery/pairing handshake described in `docs/NETWORK.md`
  Section 5 and `docs/SECURITY.md`'s pairing section, rather than pairing happening
  implicitly via the first HELLO/HEARTBEAT a device happens to hear, the way it does
  today.

## 4. Gap vs. current implementation

1. **Wi-Fi credentials are effectively mandatory today**, not opt-in.
   `node_main.cpp`'s `setup()` unconditionally calls `enterProvisioningMode()` (which
   starts both `ProvisioningPortal` and `BLEProvisioning`) whenever
   `NetworkConfig::hasCredentials()` is false, and — per `docs/NETWORK.md`'s Section 9
   gap note — ESP-NOW does not start until provisioning ends. There is no "Wi-Fi
   fallback disabled, ESP-NOW only" path a device can settle into without ever seeing a
   Wi-Fi credentials prompt.
2. **`ProvisioningPortal`'s form always shows a Wi-Fi SSID/password field**
   (`ProvisioningPortal.cpp`'s `handleRoot()`) with no way to skip it and proceed
   ESP-NOW-only.
3. **No explicit "Wi-Fi fallback enabled" toggle exists in `NetworkConfig`** — today
   it's implicit (has credentials or doesn't). `docs/NETWORK.md` Section 6's config
   model needs this as its own field, separate from "are any credentials currently
   saved," so a device can have saved Wi-Fi networks (for fallback) while still
   defaulting to ESP-NOW as primary and never blocking boot on Wi-Fi being reachable.
4. **No explicit pairing/authentication step is triggered by provisioning** — a device
   joins the mesh implicitly the first time its HELLO is heard and
   `DeviceRegistry::upsertFromDiscovery()` auto-creates an entry (Phase 6, "zero-code
   discovery"). That auto-discovery convenience is a real, intentional feature worth
   keeping for the common case — but per `docs/SECURITY.md`, it currently comes with no
   gateway identity validation, which a real pairing step (Section 3 above) would add.

As with `docs/NETWORK.md`, this is a planning document — none of the above has been
changed as part of this update.
