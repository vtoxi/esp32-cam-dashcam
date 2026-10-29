namespace CarSentinel.Backend.Models;

// Phase 21.5 — one row per incident, keyed by the incidentId IncidentCorrelator
// already generates on the gateway (docs/REMOTE_ACCESS.md Section 4: "the remote
// API's incident schema should be the same shape, not a redesign"). Upserted, not
// appended — an incident is reported multiple times as it moves through its state
// machine (DETECTED -> ... -> CLOSED per IncidentCorrelator.h), and the backend
// should hold the latest state, not one row per transition. This is the idempotency
// behavior Section 13 of the Phase 21 brief asked for, scoped to incidents
// specifically (events/telemetry are still append-only — see their own models).
public class IncidentRecord
{
    // The gateway's own IncidentCorrelator-assigned ID (e.g. "INCIDENT-000042"), used
    // directly as the primary key here rather than a server-generated one.
    public string IncidentId { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;
    public string State { get; set; } = "UNKNOWN";
    public DateTime FirstReceivedAt { get; set; }
    public DateTime LastReceivedAt { get; set; }
    public string PayloadJson { get; set; } = string.Empty;
}
