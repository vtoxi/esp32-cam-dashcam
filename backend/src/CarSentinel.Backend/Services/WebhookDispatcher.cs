using System.Security.Cryptography;
using System.Text;
using CarSentinel.Backend.Data;
using CarSentinel.Backend.Models;
using Microsoft.EntityFrameworkCore;

namespace CarSentinel.Backend.Services;

// Phase 21.9 — Section 15 of the Phase 21 brief: HTTPS delivery, signature, retry,
// timeout, delivery logging, configurable subscriptions. Attaches to
// EventBroadcaster.OnEvent (Program.cs, at startup) rather than being called
// directly from endpoints — every event already published for SSE (Phase 21.6) is
// exactly the set of events worth offering as webhooks too, same taxonomy, one
// publish call per event site.
public class WebhookDispatcher
{
    private readonly IServiceScopeFactory scopeFactory;
    private readonly IHttpClientFactory httpClientFactory;
    private readonly ILogger<WebhookDispatcher> logger;

    public WebhookDispatcher(IServiceScopeFactory scopeFactory, IHttpClientFactory httpClientFactory,
        ILogger<WebhookDispatcher> logger)
    {
        this.scopeFactory = scopeFactory;
        this.httpClientFactory = httpClientFactory;
        this.logger = logger;
    }

    public void Attach(EventBroadcaster broadcaster)
    {
        broadcaster.OnEvent += (eventType, deviceId, envelopeJson) =>
        {
            // Fire-and-forget from the caller's perspective (Publish() must stay
            // synchronous/fast — it's called from request-handling code) but each
            // dispatch runs to completion on its own Task; DispatchAsync creates its
            // own DI scope since it outlives the request that triggered it.
            _ = DispatchAsync(eventType, envelopeJson);
        };
    }

    private async Task DispatchAsync(string eventType, string envelopeJson)
    {
        using var scope = scopeFactory.CreateScope();
        var db = scope.ServiceProvider.GetRequiredService<AppDbContext>();

        var subscriptions = await db.WebhookSubscriptions
            .Where(s => s.Enabled)
            .ToListAsync();

        foreach (var sub in subscriptions)
        {
            bool matches = sub.EventTypes == "*" ||
                sub.EventTypes.Split(',', StringSplitOptions.TrimEntries).Contains(eventType);
            if (!matches) continue;

            await DeliverWithRetryAsync(sub, eventType, envelopeJson, db);
        }
    }

    // One retry (Section 15: "retry") — a webhook receiver being briefly slow/down is
    // common enough to deserve one immediate second attempt; anything beyond that
    // would need a persisted retry queue (the same category of gap this project
    // already documents honestly for BackendQueue/evidence uploads — not built here
    // either) rather than blocking this dispatch indefinitely.
    private async Task DeliverWithRetryAsync(WebhookSubscription sub, string eventType,
        string envelopeJson, AppDbContext db)
    {
        for (int attempt = 1; attempt <= 2; attempt++)
        {
            var (success, statusCode) = await TryDeliverAsync(sub, envelopeJson);
            db.WebhookDeliveries.Add(new WebhookDelivery
            {
                SubscriptionId = sub.Id,
                EventType = eventType,
                Success = success,
                StatusCode = statusCode,
                AttemptedAt = DateTime.UtcNow,
            });
            await db.SaveChangesAsync();

            if (success) return;
            if (attempt == 1) await Task.Delay(1000);
        }
        logger.LogWarning("Webhook delivery to {Url} failed after 2 attempts (event {EventType})",
            sub.Url, eventType);
    }

    private async Task<(bool success, int? statusCode)> TryDeliverAsync(WebhookSubscription sub, string envelopeJson)
    {
        try
        {
            var client = httpClientFactory.CreateClient();
            client.Timeout = TimeSpan.FromSeconds(10);

            using var request = new HttpRequestMessage(HttpMethod.Post, sub.Url)
            {
                Content = new StringContent(envelopeJson, Encoding.UTF8, "application/json"),
            };
            // HMAC-SHA256 over the raw body, same "prove this came from us" posture
            // as any webhook signature scheme (GitHub/Stripe-style) — the receiver
            // recomputes it with the secret it was given at subscription time and
            // compares.
            var signature = Convert.ToHexString(
                HMACSHA256.HashData(Encoding.UTF8.GetBytes(sub.Secret), Encoding.UTF8.GetBytes(envelopeJson)));
            request.Headers.Add("X-CarSentinel-Signature", "sha256=" + signature.ToLowerInvariant());

            var response = await client.SendAsync(request);
            return (response.IsSuccessStatusCode, (int)response.StatusCode);
        }
        catch (Exception ex)
        {
            logger.LogWarning(ex, "Webhook delivery to {Url} threw", sub.Url);
            return (false, null);
        }
    }
}
