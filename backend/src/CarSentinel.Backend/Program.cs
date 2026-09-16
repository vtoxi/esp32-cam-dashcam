using CarSentinel.Backend.Data;
using CarSentinel.Backend.Endpoints;
using CarSentinel.Backend.Services;
using Microsoft.AspNetCore.Authentication;
using Microsoft.EntityFrameworkCore;

var builder = WebApplication.CreateBuilder(args);

// SQLite by default — zero external infrastructure to run this (docs/BACKEND.md's
// "avoid introducing unnecessary infrastructure"). Override via the
// ConnectionStrings:Default config key (env var, appsettings, etc.) for a real
// deployment; nothing above AppDbContext depends on SQLite specifically.
var connectionString = builder.Configuration.GetConnectionString("Default")
    ?? "Data Source=carsentinel.db";
builder.Services.AddDbContext<AppDbContext>(options => options.UseSqlite(connectionString));

builder.Services.AddAuthentication("DeviceCredential")
    .AddScheme<Microsoft.AspNetCore.Authentication.AuthenticationSchemeOptions,
        CarSentinel.Backend.Auth.DeviceCredentialAuthenticationHandler>("DeviceCredential", null);
builder.Services.AddAuthorization();
builder.Services.AddSingleton<EventBroadcaster>();

builder.Services.AddEndpointsApiExplorer();
builder.Services.AddSwaggerGen(c =>
{
    c.SwaggerDoc("v1", new Microsoft.OpenApi.Models.OpenApiInfo
    {
        Title = "CarSentinel Backend API",
        Version = "v1",
        Description = "Phase 21 reference backend — device registration, telemetry, " +
            "events, and incident sync for a CarSentinel gateway. See " +
            "docs/BACKEND.md and docs/REMOTE_ACCESS.md in the main repository.",
    });
    c.AddSecurityDefinition("DeviceCredential", new Microsoft.OpenApi.Models.OpenApiSecurityScheme
    {
        Type = Microsoft.OpenApi.Models.SecuritySchemeType.Http,
        Scheme = "bearer",
        Description = "Device credential issued by /api/v1/register. Also requires an " +
            "X-CarSentinel-Device-Id header (Swagger UI's 'Authorize' dialog only " +
            "covers the bearer token — add the device-id header per request when " +
            "testing manually).",
    });
});

var app = builder.Build();

// EnsureCreated(), not migrations — a deliberate scope cut for this first reference
// backend (docs/IMPLEMENTATION_PLAN.md's Phase 21.5 entry), not an oversight. Fine for
// a fresh SQLite file; a real deployment with an existing database and a need to
// evolve the schema over time should switch to EF migrations before going further.
using (var scope = app.Services.CreateScope())
{
    scope.ServiceProvider.GetRequiredService<AppDbContext>().Database.EnsureCreated();
}

app.UseSwagger();
app.UseSwaggerUI(c =>
{
    c.SwaggerEndpoint("/swagger/v1/swagger.json", "CarSentinel Backend API v1");
});

app.UseAuthentication();
app.UseAuthorization();

app.MapIngestEndpoints();
app.MapQueryEndpoints();
app.MapStreamEndpoints();

app.MapGet("/", () => Results.Redirect("/swagger")).ExcludeFromDescription();

app.Run();
