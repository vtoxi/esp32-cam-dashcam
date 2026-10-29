#include "MotionSensor.h"
#include "CapabilitiesConfig.h"
#include "Logger.h"

namespace CarSentinel {

static const char* TAG = "MotionSensor";
int MotionSensor::pin = GPIO_UNCONFIGURED;

void MotionSensor::begin(int gpio) {
    pin = gpio;
    pinMode(pin, INPUT);
    Logger::info(TAG, "RCWL-0516 init on GPIO " + String(pin));
}

bool MotionSensor::isTriggered() {
    if (pin == GPIO_UNCONFIGURED) {
        return false;
    }
    return digitalRead(pin) == HIGH;
}

}  // namespace CarSentinel
