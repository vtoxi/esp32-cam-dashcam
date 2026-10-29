#include "HardwareProfiles.h"

namespace CarSentinel {

const char* PROFILE_ESP32_CAM_AI_THINKER = "ESP32_CAM_AI_THINKER";
const char* PROFILE_ESP32_S3_N16R8_GATEWAY = "ESP32_S3_N16R8_GATEWAY";

CapabilitiesConfigData defaultCapabilitiesForProfile(const String& profileName) {
    CapabilitiesConfigData caps;

    if (profileName == PROFILE_ESP32_CAM_AI_THINKER) {
        // Camera + SD are fixed onboard hardware on every AI-Thinker unit — presence is
        // unconditional. RCWL/DHT are deployed on a subset of nodes and their GPIOs are
        // still unverified (docs/HARDWARE.md open questions) — left disabled/
        // unconfigured until a bench test confirms free pins and a human enables them
        // per-node (Phase 6 deployment decision), matching "dozens of RCWL, deploy as
        // needed" / "2 DHT11, assign to specific nodes."
        caps.camera = true;
        caps.sd = true;
        caps.rcwl = false;
        caps.rcwlGpio = GPIO_UNCONFIGURED;
        caps.dht = false;
        caps.dhtGpio = GPIO_UNCONFIGURED;
        caps.gps = false;
        caps.imu = false;
        caps.display = false;
        return caps;
    }

    if (profileName == PROFILE_ESP32_S3_N16R8_GATEWAY) {
        // Pins match the "proposed (untested)" assignments in
        // docs/wiring/ESP32_S3_GATEWAY.md — enabled by default since they're the best
        // current information, but every log line touching these should make clear
        // they're unverified until a bench test confirms them.
        caps.camera = false;
        caps.sd = false;
        caps.rcwl = false;
        caps.dht = false;

        caps.gps = true;
        caps.gpsRxGpio = 17;
        caps.gpsTxGpio = 18;

        caps.imu = true;
        caps.imuSdaGpio = 8;
        caps.imuSclGpio = 9;

        caps.display = true;
        caps.displayCount = 2;
        caps.display1SdaGpio = 8;   // shares primary bus with IMU (0x68 vs 0x3C, no conflict)
        caps.display1SclGpio = 9;
        caps.display2SdaGpio = 10;  // second bus — both SSD1306 units confirmed at 0x3C
        caps.display2SclGpio = 11;
        return caps;
    }

    // Unknown/UNASSIGNED profile: everything off. Never guess capability pins for a
    // profile this code doesn't recognize.
    return caps;
}

}  // namespace CarSentinel
