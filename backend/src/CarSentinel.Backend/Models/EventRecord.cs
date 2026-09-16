namespace CarSentinel.Backend.Models;

// Phase 21.5 — one row per POST /api/v1/events (e.g. MOTION_DETECTED forwarded from
// TransportManager::sendEvent() on the node, via the Gateway). Same raw-JSON-payload
// reasoning as TelemetryRecord.
public class EventRecord
{
    public long Id { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public DateTime ReceivedAt { get; set; }
    public string PayloadJson { get; set; } = string.Empty;
}
