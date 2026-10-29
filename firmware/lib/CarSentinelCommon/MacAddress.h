#pragma once

#include <Arduino.h>

// Small shared MAC formatting helpers — used by DeviceRegistry (persisting a peer's MAC
// as a string) and gateway remote-command dispatch (parsing it back to bytes to target
// an EspNowManager::sendMessage() call).
namespace CarSentinel {

String macToString(const uint8_t mac[6]);

// Accepts "AA:BB:CC:DD:EE:FF" (case-insensitive). Returns false and leaves outMac
// untouched if the string isn't a well-formed MAC.
bool macFromString(const String& str, uint8_t outMac[6]);

}  // namespace CarSentinel
