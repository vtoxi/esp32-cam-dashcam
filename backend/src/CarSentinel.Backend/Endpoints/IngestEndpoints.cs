using System.Security.Claims;
using System.Security.Cryptography;
using System.Text.Json;
using CarSentinel.Backend.Auth;
using CarSentinel.Backend.Data;
using CarSentinel.Backend.Models;
using CarSentinel.Backend.Services;
using Microsoft.EntityFrameworkCore;

namespace CarSentinel.Backend.Endpoints;

// Phase 21.5 — the write side: exactly what HttpBackend.cpp posts to
// (firmware/lib/CarSentinelGateway/HttpBackend.cpp), one endpoint per RemoteBackend
// interface method. Paths are flat ("/api/v1/heartbeat", not
// "/api/v1/gateways/{id}/heartbeat") because the gateway identifies itself via the
// X-CarSentinel-Device-Id header (docs/BACKEND.md Section 5), not a path segment —
// matches what's already implemented and build-verified on the firmware side rather
// than the conceptual path shape docs/REMOTE_ACCESS.md sketched before this sub-phase
// made the real call (that document's own Section 4 said not to freeze those as
// final).
public static class IngestEndpoints
{
    public static void MapIngestEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/v1").WithTags("Ingest");

        // Anonymous — a device has no credential on its first-ever registration
        // (docs/BACKEND.md Section 10: "the Gateway should be able to operate locally
        // before successful backend registration," mirrored here as "registration
        // itself needs no prior credential"). Re-registration (an already-known
        // deviceId) still requires the existing credential to match, so this can't be
        // used to hijack an already-registered device's identity.
        group.MapPost("/register", RegisterAsync).AllowAnonymous();

        group.MapPost("/heartbeat", async (HttpContext ctx, AppDbContext db, EventBroadcaster broadcaster, JsonElement payload) =>
        {
            var device = await RequireDeviceAsync(ctx, db);
            if (device is null) return Results.Unauthorized();
            bool wasOffline = (DateTime.UtcNow - device.LastSeenAt).TotalSeconds > 120;
            device.LastSeenAt = DateTime.UtcNow;
            await db.SaveChangesAsync();
            if (wasOffline)
            {
                // Section 15's device.online webhook/event trigger — the only
                // online/offline transition this pass detects (a heartbeat arriving
                // after a >120s gap, the same staleness window QueryEndpoints'
                // /health uses). device.offline itself isn't detected here since
                // nothing pushes an event when a heartbeat simply stops arriving —
                // that needs a background sweep, not implemented this pass (see
                // docs/IMPLEMENTATION_PLAN.md's Phase 21.6/21.9 entries).
                broadcaster.Publish("device.online", device.Id, new { });
            }
            broadcaster.Publish("device.heartbeat", device.Id, new { });
            return Results.Ok(new { ok = true });
        }).RequireAuthorization();

        group.MapPost("/telemetry", async (HttpContext ctx, AppDbContext db, EventBroadcaster broadcaster, JsonElement payload) =>
        {
            var device = await RequireDeviceAsync(ctx, db);
            if (device is null) return Results.Unauthorized();
            db.Telemetry.Add(new TelemetryRecord
            {
                DeviceId = device.Id,
                ReceivedAt = DateTime.UtcNow,
                PayloadJson = payload.GetRawText(),
            });
            device.LastSeenAt = DateTime.UtcNow;
            await db.SaveChangesAsync();
            broadcaster.Publish("telemetry", device.Id, payload);
            return Results.Ok(new { ok = true });
        }).RequireAuthorization();

        group.MapPost("/events", async (HttpContext ctx, AppDbContext db, EventBroadcaster broadcaster, JsonElement payload) =>
        {
            var device = await RequireDeviceAsync(ctx, db);
            if (device is null) return Results.Unauthorized();
            db.Events.Add(new EventRecord
            {
                DeviceId = device.Id,
                ReceivedAt = DateTime.UtcNow,
                PayloadJson = payload.GetRawText(),
            });
            device.LastSeenAt = DateTime.UtcNow;
            await db.SaveChangesAsync();
            broadcaster.Publish("motion.detected", device.Id, payload);
            return Results.Ok(new { ok = true });
        }).RequireAuthorization();

        group.MapPost("/incidents", async (HttpContext ctx, AppDbContext db, EventBroadcaster broadcaster, JsonElement payload) =>
        {
            var device = await RequireDeviceAsync(ctx, db);
            if (device is null) return Results.Unauthorized();

            // Upsert by incidentId (Models/IncidentRecord.cs's own header comment) —
            // an incident is reported repeatedly as it moves through
            // IncidentCorrelator's state machine; this keeps one row holding the
            // latest state rather than one row per transition.
            string incidentId = payload.TryGetProperty("incidentId", out var idProp)
                ? idProp.GetString() ?? Guid.NewGuid().ToString()
                : Guid.NewGuid().ToString();
            string state = payload.TryGetProperty("state", out var stateProp)
                ? stateProp.GetString() ?? "UNKNOWN"
                : "UNKNOWN";

            var existing = await db.Incidents.FindAsync(incidentId);
            var now = DateTime.UtcNow;
            bool isNew = existing is null;
            if (existing is null)
            {
                db.Incidents.Add(new IncidentRecord
                {
                    IncidentId = incidentId,
                    DeviceId = device.Id,
                    State = state,
                    FirstReceivedAt = now,
                    LastReceivedAt = now,
                    PayloadJson = payload.GetRawText(),
                });
            }
            else
            {
                existing.State = state;
                existing.LastReceivedAt = now;
                existing.PayloadJson = payload.GetRawText();
            }
            device.LastSeenAt = now;
            await db.SaveChangesAsync();
            broadcaster.Publish(isNew ? "incident.created" : "incident.updated", device.Id, payload);
            return Results.Ok(new { ok = true, incidentId });
        }).RequireAuthorization();

