#pragma once

#include <Arduino.h>

// Onboard microSD wrapper for the AI-Thinker ESP32-CAM (docs/wiring/MICROSD.md). Uses
// SDMMC 1-bit mode at its fixed default pins (CLK14/CMD15/D0=2) to avoid the GPIO4
// flash-LED conflict — no pins to configure here, they're fixed by the SD_MMC
// peripheral on this chip, not a capability we can reassign. Phase 3 scope: mount +
// report free space. Evidence file layout (Section 27) lands in Phase 4/11.
namespace CarSentinel {

struct SdStatus {
    bool mounted = false;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
};

class SdStorage {
public:
    // Never halts boot on failure (Section 59: handle SD failure gracefully) — returns
    // false and logs; caller continues without storage.
    static bool begin();
    static SdStatus status();

private:
    static bool mounted;
};

}  // namespace CarSentinel
