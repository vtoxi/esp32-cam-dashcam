#pragma once

#include <Arduino.h>

// NEO-6M UART wrapper. Phase 3 scope: open the port and confirm bytes are arriving —
// proves the wiring/baud rate is right. NMEA parsing (TinyGPS++) lands in Phase 8.
namespace CarSentinel {

class GpsUart {
public:
    static void begin(int rxGpio, int txGpio, uint32_t baud = 9600);
    static int bytesAvailable();

    // Drains and discards available bytes, returning how many were read — used for a
    // basic "is the module actually sending NMEA data" liveness check in Phase 3.
    static int drain();
};

}  // namespace CarSentinel
