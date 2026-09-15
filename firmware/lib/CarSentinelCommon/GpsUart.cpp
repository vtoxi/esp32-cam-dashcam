#include "GpsUart.h"
#include "Logger.h"

namespace CarSentinel {

static const char* TAG = "GpsUart";
static HardwareSerial gpsSerial(1);  // UART1 — UART0 is reserved for USB/console

void GpsUart::begin(int rxGpio, int txGpio, uint32_t baud) {
    gpsSerial.begin(baud, SERIAL_8N1, rxGpio, txGpio);
    Logger::info(TAG, "GPS UART1 init baud=" + String(baud) +
                 " rx=" + String(rxGpio) + " tx=" + String(txGpio));
}

int GpsUart::bytesAvailable() {
    return gpsSerial.available();
}

int GpsUart::drain() {
    int count = 0;
    while (gpsSerial.available()) {
        gpsSerial.read();
        count++;
    }
    return count;
}

}  // namespace CarSentinel
