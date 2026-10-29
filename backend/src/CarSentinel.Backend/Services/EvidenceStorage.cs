namespace CarSentinel.Backend.Services;

// Phase 21.7 — local-disk stand-in for real object storage (docs/BACKEND.md Section
// 23). One file per upload, path derived from (incidentId, nodeId, eventId) so
// repeated uploads of the same evidence (e.g. a retried gateway request) overwrite
// rather than accumulate duplicates.
public class EvidenceStorage
{
    private readonly string root;

    public EvidenceStorage(IConfiguration config)
    {
        root = config["EvidenceStorage:Root"] ?? Path.Combine(AppContext.BaseDirectory, "evidence");
        Directory.CreateDirectory(root);
    }

    public async Task<(string relativePath, long size)> SaveAsync(string incidentId, string nodeId,
        string eventId, Stream content)
    {
        string safeIncident = Sanitize(incidentId);
        string safeNode = Sanitize(nodeId);
        string safeEvent = Sanitize(eventId);
        string relativePath = Path.Combine(safeIncident, $"{safeNode}_{safeEvent}.jpg");
        string fullPath = Path.Combine(root, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(fullPath)!);

        await using var file = File.Create(fullPath);
        await content.CopyToAsync(file);
        return (relativePath, file.Length);
    }

    public string GetFullPath(string relativePath) => Path.Combine(root, relativePath);

    // Strips anything that isn't alphanumeric/-/_ — these values come from
    // gateway-supplied query parameters (nodeId/eventId) and the incidentId in the
    // route, none of which should ever need path separators or traversal sequences.
    // Defense in depth against a compromised/malicious gateway, not just a
    // well-behaved one (docs/SECURITY.md's "local doesn't mean trusted" principle,
    // extended to the Gateway<->Backend boundary too).
    private static string Sanitize(string value)
    {
        var chars = value.Where(c => char.IsLetterOrDigit(c) || c is '-' or '_').ToArray();
        var cleaned = new string(chars);
        return string.IsNullOrEmpty(cleaned) ? "unknown" : cleaned;
    }
}
