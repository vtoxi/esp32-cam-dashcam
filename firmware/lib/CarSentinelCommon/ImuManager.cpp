#include "ImuManager.h"
#include "I2CBusManager.h"
#include "Logger.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "ImuManager";
static const char* THRESHOLDS_PATH = "/config/imu_thresholds.json";

// MPU6050 register map (subset used here) — standard, stable across the part's
// datasheet revisions.
static const uint8_t REG_PWR_MGMT_1 = 0x6B;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;  // 14 contiguous bytes: accel(6) + temp(2) + gyro(6)

// Default full-scale ranges (register left at power-on default: +/-2g, +/-250 deg/s).
static const float ACCEL_LSB_PER_G = 16384.0f;
static const float GYRO_LSB_PER_DPS = 131.0f;

bool ImuManager::initialized = false;
uint8_t ImuManager::bus = 0;
uint8_t ImuManager::address = 0x68;
ImuThresholdsData ImuManager::thresholds;
unsigned long ImuManager::lastImpactMs = 0;

bool ImuManager::loadThresholds() {
    if (!LittleFS.exists(THRESHOLDS_PATH)) {
        return false;
    }
    File f = LittleFS.open(THRESHOLDS_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Logger::error(TAG, "imu_thresholds.json parse failed: " + String(err.c_str()));
        return false;
    }
    thresholds.accelMagnitudeG = doc["accelMagnitudeG"] | 2.5f;
    thresholds.gyroMagnitudeDps = doc["gyroMagnitudeDps"] | 200.0f;
    thresholds.cooldownMs = doc["cooldownMs"] | 5000;
    return true;
}

bool ImuManager::saveThresholds() {
    JsonDocument doc;
    doc["accelMagnitudeG"] = thresholds.accelMagnitudeG;
    doc["gyroMagnitudeDps"] = thresholds.gyroMagnitudeDps;
    doc["cooldownMs"] = thresholds.cooldownMs;
    File f = LittleFS.open(THRESHOLDS_PATH, "w");
    if (!f) return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

bool ImuManager::begin(uint8_t busIndex, uint8_t i2cAddress) {
    bus = busIndex;
    address = i2cAddress;

    if (!loadThresholds()) {
        thresholds = ImuThresholdsData();
        saveThresholds();
        Logger::info(TAG, "No persisted thresholds found; wrote defaults to " + String(THRESHOLDS_PATH));
    }

    // Clear the sleep bit (bit 6) — MPU6050 powers up in sleep mode.
    if (!I2CBusManager::writeRegister(bus, address, REG_PWR_MGMT_1, 0x00)) {
        Logger::error(TAG, "MPU6050 wake write failed — not initialized");
        initialized = false;
        return false;
    }

    initialized = true;
    Logger::info(TAG, "MPU6050 initialized on bus " + String(bus) + " addr=0x" + String(address, HEX) +
                 " thresholds: accelMagnitudeG=" + String(thresholds.accelMagnitudeG, 2) +
                 " gyroMagnitudeDps=" + String(thresholds.gyroMagnitudeDps, 1) +
                 " cooldownMs=" + String(thresholds.cooldownMs));
    return true;
}

bool ImuManager::isInitialized() {
    return initialized;
}

ImuReading ImuManager::read() {
    ImuReading r;
    if (!initialized) {
        return r;
    }

    uint8_t buf[14];
    if (!I2CBusManager::readRegisters(bus, address, REG_ACCEL_XOUT_H, buf, sizeof(buf))) {
        Logger::warn(TAG, "MPU6050 register read failed");
        return r;
    }

    int16_t rawAx = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t rawAy = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t rawAz = (int16_t)((buf[4] << 8) | buf[5]);
    // buf[6..7] is temperature — not surfaced yet, DHT already covers cabin temperature.
    int16_t rawGx = (int16_t)((buf[8] << 8) | buf[9]);
    int16_t rawGy = (int16_t)((buf[10] << 8) | buf[11]);
    int16_t rawGz = (int16_t)((buf[12] << 8) | buf[13]);

    r.accelXg = rawAx / ACCEL_LSB_PER_G;
    r.accelYg = rawAy / ACCEL_LSB_PER_G;
    r.accelZg = rawAz / ACCEL_LSB_PER_G;
    r.gyroXdps = rawGx / GYRO_LSB_PER_DPS;
    r.gyroYdps = rawGy / GYRO_LSB_PER_DPS;
    r.gyroZdps = rawGz / GYRO_LSB_PER_DPS;

    r.accelMagnitudeG = sqrtf(r.accelXg * r.accelXg + r.accelYg * r.accelYg + r.accelZg * r.accelZg);
    r.gyroMagnitudeDps = sqrtf(r.gyroXdps * r.gyroXdps + r.gyroYdps * r.gyroYdps + r.gyroZdps * r.gyroZdps);
    r.valid = true;
    return r;
}

bool ImuManager::checkImpact(const ImuReading& reading) {
    if (!reading.valid) return false;

    unsigned long now = millis();
    if (now - lastImpactMs < thresholds.cooldownMs) {
        return false;
    }

    bool exceeded = reading.accelMagnitudeG > thresholds.accelMagnitudeG ||
                     reading.gyroMagnitudeDps > thresholds.gyroMagnitudeDps;
    if (exceeded) {
        lastImpactMs = now;
        Logger::warn(TAG, "Threshold exceeded: accelMagnitudeG=" + String(reading.accelMagnitudeG, 2) +
                     " (limit " + String(thresholds.accelMagnitudeG, 2) + ") gyroMagnitudeDps=" +
                     String(reading.gyroMagnitudeDps, 1) + " (limit " + String(thresholds.gyroMagnitudeDps, 1) +
                     ") — reporting IMPACT_EVENT, not a crash determination (Section 23)");
    }
    return exceeded;
}

const ImuThresholdsData& ImuManager::getThresholds() {
    return thresholds;
}

bool ImuManager::setThresholds(const ImuThresholdsData& newThresholds) {
    thresholds = newThresholds;
    return saveThresholds();
}

}  // namespace CarSentinel
