# CarSentinel — Security

**Status: consolidates existing, already-implemented security measures (with their
documented limitations, unchanged by this update) and records what the hybrid
ESP-NOW/Wi-Fi architecture (`docs/NETWORK.md`) still needs. This is a planning
document for the gaps section — nothing here has been implemented as part of this
update.**

## 1. Principle: local doesn't mean trusted

ESP-NOW communication is not treated as trusted just because it's local RF, and Wi-Fi
fallback communication is not treated as trusted just because it's on the same LAN as
the gateway. Every message is validated at the application layer regardless of which
transport carried it.

## 2. What's already implemented (ESP-NOW layer)

- **Message authentication**: every ESP-NOW packet is HMAC-SHA256 signed
  (`EspNowProtocol.h`), truncated to 8 bytes, over the full packet before the HMAC
  field. A packet that fails verification (wrong key, corrupted, or replayed under a
  different key) is rejected.
- **Replay protection (bounded)**: `PeerRegistry` tracks each known peer's last-seen
  sequence number for duplicate/replay detection (`EspNowManager` assigns a monotonic
  `sequenceNumber` to every outgoing message). Documented as "a Phase 5 scope limit,
  not a claim of strong replay resistance" in `PeerRegistry.h` — this remains an
  accurate, honest characterization, not something this update overstates.
- **Shared-key material** (`EspNowSecurity`): the HMAC key is loaded from
  `/config/espnow_psk.bin`, never transmitted over the air (only its HMAC output
  travels on the wire).

## 3. Known, already-documented limitation carried forward unchanged

**Every device ships with the same compiled-in default HMAC key** until a real
per-deployment key is generated and manually copied to every node's
`/config/espnow_psk.bin`. There is no key-distribution mechanism. This is called out
loudly in logs and in `EspNowSecurity.h` today, and remains true — this update doesn't
change or hide it.

## 4. What "no credentials in broadcast packets" means today

HELLO/HEARTBEAT broadcasts (device discovery) carry `nodeId`, `role`, `displayName`,
`ip` (Section 6's zero-code discovery payload) — device identity and network-reachable
metadata, not secrets. Wi-Fi credentials, the ESP-NOW HMAC key, and SMTP credentials
never appear in any ESP-NOW payload, broadcast or unicast.

## 5. OTA verification

Covered in detail in `docs/OTA.md`. Summary: MD5-verified streaming writes
(`Update.setMD5()`), a hardware-profile match check before accepting an update, and
`WiFiClientSecure::setInsecure()` for the HTTPS transport itself (documented TLS
limitation — no certificate validation) — unchanged by this networking update. OTA
control/status (the small "check/trigger" messages) is planned to move to ESP-NOW per
`docs/NETWORK.md` Section 2's traffic classification; the image transfer itself stays
on Wi-Fi.

## 6. Gaps this architecture update identifies (not yet implemented)

1. **No gateway identity validation.** A node currently accepts the first device that
   HELLOs/HEARTBEATs claiming role `GATEWAY` (via `DeviceRegistry::upsertFromDiscovery()`
   on the gateway side, and `EspNowManager::findGatewayMac()` on the node side) — there's
   no cryptographic proof that a device claiming to be the gateway actually is the
   paired gateway, beyond the shared HMAC key already covering message integrity.
   `docs/NETWORK.md` Section 10's discovery/pairing sequence (identify → validate →
   authenticate → register) needs a real gateway-identity-validation step, not just
   "the HMAC matched."
2. **No formal pairing handshake distinct from ordinary discovery.** Today, a new
   node's HELLO is indistinguishable from a routine heartbeat as far as the gateway's
   registry is concerned — "pairing" is really just "the first time we heard from this
   nodeId." A real pairing step (ideally tied into `docs/PROVISIONING.md`'s flow) would
   give a device an explicit, user-confirmed "this is my node" moment, closing the gap
   where any device broadcasting a plausible HELLO with the right HMAC key gets
   auto-registered.
3. **No key-distribution mechanism** (Section 3) — the single largest ESP-NOW security
   gap, unchanged by this update, still requiring a real design (most likely tied to
   provisioning: a "network key" entered once for the whole deployment, distributed via
   BLE at pairing time rather than compiled in).
4. **Wi-Fi fallback's security posture isn't yet specified.** When a node falls back to
   Wi-Fi to reach the gateway (`docs/NETWORK.md` Section 3), what authenticates that
   connection? Today's `DashboardServer`/`StatusPage` HTTP surfaces have no auth at all
   (documented posture: "trusted local network only" — see `DashboardServer.h`'s
   existing comments). Whether node → gateway-over-Wi-Fi fallback traffic needs its own
   authentication (distinct from the ESP-NOW HMAC scheme) is an open question for the
   implementation phase, not decided by this document.
