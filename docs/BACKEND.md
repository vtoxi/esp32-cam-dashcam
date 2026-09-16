# CarSentinel — Remote Backend

**Status: Phase 21.1–21.5 complete.** A real reference backend now exists
(`backend/` — ASP.NET Core 8, see `backend/README.md`), manually verified end-to-end
(register → heartbeat → telemetry → incident upsert → query → auth rejection). This
document's design sections below are now a description of what was built, not just a
plan — see `docs/IMPLEMENTATION_PLAN.md`'s Phase 21 entries for the sub-phase-by-
sub-phase history and exactly what's still open (21.6 onward).

## 1. What existed before Phase 21.5 (historical — now superseded)

Before Phase 21.5, there was no backend project anywhere in the tree, confirmed by
inspection. The Gateway's only HTTP surface was `DashboardServer`
(`lib/CarSentinelGateway/`) — a local-LAN-only page + JSON API (`/api/status`,
`/api/devices`, `/api/incidents`, `/api/settings/*`) with no authentication. This
section is kept for the record; `backend/` now exists and implements the target
design described below.

The Gateway-side abstractions this backend plugs into (`RemoteSyncManager`,
`RemoteBackend`, `HttpBackend`, `BackendQueue`, `BackendConfig`) were built in Phases
21.2–21.4 and already existed before the server did — see Section 3.

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

## 7. Exact files (Phase 21.2–21.5, all created)

Gateway (`lib/CarSentinelGateway/`, gateway-only — same reasoning as
`DashboardServer`/`AIThreatFramework`, nodes never need this):
`RemoteBackend.h`, `HttpBackend.h/.cpp`, `RemoteSyncManager.h/.cpp`,
`BackendQueue.h/.cpp`. `BackendConfig.h/.cpp` lives in `lib/CarSentinelCommon/`
instead — same tier as `NetworkConfig`/`EmailConfig`, even though only the gateway
ever populates it, for consistency with where every other `*Config` class already
lives. `gateway_main.cpp` wires `RemoteSyncManager::begin()`/`loop()` and the
`BACKENDCONFIG`/`BACKENDENABLE`/`BACKENDDISABLE`/`BACKENDSTATUS` serial commands;
`DashboardServer.cpp` gained a Backend section on the Settings page. Nothing on the
node side changed across any Phase 21 sub-phase so far.

Backend (`backend/src/CarSentinel.Backend/`, a separate .NET solution, not embedded
firmware): `Program.cs`, `Data/AppDbContext.cs`, `Models/{Device,TelemetryRecord,
EventRecord,IncidentRecord}.cs`, `Auth/DeviceCredentialAuthenticationHandler.cs`,
`Endpoints/{IngestEndpoints,QueryEndpoints}.cs`. See `backend/README.md` for how to
run it.
