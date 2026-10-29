#pragma once

#include <Arduino.h>
#include "CapabilitiesConfig.h"

// Compiled-in pin tables, one per confirmed board (Section 49). This is the one place
// project rule "never assume GPIO mappings" is deliberately relaxed for pins that ARE
// physically confirmed (see docs/wiring/) — board-level pin definitions are explicitly
// allowed to require a firmware rebuild (Section 49 closing line). Anything not yet
// bench-verified is left disabled/unconfigured (gpio = -1) here rather than guessed;
// enabling it is a runtime capabilities.json edit once the physical pin is confirmed,
// not a firmware change.
namespace CarSentinel {

// Matches docs/wiring/ESP32_CAM.md — confirmed board, but RCWL/DHT GPIOs on it are
// still unverified (see docs/HARDWARE.md open questions), so left disabled by default.
extern const char* PROFILE_ESP32_CAM_AI_THINKER;

// Matches docs/wiring/ESP32_S3_GATEWAY.md — board confirmed from photo, GPIOs below are
// "proposed, not yet bench-verified" per that document.
extern const char* PROFILE_ESP32_S3_N16R8_GATEWAY;

// Returns the factory-default capability set for a given profile name. Used only to
// seed /config/capabilities.json on first boot — after that, the persisted file is the
// source of truth and this function is not consulted again unless the user deletes it
// or changes hardwareProfile in device.json.
CapabilitiesConfigData defaultCapabilitiesForProfile(const String& profileName);

}  // namespace CarSentinel
