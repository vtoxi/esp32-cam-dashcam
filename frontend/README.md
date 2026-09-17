# CarSentinel Console

A reference Angular frontend for the [CarSentinel remote backend](../backend/) (Phase
21). It's an operator console: view registered devices, telemetry, events, and
incidents; browse incident evidence; issue remote commands; manage webhook
subscriptions; and watch the real-time event stream — all against the ASP.NET Core
backend's REST/SSE API. It never talks to a gateway or node directly.

Entirely optional, like the backend it consumes: nothing about the firmware or the
backend depends on this app existing.

## Stack

- **Angular 22**, standalone components, zoneless change detection, signals for all
  local state.
- **Tailwind CSS 3** as the design system — no Angular Material/CDK anywhere in this
  app. A small shared component library (`src/app/shared/components/`) wraps common
  patterns (data table, status badge, dialogs, toasts, JSON viewer, icons) in
  Tailwind-only markup so every feature page looks and behaves consistently.
- A hand-rolled **dialog service** (`shared/components/dialog/dialog.service.ts`)
  using Angular's own `createComponent` API instead of a CDK overlay.
- **Server-Sent Events** via the native `EventSource` API for the live activity feed
  and dashboard (`core/services/event-stream.service.ts`).

## Run it

Two things need to be running: the backend, and this app.

```bash
# Terminal 1 — backend (see ../backend/README.md)
cd ../backend/src/CarSentinel.Backend
dotnet run --urls "http://127.0.0.1:5299"

# Terminal 2 — this app
cd frontend
npm install
ng serve
```

Open `http://localhost:4200`. During `ng serve`, requests to `/api/v1/*` are proxied
to `http://127.0.0.1:5299` (see `proxy.conf.json`) so the browser never needs the
backend's real origin in development.

For a production build served from a different origin than the backend, either
rebuild with a different `environment.production.ts#apiBaseUrl`, or point an
already-built app at a different backend at runtime via **Settings → API Base URL**
(stored in `localStorage`, no rebuild needed). The backend has CORS enabled
(`Program.cs`) specifically so a separately-hosted build of this console can reach it
directly without a proxy.

## Admin key

Issuing remote commands and managing webhook subscriptions are gated on the backend
by an `X-Admin-Key` header (`Admin:ApiKey` config — unset by default, which refuses
those actions with `503`/`401`). Set the same key in this console's **Settings**
page; it's kept in `sessionStorage` only (cleared when the tab closes) and attached
automatically to exactly the requests that need it — see
`core/interceptors/admin-key.interceptor.ts`.

## What's covered

Every backend capability from Phase 21 has a page:

