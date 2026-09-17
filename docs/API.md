# CarSentinel Backend API (Phase 21.10)

This is a standalone reference for every HTTP endpoint on the optional ASP.NET Core
reference backend (`backend/`), with real example requests. It complements — does not
replace — the live interactive docs: run the backend and browse to `/` (redirects to
`/swagger`) for the generated OpenAPI/Swagger UI, which lets you execute requests
directly from the browser.

Nothing here is required reading to use CarSentinel. The backend is entirely
optional — every gateway and node works fully standalone with it disabled
(`BackendConfig.enabled = false`, the default). This document exists for anyone
integrating an external system (a dashboard, a script, a Home Assistant instance)
against a running backend.

All examples assume the backend is running locally per `backend/README.md`:

```
cd backend/src/CarSentinel.Backend
dotnet run --urls "http://127.0.0.1:5299"
```

## Conventions

- Base path: `/api/v1`.
- Two authentication schemes, never mixed on the same endpoint:
  - **Device credential** — `Authorization: Bearer <credential>` +
    `X-CarSentinel-Device-Id: <deviceId>` headers. Used by a gateway talking to its
    own backend account. Issued by `POST /register`.
  - **Admin key** — `X-Admin-Key: <key>` header, checked against the `Admin:ApiKey`
    configuration value. Used for operator actions (issuing remote commands,
    managing webhook subscriptions). **Unset by default**, which refuses every
    such request with `503` rather than silently allowing them — set
    `Admin:ApiKey` in `appsettings.json` or the `Admin__ApiKey` environment
    variable to enable.
  - Everything else (the read/query endpoints below) is anonymous — there's no
    user-account/login model yet (a documented gap, see `docs/BACKEND.md`'s
    multi-tenant-readiness note).
- All timestamps are UTC.
- All examples use `curl`; swap in whatever HTTP client you like.

---

## Device lifecycle

### `POST /api/v1/register`

Anonymous. A device's first call ever — the backend assigns a `deviceId` and
`credential`, which the caller must store and use on every subsequent request. Safe
to call again with an existing `deviceId`/credential already presented (re-registers
without issuing a new credential) — used by firmware after a reboot to refresh its
recorded hardware/firmware version.

```
curl -s -X POST http://127.0.0.1:5299/api/v1/register \
  -H "Content-Type: application/json" \
  -d '{"hardwareProfile":"esp32-s3-gateway","firmwareVersion":"1.4.0","nodeId":"gw-01"}'
```

```json
{"deviceId":"gw-abc664eb","credential":"1f61b1c8cc01819bfec4b7e329d90862888a450a9ae041d8"}
```

Save the `credential` immediately — it is returned exactly once and cannot be
recovered later (only the hash is stored server-side).

### `POST /api/v1/heartbeat`

Device credential required. Keeps `LastSeenAt` current; publishes `device.online`
(SSE + webhooks) the first time a heartbeat arrives after a >120s gap, and always
publishes `device.heartbeat`.

```
curl -s -X POST http://127.0.0.1:5299/api/v1/heartbeat \
  -H "Authorization: Bearer 1f61b1c8cc01819bfec4b7e329d90862888a450a9ae041d8" \
  -H "X-CarSentinel-Device-Id: gw-abc664eb" \
  -H "Content-Type: application/json" -d '{}'
```

### `POST /api/v1/telemetry`

Device credential required. Body is passed through as-is (any JSON object) and
stored under the authenticated device. Publishes `telemetry`.

```
curl -s -X POST http://127.0.0.1:5299/api/v1/telemetry \
  -H "Authorization: Bearer <credential>" -H "X-CarSentinel-Device-Id: gw-abc664eb" \
  -H "Content-Type: application/json" \
  -d '{"lat":37.77,"lon":-122.41,"speedKph":0,"battery":92}'
```

### `POST /api/v1/events`

Device credential required. Body is any JSON object (a motion/sensor event from a
node, relayed by the gateway). Publishes `motion.detected`.

```
curl -s -X POST http://127.0.0.1:5299/api/v1/events \
  -H "Authorization: Bearer <credential>" -H "X-CarSentinel-Device-Id: gw-abc664eb" \
  -H "Content-Type: application/json" \
  -d '{"nodeId":"node-1","type":"motion","eventId":"evt-001"}'
```

### `POST /api/v1/incidents`

Device credential required. Upserted by `incidentId` — call it again with the same
`incidentId` as an incident's state changes and the existing row is updated in
place (its `firstReceivedAt` is preserved). Publishes `incident.created` on first
insert, `incident.updated` on every subsequent call.

```
curl -s -X POST http://127.0.0.1:5299/api/v1/incidents \
  -H "Authorization: Bearer <credential>" -H "X-CarSentinel-Device-Id: gw-abc664eb" \
  -H "Content-Type: application/json" \
  -d '{"incidentId":"inc-001","state":"CONFIRMED","severity":"HIGH"}'
```

### `POST /api/v1/incidents/{incidentId}/evidence`

Device credential required. **Raw binary body**, not JSON — matches exactly what
firmware's `HttpBackend::uploadEvidence()` sends (`Content-Type: image/jpeg`).
`nodeId`/`eventId` are query parameters. Publishes `evidence.uploaded`.

```
curl -s -X POST "http://127.0.0.1:5299/api/v1/incidents/inc-001/evidence?nodeId=node-1&eventId=evt-001" \
  -H "Authorization: Bearer <credential>" -H "X-CarSentinel-Device-Id: gw-abc664eb" \
  -H "Content-Type: image/jpeg" \
  --data-binary @snapshot.jpg
```

