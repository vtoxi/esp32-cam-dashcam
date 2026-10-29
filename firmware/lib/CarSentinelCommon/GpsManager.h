#pragma once

#include <Arduino.h>

// NEO-6M GPS subsystem (Section 21) — owns UART1 and NMEA parsing directly, superseding
// Phase 3's GpsUart (which only proved the wiring/baud rate with a raw byte-liveness
// check; full parsing was explicitly deferred here). Built on TinyGPS++.
//
// Section 21's hard requirement: GPS must never be required for security operation. No
// fix is a normal state (gps.status = NO_FIX), not an error condition — every accessor
// here degrades gracefully rather than blocking or fabricating a position.
namespace CarSentinel {

enum class GpsFixStatus : uint8_t {
    NO_FIX = 0,
    FIX = 1
};

struct GpsFix {
    GpsFixStatus status = GpsFixStatus::NO_FIX;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitudeMeters = 0.0;
    double speedKmph = 0.0;
    double courseDegrees = 0.0;
    uint32_t satellites = 0;
    unsigned long lastUpdateMs = 0;  // millis() this fix was last refreshed
};

class GpsManager {
public:
    static void begin(int rxGpio, int txGpio, uint32_t baud = 9600);

    // Call every loop() iteration (not interval-gated — NMEA sentences arrive
    // continuously at 9600 baud and HardwareSerial's internal buffer is finite).
    static void loop();

    static GpsFix getFix();
    static bool hasFix();

    // Compact JSON for embedding in event/incident logs and (Phase 11+) payloads:
    // {"lat":...,"lon":...,"altM":...,"speedKmph":...,"courseDeg":...,"sats":...} when
    // fixed, or {"status":"NO_FIX"} otherwise — callers get an honest absence, never a
    // stale or fabricated position.
    static String toJson();

private:
    static GpsFix currentFix;
    // If no NMEA update refreshes the fix within this window, it's downgraded back to
    // NO_FIX rather than reporting an increasingly stale position as current.
    static const unsigned long FIX_STALE_MS = 10000;
};

}  // namespace CarSentinel
