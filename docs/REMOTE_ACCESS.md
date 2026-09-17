# CarSentinel — Remote Access (API, Real-Time, Webhooks, Dashboards)

**Status: Phase 21.1 architecture audit. Nothing in this document has been
implemented.** Complements `docs/BACKEND.md` (sync engine/data flow) with the
client-facing surface: the REST API, real-time updates, webhooks, and where UIs live.

## 1. What exists today

Only `DashboardServer`'s local API (`docs/IMPLEMENTATION_PLAN.md`'s Phase 19 entry):
`/api/status`, `/api/devices`, `/api/incidents`, `/api/settings/*` — unauthenticated,
LAN-only, served directly by the ESP32-S3, JSON but not versioned (`/api/status`, not
`/api/v1/status`) and not OpenAPI-documented. This stays exactly as it is (Section 3)
— it's the "local Gateway API" Section 18 of the Phase 21 brief already asks to keep.

No remote/Internet-facing API exists anywhere in this project yet.

## 2. Principle: heavy UI lives outside the firmware

Confirmed already true and staying true: `DashboardServer`'s embedded page
(`docs/IMPLEMENTATION_PLAN.md` Phase 19) is a single hand-written HTML/CSS/vanilla-JS
file with no framework, no build step, no CDN — chosen specifically because the
ESP32-S3 shouldn't host a real SPA. Phase 21 doesn't change this; a future
Angular/React/Flutter/Home-Assistant client talks to the *remote backend's* API
(Section 4), never to the Gateway's own limited HTTP server for anything beyond local
LAN diagnostics.

## 3. Two APIs, not one

```text
Local (LAN, no Internet needed)          Remote (Internet, backend-hosted)
────────────────────────────────         ─────────────────────────────────
http://<gateway-ip>/api/...              https://<backend>/api/v1/...
DashboardServer (exists, Phase 19)       New backend project (Phase 21.5+)
Unauthenticated (documented posture,     Authenticated (device credential,
 trusted-LAN-only, unchanged)             Section 11 of the Phase 21 brief)
Unversioned today                        Versioned (/api/v1/)
No OpenAPI doc                           OpenAPI 3.x required
```

These stay genuinely separate systems. The Gateway's local API is for "phone on the
same Wi-Fi, no Internet" (Section 18); the remote API is for "anywhere, via the
backend." A phone app could reasonably talk to either depending on network
reachability, but that's a client-side concern, not something the Gateway needs to
unify into one server.

## 4. Remote REST API — design notes (not routes to build yet)

Per the Phase 21 brief's own instruction ("the exact routes should be finalized after
inspecting the existing implementation... do not blindly expose these exact routes"),
this section records what the *existing domain model* already gives the API to work
with, so Phase 21.5's route design starts from real schemas, not guesses:

- **Devices** → `DeviceRegistryEntry` (`DeviceRegistry.h`) already has nodeId,
  displayName, role, mac, hardwareProfile, firmwareVersion, ip, enabled, lastSeenMs,
  lastFreeHeap, lastUptimeMs — a `GET /api/v1/devices` response maps onto this almost
  directly.
- **Incidents/evidence** → `IncidentRecord`/`IncidentTriggerInfo`/`IncidentEvidenceRef`
  (`IncidentCorrelator.h`) already define the full incident shape (state machine,
  trigger info with GPS/IMU/env, evidence references by nodeId+localEventId) —
  `IncidentCorrelator::listRecentJson()` (Phase 19) already serializes this; the
  remote API's incident schema should be the same shape, not a redesign.
  **Fixed in Phase 21.7.** Evidence images used to be un-fetchable — they live on
  each node's own SD card (`EvidenceManager`, Phase 4), referenced not copied by the
  incident record, and nodes only ever pushed evidence locally, never served it on
  request. Now: `EvidenceManager::imagePath()` + a new `GET /evidence?eventId=...`
  route on the node's `StatusPage`, fetched by the Gateway
  (`fetchAndUploadEvidence()` in `gateway_main.cpp`) and uploaded to the backend
  (`POST /api/v1/incidents/{id}/evidence`, `GET .../evidence` for metadata,
  `GET /api/v1/evidence/{id}/file` for the bytes) — verified byte-for-byte
  end-to-end. Not yet filtered by `docs/BACKEND.md` Section 7's evidence sync
  policy (every evidenced image uploads whenever the backend is enabled at all).
