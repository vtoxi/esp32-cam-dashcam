#include "IgnitionSense.h"
#include "Logger.h"

namespace CarSentinel {

static const char* TAG = "IgnitionSense";

int IgnitionSense::pin = -1;
bool IgnitionSense::debouncedState = false;
bool IgnitionSense::rawStateAtLastChange = false;
unsigned long IgnitionSense::lastChangeMs = 0;

void IgnitionSense::begin(int gpio) {
    pin = gpio;
    if (pin < 0) {
        return;
    }
    pinMode(pin, INPUT);
    rawStateAtLastChange = digitalRead(pin) == HIGH;
    debouncedState = rawStateAtLastChange;
    lastChangeMs = millis();
    Logger::info(TAG, "Ignition sense configured on GPIO " + String(pin) +
                 ", initial state=" + String(debouncedState ? "ON" : "OFF"));
}

bool IgnitionSense::isConfigured() {
    return pin >= 0;
}

bool IgnitionSense::isOn() {
    if (pin < 0) {
        return false;
    }
    bool raw = digitalRead(pin) == HIGH;
    if (raw != rawStateAtLastChange) {
        rawStateAtLastChange = raw;
        lastChangeMs = millis();
    } else if (raw != debouncedState && millis() - lastChangeMs >= DEBOUNCE_MS) {
        debouncedState = raw;
        Logger::info(TAG, String("Ignition ") + (debouncedState ? "ON" : "OFF"));
    }
    return debouncedState;
}

}  // namespace CarSentinel
