namespace CarSentinel.Backend.Models;

// Phase 21.5 — one row per Gateway. Camera/sensor nodes never register here directly
// (docs/BACKEND.md Section 5: "Node -> ESP-NOW -> Gateway -> HTTPS -> Backend" — the
// Gateway is the only device this backend ever talks to).
public class Device
{
    // Server-assigned on first registration ("gw-" + a short random suffix), unless
    // the operator already set one via the dashboard's Settings page / BACKENDCONFIG
    // before ever registering — in that case RemoteSyncManager treats the device as
    // already registered and never calls /register at all, so this Id only ever gets
    // server-assigned for a device that used the zero-config path.
    public string Id { get; set; } = string.Empty;

    // SHA-256 hash of the device credential, never the plaintext (Section 11 of the
    // Phase 21 brief: "credentials must never be logged" — extended here to "never
    // stored reversibly either", a stricter posture than most of this project's other
    // credentials, which are plaintext-on-disk-on-the-device with that limitation
    // documented rather than hidden; a backend holding many devices' credentials is a
    // higher-value target, so it gets the stronger treatment).
    public string CredentialHash { get; set; } = string.Empty;

    public string HardwareProfile { get; set; } = string.Empty;
    public string FirmwareVersion { get; set; } = string.Empty;
    public string NodeId { get; set; } = string.Empty;
    public string? TenantId { get; set; }

    public DateTime RegisteredAt { get; set; }
    public DateTime LastSeenAt { get; set; }
}
