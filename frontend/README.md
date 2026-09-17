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

## Building

```bash
ng build
```

Output goes to `dist/frontend/`. `fileReplacements` swaps in
`environment.production.ts` for a production build (see `angular.json`).
