using CarSentinel.Backend.Data;
using Microsoft.EntityFrameworkCore;

namespace CarSentinel.Backend.Endpoints;

// Phase 21.5 — the read side for external clients (docs/REMOTE_ACCESS.md Section 3:
// "a future Angular/React/Flutter/Home-Assistant client talks to the remote backend's
// API"). Anonymous for this first pass — there is no user-account/login model yet
// (Phase 21 brief Section 27's "multi-tenant readiness" is a data-model note for
// later, not built here); a production deployment would put these behind real user
// authentication before exposing them publicly. Documented as a gap, not hidden.
public static class QueryEndpoints
{
    public static void MapQueryEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/v1").WithTags("Query");

        group.MapGet("/devices", async (AppDbContext db) =>
            await db.Devices
                .OrderByDescending(d => d.LastSeenAt)
                .Select(d => new
                {
                    d.Id, d.HardwareProfile, d.FirmwareVersion, d.NodeId, d.TenantId,
                    d.RegisteredAt, d.LastSeenAt,
                })
                .ToListAsync());

        group.MapGet("/devices/{id}", async (string id, AppDbContext db) =>
            await db.Devices.FindAsync(id) is { } d
                ? Results.Ok(new
                {
                    d.Id, d.HardwareProfile, d.FirmwareVersion, d.NodeId, d.TenantId,
                    d.RegisteredAt, d.LastSeenAt,
                })
                : Results.NotFound());

        group.MapGet("/devices/{id}/telemetry", async (string id, int? limit, AppDbContext db) =>
            await db.Telemetry
                .Where(t => t.DeviceId == id)
                .OrderByDescending(t => t.ReceivedAt)
                .Take(Math.Clamp(limit ?? 50, 1, 500))
                .ToListAsync());

        group.MapGet("/events", async (string? deviceId, int? limit, AppDbContext db) =>
        {
            var query = db.Events.AsQueryable();
            if (!string.IsNullOrEmpty(deviceId)) query = query.Where(e => e.DeviceId == deviceId);
            return await query
                .OrderByDescending(e => e.ReceivedAt)
                .Take(Math.Clamp(limit ?? 50, 1, 500))
                .ToListAsync();
        });

        group.MapGet("/incidents", async (string? deviceId, int? limit, AppDbContext db) =>
        {
            var query = db.Incidents.AsQueryable();
            if (!string.IsNullOrEmpty(deviceId)) query = query.Where(i => i.DeviceId == deviceId);
            return await query
                .OrderByDescending(i => i.LastReceivedAt)
                .Take(Math.Clamp(limit ?? 50, 1, 500))
                .ToListAsync();
        });

        group.MapGet("/incidents/{incidentId}", async (string incidentId, AppDbContext db) =>
            await db.Incidents.FindAsync(incidentId) is { } i ? Results.Ok(i) : Results.NotFound());

        // Section 24 of the Phase 21 brief: "the backend should be able to show
        // Gateway online/offline, last heartbeat." A device is "online" if it's been
        // heard from within 2x the gateway's own heartbeat interval (60s default,
        // RemoteSyncManager.h's HEARTBEAT_INTERVAL_MS) — same staleness reasoning
        // HttpBackend::isConnected() already uses on the firmware side.
        group.MapGet("/health", async (AppDbContext db) =>
        {
            var cutoff = DateTime.UtcNow.AddSeconds(-120);
            var devices = await db.Devices.ToListAsync();
            return new
            {
                deviceCount = devices.Count,
                onlineCount = devices.Count(d => d.LastSeenAt >= cutoff),
                devices = devices.Select(d => new { d.Id, online = d.LastSeenAt >= cutoff, d.LastSeenAt }),
            };
        }).WithTags("Health");
    }
}
