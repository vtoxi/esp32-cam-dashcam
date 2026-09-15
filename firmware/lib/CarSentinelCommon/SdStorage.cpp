#include "SdStorage.h"
#include "Logger.h"

#include <SD_MMC.h>

namespace CarSentinel {

static const char* TAG = "SdStorage";
bool SdStorage::mounted = false;

bool SdStorage::begin() {
    // true = 1-bit mode (CLK/CMD/D0 only), avoiding the D1/GPIO4 flash-LED conflict —
    // see docs/wiring/MICROSD.md Section 9.
    if (!SD_MMC.begin("/sdcard", true)) {
        Logger::error(TAG, "SD_MMC mount failed — no card, or wiring/format issue");
        mounted = false;
        return false;
    }

    if (SD_MMC.cardType() == CARD_NONE) {
        Logger::error(TAG, "SD_MMC mounted but no card detected");
        mounted = false;
        return false;
    }

    mounted = true;
    Logger::info(TAG, "SD mounted, total=" + String((uint32_t)(SD_MMC.totalBytes() / (1024 * 1024))) + "MB");
    return true;
}

SdStatus SdStorage::status() {
    SdStatus s;
    s.mounted = mounted;
    if (mounted) {
        s.totalBytes = SD_MMC.totalBytes();
        s.usedBytes = SD_MMC.usedBytes();
    }
    return s;
}

}  // namespace CarSentinel
