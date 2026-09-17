namespace CarSentinel.Backend.Models;

// Phase 21.7 — metadata only; the actual JPEG bytes live on disk under
// EvidenceStorage's configured root (docs/BACKEND.md Section 23: "don't store large
// binaries directly in the main relational database" — a local folder standing in
// for real object storage in this reference implementation, same
// "avoid unnecessary infrastructure" reasoning as SQLite standing in for a full DB
// server).
public class EvidenceRecord
{
    public long Id { get; set; }
    public string IncidentId { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;  // the gateway that uploaded it
    public string NodeId { get; set; } = string.Empty;    // the camera node that captured it
    public string EventId { get; set; } = string.Empty;   // that node's own EvidenceManager event ID
    public string ContentType { get; set; } = "image/jpeg";
    public long SizeBytes { get; set; }
    public string StoragePath { get; set; } = string.Empty;  // relative path under the evidence root
    public DateTime UploadedAt { get; set; }
}
