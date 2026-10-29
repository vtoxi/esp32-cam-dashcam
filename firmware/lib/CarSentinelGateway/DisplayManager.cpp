#include "DisplayManager.h"
#include "Logger.h"

#include <Wire.h>
#include <soc/soc_caps.h>  // SOC_I2C_NUM — Wire1 only exists on chips with >1 I2C peripheral (ESP32-S3, not classic ESP32)
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "DisplayManager";
static const char* CONFIG_PATH = "/config/display_config.json";
static const uint8_t SSD1306_I2C_ADDR = 0x3C;
static const uint8_t SCREEN_WIDTH = 128;
static const uint8_t SCREEN_HEIGHT = 64;

bool DisplayManager::present[2] = {false, false};
Adafruit_SSD1306* DisplayManager::displays[2] = {nullptr, nullptr};
DisplayConfigData DisplayManager::config;
DisplayRenderCallback DisplayManager::renderCallback = nullptr;
unsigned long DisplayManager::lastPageChangeMs = 0;
uint8_t DisplayManager::currentPageIndex[2] = {0, 0};

const char* displayPageIdToString(DisplayPageId id) {
    switch (id) {
        case DisplayPageId::HOME: return "HOME";
        case DisplayPageId::NETWORK: return "NETWORK";
        case DisplayPageId::GPS_PAGE: return "GPS";
        case DisplayPageId::IMU_PAGE: return "IMU";
        case DisplayPageId::SECURITY: return "SECURITY";
        case DisplayPageId::DEVICES: return "DEVICES";
        default: return "SYSTEM";
    }
}

DisplayPageId displayPageIdFromString(const String& value) {
    if (value == "HOME") return DisplayPageId::HOME;
    if (value == "NETWORK") return DisplayPageId::NETWORK;
    if (value == "GPS") return DisplayPageId::GPS_PAGE;
    if (value == "IMU") return DisplayPageId::IMU_PAGE;
    if (value == "SECURITY") return DisplayPageId::SECURITY;
    if (value == "DEVICES") return DisplayPageId::DEVICES;
    return DisplayPageId::SYSTEM;
}

static void loadPageArray(JsonArray arr, DisplayPageId* pages, uint8_t& count, uint8_t maxPages) {
    count = 0;
    for (JsonVariant v : arr) {
        if (count >= maxPages) break;
        pages[count++] = displayPageIdFromString(v.as<String>());
    }
}

static void savePageArray(JsonArray arr, const DisplayPageId* pages, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        arr.add(String(displayPageIdToString(pages[i])));
    }
}

bool DisplayManager::loadConfig() {
    if (!LittleFS.exists(CONFIG_PATH)) return false;
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Logger::error(TAG, "display_config.json parse failed: " + String(err.c_str()));
        return false;
    }
    loadPageArray(doc["display0Pages"].as<JsonArray>(), config.display0Pages,
                  config.display0PageCount, DisplayConfigData::MAX_PAGES);
    loadPageArray(doc["display1Pages"].as<JsonArray>(), config.display1Pages,
                  config.display1PageCount, DisplayConfigData::MAX_PAGES);
    config.pageIntervalMs = doc["pageIntervalMs"] | 4000;
    return config.display0PageCount > 0 || config.display1PageCount > 0;
}

bool DisplayManager::saveConfig() {
    JsonDocument doc;
    JsonArray a0 = doc["display0Pages"].to<JsonArray>();
    savePageArray(a0, config.display0Pages, config.display0PageCount);
    JsonArray a1 = doc["display1Pages"].to<JsonArray>();
    savePageArray(a1, config.display1Pages, config.display1PageCount);
    doc["pageIntervalMs"] = config.pageIntervalMs;

    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}

bool DisplayManager::begin(bool present0, bool present1) {
    if (!loadConfig()) {
        config = DisplayConfigData();  // compiled-in defaults from the header
        saveConfig();
        Logger::info(TAG, "No display config found; wrote defaults");
    }

    present[0] = present0;
    present[1] = present1;

    if (present0) {
        displays[0] = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
        // periphBegin=false: I2CBusManager already called Wire.begin() with the
        // confirmed pins (Phase 3/9) — don't let the display library re-init the bus.
        if (!displays[0]->begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDR, false, false)) {
            Logger::error(TAG, "Display 0 init failed despite presence probe succeeding");
            present[0] = false;
        } else {
            displays[0]->clearDisplay();
            displays[0]->display();
            Logger::info(TAG, "Display 0 initialized on bus 0");
        }
    }

#if SOC_I2C_NUM > 1
    if (present1) {
        displays[1] = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, -1);
        if (!displays[1]->begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDR, false, false)) {
            Logger::error(TAG, "Display 1 init failed despite presence probe succeeding");
            present[1] = false;
        } else {
            displays[1]->clearDisplay();
            displays[1]->display();
            Logger::info(TAG, "Display 1 initialized on bus 1");
        }
    }
#else
    if (present1) {
        Logger::warn(TAG, "Display 1 requested but this chip has no second I2C bus (Wire1)");
        present[1] = false;
    }
#endif

    return present[0] || present[1];
}

bool DisplayManager::isPresent(uint8_t displayIndex) {
    return displayIndex < 2 && present[displayIndex];
}

void DisplayManager::setRenderCallback(DisplayRenderCallback cb) {
    renderCallback = cb;
}

void DisplayManager::renderDisplay(uint8_t displayIndex) {
    if (!present[displayIndex] || !displays[displayIndex]) return;

    uint8_t pageCount = displayIndex == 0 ? config.display0PageCount : config.display1PageCount;
    if (pageCount == 0) return;
    DisplayPageId* pages = displayIndex == 0 ? config.display0Pages : config.display1Pages;
    DisplayPageId page = pages[currentPageIndex[displayIndex] % pageCount];

    Adafruit_SSD1306& d = *displays[displayIndex];
    d.clearDisplay();
    d.setTextSize(1);
    d.setTextColor(SSD1306_WHITE);
    d.setCursor(0, 0);
    if (renderCallback) {
        renderCallback(displayIndex, page, d);
    } else {
        d.println(displayPageIdToString(page));
    }
    d.display();
}

void DisplayManager::loop() {
    if (!present[0] && !present[1]) return;

    unsigned long now = millis();
    if (now - lastPageChangeMs < config.pageIntervalMs && lastPageChangeMs != 0) {
        return;
    }
    bool firstRun = (lastPageChangeMs == 0);
    lastPageChangeMs = now;

    if (!firstRun) {
        currentPageIndex[0]++;
        currentPageIndex[1]++;
    }
    renderDisplay(0);
    renderDisplay(1);
}

const DisplayConfigData& DisplayManager::getConfig() {
    return config;
}

bool DisplayManager::setConfig(const DisplayConfigData& newConfig) {
    config = newConfig;
    currentPageIndex[0] = 0;
    currentPageIndex[1] = 0;
    return saveConfig();
}

}  // namespace CarSentinel