        // Phase 21.7 — raw JPEG body (matches HttpBackend.cpp's uploadEvidence(),
        // Content-Type: image/jpeg, not JSON). incidentId is a route segment since
        // evidence always belongs to one incident; nodeId/eventId are query params
        // since they identify which node/local-event captured it, not this backend's
        // own identifiers.
        group.MapPost("/incidents/{incidentId}/evidence",
            async (string incidentId, string? nodeId, string? eventId, HttpContext ctx, AppDbContext db,
                   EvidenceStorage storage, EventBroadcaster broadcaster) =>
        {
            var device = await RequireDeviceAsync(ctx, db);
            if (device is null) return Results.Unauthorized();
            if (ctx.Request.ContentLength is null or 0)
            {
                return Results.BadRequest(new { error = "empty body" });
            }

            var (relativePath, size) = await storage.SaveAsync(
                incidentId, nodeId ?? "unknown", eventId ?? "unknown", ctx.Request.Body);

            db.Evidence.Add(new EvidenceRecord
            {
                IncidentId = incidentId,
                DeviceId = device.Id,
                NodeId = nodeId ?? "unknown",
                EventId = eventId ?? "unknown",
                ContentType = ctx.Request.ContentType ?? "image/jpeg",
                SizeBytes = size,
                StoragePath = relativePath,
                UploadedAt = DateTime.UtcNow,
            });
            device.LastSeenAt = DateTime.UtcNow;
            await db.SaveChangesAsync();
            broadcaster.Publish("evidence.uploaded", device.Id, new { incidentId, nodeId, eventId, size });
            return Results.Ok(new { ok = true, size });
        }).RequireAuthorization();
    }

    private static async Task<Device?> RequireDeviceAsync(HttpContext ctx, AppDbContext db)
    {
        var deviceId = ctx.User.FindFirstValue(ClaimTypes.NameIdentifier);
        if (deviceId is null) return null;
        return await db.Devices.FirstOrDefaultAsync(d => d.Id == deviceId);
    }

    public record RegisterRequest(string? HardwareProfile, string? FirmwareVersion, string? NodeId);

    private static async Task<IResult> RegisterAsync(HttpContext ctx, AppDbContext db, RegisterRequest body)
    {
        // If the gateway already presents a deviceId + credential (re-registration —
        // e.g. it was operator-assigned, or the gateway rebooted with a
        // server-assigned one already saved), confirm rather than issue a new one.
        ctx.Request.Headers.TryGetValue("X-CarSentinel-Device-Id", out var existingIdHeader);
        ctx.Request.Headers.TryGetValue("Authorization", out var authHeader);
        var existingId = existingIdHeader.ToString();

        if (!string.IsNullOrEmpty(existingId))
        {
            var existing = await db.Devices.FirstOrDefaultAsync(d => d.Id == existingId);
            if (existing is not null)
            {
                var presentedCredential = authHeader.ToString().StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase)
                    ? authHeader.ToString()["Bearer ".Length..].Trim()
                    : string.Empty;
                if (DeviceCredentialAuthenticationHandler.HashCredential(presentedCredential) != existing.CredentialHash)
                {
                    return Results.Unauthorized();
                }
                existing.HardwareProfile = body.HardwareProfile ?? existing.HardwareProfile;
                existing.FirmwareVersion = body.FirmwareVersion ?? existing.FirmwareVersion;
                existing.NodeId = body.NodeId ?? existing.NodeId;
                existing.LastSeenAt = DateTime.UtcNow;
                await db.SaveChangesAsync();
                // No new credential issued — the one already presented is still valid.
                return Results.Ok(new { deviceId = existing.Id });
            }
        }

        // Fresh registration: server-assigns both deviceId and credential.
        var newId = "gw-" + Convert.ToHexString(RandomNumberGenerator.GetBytes(4)).ToLowerInvariant();
        var newCredential = Convert.ToHexString(RandomNumberGenerator.GetBytes(24)).ToLowerInvariant();
        var device = new Device
        {
            Id = newId,
            CredentialHash = DeviceCredentialAuthenticationHandler.HashCredential(newCredential),
            HardwareProfile = body.HardwareProfile ?? string.Empty,
            FirmwareVersion = body.FirmwareVersion ?? string.Empty,
            NodeId = body.NodeId ?? string.Empty,
            RegisteredAt = DateTime.UtcNow,
            LastSeenAt = DateTime.UtcNow,
        };
        db.Devices.Add(device);
        await db.SaveChangesAsync();

        // The only time the plaintext credential is ever returned — HttpBackend.cpp
        // saves it into BackendConfig immediately (RemoteSyncManager.cpp's
        // attemptRegistration()) and it's never sent back by this server again.
        return Results.Ok(new { deviceId = newId, credential = newCredential });
    }
}
