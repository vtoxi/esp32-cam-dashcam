namespace CarSentinel.Backend.Models;

// Phase 21.9 — Section 15 of the Phase 21 brief: "allow external systems to subscribe
// to events... without polling." Reuses the exact event-type taxonomy Phase 21.6's
// EventBroadcaster already established for SSE (device.online, motion.detected,
// incident.created, etc., plus evidence.uploaded and command.* added in 21.7/21.8) --
// one set of event names serving both delivery mechanisms, not two competing ones.
public class WebhookSubscription
{
    public int Id { get; set; }
    public string Url { get; set; } = string.Empty;

    // Comma-separated event types, or "*" for everything. Kept as a simple string
    // (not a normalized child table) -- this is a reference backend, not a system
    // expecting hundreds of subscriptions to filter efficiently.
    public string EventTypes { get; set; } = "*";

    // HMAC-SHA256 signing secret, generated server-side on creation. Unlike
    // Device.CredentialHash (this backend only ever needs to *verify* a device
    // credential, so a one-way hash is enough), this backend is the one *proving*
    // authenticity to the receiving webhook endpoint -- computing an HMAC on every
    // delivery needs the plaintext secret, not a hash of it. Stored in plaintext,
    // same documented-not-hidden posture as EmailConfig's SMTP password/
    // EspNowSecurity's key elsewhere in this project. Returned to the caller exactly
    // once, at creation.
    public string Secret { get; set; } = string.Empty;

    public bool Enabled { get; set; } = true;
    public DateTime CreatedAt { get; set; }
}

// Phase 21.9 — delivery logging (Section 15: "delivery logging"). One row per attempt,
// not per subscription -- a subscription's delivery history is its own audit trail.
public class WebhookDelivery
{
    public long Id { get; set; }
    public int SubscriptionId { get; set; }
    public string EventType { get; set; } = string.Empty;
    public bool Success { get; set; }
    public int? StatusCode { get; set; }
    public DateTime AttemptedAt { get; set; }
}
