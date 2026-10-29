#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

// Section 25/26: gateway-only, drives up to two SSD1306 OLEDs (confirmed both at I2C
// address 0x3C with no address-select jumper — docs/wiring/SSD1306.md — hence display 2
// lives on a second I2C bus, Wire1, set up by I2CBusManager before this begins). Each
// display cycles through a configurable, persisted sequence of pages — "configuration
// determines display content" (Section 25), not a hardcoded "display 1 = GPS" rule.
//
// Sensor-agnostic by design, same pattern as IncidentCorrelator/StatusPage: this class
// owns the Adafruit_SSD1306 objects and the page-cycling timer, but never reads a
// sensor itself — gateway_main.cpp supplies a render callback that draws each page's
// actual content using whatever managers it already has.
namespace CarSentinel {

enum class DisplayPageId : uint8_t {
    HOME = 0,
    NETWORK = 1,
    GPS_PAGE = 2,
    IMU_PAGE = 3,
    SECURITY = 4,
    DEVICES = 5,
    SYSTEM = 6
};

const char* displayPageIdToString(DisplayPageId id);
DisplayPageId displayPageIdFromString(const String& value);

struct DisplayConfigData {
    static const uint8_t MAX_PAGES = 6;

    uint8_t display0PageCount = 3;
    DisplayPageId display0Pages[MAX_PAGES] = {DisplayPageId::HOME, DisplayPageId::SECURITY,
                                                DisplayPageId::GPS_PAGE};

    uint8_t display1PageCount = 3;
    DisplayPageId display1Pages[MAX_PAGES] = {DisplayPageId::NETWORK, DisplayPageId::DEVICES,
                                                DisplayPageId::SYSTEM};

    unsigned long pageIntervalMs = 4000;
};

// Called once per display per render tick — draw directly onto `display` (already
// cleared; caller calls display.display() afterward). Never called for a page index a
// display doesn't have configured.
typedef void (*DisplayRenderCallback)(uint8_t displayIndex, DisplayPageId page, Adafruit_SSD1306& display);

class DisplayManager {
public:
    // present0/present1 come from Phase 3's I2C presence probe — this never touches
    // hardware that wasn't already confirmed present (Section 2.2: never initialize
    // hardware that doesn't exist).
    static bool begin(bool present0, bool present1);
    static bool isPresent(uint8_t displayIndex);

    // Call every loop() iteration — internally rate-limited to pageIntervalMs, so this
    // is cheap to call unconditionally.
    static void loop();

    static void setRenderCallback(DisplayRenderCallback cb);

    static const DisplayConfigData& getConfig();
    static bool setConfig(const DisplayConfigData& config);

private:
    static bool present[2];
    static Adafruit_SSD1306* displays[2];
    static DisplayConfigData config;
    static DisplayRenderCallback renderCallback;
    static unsigned long lastPageChangeMs;
    static uint8_t currentPageIndex[2];

    static bool loadConfig();
    static bool saveConfig();
    static void renderDisplay(uint8_t displayIndex);
};

}  // namespace CarSentinel
