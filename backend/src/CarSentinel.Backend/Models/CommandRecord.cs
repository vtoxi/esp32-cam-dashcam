namespace CarSentinel.Backend.Models;

// Phase 21.8 — Backend -> Gateway -> Node command flow. Polling, not push: the Gateway
// already polls for registration/heartbeat on its own timer (RemoteSyncManager.cpp),
// and adding a persistent connection just for commands would be new infrastructure
// (docs/BACKEND.md's "avoid unnecessary infrastructure") for a use case a short poll
// interval already serves well enough.
public class CommandRecord
{
    public string CommandId { get; set; } = Guid.NewGuid().ToString();
    public string TargetDeviceId { get; set; } = string.Empty;
    public string CommandType { get; set; } = string.Empty;
    public string PayloadJson { get; set; } = "{}";
    public DateTime IssuedAt { get; set; }
    public DateTime ExpiresAt { get; set; }
    public string Status { get; set; } = "PENDING";  // PENDING | DELIVERED | EXECUTED | FAILED | EXPIRED | REJECTED
    public string? ResultJson { get; set; }
    public DateTime? CompletedAt { get; set; }
}
