using CarSentinel.Backend.Models;
using Microsoft.EntityFrameworkCore;

namespace CarSentinel.Backend.Data;

// Phase 21.5 — SQLite via EF Core, EnsureCreated() at startup (Program.cs), not
// migrations. docs/BACKEND.md Section 23 ("don't store large binaries in the main
// relational database") and Section 33 ("keep the embedded implementation
// memory-conscious" — this is the backend, not the embedded side, but the same
// "avoid unnecessary infrastructure" spirit applies): SQLite needs no separate
// server process, matching "reuse existing technology, avoid introducing unnecessary
// infrastructure" from the Phase 21 brief's Section 28. Swapping to a real SQL Server/
// Postgres later is a connection-string + provider-package change, not a rewrite —
// nothing above this class depends on SQLite specifically.
public class AppDbContext : DbContext
{
    public AppDbContext(DbContextOptions<AppDbContext> options) : base(options) { }

    public DbSet<Device> Devices => Set<Device>();
    public DbSet<TelemetryRecord> Telemetry => Set<TelemetryRecord>();
    public DbSet<EventRecord> Events => Set<EventRecord>();
    public DbSet<IncidentRecord> Incidents => Set<IncidentRecord>();
    public DbSet<EvidenceRecord> Evidence => Set<EvidenceRecord>();

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<Device>().HasKey(d => d.Id);
        modelBuilder.Entity<IncidentRecord>().HasKey(i => i.IncidentId);
        modelBuilder.Entity<TelemetryRecord>().HasIndex(t => t.DeviceId);
        modelBuilder.Entity<EventRecord>().HasIndex(e => e.DeviceId);
        modelBuilder.Entity<IncidentRecord>().HasIndex(i => i.DeviceId);
        modelBuilder.Entity<EvidenceRecord>().HasIndex(e => e.IncidentId);
    }
}
