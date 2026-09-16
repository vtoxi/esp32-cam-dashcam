#include "StatusPage.h"
#include "Logger.h"

#include <WebServer.h>

namespace CarSentinel {

static const char* TAG = "StatusPage";
static WebServer server(80);

StatusContentProvider StatusPage::contentProvider = nullptr;
String StatusPage::title;
bool StatusPage::active = false;

void StatusPage::handleRoot() {
    String body = contentProvider ? contentProvider() : "<p>(no status available)</p>";

    String page =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<meta http-equiv=refresh content=5>"
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

void StatusPage::begin(const String& deviceTitle, StatusContentProvider provider) {
    title = deviceTitle;
    contentProvider = provider;
    server.on("/", HTTP_GET, handleRoot);
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
