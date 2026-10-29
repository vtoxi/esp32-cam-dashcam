using System.Security.Cryptography;
using CarSentinel.Backend.Auth;
using CarSentinel.Backend.Data;
using CarSentinel.Backend.Models;
using Microsoft.EntityFrameworkCore;

namespace CarSentinel.Backend.Endpoints;

// Admin-side device lifecycle management, added on top of the Phase 21.5 read API
// (QueryEndpoints.cs) and the firmware-driven self-registration flow
// (IngestEndpoints.cs's POST /register). Every route here is gated by the same
// X-Admin-Key convention as CommandEndpoints/WebhookEndpoints — an operator action,
// not something a device does to itself.
//
// There is deliberately no "create a fully-formed device" here in the sense of
// fabricating a device that's already "registered" — a real gateway is the only
// thing that can ever prove it holds a credential. What POST /devices does instead
// is pre-provision: generate a deviceId + credential an operator can hand to a
// gateway (via its dashboard Settings page or BACKENDCONFIG serial command) *before*
// that gateway ever calls /register itself. When it later does, IngestEndpoints'
// RegisterAsync already handles "an existing deviceId presented with a matching
// credential" as an update, not a fresh registration — so a pre-provisioned device
// and a self-registered one converge on the exact same code path.
public static class DeviceEndpoints
{
    public static void MapDeviceEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/v1/devices").WithTags("Devices (admin)");

        group.MapPost("/", async (HttpContext ctx, AppDbContext db, IConfiguration config, PreProvisionDeviceRequest body) =>
        {
            if (!IsAdmin(ctx, config)) return Results.Unauthorized();

            // Same "gw-" + short random suffix shape as a self-registered device
            // (IngestEndpoints.RegisterAsync) — nothing about a device's ID should
            // reveal whether it was pre-provisioned or self-registered.
            var id = "gw-" + Convert.ToHexString(RandomNumberGenerator.GetBytes(4)).ToLowerInvariant();
            var credential = Convert.ToHexString(RandomNumberGenerator.GetBytes(24)).ToLowerInvariant();

            var device = new Device
            {
                Id = id,
                CredentialHash = DeviceCredentialAuthenticationHandler.HashCredential(credential),
                HardwareProfile = body.HardwareProfile ?? string.Empty,
                NodeId = body.NodeId ?? string.Empty,
                TenantId = body.TenantId,
                FirmwareVersion = string.Empty,
                RegisteredAt = DateTime.UtcNow,
                LastSeenAt = DateTime.UtcNow,
            };
            db.Devices.Add(device);
            await db.SaveChangesAsync();

            // The only time the plaintext credential is returned — identical posture
            // to /register's own response and to WebhookEndpoints' secret. Hand this
            // (plus the deviceId) to whoever is configuring the physical gateway.
            return Results.Ok(new { deviceId = id, credential, device.HardwareProfile, device.NodeId, device.TenantId });
        });

        // Admin-editable metadata only. HardwareProfile/FirmwareVersion/NodeId are
        // firmware-reported (overwritten on the device's next register/heartbeat), so
        // editing them here would just be undone by the device itself — TenantId is
        // the one field this backend never sets on a device's behalf (Section 27 of
        // the Phase 21 brief's multi-tenant-readiness note), making it the one field
        // worth an admin edit endpoint.
        group.MapPatch("/{id}", async (string id, HttpContext ctx, AppDbContext db, IConfiguration config, UpdateDeviceRequest body) =>
        {
            if (!IsAdmin(ctx, config)) return Results.Unauthorized();
            var device = await db.Devices.FindAsync(id);
            if (device is null) return Results.NotFound();

            device.TenantId = body.TenantId;
            await db.SaveChangesAsync();
            return Results.Ok(new { device.Id, device.TenantId });
        });

        group.MapDelete("/{id}", async (string id, HttpContext ctx, AppDbContext db, IConfiguration config) =>
        {
            if (!IsAdmin(ctx, config)) return Results.Unauthorized();
            var device = await db.Devices.FindAsync(id);
            if (device is null) return Results.NotFound();

            // Deletes the device record only — telemetry/events/incidents/evidence
            // already ingested under this deviceId are left in place. This backend
            // has no FK/cascade wired between Devices and those tables (no
            // relational constraint forces it), and keeping historical data after a
            // device is deregistered/decommissioned is the more defensible default
            // for anything evidence-adjacent — an operator who actually wants those
            // rows gone would need to say so explicitly, which this endpoint doesn't
            // do today (a documented scope cut, not an oversight).
            db.Devices.Remove(device);
            await db.SaveChangesAsync();
            return Results.Ok(new { ok = true });
        });
    }

    private static bool IsAdmin(HttpContext ctx, IConfiguration config)
    {
        var adminKey = config["Admin:ApiKey"];
        if (string.IsNullOrEmpty(adminKey)) return false;
        return ctx.Request.Headers.TryGetValue("X-Admin-Key", out var presented) && presented == adminKey;
    }

    public record PreProvisionDeviceRequest(string? HardwareProfile, string? NodeId, string? TenantId);
    public record UpdateDeviceRequest(string? TenantId);
}
