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

**Status: items 1 and 3 implemented; items 2 and 4 remain open** — see
`docs/IMPLEMENTATION_PLAN.md`'s Architecture Update entry for exact commit-level
detail.

1. ~~Wi-Fi credentials are effectively mandatory today~~ **Fixed.** ESP-NOW now starts
   unconditionally regardless of Wi-Fi/provisioning state, and `NetworkConfig`'s new
   `wifiFallbackEnabled` flag (item 3) lets a device settle into "ESP-NOW only, never
   prompted for Wi-Fi" — via `WIFIFALLBACK OFF` (serial, both roles, or the gateway
   dashboard's Settings page).
2. **`ProvisioningPortal`'s form still always shows a Wi-Fi SSID/password field**
   (`ProvisioningPortal.cpp`'s `handleRoot()`), with no way to skip it *during
   first-time provisioning itself* and proceed ESP-NOW-only. Still open — item 1's fix
   means a device can be switched to ESP-NOW-only immediately *after* provisioning,
   just not opted out of the Wi-Fi field within the provisioning form.
3. ~~No explicit "Wi-Fi fallback enabled" toggle exists~~ **Implemented** —
   `NetworkConfig.wifiFallbackEnabled` (schema v2→v3), its own field, independent of
   whether credentials happen to be saved.
4. **No explicit pairing/authentication step is triggered by provisioning.** Still
   open — a device joins the mesh implicitly the first time its HELLO is heard and
   `DeviceRegistry::upsertFromDiscovery()` auto-creates an entry (Phase 6, "zero-code
   discovery"). That auto-discovery convenience is a real, intentional feature worth
   keeping for the common case — but per `docs/SECURITY.md`, it currently comes with no
   gateway identity validation, which a real pairing step (Section 3 above) would add.