| Page | Backend surface |
|---|---|
| Dashboard | `GET /health`, live SSE feed |
| Devices (list + detail) | `GET /devices`, `/devices/{id}`, `/devices/{id}/telemetry`, plus that device's events/incidents/commands |
| Telemetry | `GET /devices/{id}/telemetry` (merged across devices when no device is selected — there's no global telemetry endpoint) |
| Events | `GET /events` |
| Incidents (list + detail) | `GET /incidents`, `/incidents/{id}`, `/incidents/{id}/evidence`, `/evidence/{id}/file` (image gallery + lightbox + download) |
| Commands | `POST /devices/{id}/commands`, `GET .../commands/pending` is device-side only (not shown here), `GET .../commands` history |
| Webhooks | Full CRUD (`POST/GET/DELETE /webhooks`) + `GET /webhooks/{id}/deliveries` |
| Live Activity | `GET /stream` (SSE), filterable by event type |
| Settings | API base URL override, admin key, connection test |

Not in scope, because the backend doesn't expose it to an external client either:
device registration/heartbeat/telemetry-ingestion/evidence-upload and command
poll/result are all firmware-initiated (`HttpBackend.cpp` on the gateway) — this
console only ever reads what firmware already sent, plus issues commands and manages
webhooks as an operator.

## Reusable pieces

Everything list-like (devices, telemetry, events, incidents, commands, webhooks)
shares one component: `shared/components/data-table/data-table.component.ts` — a
generic, signal-driven table with client-side search, sortable columns, and
pagination built in once. A page supplies `columns` + `rows`; custom cell rendering
(badges, links, JSON viewers, buttons) is opted into per column via
`<ng-template appCellTemplate="key" let-row>` rather than the table needing to know
about any specific feature.

Other shared pieces: `status-badge` (consistent color-coding for every status-like
value across the app), `json-viewer` (collapsible pretty-printed JSON for every
`payloadJson`/`resultJson` field), `dialog-shell` + `dialog.service` (backdrop +
card chrome for every modal), `toast` (success/error/info notifications, wired to a
global HTTP error interceptor so no feature code writes its own error handling
boilerplate), `device-picker`, `copy-field` (for one-time secrets/credentials),
`stat-card`, `page-header`, `empty-state`, and a small inline-SVG `icon` component.

## Device lifecycle (admin)

Beyond reading what devices report, the Devices page (with an admin key set in
Settings) can:
- **Pre-provision** a device — generates a device ID + credential before any real
  hardware registers (`POST /api/v1/devices`), so an operator can hand a known
  identity to a technician ahead of a physical install. The credential is shown
  exactly once; it must be entered into the gateway's own Settings page (Remote
  Backend section) or `BACKENDCONFIG` serial command. A gateway that registers
  with that same ID + credential is treated as an update to the pre-provisioned
  record, not a new device (the backend's existing re-registration logic already
  did this — pre-provisioning just seeds it ahead of time).
- **Edit** a device's Tenant ID — the one field this backend never sets on a
  device's own behalf. Hardware profile/firmware version/node ID are
  firmware-reported and would just be overwritten on the device's next
  register/heartbeat, so there's no edit UI for those.
- **Delete** a device record — removes the `Device` row only; historical
  telemetry/events/incidents/evidence already ingested under that device ID are
  kept (a documented choice, not an oversight — decommissioning a device doesn't
  erase its audit trail).

There's deliberately no "create a fully-registered device out of nothing" —
a manufactured device record with no real hardware behind it could never present
a valid credential, which is exactly what pre-provisioning solves instead.

## Building

```bash
ng build --configuration production
```

Output goes to `dist/frontend/browser/`. `fileReplacements` swaps in
`environment.production.ts` (see `angular.json`), which by default points the
build at `https://carsentinal-api.vtoxi.com/api/v1` — override per-deployment via
the app's own Settings page (localStorage) without rebuilding.

`public/web.config` is copied into every build automatically (Angular copies
everything under `public/` into the output root) — it's an IIS config for a
Plesk/Windows deployment: an SPA-fallback rewrite rule (any request that isn't a
real file on disk serves `index.html`, so refreshing or deep-linking to a route
like `/devices/gw-abc123` doesn't 404 against IIS) plus the Plesk-managed
error-page config it was layered on top of. A non-IIS host (Apache/Nginx/static
host) needs the equivalent SPA-fallback rule in whatever config that host uses
instead — `web.config` itself does nothing there.

### Deploying to carsentinal.vtoxi.com (Plesk/IIS, FTP)

The console is deployed at `https://carsentinal.vtoxi.com/`, uploaded via FTP to
the Plesk-provisioned `carsentinal.vtoxi.com/` webroot (contents of
`dist/frontend/browser/` go directly there — no `httpdocs` subfolder on this
particular IIS-based Plesk layout). A sibling subdomain,
`carsentinal-api.vtoxi.com`, is already reserved for the backend but nothing is
deployed there yet (still Plesk's default placeholder) — until it is, the
deployed console's API calls fail with a clear "cannot reach backend" toast,
exactly as designed.

Known gap as of this deployment: the SSL certificate presented for
`carsentinal.vtoxi.com` doesn't match its hostname (a TLS handshake against it
fails with a principal-name mismatch) — the site serves correctly once that's
fixed in Plesk (subdomain → SSL/TLS Certificates → issue a Let's Encrypt cert for
it), it's a certificate-provisioning step, not a deployment or code issue.
