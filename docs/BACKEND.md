# CarSentinel — Remote Backend

**Status: Phase 21.1 architecture audit. Nothing in this document has been
implemented — no backend code, no Gateway sync code, no repository scaffolding.**
This records what exists today (nothing, confirmed by inspection — see below), the
target design, and exactly what needs building. See
`docs/IMPLEMENTATION_PLAN.md`'s Phase 21 entry for the sub-phase breakdown.

## 1. What exists today: nothing

Confirmed by inspecting the repository root and `firmware/`: there is no backend
project (no `.csproj`/`.sln`, no `server/`, no `backend/`, no database, no API code
anywhere in the tree). The Gateway's only HTTP surface today is
`DashboardServer` (`lib/CarSentinelGateway/`) — a local-LAN-only page + JSON API
(`/api/status`, `/api/devices`, `/api/incidents`, `/api/settings/*`) with no
authentication, serving the browser directly from the ESP32-S3. There is no outbound
HTTP client code on the Gateway today except `OtaManager` (fetches a firmware image
from a configured URL) and `EmailProvider` (SMTP). Nothing in the firmware talks to
any remote API, and no remote API exists to talk to.

This means Phase 21 is greenfield for the backend itself, but not for the Gateway-side
abstractions it plugs into — those already exist in a form worth extending rather than
replacing (Section 3).

## 2. Existing patterns this phase should reuse, not compete with

The codebase already has two examples of the exact "swappable provider" shape Section
6 of the Phase 21 brief asks for:

- **`AIThreatFramework`** (`lib/CarSentinelGateway/AIThreatFramework.h`) — a
  `ThreatAnalyzer` function-pointer slot, `setAnalyzer()` to swap implementations,
  one concrete implementation shipped today (`heuristicAnalyzer`).
- **`NotificationManager`/`EmailProvider`** — a notification-sending abstraction with
  one concrete transport (SMTP) behind it.

`RemoteBackend`/`BackendProvider` (Section 6 below) should follow this same shape:
an interface + a registered active implementation, not a new architectural pattern.

The existing **`Transport`/`EspNowTransport`** split (`docs/NETWORK.md` Section 5) is
the other precedent worth following structurally: a low-level transport interface
(`sendTo`, `broadcastMessage`, etc.) with a manager above it
(`EspNowManager`/`TransportManager`) that owns policy (retries, state, generic
send ops). `RemoteSyncManager` sitting above a `RemoteBackend` interface mirrors this
exactly.

## 3. Target abstraction

```text
                     Gateway application code
                    (gateway_main.cpp, and
                     anything that wants to
                     report a status/event)
                            │
                    RemoteSyncManager
              (connection state, auth, retry/
               backoff, offline queue, sync
               policy per data category —
               Section 7)
                            │
                    RemoteBackend (interface)
                     ┌──────┴───────┐
                     ↓              ↓
              HttpBackend      (future: MQTT,
              (REST, the       CarSentinelCloud-
              only impl         specific backend,
              this phase        a custom self-
              needs)            hosted variant)
```

- **`RemoteBackend`** — interface: `begin()`, `isConnected()`, `sendTelemetry(...)`,
  `sendEvent(...)`, `sendIncident(...)`, `uploadEvidence(...)`, `pollCommands(...)`,
  `sendHeartbeat(...)`. Mirrors `Transport`'s shape (an interface application code
  never calls directly).
