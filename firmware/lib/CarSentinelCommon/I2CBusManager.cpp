#include "I2CBusManager.h"
#include "Logger.h"

#include <Wire.h>
#include <soc/soc_caps.h>  // SOC_I2C_NUM — Wire1 only exists on chips with >1 I2C peripheral

namespace CarSentinel {

static const char* TAG = "I2CBusManager";

static TwoWire* busFor(uint8_t busIndex) {
#if SOC_I2C_NUM > 1
    if (busIndex == 1) return &Wire1;
#endif
    return &Wire;
}

bool I2CBusManager::begin(uint8_t busIndex, int sdaGpio, int sclGpio) {
    if (sdaGpio < 0 || sclGpio < 0) {
        Logger::warn(TAG, "I2C bus " + String(busIndex) + " has unconfigured pins; skipping");
        return false;
    }

    bool ok;
    if (busIndex == 0) {
        ok = Wire.begin(sdaGpio, sclGpio);
#if SOC_I2C_NUM > 1
    } else if (busIndex == 1) {
        ok = Wire1.begin(sdaGpio, sclGpio);
#endif
    } else {
        Logger::error(TAG, "Invalid I2C bus index " + String(busIndex));
        return false;
    }

    Logger::info(TAG, "I2C bus " + String(busIndex) + " init " + (ok ? "OK" : "FAILED") +
                 " sda=" + String(sdaGpio) + " scl=" + String(sclGpio));
    return ok;
}

bool I2CBusManager::isPresent(uint8_t busIndex, uint8_t address) {
    TwoWire* bus = busFor(busIndex);
    bus->beginTransmission(address);
    uint8_t result = bus->endTransmission();
    return result == 0;
}

bool I2CBusManager::writeRegister(uint8_t busIndex, uint8_t address, uint8_t reg, uint8_t value) {
    TwoWire* bus = busFor(busIndex);
    bus->beginTransmission(address);
    bus->write(reg);
    bus->write(value);
    return bus->endTransmission() == 0;
}

bool I2CBusManager::readRegisters(uint8_t busIndex, uint8_t address, uint8_t startReg,
                                   uint8_t* buffer, size_t len) {
    TwoWire* bus = busFor(busIndex);
    bus->beginTransmission(address);
    bus->write(startReg);
    if (bus->endTransmission(false) != 0) {  // repeated start, keep the bus held
        return false;
    }
    size_t received = bus->requestFrom((int)address, (int)len);
    if (received != len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        buffer[i] = bus->read();
    }
    return true;
}

}  // namespace CarSentinel
