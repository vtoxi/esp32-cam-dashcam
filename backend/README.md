# CarSentinel Backend (Phase 21.5)

The first reference implementation of the optional remote backend described in
[`docs/BACKEND.md`](../docs/BACKEND.md) and [`docs/REMOTE_ACCESS.md`](../docs/REMOTE_ACCESS.md).
ASP.NET Core 8, minimal APIs, SQLite (via EF Core, `EnsureCreated()` — no migrations
yet). Entirely optional: the gateway and every node work fully without this running
at all (`BackendConfig.enabled = false` is the default).

## Run it

```
cd backend/src/CarSentinel.Backend
dotnet run --urls "http://0.0.0.0:5299"
```

Pass `--urls` explicitly (rather than relying on `ASPNETCORE_URLS` or the default) —
a `Properties/launchSettings.json` can silently override the port during `dotnet run`
if one ever gets generated; explicit `--urls` always wins. Browse to `/` for the
Swagger UI. A `carsentinel.db` SQLite file is created next to the project on first run.

## Point a gateway at it

On the gateway's dashboard Settings page (or `BACKENDCONFIG` over serial):

```
BACKENDCONFIG CUSTOM_SERVER http://<this-machine's-ip>:<port>/api/v1 "" ""
```

Leave device ID and credential blank on first setup — the gateway registers itself
against `POST /api/v1/register` and saves whatever `deviceId`/`credential` the server
assigns. See `RemoteSyncManager.cpp`'s `attemptRegistration()` for exactly what happens.

## What's here

- `POST /api/v1/register`, `/heartbeat`, `/telemetry`, `/events`, `/incidents` — exactly
  what `HttpBackend.cpp` (firmware) posts to. Device-credential auth (`Authorization:
  Bearer <credential>` + `X-CarSentinel-Device-Id` header) on everything except
  `/register` itself.
- `GET /api/v1/devices`, `/devices/{id}`, `/devices/{id}/telemetry`, `/events`,
  `/incidents`, `/incidents/{id}`, `/health` — anonymous reads for now (no user-account
  model yet — see `docs/BACKEND.md`'s multi-tenant-readiness note).
- Incidents are upserted by `incidentId`, not appended — one row holds the latest state
  as an incident moves through its lifecycle.
- `GET /api/v1/stream` — Server-Sent Events, pushes every ingested heartbeat/telemetry/
  event/incident/evidence upload in real time (`new EventSource('/api/v1/stream')`).
- `POST /api/v1/incidents/{incidentId}/evidence` — raw JPEG body (not JSON), matching
  `HttpBackend.cpp`'s `uploadEvidence()`. `GET .../evidence` lists metadata, `GET
  /api/v1/evidence/{id}/file` serves the bytes. Stored under `evidence/` next to the
  project (gitignored) — a local-disk stand-in for real object storage.
- `POST /api/v1/devices/{id}/commands` — issue a command, gated by an `X-Admin-Key`
  header matching the `Admin:ApiKey` config value (**unset by default, which refuses
  every issue attempt with 503** — set it in `appsettings.json`/an environment
  variable to enable). `GET .../commands/pending` (device-credential authenticated,
  a device can only ever poll its own commands) and `POST
  .../commands/{commandId}/result` are what the gateway calls. `GET
  /api/v1/devices/{id}/commands` lists history (any status).
- `POST /api/v1/webhooks` — create a subscription (`X-Admin-Key` gated, same as
  commands above); returns a server-generated HMAC secret exactly once. `GET
  /api/v1/webhooks` lists subscriptions, `DELETE /api/v1/webhooks/{id}` removes one,
  `GET /api/v1/webhooks/{id}/deliveries` shows recent delivery attempts. Every event
  published for SSE (see above) is also POSTed to every enabled, matching
  subscription, signed with `X-CarSentinel-Signature: sha256=<hex HMAC-SHA256 of the
  body>`, one retry on failure, every attempt logged.
- `POST /api/v1/devices` — admin-gated device lifecycle management, added for the
  Phase 22 console: pre-provisions a device ID + credential (returned exactly once)
  before any real hardware ever registers, so an operator can hand a known identity
  to a technician ahead of a physical install. `PATCH /api/v1/devices/{id}` edits
  `tenantId` (the only field this backend never sets on a device's own behalf —
  everything else is firmware-reported and gets overwritten on the next
  register/heartbeat anyway). `DELETE /api/v1/devices/{id}` removes the device
  record only; historical telemetry/events/incidents/evidence stay. Deliberately no
  "create a fully-registered device" — a manufactured record has no real credential
  a gateway could ever present, which is exactly what pre-provisioning solves
  instead (a gateway that later registers with that same ID+credential is treated
  as updating the pre-provisioned record, via the same re-registration logic
  `POST /register` already had).

## What's not here yet (see `docs/IMPLEMENTATION_PLAN.md`'s Phase 21 entries)

No EF migrations (schema changes mean deleting `carsentinel.db` and starting over — fine
for this reference implementation, not for a real deployment), no user-facing
authentication on the read API, no `device.offline` detection (only reacts to a
heartbeat arriving after a gap, doesn't detect one that stops), no evidence sync-policy
filtering (every evidenced image uploads whenever the backend is enabled, not filtered
by `METADATA_ONLY`/etc.), only one real command type wired up on the firmware side
(`SECURITY_MODE` — others need a matching `else if` in `gateway_main.cpp`'s
`handleRemoteCommand()`), webhook delivery is at-most-two-attempts (no persisted
retry queue beyond that — a receiver down for longer than one retry misses the event).
