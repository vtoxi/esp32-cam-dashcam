#include "CameraManager.h"
#include "Logger.h"

namespace CarSentinel {

static const char* TAG = "CameraManager";
bool CameraManager::initialized = false;
bool CameraManager::flashOn = false;

#define FLASH_LED_GPIO 4

// Standard CAMERA_MODEL_AI_THINKER pin set — see docs/wiring/ESP32_CAM.md for the
// per-signal table and verification status.
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_Y9 35
#define CAM_PIN_Y8 34
#define CAM_PIN_Y7 39
#define CAM_PIN_Y6 36
#define CAM_PIN_Y5 21
#define CAM_PIN_Y4 19
#define CAM_PIN_Y3 18
#define CAM_PIN_Y2 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

bool CameraManager::begin() {
    // Power-cycle the sensor via PWDN (active-high power-down on the OV2640) before
    // init. Confirmed on real hardware: after a watchdog-triggered reboot (a soft
    // reset, not a full power cycle), esp_camera_init failed with
    // "SCCB_Write Failed... Camera probe failed" even though the previous boot's init
    // had succeeded — a well-documented ESP32-CAM quirk where the sensor is left in a
    // state a soft reset alone doesn't clear. This toggle is cheap and harmless on a
    // clean boot too, so it's unconditional rather than only-on-retry.
    pinMode(CAM_PIN_PWDN, OUTPUT);
    digitalWrite(CAM_PIN_PWDN, HIGH);
    delay(10);
    digitalWrite(CAM_PIN_PWDN, LOW);
    delay(10);

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAM_PIN_Y2;
    config.pin_d1 = CAM_PIN_Y3;
    config.pin_d2 = CAM_PIN_Y4;
    config.pin_d3 = CAM_PIN_Y5;
    config.pin_d4 = CAM_PIN_Y6;
    config.pin_d5 = CAM_PIN_Y7;
    config.pin_d6 = CAM_PIN_Y8;
    config.pin_d7 = CAM_PIN_Y9;
    config.pin_xclk = CAM_PIN_XCLK;
    config.pin_pclk = CAM_PIN_PCLK;
    config.pin_vsync = CAM_PIN_VSYNC;
    config.pin_href = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn = CAM_PIN_PWDN;
    config.pin_reset = CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    bool hasPsram = psramFound();
    // SVGA/quality-12 with PSRAM double-buffering; a smaller frame + single buffer
    // without PSRAM, since JPEG frame buffers are too large for the ESP32's internal
    // SRAM at higher resolutions.
    config.frame_size = hasPsram ? FRAMESIZE_SVGA : FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = hasPsram ? 2 : 1;
    config.fb_location = hasPsram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Logger::warn(TAG, "esp_camera_init failed (0x" + String(err, HEX) +
                     "), retrying once after a fresh power-cycle");
        esp_camera_deinit();
        digitalWrite(CAM_PIN_PWDN, HIGH);
        delay(50);
        digitalWrite(CAM_PIN_PWDN, LOW);
        delay(50);
        err = esp_camera_init(&config);
    }
    if (err != ESP_OK) {
        Logger::error(TAG, "esp_camera_init failed after retry: 0x" + String(err, HEX));
        initialized = false;
        return false;
    }

    initialized = true;
    Logger::info(TAG, String("Camera init OK, psram=") + (hasPsram ? "yes" : "no") +
                 " frameSize=" + (hasPsram ? "SVGA" : "VGA"));
    return true;
}

camera_fb_t* CameraManager::captureJpeg() {
    if (!initialized) {
        Logger::warn(TAG, "captureJpeg called before successful begin()");
        return nullptr;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Logger::error(TAG, "esp_camera_fb_get returned null");
        return nullptr;
    }
    return fb;
}

void CameraManager::returnFrame(camera_fb_t* fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

bool CameraManager::isInitialized() {
    return initialized;
}

void CameraManager::setFlash(bool on) {
    pinMode(FLASH_LED_GPIO, OUTPUT);
    digitalWrite(FLASH_LED_GPIO, on ? HIGH : LOW);
    flashOn = on;
}

bool CameraManager::isFlashOn() {
    return flashOn;
}

}  // namespace CarSentinel
