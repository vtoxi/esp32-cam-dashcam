#include "TemperatureHumiditySensor.h"
#include "Logger.h"

#include <DHT.h>

namespace CarSentinel {

static const char* TAG = "TemperatureHumiditySensor";
static DHT* dhtSensor = nullptr;
bool TemperatureHumiditySensor::initialized = false;

void TemperatureHumiditySensor::begin(int gpio) {
    if (dhtSensor != nullptr) {
        delete dhtSensor;
    }
    dhtSensor = new DHT(gpio, DHT11);
    dhtSensor->begin();
    initialized = true;
    Logger::info(TAG, "DHT11 init on GPIO " + String(gpio));
}

TemperatureHumidityReading TemperatureHumiditySensor::read() {
    TemperatureHumidityReading reading;
    if (!initialized || dhtSensor == nullptr) {
        return reading;
    }
    reading.temperatureC = dhtSensor->readTemperature();
    reading.humidityPercent = dhtSensor->readHumidity();
    reading.valid = !isnan(reading.temperatureC) && !isnan(reading.humidityPercent);
    if (!reading.valid) {
        Logger::warn(TAG, "DHT11 read returned NaN (wiring/timing issue, or <1s since last read)");
    }
    return reading;
}

}  // namespace CarSentinel
