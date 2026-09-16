#include "StatusPage.h"
#include "Logger.h"

#include <WebServer.h>

namespace CarSentinel {

static const char* TAG = "StatusPage";
static WebServer server(80);

StatusContentProvider StatusPage::contentProvider = nullptr;
StatusStreamProvider StatusPage::streamProvider = nullptr;
StatusFlashToggleProvider StatusPage::flashProvider = nullptr;
String StatusPage::title;
bool StatusPage::active = false;

void StatusPage::handleRoot() {
    String body = contentProvider ? contentProvider() : "<p>(no status available)</p>";

    String page =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>" + title + "</title>"
        "<style>"
        "body{font-family:monospace;background:#111;color:#eee;padding:16px;}"
        "h2{color:#6cf;}"
        "table{border-collapse:collapse;margin-bottom:16px;}"
        "td{padding:2px 10px 2px 0;vertical-align:top;}"
        "td.k{color:#8f8;white-space:nowrap;}"
        ".sub{color:#888;font-size:0.85em;}"
        "</style></head><body>"
        "<h2>" + title + "</h2>"
        "<p class=sub>Read-only status — auto-refreshes every 5s. Configuration UI lands in a later phase.</p>"
        + body +
        "</body></html>";

    server.send(200, "text/html; charset=utf-8", page);
}

void StatusPage::handleStream() {
    if (streamProvider) {
        streamProvider(server.client(), server.arg("node"));
        return;
    }
    server.send(404, "text/plain", "Live stream unavailable");
}

// Cross-origin so the gateway's dashboard (Phase 19, a different origin — its own IP)
// can call this directly rather than the gateway needing to proxy it. Local network
// only, same trust posture as every other HTTP surface in this project — see
// StatusPage.h/DashboardServer.h's existing "no auth" notes.
void StatusPage::handleFlash() {
    if (!flashProvider) {
        server.send(404, "text/plain", "Flash control unavailable on this device");
        return;
    }
    bool requestedOn = server.arg("on") == "1";
    bool actualState = flashProvider(requestedOn);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", String("{\"flash\":") + (actualState ? "true" : "false") + "}");
}

void StatusPage::begin(const String& deviceTitle, StatusContentProvider provider,
                       StatusStreamProvider streamP, StatusFlashToggleProvider flashP) {
    title = deviceTitle;
    contentProvider = provider;
    streamProvider = streamP;
    flashProvider = flashP;
    server.on("/", HTTP_GET, handleRoot);
    server.on("/stream", HTTP_GET, handleStream);
    server.on("/flash", HTTP_GET, handleFlash);
    server.begin();
    active = true;
    Logger::info(TAG, "Status page active at http://<device-ip>/");
}

void StatusPage::loop() {
    if (active) {
        server.handleClient();
    }
}

bool StatusPage::isActive() {
    return active;
}

}  // namespace CarSentinel
