using System.Security.Claims;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using CarSentinel.Backend.Data;
using Microsoft.AspNetCore.Authentication;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;

namespace CarSentinel.Backend.Auth;

// Phase 21.5 — validates the exact scheme HttpBackend.cpp already sends (docs/BACKEND.md
// Section 3 / firmware/lib/CarSentinelGateway/HttpBackend.cpp's post()):
//   Authorization: Bearer <credential>
//   X-CarSentinel-Device-Id: <deviceId>
// Per-device credential (Section 11 of the Phase 21 brief: "do not use a hard-coded
// global API key shared by every installation") — not a single shared secret,
// checked against that specific device's stored hash. /register is the one endpoint
// that allows anonymous access (a device has no credential yet on its first-ever
// registration) — see Endpoints/RegisterEndpoint.cs.
public class DeviceCredentialAuthenticationHandler : AuthenticationHandler<AuthenticationSchemeOptions>
{
    private readonly AppDbContext db;

    public DeviceCredentialAuthenticationHandler(
        IOptionsMonitor<AuthenticationSchemeOptions> options,
        ILoggerFactory logger,
        UrlEncoder encoder,
        AppDbContext db)
        : base(options, logger, encoder)
    {
        this.db = db;
    }

    protected override async Task<AuthenticateResult> HandleAuthenticateAsync()
    {
        if (!Request.Headers.TryGetValue("Authorization", out var authHeader) ||
            !authHeader.ToString().StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase))
        {
            return AuthenticateResult.Fail("Missing or malformed Authorization header");
        }
        if (!Request.Headers.TryGetValue("X-CarSentinel-Device-Id", out var deviceIdHeader) ||
            string.IsNullOrWhiteSpace(deviceIdHeader))
        {
            return AuthenticateResult.Fail("Missing X-CarSentinel-Device-Id header");
        }

        var credential = authHeader.ToString()["Bearer ".Length..].Trim();
        var deviceId = deviceIdHeader.ToString();

        var device = await db.Devices.FirstOrDefaultAsync(d => d.Id == deviceId);
        if (device is null)
        {
            return AuthenticateResult.Fail("Unknown device — not registered");
        }

        if (!FixedTimeEquals(HashCredential(credential), device.CredentialHash))
        {
            return AuthenticateResult.Fail("Invalid credential");
        }

        var identity = new ClaimsIdentity(new[] { new Claim(ClaimTypes.NameIdentifier, deviceId) },
            Scheme.Name);
        var ticket = new AuthenticationTicket(new ClaimsPrincipal(identity), Scheme.Name);
        return AuthenticateResult.Success(ticket);
    }

    public static string HashCredential(string credential)
    {
        var bytes = SHA256.HashData(Encoding.UTF8.GetBytes(credential));
        return Convert.ToHexString(bytes);
    }

    // Constant-time comparison — a credential check is exactly the kind of place a
    // timing side-channel matters (docs/SECURITY.md's existing posture on ESP-NOW's
    // own HMAC already treats message validation as a real trust boundary; this
    // extends the same care to the new Gateway<->Backend one, docs/REMOTE_ACCESS.md
    // Section 7).
    private static bool FixedTimeEquals(string a, string b)
    {
        var aBytes = Encoding.UTF8.GetBytes(a);
        var bBytes = Encoding.UTF8.GetBytes(b);
        if (aBytes.Length != bBytes.Length) return false;
        return CryptographicOperations.FixedTimeEquals(aBytes, bBytes);
    }
}
