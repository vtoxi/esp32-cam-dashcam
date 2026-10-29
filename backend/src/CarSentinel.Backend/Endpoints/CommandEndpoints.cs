using System.Security.Claims;
using System.Text.Json;
using CarSentinel.Backend.Data;
using CarSentinel.Backend.Models;
using CarSentinel.Backend.Services;
using Microsoft.EntityFrameworkCore;

namespace CarSentinel.Backend.Endpoints;

// Phase 21.8 — Backend -> Gateway -> Node command flow (docs/REMOTE_ACCESS.md /
// Phase 21 brief Section 20). Three legs:
//   1. Something external issues a command (POST .../commands) -- gated by a shared
//      admin key (Section 11's "never allow arbitrary unauthenticated remote
//      commands"), a deliberate placeholder for real user auth (no account model
//      exists yet -- docs/BACKEND.md's multi-tenant-readiness note). Locked by
//      default: if Admin:ApiKey isn't configured, issuing a command is refused
//      outright rather than silently left open.
//   2. The target Gateway polls for its own pending commands (device-credential
//      auth -- it can only ever see/claim commands addressed to itself).
//   3. The Gateway reports back what happened (also device-credential auth).
public static class CommandEndpoints
{
    public static void MapCommandEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/v1").WithTags("Commands");

        group.MapPost("/devices/{id}/commands",
            async (string id, HttpContext ctx, AppDbContext db, IConfiguration config,
                   EventBroadcaster broadcaster, CommandRequest body) =>
        {
            var adminKey = config["Admin:ApiKey"];
            if (string.IsNullOrEmpty(adminKey))
            {
                return Results.Problem(
                    "Remote commands are disabled: set Admin:ApiKey in configuration to enable issuing them.",
                    statusCode: 503);
            }
            if (!ctx.Request.Headers.TryGetValue("X-Admin-Key", out var presented) || presented != adminKey)
            {
                return Results.Unauthorized();
            }
            if (await db.Devices.FindAsync(id) is null)
            {
                return Results.NotFound(new { error = "unknown device" });
            }
            if (string.IsNullOrWhiteSpace(body.CommandType))
            {
                return Results.BadRequest(new { error = "commandType is required" });
            }

            var command = new CommandRecord
            {
                TargetDeviceId = id,
                CommandType = body.CommandType,
                PayloadJson = body.Payload.ValueKind == JsonValueKind.Undefined
                    ? "{}" : body.Payload.GetRawText(),
                IssuedAt = DateTime.UtcNow,
                ExpiresAt = DateTime.UtcNow.AddSeconds(Math.Clamp(body.TtlSeconds ?? 300, 10, 3600)),
                Status = "PENDING",
            };
            db.Commands.Add(command);
            await db.SaveChangesAsync();
            broadcaster.Publish("command.issued", id, new { command.CommandId, command.CommandType });
            return Results.Ok(new { command.CommandId, command.Status, command.ExpiresAt });
        });

        // The Gateway polls this for itself only — {id} must match the authenticated
        // device's own ID, not an arbitrary target, so one gateway can never read
        // (let alone claim) another's commands.
        group.MapGet("/devices/{id}/commands/pending", async (string id, HttpContext ctx, AppDbContext db) =>
        {
            var deviceId = ctx.User.FindFirstValue(ClaimTypes.NameIdentifier);
            if (deviceId is null || deviceId != id) return Results.Unauthorized();

            var now = DateTime.UtcNow;
            var expired = await db.Commands
                .Where(c => c.TargetDeviceId == id && c.Status == "PENDING" && c.ExpiresAt < now)
                .ToListAsync();
            foreach (var e in expired) e.Status = "EXPIRED";

            var pending = await db.Commands
                .Where(c => c.TargetDeviceId == id && c.Status == "PENDING" && c.ExpiresAt >= now)
                .OrderBy(c => c.IssuedAt)
                .ToListAsync();
            foreach (var p in pending) p.Status = "DELIVERED";

            await db.SaveChangesAsync();
            return Results.Ok(pending.Select(c => new
            {
                c.CommandId, c.CommandType, payload = JsonDocument.Parse(c.PayloadJson).RootElement,
                c.ExpiresAt,
            }));
        }).RequireAuthorization();

        group.MapPost("/devices/{id}/commands/{commandId}/result",
            async (string id, string commandId, HttpContext ctx, AppDbContext db,
                   EventBroadcaster broadcaster, CommandResultRequest body) =>
        {
            var deviceId = ctx.User.FindFirstValue(ClaimTypes.NameIdentifier);
            if (deviceId is null || deviceId != id) return Results.Unauthorized();

            var command = await db.Commands.FindAsync(commandId);
            if (command is null || command.TargetDeviceId != id) return Results.NotFound();

            command.Status = body.Success ? "EXECUTED" : "FAILED";
            command.ResultJson = body.Result.ValueKind == JsonValueKind.Undefined
                ? null : body.Result.GetRawText();
            command.CompletedAt = DateTime.UtcNow;
            await db.SaveChangesAsync();
            broadcaster.Publish(body.Success ? "command.executed" : "command.failed", id,
                new { commandId, command.CommandType });
            return Results.Ok(new { ok = true });
        }).RequireAuthorization();

        group.MapGet("/devices/{id}/commands", async (string id, int? limit, AppDbContext db) =>
            await db.Commands
                .Where(c => c.TargetDeviceId == id)
                .OrderByDescending(c => c.IssuedAt)
                .Take(Math.Clamp(limit ?? 20, 1, 200))
                .ToListAsync());
    }

    public record CommandRequest(string CommandType, JsonElement Payload, int? TtlSeconds);
    public record CommandResultRequest(bool Success, JsonElement Result);
}