- **`RemoteSyncManager`** — owns everything Section 6 of the brief lists: connection
  state (its own small state machine, analogous to `TransportManager`'s but for
  Gateway↔Internet rather than Node↔Gateway — `LOCAL_ONLY` / `CONNECTING` /
  `CONNECTED` / `AUTH_FAILED` / `RETRY_BACKOFF`; `AUTH_FAILED` is defined but not yet
  reachable — see Phase 21.2's own gap note), and drives the persisted queue
  (**implemented**, Phase 21.3 — `BackendQueue`, bounded/LittleFS-persisted/
  exponential-backoff, the same structural echo of `OfflineQueue` this document
  originally proposed). This is the single place Gateway code calls into — never
  scatter `HttpClient` calls through `gateway_main.cpp`/`IncidentCorrelator`/etc.
  directly, the same discipline already followed for ESP-NOW (`docs/NETWORK.md`
  Section 5's "never call `EspNowManager::sendMessage()` from just anywhere" — well,
  today it still is called from several places pre-`TransportManager`; the target is
  for backend calls to not repeat that mistake from day one). Authentication,
  registration, and per-category sync-policy filtering (Section 7) remain
  unimplemented — Phase 21.4+.
- **`HttpBackend`** — the one concrete implementation this phase actually needs:
  REST over HTTPS (`WiFiClientSecure` + `HTTPClient`, already linked on the gateway
  build — see `docs/OTA.md`, same libraries `OtaManager` already uses). `mode` config
  (`LOCAL_ONLY` / `CAR_SENTINEL_CLOUD` / `CUSTOM_SERVER` — Section 4's requirement)
  just changes `baseUrl`/auth details fed into the same `HttpBackend`, not a different
  class, unless `CAR_SENTINEL_CLOUD` ends up needing protocol differences later.

## 4. Configuration model

Extends the existing versioned-JSON-per-concern pattern (`NetworkConfig`,
`EmailConfig`, `CapabilitiesConfig` — one file per concern, schema-versioned,
migration seam). A new `BackendConfig` (`/config/backend.json`), gateway-only (nodes
never talk to the backend directly — Section 3 of the Phase 21 brief, "Remote Backend
Is Gateway-Facing," matches this project's existing gateway-is-the-Internet-boundary
posture from `docs/NETWORK.md` Section 8 exactly):

```text
BackendConfigData {
  schemaVersion
  enabled            // false = LOCAL_ONLY, matches "no Internet needed to boot"
  mode               // LOCAL_ONLY | CAR_SENTINEL_CLOUD | CUSTOM_SERVER
  baseUrl
  deviceId           // this gateway's stable backend-facing identity (Section 9)
  tenantId           // optional, for multi-tenant readiness (Section 27)
  authMethod         // e.g. DEVICE_CREDENTIAL
  credentialRef       // never the raw secret inline in a way that gets logged —
                      // same "never in logs" posture as EmailConfig's password today
  tlsVerify          // documented gap already exists for OTA/email
  syncPolicy: {
    telemetryIntervalMs, gpsIntervalMs,
    events: OFF|METADATA_ONLY|SELECTED|FULL,
    incidents: OFF|METADATA_ONLY|SELECTED|FULL,
    evidence: LOCAL_ONLY|INCIDENT_ONLY|HIGH_PRIORITY_ONLY|ALL_EVENTS
  }
}
```

Not a final schema (per the brief's own instruction) — the exact fields get decided
during Phase 21.2, this is the shape to design against.

## 5. Data flow

```text
Node --ESP-NOW--> Gateway --(existing local processing:
                              DeviceRegistry, IncidentCorrelator,
                              AIThreatFramework)--> RemoteSyncManager
                                                          │
                                                     HTTPS (HttpBackend)
                                                          ↓
                                                  Remote Backend (Phase 21.5+)
```

Nodes never gain their own backend connection — this is already true today (nodes
have no `HTTPClient` usage at all except the gateway-only-scoped `OtaManager`, per
`docs/OTA.md`) and stays true. The Gateway is the only device that ever needs Internet
access, exactly matching `docs/NETWORK.md` Section 8's existing "gateway for
Internet, ESP-NOW for the mesh" split — Phase 21 extends what the gateway *does* with
its Internet connection, it doesn't change who has one.

## 6. Local-first guarantee (unchanged, extended)

Every existing local-first guarantee (`docs/NETWORK.md` Sections 3–4) stays exactly as
built: ESP-NOW primary, Wi-Fi fallback, standalone mandatory, `TransportManager` +
`OfflineQueue` for Node↔Gateway. This phase adds one more tier on top, not a
replacement:

```text
Node -ESP-NOW/WiFi-> Gateway -HTTPS-> Backend
        (existing,                 (new — must never
         unaffected)                be a dependency for
                                     the tier to its left)
```

A `RemoteSyncManager` connection failure must never block or degrade
`IncidentCorrelator`, `AIThreatFramework`, `NotificationManager` (email), the local
dashboard, or anything else already working — it only stops backend sync, queuing
whatever would have been sent (Section 8, extending `OfflineQueue`'s existing
bounded/persisted pattern rather than building a second, incompatible queue).

## 7. Exact files (recommendation for Phase 21.2+, not created yet)

New (all `lib/CarSentinelGateway/`, gateway-only — same reasoning as
`DashboardServer`/`AIThreatFramework`, nodes never need this):
- `RemoteBackend.h` (interface)
- `HttpBackend.h/.cpp`
- `RemoteSyncManager.h/.cpp`
- `BackendConfig.h/.cpp` (`lib/CarSentinelCommon/` instead — same tier as
  `NetworkConfig`/`EmailConfig`, even though only the gateway ever populates it,
  for consistency with where every other `*Config` class already lives)

Modified: `gateway_main.cpp` (wires `RemoteSyncManager::begin()`/`loop()`, feeds it
events from the same places that already call `IncidentCorrelator`/
`NotificationManager`), `DashboardServer.cpp` (Settings page gains a Backend section,
same pattern as the Wi-Fi/SMTP sections already there).

Nothing on the node side changes for this phase.
