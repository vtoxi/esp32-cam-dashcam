#include "DashboardServer.h"
#include "Logger.h"

#include <WebServer.h>

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
<h1 id=title>CarSentinel Dashboard</h1>
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

void DashboardServer::handleRoot() {
    server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
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
