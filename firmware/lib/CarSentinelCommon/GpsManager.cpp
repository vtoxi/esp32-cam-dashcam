#include "GpsManager.h"
#include "Logger.h"

#include <TinyGPSPlus.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "GpsManager";
static TinyGPSPlus tinyGps;
static HardwareSerial gpsSerial(1);  // UART1 — UART0 is reserved for USB/console

GpsFix GpsManager::currentFix;

void GpsManager::begin(int rxGpio, int txGpio, uint32_t baud) {
    gpsSerial.begin(baud, SERIAL_8N1, rxGpio, txGpio);
    Logger::info(TAG, "GPS UART1 init baud=" + String(baud) +
                 " rx=" + String(rxGpio) + " tx=" + String(txGpio));
}

void GpsManager::loop() {
    while (gpsSerial.available()) {
        char c = gpsSerial.read();
        if (!tinyGps.encode(c)) {
            continue;  // mid-sentence; nothing new to extract yet
        }

        bool gotFixThisSentence = false;
        if (tinyGps.location.isValid() && tinyGps.location.isUpdated()) {
            currentFix.latitude = tinyGps.location.lat();
            currentFix.longitude = tinyGps.location.lng();
            gotFixThisSentence = true;
        }
        if (tinyGps.altitude.isValid()) currentFix.altitudeMeters = tinyGps.altitude.meters();
        if (tinyGps.speed.isValid()) currentFix.speedKmph = tinyGps.speed.kmph();
        if (tinyGps.course.isValid()) currentFix.courseDegrees = tinyGps.course.deg();
        if (tinyGps.satellites.isValid()) currentFix.satellites = tinyGps.satellites.value();

        if (gotFixThisSentence) {
            bool hadFixBefore = (currentFix.status == GpsFixStatus::FIX);
            currentFix.status = GpsFixStatus::FIX;
            currentFix.lastUpdateMs = millis();
            if (!hadFixBefore) {
                Logger::info(TAG, "GPS fix acquired: sats=" + String(currentFix.satellites));
            }
        }
    }

    // Section 21: don't keep reporting an increasingly stale position as "current" —
    // downgrade back to NO_FIX if nothing has refreshed it recently (antenna
    // disconnected, moved indoors, satellites lost, etc.).
    if (currentFix.status == GpsFixStatus::FIX &&
        millis() - currentFix.lastUpdateMs > FIX_STALE_MS) {
        currentFix.status = GpsFixStatus::NO_FIX;
        Logger::warn(TAG, "GPS fix lost (no update in " + String(FIX_STALE_MS / 1000) + "s)");
    }
}

GpsFix GpsManager::getFix() {
    return currentFix;
}

bool GpsManager::hasFix() {
    return currentFix.status == GpsFixStatus::FIX;
}

String GpsManager::toJson() {
    JsonDocument doc;
    if (currentFix.status == GpsFixStatus::FIX) {
        doc["lat"] = currentFix.latitude;
        doc["lon"] = currentFix.longitude;
        doc["altM"] = currentFix.altitudeMeters;
        doc["speedKmph"] = currentFix.speedKmph;
        doc["courseDeg"] = currentFix.courseDegrees;
        doc["sats"] = currentFix.satellites;
    } else {
        doc["status"] = "NO_FIX";
    }
    String out;
    serializeJson(doc, out);
    return out;
}

}  // namespace CarSentinel
