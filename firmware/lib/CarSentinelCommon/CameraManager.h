#pragma once

#include <Arduino.h>
#include <esp_camera.h>

// esp32-camera wrapper for the AI-Thinker ESP32-CAM (OV2640). It supports both
// snapshot capture and the short-lived MJPEG stream served by node_main.cpp.
namespace CarSentinel {

class CameraManager {
public:
    // Configures and powers up the sensor. Uses the standard CAMERA_MODEL_AI_THINKER
    // pin set (see docs/wiring/ESP32_CAM.md) — not configurable per capabilities.json
    // since these pins are fixed by the module, not a deployment choice.
    static bool begin();

    // Captures one JPEG frame. Returns nullptr on failure. Caller MUST call
    // returnFrame() when done with the buffer (esp_camera_fb_return underneath) —
    // holding a frame buffer indefinitely starves the driver's buffer pool.
    static camera_fb_t* captureJpeg();
    static void returnFrame(camera_fb_t* fb);

    static bool isInitialized();

    // AI-Thinker ESP32-CAM's onboard white LED flash is fixed to GPIO4 — safe to drive
    // directly since SdStorage mounts SD_MMC in 1-bit mode (SdStorage.cpp,
    // `SD_MMC.begin("/sdcard", true)`), which frees GPIO4 (otherwise SD_MMC D1) for
    // this. Not a capability/GPIO-configurable setting like the sensors in
    // CapabilitiesConfig — it's fixed by the board, same reasoning as the camera pins
    // above.
    static void setFlash(bool on);
    static bool isFlashOn();

private:
    static bool initialized;
    static bool flashOn;
};

}  // namespace CarSentinel