- **Telemetry** → `GpsFix`/`ImuReading` (`GpsManager.h`/`ImuManager.h`) and
  `buildStatusJson()` in `gateway_main.cpp` (Phase 19) already assemble the live
  status payload the local dashboard polls — same shape, different transport.
  There is currently no *historical* telemetry storage anywhere (gateway only ever
  reports current values); Section 12's `GET /api/v1/telemetry` (plural, implying
  history) needs backend-side storage, not something the Gateway can serve from
  memory.
- **Security** → `SecurityModeConfig`/`SecurityMode` already model
  DISARMED/DRIVING/PARKED/SERVICE; `POST /api/v1/security/arm`/`disarm` maps onto the
  existing `applyModeChange()` in `gateway_main.cpp`, which already broadcasts mode
  changes to nodes over ESP-NOW (`broadcastModeToAllDevices`) — a remote arm/disarm
  command would need to reach that same function via `RemoteSyncManager`'s command
  channel (Section 6, `docs/BACKEND.md`).
- **Cameras** → nodes with role `CAMERA`; `GET /api/v1/cameras/{id}/snapshot` would
  need the Gateway to request a fresh frame from the node (no existing "grab one JPEG
  right now" ESP-NOW message type — `CAPTURE_REQUEST` exists but is designed for the
  multi-camera-correlation flow, Section 18, not an on-demand remote snapshot; may be
  reusable or may need a new message type, a Phase 21.5+ design decision).

## 5. Real-time updates

**Implemented (Phase 21.6), on the backend, not the Gateway** — matching the
placement this section originally called for:

```text
Camera motion -> ESP-NOW -> Gateway -> Backend -> SSE -> Browser
```

`GET /api/v1/stream` (Server-Sent Events, not WebSocket — one-directional is all this
needs) pushes every ingested heartbeat/telemetry/event/incident to connected clients
in real time via an in-memory `EventBroadcaster`. The Gateway's own job is unchanged
from this section's original description: get the event to the backend promptly
(`RemoteSyncManager`'s existing send path), fan-out to browsers is entirely the
backend's concern. `DashboardServer` (the Gateway's own local dashboard) still polls
every 3s — Phase 21.6 only added real-time push on the remote/backend side, the local
LAN dashboard is unchanged and would need its own separate work to adopt SSE/
WebSocket if that's ever wanted (not currently planned).

Current limitation: single-process, in-memory pub/sub — fine for this reference
backend, would need a shared broker (Redis, etc.) for a real multi-instance
deployment (docs/BACKEND.md's "avoid unnecessary infrastructure until there's a
second instance to justify it").

## 6. Webhooks

Entirely backend-side (Section 15 of the brief) — delivery, retry, signing, and
subscription management all belong to the backend project (Phase 21.5+/21.9), not the
Gateway. The Gateway's only involvement is being the source of the events
(`device.online`/`motion.detected`/etc.) that the backend turns into webhook
deliveries once it receives them via `RemoteSyncManager`.

## 7. Security / trust boundaries

Matches `docs/SECURITY.md`'s existing structure — this phase adds one more boundary
to the chain already documented there:

```text
ESP32 Node --ESP-NOW HMAC trust boundary--> Gateway --TLS trust boundary--> Backend --Authenticated clients-->
(docs/SECURITY.md,                          (new: HttpBackend's TLS +      (new: backend's own
 existing)                                   device credential auth,        user-auth model,
                                              Section 11 of the brief)       Phase 21.4+/21.5+)
```

`docs/SECURITY.md`'s existing gaps (no gateway-identity validation, no key
distribution) are upstream of this boundary and unaffected by it — a compromised
ESP-NOW mesh is a problem regardless of whether a backend exists. The new boundary
this phase adds is Gateway↔Backend: device credential (not a shared global API key,
per Section 11), TLS, never logged, rotation/revocation support designed for from the
start even if not fully implemented in Phase 21.4.

## 8. Multi-tenant readiness (data-model note only)

Per Section 27 of the brief: design the backend's data model with
`User -> Account/Tenant -> Installation -> Gateway -> Devices` in mind (a `tenantId`
field on `BackendConfig`, Section 4 of `docs/BACKEND.md`, and on backend-side records)
without building actual multi-tenant infrastructure (multiple accounts, permission
scoping, etc.) now. This is a schema-design constraint for Phase 21.5, not a Phase 21.1
deliverable.

## 9. OpenAPI

No API exists yet to document. Tracked as Phase 21.10 in
`docs/IMPLEMENTATION_PLAN.md`'s Phase 21 breakdown — `openapi.yaml` plus a
Swagger/OpenAPI UI, once the backend's actual routes exist (Section 4 above is design
notes, not a route list to freeze).
