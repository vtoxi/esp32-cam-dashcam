#include "OtaManager.h"
#include "DeviceConfig.h"
#include "Logger.h"
#include <esp_ota_ops.h>

#if CARSENTINEL_ROLE_GATEWAY
#include "Watchdog.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#endif

namespace CarSentinel {

static const char* TAG = "OtaManager";

#if CARSENTINEL_ROLE_GATEWAY
// Real implementation — gateway only. Camera nodes (classic ESP32) can't fit this
// alongside their existing WiFi/BLE/camera/SD footprint: linking HTTPClient + Update.h
// (whose flash-write path needs IRAM-resident code) overflowed the node's fixed IRAM
// region by 52 bytes even after every safe trim available (LTO, disabling NimBLE's
// unused central/observer roles, dropping TLS from the node's own OTA path) — a real,
// measured link failure, not a guess. Rather than leave the build broken, OTA is scoped
// to the gateway for this phase; node self-update is a known limitation (see
// docs/IMPLEMENTATION_PLAN.md Phase 14) to revisit if node-side IRAM headroom improves
// (e.g. a future board swap, or Espressif toolchain changes).

// Section 37 "check storage"/resource safety: refuse to even attempt an OTA download if
// free heap is already low — a failed-midway OTA on a resource-starved device is worse
// than refusing upfront.
static const uint32_t MIN_FREE_HEAP_FOR_OTA = 40000;

static bool httpBegin(HTTPClient& http, WiFiClientSecure& secureClient, WiFiClient& plainClient,
                       const String& url) {
    if (url.startsWith("https://")) {
        secureClient.setInsecure();  // same documented trust posture as EmailProvider
        return http.begin(secureClient, url);
    }
    return http.begin(plainClient, url);
}

bool OtaManager::fetchManifest(const String& manifestUrl, OtaManifest& out) {
    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (!httpBegin(http, secureClient, plainClient, manifestUrl)) {
        Logger::error(TAG, "Failed to begin HTTP request to " + manifestUrl);
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        Logger::error(TAG, "Manifest fetch failed, HTTP " + String(code));
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Logger::error(TAG, "Manifest JSON parse failed: " + String(err.c_str()));
        return false;
    }

    out.version = doc["version"] | "";
    out.url = doc["url"] | "";
    out.md5 = doc["md5"] | "";
    out.hardwareProfile = doc["hardwareProfile"] | "";

    if (out.version.isEmpty() || out.url.isEmpty()) {
        Logger::error(TAG, "Manifest missing required version/url fields");
        return false;
    }
    Logger::info(TAG, "Manifest: version=" + out.version + " hardwareProfile=" + out.hardwareProfile +
                 " md5=" + (out.md5.isEmpty() ? "(none)" : out.md5));
    return true;
}

bool OtaManager::isUpdateNeeded(const OtaManifest& manifest) {
    const DeviceConfigData& cfg = DeviceConfig::get();
    if (!manifest.hardwareProfile.isEmpty() && manifest.hardwareProfile != cfg.hardwareProfile) {
        Logger::error(TAG, "Manifest hardwareProfile (" + manifest.hardwareProfile +
                      ") does not match this device's (" + cfg.hardwareProfile +
                      ") — refusing, Section 37 safety check");
        return false;
    }
    if (manifest.version == cfg.firmwareVersion) {
        Logger::info(TAG, "Already running manifest version " + manifest.version + "; nothing to do");
        return false;
    }
    return true;
}

bool OtaManager::performUpdate(const OtaManifest& manifest) {
    if (!isUpdateNeeded(manifest)) {
        return false;
    }
    if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_OTA) {
        Logger::error(TAG, "Free heap too low for OTA (" + String(ESP.getFreeHeap()) +
                      " bytes) — refusing");
        return false;
    }

    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (!httpBegin(http, secureClient, plainClient, manifest.url)) {
        Logger::error(TAG, "Failed to begin HTTP request to " + manifest.url);
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        Logger::error(TAG, "OTA download failed, HTTP " + String(code));
        http.end();
        return false;
    }

    int len = http.getSize();
    if (len <= 0) {
        Logger::error(TAG, "OTA server did not report a valid content length");
        http.end();
        return false;
    }

    if (!Update.begin((size_t)len)) {
        Logger::error(TAG, String("Update.begin failed: ") + Update.errorString());
        http.end();
        return false;
    }

    if (!manifest.md5.isEmpty() && !Update.setMD5(manifest.md5.c_str())) {
        Logger::error(TAG, "Manifest MD5 string is malformed — aborting before writing anything");
        Update.abort();
        http.end();
        return false;
    }

    Update.onProgress([](size_t written, size_t total) {
        static int8_t lastLoggedDecile = -1;
        int8_t decile = total ? (int8_t)((written * 10) / total) : 0;
        if (decile != lastLoggedDecile) {
            lastLoggedDecile = decile;
            Logger::info(TAG, "OTA progress: " + String(decile * 10) + "% (" +
                         String(written) + "/" + String(total) + " bytes)");
        }
        // A multi-second streaming download must not starve the watchdog — same lesson
        // as Phase 1/10/12's blocking-call fixes.
        Watchdog::feed();
    });

    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    if (written != (size_t)len) {
        Logger::error(TAG, "OTA short write: " + String(written) + "/" + String(len) +
                      " bytes — aborting, device unchanged");
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        Logger::error(TAG, String("Update.end failed (likely MD5 mismatch or corrupt "
                       "image): ") + Update.errorString());
        return false;
    }

    Logger::info(TAG, "OTA update installed (version " + manifest.version + ") — restarting");
    delay(200);
    ESP.restart();
    return true;  // unreachable after restart; present for a clean function signature
}

#else
// Camera-node stub — see the comment above the gateway implementation for why. Neither
// HTTPClient.h, Update.h, nor WiFiClientSecure.h are included here, so none of their
// (IRAM-hungry) object code is pulled into the node build at all.
bool OtaManager::fetchManifest(const String& /*manifestUrl*/, OtaManifest& /*out*/) {
    Logger::error(TAG, "OTA is not supported on this hardware profile (classic ESP32 "
                  "IRAM budget) — see docs/IMPLEMENTATION_PLAN.md Phase 14");
    return false;
}

bool OtaManager::isUpdateNeeded(const OtaManifest& /*manifest*/) {
    return false;
}

bool OtaManager::performUpdate(const OtaManifest& /*manifest*/) {
    Logger::error(TAG, "OTA is not supported on this hardware profile (classic ESP32 "
                  "IRAM budget) — see docs/IMPLEMENTATION_PLAN.md Phase 14");
    return false;
}
#endif

void OtaManager::confirmHealthyBoot() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        Logger::info(TAG, "Boot confirmed healthy (any pending rollback cancelled)");
    } else {
        Logger::debug(TAG, "esp_ota_mark_app_valid_cancel_rollback: " + String(esp_err_to_name(err)) +
                      " — rollback support is likely not enabled in this bootloader build "
                      "(not necessarily an error; see docs/IMPLEMENTATION_PLAN.md Phase 14)");
    }
}

}  // namespace CarSentinel
