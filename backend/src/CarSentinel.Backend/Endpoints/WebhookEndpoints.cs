using System.Security.Cryptography;
using CarSentinel.Backend.Data;
using CarSentinel.Backend.Models;
using Microsoft.EntityFrameworkCore;

namespace CarSentinel.Backend.Endpoints;

// Phase 21.9 — CRUD for webhook subscriptions. Same admin-key gate as
// CommandEndpoints' issue route (Section 11: no arbitrary unauthenticated party
// should be able to register a URL to receive every event this backend sees) — a
// placeholder for real user auth until an account model exists (docs/BACKEND.md's
// multi-tenant-readiness note).
public static class WebhookEndpoints
{
    public static void MapWebhookEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/v1/webhooks").WithTags("Webhooks");

        group.MapPost("/", async (HttpContext ctx, AppDbContext db, IConfiguration config, WebhookCreateRequest body) =>
        {
            if (!IsAdmin(ctx, config)) return Results.Unauthorized();
            if (string.IsNullOrWhiteSpace(body.Url) || !Uri.TryCreate(body.Url, UriKind.Absolute, out _))
            {
                return Results.BadRequest(new { error = "a valid absolute url is required" });
            }

            var secret = Convert.ToHexString(RandomNumberGenerator.GetBytes(32)).ToLowerInvariant();
            var sub = new WebhookSubscription
            {
                Url = body.Url,
                EventTypes = string.IsNullOrWhiteSpace(body.EventTypes) ? "*" : body.EventTypes,
                Secret = secret,
                Enabled = true,
                CreatedAt = DateTime.UtcNow,
            };
            db.WebhookSubscriptions.Add(sub);
            await db.SaveChangesAsync();
            // The only time the plaintext secret is ever returned — the caller must
            // store it now to verify signatures later; we don't expose it again.
            return Results.Ok(new { sub.Id, sub.Url, sub.EventTypes, sub.Enabled, secret });
        });

        group.MapGet("/", async (HttpContext ctx, AppDbContext db, IConfiguration config) =>
        {
            if (!IsAdmin(ctx, config)) return Results.Unauthorized();
            var subs = await db.WebhookSubscriptions.ToListAsync();
            return Results.Ok(subs.Select(s => new { s.Id, s.Url, s.EventTypes, s.Enabled, s.CreatedAt }));
        });

        group.MapDelete("/{id:int}", async (int id, HttpContext ctx, AppDbContext db, IConfiguration config) =>
        {
            if (!IsAdmin(ctx, config)) return Results.Unauthorized();
            var sub = await db.WebhookSubscriptions.FindAsync(id);
            if (sub is null) return Results.NotFound();
            db.WebhookSubscriptions.Remove(sub);
            await db.SaveChangesAsync();
            return Results.Ok(new { ok = true });
        });

        group.MapGet("/{id:int}/deliveries", async (int id, int? limit, HttpContext ctx, AppDbContext db, IConfiguration config) =>
        {
            if (!IsAdmin(ctx, config)) return Results.Unauthorized();
            var deliveries = await db.WebhookDeliveries
                .Where(d => d.SubscriptionId == id)
                .OrderByDescending(d => d.AttemptedAt)
                .Take(Math.Clamp(limit ?? 20, 1, 200))
                .ToListAsync();
            return Results.Ok(deliveries);
        });
    }

    private static bool IsAdmin(HttpContext ctx, IConfiguration config)
    {
        var adminKey = config["Admin:ApiKey"];
        if (string.IsNullOrEmpty(adminKey)) return false;
        return ctx.Request.Headers.TryGetValue("X-Admin-Key", out var presented) && presented == adminKey;
    }

    public record WebhookCreateRequest(string Url, string? EventTypes);
}
