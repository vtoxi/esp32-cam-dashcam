#include "I2CBusManager.h"
#include "Logger.h"

#include <Wire.h>
#include <soc/soc_caps.h>  // SOC_I2C_NUM — Wire1 only exists on chips with >1 I2C peripheral

namespace CarSentinel {

static const char* TAG = "I2CBusManager";

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
    TwoWire* bus = &Wire;
#if SOC_I2C_NUM > 1
    if (busIndex == 1) {
        bus = &Wire1;
    }
#endif
    bus->beginTransmission(address);
    uint8_t result = bus->endTransmission();
    return result == 0;
}

}  // namespace CarSentinel
