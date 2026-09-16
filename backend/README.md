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
  event/incident in real time (`new EventSource('/api/v1/stream')` from a browser).

## What's not here yet (see `docs/IMPLEMENTATION_PLAN.md`'s Phase 21 entries)

No EF migrations (schema changes mean deleting `carsentinel.db` and starting over — fine
for this reference implementation, not for a real deployment), no user-facing
authentication on the read API, no `device.offline` detection (only reacts to a
heartbeat arriving after a gap, doesn't detect one that stops), no evidence/image
upload endpoint (Phase 21.7), no remote-command relay (Phase 21.8), no webhooks
(Phase 21.9).
