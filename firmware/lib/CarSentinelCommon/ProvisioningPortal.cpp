#include "ProvisioningPortal.h"
#include "Logger.h"
#include "NetworkConfig.h"
#include "DeviceConfig.h"

#include <WiFi.h>
#include <WebServer.h>

namespace CarSentinel {

static const char* TAG = "ProvisioningPortal";
static WebServer server(80);

bool ProvisioningPortal::active = false;
bool ProvisioningPortal::submitted = false;

static String htmlEscape(const String& in) {
    String out = in;
    out.replace("&", "&amp;");
    out.replace("\"", "&quot;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    return out;
}

void ProvisioningPortal::handleRoot() {
    const DeviceConfigData& dev = DeviceConfig::get();
    const NetworkConfigData& net = NetworkConfig::get();

    String page = "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<title>CarSentinel Setup</title></head><body>"
                  "<h2>CarSentinel Setup</h2>"
                  "<p>Node: " + htmlEscape(dev.nodeId) + "</p>"
                  "<form method=POST action=/save>"
                  "<label>Wi-Fi SSID</label><br><input name=ssid value=\"" + htmlEscape(net.ssid) + "\"><br>"
                  "<label>Wi-Fi Password</label><br><input name=password type=password><br>"
                  "<label>Hostname</label><br><input name=hostname value=\"" + htmlEscape(net.hostname) + "\"><br>"
                  "<label>Display Name</label><br><input name=displayName value=\"" + htmlEscape(dev.displayName) + "\"><br>"
                  "<label>Role</label><br><select name=role>";
    const char* roles[] = {"UNASSIGNED", "GATEWAY", "CAMERA", "SENSOR", "DISPLAY", "VEHICLE_CONTROLLER"};
    for (const char* r : roles) {
        page += "<option value='" + String(r) + "'";
        if (String(roleToString(dev.role)) == r) page += " selected";
        page += ">" + String(r) + "</option>";
    }
    page += "</select><br><br><input type=submit value=Save></form></body></html>";

    server.send(200, "text/html", page);
}

void ProvisioningPortal::handleSave() {
    if (!server.hasArg("ssid") || server.arg("ssid").isEmpty()) {
        server.send(400, "text/plain", "SSID is required");
        return;
    }

    NetworkConfigData net = NetworkConfig::get();
    net.ssid = server.arg("ssid");
    // Only overwrite the stored password if the user typed a new one — leaving the
    // field blank on a re-visit must not wipe a previously working credential.
    if (server.hasArg("password") && server.arg("password").length() > 0) {
        net.password = server.arg("password");
    }
    if (server.hasArg("hostname") && server.arg("hostname").length() > 0) {
        net.hostname = server.arg("hostname");
    }
    NetworkConfig::save(net);
    NetworkConfig::addNetwork(net.ssid, net.password);  // also remembered for connectBestKnown()

    DeviceConfigData dev = DeviceConfig::get();
    if (server.hasArg("displayName") && server.arg("displayName").length() > 0) {
        dev.displayName = server.arg("displayName");
    }
    if (server.hasArg("role")) {
        dev.role = roleFromString(server.arg("role"));
    }
    DeviceConfig::save(dev);

    Logger::info(TAG, "Provisioning form submitted: ssid=" + net.ssid +
                 " displayName=" + dev.displayName + " role=" + String(roleToString(dev.role)));

    server.send(200, "text/html",
                "<html><body><h3>Saved. Device is restarting and will attempt to join "
                "the configured Wi-Fi network.</h3></body></html>");
    submitted = true;
}

void ProvisioningPortal::handleNotFound() {
    // Captive-portal-style redirect: send every unknown path back to the setup form so
    // phones/laptops that auto-open a browser on AP join land on the right page.
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void ProvisioningPortal::begin(const String& apSsid) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid.c_str());
    Logger::info(TAG, "Provisioning AP started: ssid=" + apSsid +
                 " ip=" + WiFi.softAPIP().toString());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    active = true;
    submitted = false;
}

void ProvisioningPortal::stop() {
    server.stop();
    WiFi.softAPdisconnect(true);
    active = false;
    Logger::info(TAG, "Provisioning AP stopped");
}

void ProvisioningPortal::loop() {
    if (active) {
        server.handleClient();
    }
}

bool ProvisioningPortal::isActive() {
    return active;
}

bool ProvisioningPortal::isSubmitted() {
    return submitted;
}

}  // namespace CarSentinel
