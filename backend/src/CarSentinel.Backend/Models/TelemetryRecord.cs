namespace CarSentinel.Backend.Models;

// Phase 21.5 — one row per POST /api/v1/telemetry. PayloadJson is stored as-received
// rather than mapped onto rigid columns: the gateway's telemetry shape
// (buildStatusJson() in gateway_main.cpp) isn't a frozen schema yet, and re-parsing a
// raw JSON blob on read is a reasonable trade against forcing a backend schema
// migration every time the firmware's status payload gains a field.
public class TelemetryRecord
{
    public long Id { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public DateTime ReceivedAt { get; set; }
    public string PayloadJson { get; set; } = string.Empty;
}