---

## Reading data (anonymous)

| Method & path | Notes |
|---|---|
| `GET /api/v1/devices` | All registered devices, newest-seen first. |
| `GET /api/v1/devices/{id}` | One device. |
| `GET /api/v1/devices/{id}/telemetry?limit=50` | Most recent telemetry rows (`limit` clamped 1–500). |
| `GET /api/v1/events?deviceId=&limit=50` | Optionally filtered by device. |
| `GET /api/v1/incidents?deviceId=&limit=50` | Optionally filtered by device. |
| `GET /api/v1/incidents/{incidentId}` | One incident's current state. |
| `GET /api/v1/incidents/{incidentId}/evidence` | Evidence metadata for an incident (no bytes). |
| `GET /api/v1/evidence/{id}/file` | Downloads the actual evidence bytes for one record. |
| `GET /api/v1/health` | Device/online counts — `online` means seen within the last 120s. |

```
curl -s http://127.0.0.1:5299/api/v1/health
```

```json
{"deviceCount":1,"onlineCount":1,"devices":[{"id":"gw-abc664eb","online":true,"lastSeenAt":"2026-09-17T07:44:56Z"}]}
```

---

## Real-time stream (Phase 21.6)

### `GET /api/v1/stream`

Server-Sent Events — no auth (matches the anonymous read endpoints above; add a
reverse-proxy auth layer for a real deployment). Every `heartbeat`/`telemetry`/
`motion.detected`/`incident.*`/`evidence.uploaded`/`command.*` publish arrives here
as it happens, as a JSON envelope: `{schemaVersion, type, deviceId, timestamp,
payload}`.

```
curl -N http://127.0.0.1:5299/api/v1/stream
```

From a browser: `new EventSource('/api/v1/stream').onmessage = e => console.log(JSON.parse(e.data))`.

---

## Remote commands (Phase 21.8, admin-key gated)

### `POST /api/v1/devices/{id}/commands`

Requires `X-Admin-Key`. Issues a command to a specific device; `commandType` is
firmware-defined (only `SECURITY_MODE` is wired up end-to-end on the gateway side
today — see `backend/README.md`). `ttlSeconds` (default 300, clamped 10–3600)
controls how long the command stays `PENDING` before expiring unclaimed.

```
curl -s -X POST http://127.0.0.1:5299/api/v1/devices/gw-abc664eb/commands \
  -H "X-Admin-Key: <admin key>" -H "Content-Type: application/json" \
  -d '{"commandType":"SECURITY_MODE","payload":{"mode":"ARMED"},"ttlSeconds":120}'
```

```json
{"commandId":"...","status":"PENDING","expiresAt":"2026-09-17T07:50:00Z"}
```

### `GET /api/v1/devices/{id}/commands/pending`

Device credential required, and `{id}` must be the authenticated device's own ID —
a device can only ever see its own commands. Marks returned commands `DELIVERED`.
This is what the gateway itself polls every 15s (`RemoteSyncManager`).

### `POST /api/v1/devices/{id}/commands/{commandId}/result`

Device credential required, same per-device restriction. Marks the command
`EXECUTED` or `FAILED`. Publishes `command.executed`/`command.failed`.

### `GET /api/v1/devices/{id}/commands?limit=20`

Anonymous. Full command history for a device, any status.

---

## Webhooks (Phase 21.9, admin-key gated)

### `POST /api/v1/webhooks`

Requires `X-Admin-Key`. `eventTypes` is a comma-separated list or `"*"` (default) for
every event type. Returns the HMAC secret exactly once — store it immediately.

```
curl -s -X POST http://127.0.0.1:5299/api/v1/webhooks \
  -H "X-Admin-Key: <admin key>" -H "Content-Type: application/json" \
  -d '{"url":"https://example.com/carsentinel-hook","eventTypes":"incident.created,incident.updated"}'
```

```json
{"id":1,"url":"https://example.com/carsentinel-hook","eventTypes":"incident.created,incident.updated","enabled":true,"secret":"a34168ba7b2e8bee1738401e25770fe1f909d1cd79836d260a2557e85e68a612"}
```

Every matching event thereafter is POSTed to `url` with the same JSON envelope the
SSE stream sends, plus a signature header:

```
X-CarSentinel-Signature: sha256=<hex HMAC-SHA256 of the raw request body, keyed by secret>
```

Verify it server-side (pseudocode): `hex(hmac_sha256(secret, raw_body)) == header_value_after_"sha256="`.
One retry (1s later) on failure/timeout; every attempt is logged regardless of outcome.

### `GET /api/v1/webhooks`

Requires `X-Admin-Key`. Lists subscriptions (secret not included).

### `DELETE /api/v1/webhooks/{id}`

Requires `X-Admin-Key`. Removes a subscription; no further deliveries follow.

### `GET /api/v1/webhooks/{id}/deliveries?limit=20`

Requires `X-Admin-Key`. Recent delivery attempts for one subscription — useful for
diagnosing a receiver that stopped responding.

---

## What's intentionally not here

No EF migrations, no user-account/login system (every read endpoint above is
anonymous), no `device.offline` event (only reacts to a heartbeat arriving late, not
to one that stops arriving), no evidence sync-policy filtering, webhook delivery is
at-most-two-attempts with no persisted retry queue. All documented in
`backend/README.md` and `docs/IMPLEMENTATION_PLAN.md`'s Phase 21 entries — scope
cuts for a reference implementation, not oversights.
