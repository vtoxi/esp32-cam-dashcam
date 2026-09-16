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

private:
    static bool initialized;
};

}  // namespace CarSentinel
