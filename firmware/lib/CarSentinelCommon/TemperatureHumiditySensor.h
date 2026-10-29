#pragma once

#include <Arduino.h>

// DHT11 wrapper (Adafruit DHT sensor library). Phase 3 scope: init + a bounds-checked
// read. Reads are inherently slow (DHT11 minimum ~1s between samples) — callers must
// throttle, this class does not rate-limit internally.
namespace CarSentinel {

struct TemperatureHumidityReading {
    bool valid = false;
    float temperatureC = NAN;
    float humidityPercent = NAN;
};

class TemperatureHumiditySensor {
public:
    static void begin(int gpio);
    static TemperatureHumidityReading read();

private:
    static bool initialized;
};

}  // namespace CarSentinel
