#include "DashboardServer.h"
#include "Logger.h"
#include "NetworkConfig.h"
#include "EmailConfig.h"
#include "BackendConfig.h"
#include "RemoteSyncManager.h"

#include <WebServer.h>
#include <ArduinoJson.h>

namespace CarSentinel {

static const char* TAG = "DashboardServer";
static WebServer server(80);

JsonContentProvider DashboardServer::statusProvider = nullptr;
JsonContentProvider DashboardServer::devicesProvider = nullptr;
JsonContentProvider DashboardServer::incidentsProvider = nullptr;
DashboardStreamProvider DashboardServer::streamProvider = nullptr;
String DashboardServer::title;
bool DashboardServer::active = false;

// Single embedded page: no CDN, no build step, no external assets — must work fully
// offline in a parked vehicle with only the gateway's own AP/Wi-Fi reachable. Polls the
// three JSON endpoints every 3s and patches the DOM in place (no flicker, unlike the old
// StatusPage's meta-refresh).
static const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name=viewport content='width=device-width,initial-scale=1'>
<title>CarSentinel Dashboard</title>
<style>
:root{color-scheme:dark;}
body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:16px;}
h1{font-size:1.3em;color:#58a6ff;margin:0 0 4px;}
.sub{color:#8b949e;font-size:0.85em;margin-bottom:16px;}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-bottom:20px;}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px 14px;}
.card h3{margin:0 0 8px;font-size:0.8em;text-transform:uppercase;letter-spacing:0.05em;color:#8b949e;}
.card .val{font-size:1.4em;font-weight:600;}
.card .val small{font-size:0.5em;color:#8b949e;font-weight:400;}
table{width:100%;border-collapse:collapse;font-size:0.9em;}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid #30363d;}
th{color:#8b949e;font-weight:600;font-size:0.8em;text-transform:uppercase;}
.badge{display:inline-block;padding:1px 8px;border-radius:10px;font-size:0.75em;}
.badge.ok{background:#1a7f37;color:#fff;}
.badge.off{background:#3d444d;color:#8b949e;}
.badge.warn{background:#9e6a03;color:#fff;}
.section{margin-bottom:20px;}
.section h2{font-size:1em;color:#58a6ff;border-bottom:1px solid #30363d;padding-bottom:6px;}
.empty{color:#8b949e;font-style:italic;padding:8px 0;}
</style></head><body>
<h1 id=title>CarSentinel Dashboard <a href=/settings style="float:right;font-size:0.5em;color:#58a6ff;text-decoration:none;border:1px solid #30363d;border-radius:6px;padding:4px 10px;">&#9881; Settings</a></h1>
<p class=sub>Live view, polling every 3s. Companion JSON API: <code>/api/status</code>, <code>/api/devices</code>, <code>/api/incidents</code>.</p>
<div class=grid id=statusCards></div>
<div class=section><h2>Live camera</h2><select id=cameraSelect onchange=selectCamera()><option value=''>Select a camera</option></select>
<button id=flashBtn onclick=toggleFlash() disabled>Flash</button>
<br><img id=cameraFeed alt='Live camera feed' style='display:block;max-width:100%;margin-top:10px;background:#000'></div>
<div class=section><h2>Devices</h2><table id=devicesTable><thead><tr><th>Node</th><th>Name</th><th>Role</th><th>Status</th><th>Last seen</th><th>IP</th></tr></thead><tbody></tbody></table></div>
<div class=section><h2>Recent Incidents</h2><table id=incidentsTable><thead><tr><th>ID</th><th>State</th><th>Trigger</th><th>Severity</th><th>Evidence</th></tr></thead><tbody></tbody></table></div>
<script>
function agoStr(ms){if(!ms)return 'never';var s=Math.round(ms/1000);if(s<60)return s+'s ago';var m=Math.round(s/60);if(m<60)return m+'m ago';return Math.round(m/60)+'h ago';}
function renderStatus(d){
  document.getElementById('title').textContent = d.nodeId ? ('CarSentinel Gateway ' + d.nodeId) : 'CarSentinel Dashboard';
  var cards = [
    ['Firmware', d.firmwareVersion || '?'],
    ['Uptime', d.uptimeS!=null ? d.uptimeS+'s' : '?'],
    ['Free Heap', d.freeHeap!=null ? d.freeHeap+' B' : '?'],
    ['Wi-Fi', (d.wifiSsid||'?') + ' <small>' + (d.wifiIp||'') + '</small>'],
    ['GPS', d.gps ? (d.gps.status + (d.gps.satellites!=null?' <small>'+d.gps.satellites+' sats</small>':'')) : 'n/a'],
    ['IMU', d.imu ? (d.imu.accelG.toFixed(2)+'g <small>'+d.imu.gyroDps.toFixed(1)+' dps</small>') : 'n/a'],
    ['Security Mode', d.securityMode || '?'],
    ['ESP-NOW', d.espNowActive ? 'active' : 'inactive'],
    ['Devices Seen', d.deviceCount!=null ? d.deviceCount : '?'],
  ];
  var html = '';
  cards.forEach(function(c){ html += '<div class=card><h3>'+c[0]+'</h3><div class=val>'+c[1]+'</div></div>'; });
  document.getElementById('statusCards').innerHTML = html;
}
var cameraIps = {};
function renderDevices(list){
  var tb = document.querySelector('#devicesTable tbody');
  var select = document.getElementById('cameraSelect');
  var selected = select.value;
  select.innerHTML = '<option value="">Select a camera</option>';
  cameraIps = {};
  (list||[]).filter(function(d){return d.role==='CAMERA' && d.ip;}).forEach(function(d){
    cameraIps[d.nodeId] = d.ip;
    var option=document.createElement('option'); option.value=d.nodeId; option.textContent=d.displayName+' ('+d.nodeId+')'; select.appendChild(option);
  });
  if(selected && Array.from(select.options).some(function(option){return option.value===selected;})) select.value=selected;
  document.getElementById('flashBtn').disabled = !select.value;
  if(!list || !list.length){ tb.innerHTML = '<tr><td colspan=6 class=empty>No devices seen yet</td></tr>'; return; }
  tb.innerHTML = list.map(function(d){
    var badge = !d.enabled ? '<span class=badge>disabled</span>' :
      (!d.lastSeenAgoMs ? '<span class=badge>never seen</span>' :
       (d.lastSeenAgoMs<30000 ? '<span class="badge ok">online</span>' : '<span class="badge warn">stale</span>'));
    return '<tr><td>'+d.nodeId+'</td><td>'+d.displayName+'</td><td>'+d.role+'</td><td>'+badge+'</td><td>'+agoStr(d.lastSeenAgoMs)+'</td><td>'+(d.ip||'-')+'</td></tr>';
  }).join('');
}
function selectCamera(){var n=document.getElementById('cameraSelect').value;var feed=document.getElementById('cameraFeed');feed.onerror=function(){if(n===document.getElementById('cameraSelect').value)setTimeout(selectCamera,1000);};feed.src=n?('/stream?node='+encodeURIComponent(n)):'';document.getElementById('flashBtn').disabled=!n;document.getElementById('flashBtn').textContent='Flash';}
// Talks directly to the selected node's own IP (StatusPage's /flash route sends
// Access-Control-Allow-Origin: * for exactly this) rather than routing through the
// gateway — same "lightest technique, no unnecessary proxying" reasoning as the
// /stream redirect.
function toggleFlash(){
  var n=document.getElementById('cameraSelect').value, ip=cameraIps[n];
  if(!ip) return;
  var btn=document.getElementById('flashBtn');
  var wantOn = btn.textContent.indexOf('ON')<0;
  fetch('http://'+ip+'/flash?on='+(wantOn?'1':'0')).then(function(r){return r.json();}).then(function(d){
    btn.textContent = 'Flash: '+(d.flash?'ON':'OFF');
  }).catch(function(){ btn.textContent='Flash (unreachable)'; });
}
function renderIncidents(list){
  var tb = document.querySelector('#incidentsTable tbody');
  if(!list || !list.length){ tb.innerHTML = '<tr><td colspan=5 class=empty>No incidents recorded</td></tr>'; return; }
  tb.innerHTML = list.map(function(inc){
    var ev = (inc.evidence||[]).length;
    return '<tr><td>'+inc.incidentId+'</td><td>'+inc.state+'</td><td>'+(inc.trigger?inc.trigger.triggerType:'?')+'</td><td>'+(inc.trigger?inc.trigger.severity:'?')+'</td><td>'+ev+' item(s)</td></tr>';
  }).join('');
}
function poll(){
  fetch('/api/status').then(function(r){return r.json();}).then(renderStatus).catch(function(){});
  fetch('/api/devices').then(function(r){return r.json();}).then(renderDevices).catch(function(){});
  fetch('/api/incidents').then(function(r){return r.json();}).then(renderIncidents).catch(function(){});
}
poll();
setInterval(poll, 3000);
</script>
</body></html>)HTML";

// Settings page: WiFi networks (remembered list, add/remove — Section "multiple wifi
// remember", both gateway and every node independently support the same
// NetworkConfig::addNetwork()/removeNetwork() API, though only the gateway gets a web
// UI for it this phase; nodes still expose it as WIFIADD/WIFIREMOVE serial commands)
// and SMTP (reuses EmailConfig, same fields as the existing EMAILCONFIG serial
// command). No CDN, same offline-first constraint as the main dashboard.
static const char SETTINGS_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name=viewport content='width=device-width,initial-scale=1'>
<title>CarSentinel Settings</title>
<style>
:root{color-scheme:dark;}
body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:16px;max-width:520px;}
h1{font-size:1.3em;color:#58a6ff;margin:0 0 4px;}
a.back{color:#58a6ff;text-decoration:none;font-size:0.85em;}
.section{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:14px 16px;margin:16px 0;}
.section h2{font-size:1em;color:#58a6ff;margin-top:0;}
label{display:block;font-size:0.8em;color:#8b949e;margin:8px 0 3px;}
input,select{width:100%;box-sizing:border-box;padding:6px 8px;background:#0d1117;border:1px solid #30363d;color:#e6edf3;border-radius:4px;}
button{margin-top:12px;padding:7px 14px;background:#238636;color:#fff;border:none;border-radius:6px;cursor:pointer;}
button.danger{background:#a12622;}
ul{list-style:none;padding:0;margin:0;}
li{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid #30363d;}
.msg{font-size:0.85em;color:#3fb950;min-height:1.2em;}
</style></head><body>
<a class=back href=/>&larr; Back to dashboard</a>
<h1>Settings</h1>

<div class=section><h2>Network Transport</h2>
<p class=sub style="color:#8b949e;font-size:0.85em">ESP-NOW is always the primary transport to every node. Wi-Fi is only a fallback (and this gateway's own path to the router/Internet) — disable it to run ESP-NOW only. See docs/NETWORK.md.</p>
<label><input id=wifiFallbackEnabled type=checkbox style="width:auto;display:inline-block" onchange=saveTransport()> Wi-Fi fallback enabled</label>
<div class=msg id=transportMsg></div>
</div>

<div class=section><h2>Wi-Fi networks</h2>
<p class=sub style="color:#8b949e;font-size:0.85em">Remembered networks, tried in order at boot — add a network without removing the current one.</p>
<ul id=wifiList></ul>
<label>SSID</label><input id=wifiSsid>
<label>Password</label><input id=wifiPass type=password>
<button onclick=addWifi()>Add / Update Network</button>
<div class=msg id=wifiMsg></div>
</div>

<div class=section><h2>Email (SMTP) Alerts</h2>
<label><input id=emailEnabled type=checkbox style="width:auto;display:inline-block"> Enabled</label>
<label>SMTP Host</label><input id=emailHost>
<label>SMTP Port</label><input id=emailPort type=number value=465>
<label>Username</label><input id=emailUser>
<label>Password (leave blank to keep current)</label><input id=emailPass type=password>
<label>Sender address</label><input id=emailSender>
<label>Recipient address</label><input id=emailRecipient>
<button onclick=saveEmail()>Save Email Settings</button>
<div class=msg id=emailMsg></div>
</div>

<div class=section><h2>Remote Backend</h2>
<p class=sub style="color:#8b949e;font-size:0.85em">Optional. Disabled by default (LOCAL_ONLY) — this gateway and every node work fully without it. See docs/BACKEND.md.</p>
<label>Status: <span id=backendState>-</span></label>
<label>Mode</label>
<select id=backendMode>
<option value=LOCAL_ONLY>Disabled (LOCAL_ONLY)</option>
<option value=CAR_SENTINEL_CLOUD>CarSentinel Cloud</option>
<option value=CUSTOM_SERVER>Custom Server</option>
</select>
<label>Base URL</label><input id=backendUrl placeholder="https://example.com/api/v1">
<label>Device ID</label><input id=backendDeviceId>
<label>Credential (leave blank to keep current)</label><input id=backendCredential type=password>
<button onclick=saveBackend()>Save Backend Settings</button>
<div class=msg id=backendMsg></div>
</div>

<script>
function loadTransport(){
  fetch('/api/settings/transport').then(function(r){return r.json();}).then(function(t){
    document.getElementById('wifiFallbackEnabled').checked = !!t.wifiFallbackEnabled;
  });
}
function saveTransport(){
  var enabled = document.getElementById('wifiFallbackEnabled').checked;
  fetch('/api/settings/transport',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'wifiFallbackEnabled='+(enabled?'1':'0')})
    .then(function(r){return r.json();}).then(function(d){
      document.getElementById('transportMsg').textContent = d.ok ? 'Saved. Restart the device for this to take effect.' : 'Failed to save.';
    });
}
function loadWifi(){
  fetch('/api/settings/wifi').then(function(r){return r.json();}).then(function(list){
    var ul=document.getElementById('wifiList');
    if(!list.length){ ul.innerHTML='<li style="color:#8b949e;font-style:italic">No saved networks</li>'; return; }
    ul.innerHTML=list.map(function(ssid){
      return '<li><span>'+ssid+'</span><button class=danger onclick="removeWifi(\''+ssid.replace(/'/g,"\\'")+'\')">Remove</button></li>';
    }).join('');
  });
}
function addWifi(){
  var ssid=document.getElementById('wifiSsid').value, pass=document.getElementById('wifiPass').value;
  if(!ssid){ return; }
  var body='ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pass);
  fetch('/api/settings/wifi/add',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
    .then(function(r){return r.json();}).then(function(d){
      document.getElementById('wifiMsg').textContent = d.ok ? 'Saved.' : 'Failed to save.';
      document.getElementById('wifiSsid').value=''; document.getElementById('wifiPass').value='';
      loadWifi();
    });
}
function removeWifi(ssid){
  fetch('/api/settings/wifi/remove',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(ssid)})
    .then(function(){ loadWifi(); });
}
function loadEmail(){
  fetch('/api/settings/email').then(function(r){return r.json();}).then(function(e){
    document.getElementById('emailEnabled').checked=!!e.enabled;
    document.getElementById('emailHost').value=e.smtpHost||'';
    document.getElementById('emailPort').value=e.smtpPort||465;
    document.getElementById('emailUser').value=e.username||'';
    document.getElementById('emailSender').value=e.sender||'';
    document.getElementById('emailRecipient').value=e.recipient||'';
  });
}
function saveEmail(){
  var params = new URLSearchParams();
  params.set('enabled', document.getElementById('emailEnabled').checked ? '1':'0');
  params.set('host', document.getElementById('emailHost').value);
  params.set('port', document.getElementById('emailPort').value);
  params.set('user', document.getElementById('emailUser').value);
  params.set('password', document.getElementById('emailPass').value);
  params.set('sender', document.getElementById('emailSender').value);
  params.set('recipient', document.getElementById('emailRecipient').value);
  fetch('/api/settings/email',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()})
    .then(function(r){return r.json();}).then(function(d){
      document.getElementById('emailMsg').textContent = d.ok ? 'Saved.' : 'Failed to save.';
      document.getElementById('emailPass').value='';
    });
}
function loadBackend(){
  fetch('/api/settings/backend').then(function(r){return r.json();}).then(function(b){
    document.getElementById('backendState').textContent = b.state;
    document.getElementById('backendMode').value = b.mode;
    document.getElementById('backendUrl').value = b.baseUrl||'';
    document.getElementById('backendDeviceId').value = b.deviceId||'';
  });
}
function saveBackend(){
  var params = new URLSearchParams();
  params.set('mode', document.getElementById('backendMode').value);
  params.set('baseUrl', document.getElementById('backendUrl').value);
  params.set('deviceId', document.getElementById('backendDeviceId').value);
  params.set('credential', document.getElementById('backendCredential').value);
  fetch('/api/settings/backend',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()})
    .then(function(r){return r.json();}).then(function(d){
      document.getElementById('backendMsg').textContent = d.ok ? 'Saved.' : 'Failed to save.';
      document.getElementById('backendCredential').value='';
      loadBackend();
    });
}
loadTransport();
loadWifi();
loadEmail();
loadBackend();
</script>
</body></html>)HTML";

void DashboardServer::handleRoot() {
    server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
}

void DashboardServer::handleSettingsPage() {
    server.send_P(200, "text/html; charset=utf-8", SETTINGS_HTML);
}

void DashboardServer::handleApiTransportGet() {
    server.send(200, "application/json",
                String("{\"wifiFallbackEnabled\":") +
                (NetworkConfig::get().wifiFallbackEnabled ? "true" : "false") + "}");
}

void DashboardServer::handleApiTransportSave() {
    bool ok = NetworkConfig::setWifiFallbackEnabled(server.arg("wifiFallbackEnabled") == "1");
    server.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

void DashboardServer::handleApiWifiList() {
    const NetworkConfigData& net = NetworkConfig::get();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < net.savedCount; i++) {
        arr.add(net.saved[i].ssid);
    }
    String out;
    serializeJson(arr, out);
    server.send(200, "application/json", out);
}

void DashboardServer::handleApiWifiAdd() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    bool ok = NetworkConfig::addNetwork(ssid, password);
    server.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

void DashboardServer::handleApiWifiRemove() {
    bool ok = NetworkConfig::removeNetwork(server.arg("ssid"));
    server.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

// Password is never sent back to the browser (Section 41 posture, same as every other
// credential in this project) — only whether one is already set.
void DashboardServer::handleApiEmailGet() {
    const EmailConfigData& e = EmailConfig::get();
    JsonDocument doc;
    doc["enabled"] = e.enabled;
    doc["smtpHost"] = e.smtpHost;
    doc["smtpPort"] = e.smtpPort;
    doc["username"] = e.username;
    doc["sender"] = e.sender;
    doc["recipient"] = e.recipient;
    doc["hasPassword"] = e.password.length() > 0;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void DashboardServer::handleApiEmailSave() {
    EmailConfigData e = EmailConfig::get();
    e.enabled = server.arg("enabled") == "1";
    e.smtpHost = server.arg("host");
    e.smtpPort = server.arg("port").toInt() > 0 ? server.arg("port").toInt() : e.smtpPort;
    e.username = server.arg("user");
    if (server.arg("password").length() > 0) {
        e.password = server.arg("password");  // blank means "keep the current one"
    }
    e.sender = server.arg("sender");
    e.recipient = server.arg("recipient");
    bool ok = EmailConfig::save(e);
    server.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

// Credential is never sent back to the browser — same posture as EmailConfig's
// password above.
void DashboardServer::handleApiBackendGet() {
    const BackendConfigData& b = BackendConfig::get();
    JsonDocument doc;
    doc["mode"] = backendModeToString(b.mode);
    doc["baseUrl"] = b.baseUrl;
    doc["deviceId"] = b.deviceId;
    doc["hasCredential"] = b.credential.length() > 0;
    doc["state"] = backendConnectionStateToString(RemoteSyncManager::getState());
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void DashboardServer::handleApiBackendSave() {
    BackendConfigData b = BackendConfig::get();
    b.mode = backendModeFromString(server.arg("mode"));
    b.baseUrl = server.arg("baseUrl");
    b.deviceId = server.arg("deviceId");
    if (server.arg("credential").length() > 0) {
        b.credential = server.arg("credential");  // blank means "keep the current one"
    }
    b.enabled = b.mode != BackendMode::LOCAL_ONLY;
    bool ok = BackendConfig::save(b);
    RemoteSyncManager::begin();  // apply immediately, no reboot required
    server.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

void DashboardServer::handleApiStatus() {
    String json = statusProvider ? statusProvider() : "{}";
    server.send(200, "application/json", json);
}

void DashboardServer::handleApiDevices() {
    String json = devicesProvider ? devicesProvider() : "[]";
    server.send(200, "application/json", json);
}

void DashboardServer::handleApiIncidents() {
    String json = incidentsProvider ? incidentsProvider() : "[]";
    server.send(200, "application/json", json);
}

void DashboardServer::handleStream() {
  if (streamProvider) {
    streamProvider(server.client(), server.arg("node"));
    return;
  }
  server.send(404, "text/plain", "Live stream unavailable");
}

void DashboardServer::begin(const String& deviceTitle, JsonContentProvider statusP,
               JsonContentProvider devicesP, JsonContentProvider incidentsP,
               DashboardStreamProvider streamP) {
    title = deviceTitle;
    statusProvider = statusP;
    devicesProvider = devicesP;
    incidentsProvider = incidentsP;
    streamProvider = streamP;
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleApiStatus);
    server.on("/api/devices", HTTP_GET, handleApiDevices);
    server.on("/api/incidents", HTTP_GET, handleApiIncidents);
    server.on("/stream", HTTP_GET, handleStream);
    server.on("/settings", HTTP_GET, handleSettingsPage);
    server.on("/api/settings/transport", HTTP_GET, handleApiTransportGet);
    server.on("/api/settings/transport", HTTP_POST, handleApiTransportSave);
    server.on("/api/settings/wifi", HTTP_GET, handleApiWifiList);
    server.on("/api/settings/wifi/add", HTTP_POST, handleApiWifiAdd);
    server.on("/api/settings/wifi/remove", HTTP_POST, handleApiWifiRemove);
    server.on("/api/settings/email", HTTP_GET, handleApiEmailGet);
    server.on("/api/settings/email", HTTP_POST, handleApiEmailSave);
    server.on("/api/settings/backend", HTTP_GET, handleApiBackendGet);
    server.on("/api/settings/backend", HTTP_POST, handleApiBackendSave);
    server.begin();
    active = true;
    Logger::info(TAG, "Dashboard active at http://<gateway-ip>/ (API: /api/status, /api/devices, /api/incidents)");
}

void DashboardServer::loop() {
    if (active) {
        server.handleClient();
    }
}

bool DashboardServer::isActive() {
    return active;
}

}  // namespace CarSentinel
