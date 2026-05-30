/*
 * RIKA Firenet 2.0 Bridge — ESP32-S3
 *
 * First boot: the user configures WiFi from the stove screen.
 * The stove sends credentials to the dongle (USB CDC); the dongle saves them
 * to persistent storage and connects. No credentials are compiled in.
 *
 * CDC protocol (ASCII, \n) — confirmed against DOMO v2.29.585.12 binary + live logs.
 */

#include "USB.h"
#include "USBCDC.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "class/cdc/cdc_device.h"
#include <Update.h>

#define RIKA_VID       0x303A
#define RIKA_PID       0x819A
#define NTP_SERVER     "pool.ntp.org"
#define LOG_MAX_CHARS  32768

Preferences prefs;
USBCDC      USBSerial;
WebServer   server(80);
String      webLog;

// ── Credentials (stored in ESP32 NVS, never hard-coded) ──────────────────────
String wifiSsid, wifiPass, wifiSsidHex;
String stoveId   = "0000000";
String stoveToken = "00000000";
bool   provisioningMode = true;   // true until a WiFi SSID is stored

// ── Setpoints sent to the stove ───────────────────────────────────────────────
// tempRoomTarget is ×10 on the wire (220 = 22.0°C, stove range: 140–280)
String desiredControls = "onOff=0; operatingMode=1; heatingPower=50; tempRoomTarget=160;";

// ── Sensors (POST_SENSORS multi-line) ────────────────────────────────────────
struct SensorEntry { String key; String val; };
#define MAX_SENSORS 32
SensorEntry sensors[MAX_SENSORS];
int  sensorCount = 0;
String lastSensorsRaw, lastControlsRaw;

// ── Protocol state ────────────────────────────────────────────────────────────
bool mainLoopActive = false, phase2Done = false;
bool rtcSynced = false, ntpDone = false;
bool needsRearm = false, pollInProgress = false, pendingControlsWrite = true;
bool pendingPostControls = false;
int  postControlsPos = 0;  // positional index in POST_CONTROLS (shifted format, artefact filtered)
// Values received from the stove via POST_CONTROLS (pos 2=onOff, 3=opMode, 4=power, 5=target)
int stoveOnOff = -1, stoveOpMode = -1, stovePower = -1, stoveTempTarget = -1;
bool ctrlAutoSynced = false;  // first POST_CONTROLS → auto-sync desiredControls
// WiFi reconnect backoff (avoids flooding the AP with auth requests)
unsigned long wifiNextRetryMs = 0;
unsigned long wifiRetryDelayMs = 5000;  // 5s → 10s → 20s → ... → 600s max
unsigned long wifiConnectPendingAt = 0;  // if >0: call esp_wifi_connect() when millis()>=this value
bool serverStarted = false;
unsigned long lastHeartbeatMs = 0, phase1SentAt = 0;
unsigned long phase2GetSentAt = 0, phase2EchoAt = 0;
unsigned long lastPollMs = 0, lastSensorPollMs = 0, lastCDCKeepaliveMs = 0;
int phase1EchoCount = 0;

// ── Incoming GET_CDCDEVICE_STATUS stove→dongle (WiFi provisioning) ───────────
// The stove sends this frame after the user enters SSID+WPA2
// on the stove screen (scan_command or init_command field).
bool   pendingCredFrame = false;
int    credFieldIdx     = 0;
String credSsidHex, credWpa2, credId, credToken;

// ── Parsing POST_CDCDEVICE_STATUS echo from stove (Phase 1 + keepalive) ──────
bool   parsePh1Echo = false;
int    ph1FieldIdx  = 0;
String ph1SsidHex, ph1Wpa2, ph1Id, ph1Token;
bool   pendingPostSensors = false;
int    pendingSensorIdx   = 0;

// ── WiFi scan (triggered by scan_command=1 in the stove echo) ────────────────
bool scanRequested    = false;
bool scanBusy         = false;
bool scanAwaitingGNF  = false;  // true after GET_NETWORKS sent, waiting for stove GNF to send sym=7
String cachedNetworksMsg;        // last GET_NETWORKS=1 message ready to send
unsigned long lastProactiveScanMs = 0;  // timestamp of last proactive scan

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void addLog(const String& msg) {
  unsigned long ms = millis();
  char ts[16];
  snprintf(ts, sizeof(ts), "[%lu.%01lu] ", ms / 1000, (ms % 1000) / 100);
  String entry = String(ts) + msg;
  Serial.println(entry);
  webLog += entry + "\n";
  if (webLog.length() > LOG_MAX_CHARS)
    webLog.remove(0, webLog.length() - LOG_MAX_CHARS);
}

void sendRaw(const String& msg) {
  const char* buf = msg.c_str();
  size_t rem = msg.length(), tot = 0;
  while (rem > 0) {
    uint32_t av = tud_cdc_n_write_available(0);
    if (av > 0) {
      uint32_t n = tud_cdc_n_write(0, buf + tot, rem < av ? rem : av);
      tud_cdc_n_write_flush(0);
      tot += n; rem -= n;
    } else delay(1);
    yield();
  }
}

void sendStove(const String& msg) {
  sendRaw(msg);
  int nl = msg.indexOf('\n');
  String p = nl > 0 ? msg.substring(0, nl) : msg;
  addLog(">>> " + p + (msg.length() > (size_t)(p.length()+1) ? " [+" + String(msg.length()) + "B]" : ""));
}

// Encode SSID ASCII → uppercase hex
String ssidToHex(const String& ssid) {
  String h;
  for (int i = 0; i < (int)ssid.length(); i++) {
    char b[3]; sprintf(b, "%02X", (uint8_t)ssid[i]); h += b;
  }
  return h;
}

// Decode hex → ASCII SSID
String hexToSsid(const String& hex) {
  String s;
  for (int i = 0; i + 1 < (int)hex.length(); i += 2)
    s += (char)strtol(hex.substring(i, i + 2).c_str(), nullptr, 16);
  return s;
}

// Save credentials to persistent storage and start WiFi
void saveAndConnect(const String& ssidHex, const String& pass,
                    const String& id,      const String& token) {
  if (ssidHex.length() == 0 || pass.length() == 0) {
    addLog("PROV: empty credentials, ignored");
    return;
  }
  String ssid = hexToSsid(ssidHex);
  // Validate: SSID must be printable ASCII (otherwise ssidHex was not valid hex)
  bool ssidValid = ssid.length() > 0;
  for (int i = 0; i < (int)ssid.length() && ssidValid; i++) {
    if ((uint8_t)ssid[i] < 0x20 || (uint8_t)ssid[i] > 0x7E) ssidValid = false;
  }
  if (!ssidValid) {
    addLog("PROV: non-ASCII SSID after decode (hex=" + ssidHex.substring(0,20) + ") → ignored");
    return;
  }
  prefs.putString("ssid",  ssid);
  prefs.putString("pass",  pass);
  if (id.length())    { prefs.putString("id",    id);    stoveId    = id;    }
  if (token.length()) { prefs.putString("token", token); stoveToken = token; }

  wifiSsid    = ssid;
  wifiPass    = pass;
  wifiSsidHex = ssidHex;
  provisioningMode = false;

  addLog("PROV OK: SSID=\"" + ssid + "\" → WiFi WPA2/WPA3 connect");
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  wifiRetryDelayMs = 5000;
  wifiNextRetryMs  = 0;
  WiFi.setTxPower(WIFI_POWER_17dBm);
  esp_wifi_set_max_tx_power(68);
  addLog("WiFi MAC: " + WiFi.macAddress());
  {
    wifi_config_t conf = {};
    memcpy(conf.sta.ssid,     ssid.c_str(), min(ssid.length(), (size_t)32));
    memcpy(conf.sta.password, pass.c_str(), min(pass.length(), (size_t)64));
    conf.sta.threshold.authmode  = WIFI_AUTH_OPEN;
    conf.sta.pmf_cfg.capable     = true;
    conf.sta.pmf_cfg.required    = false;
    esp_wifi_set_config(WIFI_IF_STA, &conf);
  }
  esp_wifi_set_max_tx_power(68);
  // 500ms non-blocking delay before connect (lets WiFi driver settle after stop/start)
  wifiConnectPendingAt = millis() + 500;
}

// Sync desiredControls from values received from the stove (first POST_CONTROLS only)
void syncCtrlFromStove() {
  if (ctrlAutoSynced) return;
  if (stoveTempTarget < 0) return;
  // Full sync if all fields are available
  if (stoveOnOff >= 0 && stoveOpMode >= 0 && stovePower >= 0) {
    ctrlAutoSynced = true;
    int syncPower = (stovePower < 50) ? 50 : stovePower;
    desiredControls = "onOff=" + String(stoveOnOff) +
                      "; operatingMode=" + String(stoveOpMode) +
                      "; heatingPower=" + String(syncPower) +
                      "; tempRoomTarget=" + String(stoveTempTarget) + ";";
    prefs.putString("ctrl", desiredControls);
    addLog("--- ctrl full-sync: " + desiredControls);
    return;
  }
  // Partial sync: only target received from stove — update that field only
  int dc_onOff, dc_opMode, dc_power, dc_target;
  parseDesiredControls(dc_onOff, dc_opMode, dc_power, dc_target);
  if (dc_target != stoveTempTarget) {
    desiredControls = "onOff=" + String(dc_onOff) +
                      "; operatingMode=" + String(dc_opMode) +
                      "; heatingPower=" + String(dc_power) +
                      "; tempRoomTarget=" + String(stoveTempTarget) + ";";
    prefs.putString("ctrl", desiredControls);
    addLog("--- ctrl partial-sync target=" + String(stoveTempTarget) + " (" +
           String(stoveTempTarget/10) + "." + String(stoveTempTarget%10) + "°C)");
  }
}

// Parse desiredControls ("onOff=N; operatingMode=N; heatingPower=N; tempRoomTarget=N;")
// and extract the 4 integer values.
void parseDesiredControls(int &dc_onOff, int &dc_opMode, int &dc_power, int &dc_target) {
  dc_onOff = 0; dc_opMode = 1; dc_power = 30; dc_target = 190;
  auto extractField = [&](const char* name) -> int {
    int idx = desiredControls.indexOf(name);
    if (idx < 0) return -1;
    int eq = desiredControls.indexOf('=', idx);
    if (eq < 0) return -1;
    int sc = desiredControls.indexOf(';', eq);
    return (sc >= 0) ? desiredControls.substring(eq+1, sc).toInt() : desiredControls.substring(eq+1).toInt();
  };
  int v;
  if ((v = extractField("onOff=")) >= 0) dc_onOff = v;
  if ((v = extractField("operatingMode=")) >= 0) dc_opMode = v;
  if ((v = extractField("heatingPower=")) >= 0) dc_power = v;
  if (dc_power < 50) dc_power = 50;  // lower bound 50%
  if ((v = extractField("tempRoomTarget=")) >= 0) dc_target = v;
}

// Send GET_CONTROLS=1 in shifted-named format (matches official firmware).
// Payload is parsed from desiredControls and reformatted:
//   line 1: GET_CONTROLS=1; onOff=12201; operatingMode=<onOff>; heatingPower=<opMode>; tempRoomTarget=<power>;
//   line 2: =<target>;
void sendGetControls() {
  int dc_onOff, dc_opMode, dc_power, dc_target;
  parseDesiredControls(dc_onOff, dc_opMode, dc_power, dc_target);
  String line1 = "GET_CONTROLS=1; onOff=12201; operatingMode=" + String(dc_onOff) +
                 "; heatingPower=" + String(dc_opMode) +
                 "; tempRoomTarget=" + String(dc_power) + ";";
  String line2 = "=" + String(dc_target) + ";";
  sendStove(line1 + "\r\n" + line2 + "\r\n");
  addLog(">>> " + line1 + " / " + line2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Build CDC status fields
// 20 fields + optionally 3 OTA fields
// full=false → Phase 1 (blank, dongle announces presence without credentials)
// full=true  → Phase 2 (full credentials)
// ─────────────────────────────────────────────────────────────────────────────
String buildFields(bool full, int symbol = -1) {
  if (symbol < 0) symbol = provisioningMode ? 5 : 4;  // 5=disconnected, 4=connected
  // Always cache IP+RSSI so they survive temporary WiFi drops
  static String cachedIP = "";
  static int    cachedRssi = -60;
  if (WiFi.status() == WL_CONNECTED) {
    cachedIP = WiFi.localIP().toString();
    int r = WiFi.RSSI(); if (r != 0) cachedRssi = r;
  }
  String ip  = full ? cachedIP : "";
  String mac = full ? WiFi.macAddress() : "";

  String s;
  s += "0\n";                                              // 1  monitoring
  s += "1\n";                                              // 2  on_off
  s += "0\n";                                              // 3  scan_command
  s += "0\n";                                              // 4  init_command
  s += (full ? "1\n" : "0\n");                            // 5  initialised
  s += String(symbol) + "\n";                              // 6  symbol
  s += "0\n";                                              // 7  error
  s += "999\n";                                            // 8  bl_version
  s += "201\n";                                            // 9  app_version
  s += "12201\n";                                          // 10 app_revision
  s += (full ? "229\n" : "0\n");                          // 11 spwf_version
  s += (full ? String(cachedRssi) + "\n" : "0\n");        // 12 rssi
  s += (full ? stoveId    + "\n" : "\n");                  // 13 id
  s += (full ? stoveToken + "\n" : "\n");                  // 14 token
  s += "3\n";                                              // 15 protocol
  s += (full && wifiSsidHex.length() ? wifiSsidHex + "\n" : "\n");  // 16 ssid hex
  s += (full && wifiPass.length()    ? wifiPass    + "\n" : "\n");   // 17 wpa2
  s += (full ? ip  + "\n" : "\n");                        // 18 ip
  s += (full ? mac + "\n" : "\n");                        // 19 mac
  s += "1\n";                                              // 20 cdc_device
  return s;
}

void sendPostCDCStatus(bool full) {
  String msg = "POST_CDCDEVICE_STATUS=0;\n";
  msg += buildFields(full);
  msg += "-------\n";
  sendRaw(msg);
  addLog(">>> POST_CDCDEVICE_STATUS(" + String(full?"full":"blank") + ") [+" + String(msg.length()) + "B]");
}

void sendGetCDCStatus(bool full, int symbol = -1) {
  String msg = "GET_CDCDEVICE_STATUS=0;\n";
  msg += buildFields(full, symbol);
  msg += "0\n0\n0\n";   // 3 champs OTA
  sendRaw(msg);
  addLog(">>> GET_CDCDEVICE_STATUS(" + String(full?"full":"blank") + (symbol >= 0 ? " sym="+String(symbol) : "") + ") [+" + String(msg.length()) + "B]");
}

// ─────────────────────────────────────────────────────────────────────────────
// NTP / RTC
// ─────────────────────────────────────────────────────────────────────────────
void startNTP() {
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, NTP_SERVER);
  sntp_init();
  ntpDone = true;
  addLog("NTP init");
}

String buildRTCTimestamp() {
  time_t now = time(nullptr);
  if (!ntpDone || now < 1000000) return "START_20260527_120000;\r\n";
  struct tm* t = localtime(&now);
  char buf[32];
  snprintf(buf, sizeof(buf), "START_20%02d%02d%02d_%02d%02d%02d;\r\n",
           t->tm_year - 100, t->tm_mon + 1, t->tm_mday,
           t->tm_hour, t->tm_min, t->tm_sec);
  return String(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser POST_SENSORS
// ─────────────────────────────────────────────────────────────────────────────
void parseSensors(const String& raw) {
  sensorCount = 0;
  int pos = raw.indexOf(';');
  if (pos < 0) return;
  pos++;
  int fi = 0;
  while (pos < (int)raw.length() && sensorCount < MAX_SENSORS) {
    while (pos < (int)raw.length() &&
           (raw[pos]==' ' || raw[pos]=='\n' || raw[pos]=='\r')) pos++;
    int eq = raw.indexOf('=', pos);
    if (eq < 0) break;
    int sc = raw.indexOf(';', eq);
    if (sc < 0) sc = raw.length();
    String key = raw.substring(pos, eq); key.trim();
    String val = raw.substring(eq + 1, sc); val.trim();
    if (key.length() == 0) key = "f" + String(fi);
    if (val.length() > 0) {
      sensors[sensorCount].key = key;
      sensors[sensorCount].val = val;
      sensorCount++;
    }
    fi++;
    pos = sc + 1;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Web handlers
// ─────────────────────────────────────────────────────────────────────────────
void corsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin",  "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleLog() { corsHeaders(); server.send(200, "text/plain", webLog); }

void handleApiStatus() {
  corsHeaders();
  String json = "{";
  json += "\"wifi\":" + String(WiFi.status()==WL_CONNECTED?"true":"false") + ",";
  json += "\"ip\":\"" + (WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("")) + "\",";
  json += "\"ssid\":\"" + wifiSsid + "\",";
  json += "\"provisioning\":" + String(provisioningMode?"true":"false") + ",";
  json += "\"mainLoop\":" + String(mainLoopActive?"true":"false") + ",";
  json += "\"controls\":\"" + desiredControls + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiSensors() {
  corsHeaders();
  String json = "{";
  for (int i = 0; i < sensorCount; i++) {
    if (i) json += ",";
    json += "\"" + sensors[i].key + "\":\"" + sensors[i].val + "\"";
  }
  // Append values received from the stove (POST_CONTROLS parsed positionally)
  if (stoveOnOff >= 0) {
    if (sensorCount) json += ",";
    json += "\"stoveOnOff\":\"" + String(stoveOnOff) + "\"";
    json += ",\"stoveOpMode\":\"" + String(stoveOpMode) + "\"";
    json += ",\"stovePower\":\"" + String(stovePower) + "\"";
    json += ",\"stoveTempTarget\":\"" + String(stoveTempTarget) + "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiControls() {
  corsHeaders();
  if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
  if (server.method() == HTTP_POST || server.hasArg("cmd")) {
    String cmd = server.hasArg("cmd") ? server.arg("cmd") : server.arg("plain");
    if (cmd.length() > 0 && cmd.indexOf('=') >= 0) {
      desiredControls = cmd;
      if (mainLoopActive) { needsRearm = true; pendingControlsWrite = true; }
      addLog("CTRL update: " + desiredControls);
      server.send(200, "application/json", "{\"ok\":true}");
    } else {
      server.send(400, "application/json", "{\"error\":\"bad cmd\"}");
    }
  } else {
    server.send(200, "text/plain", desiredControls);
  }
}

void handleOtaPage() {
  server.send(200, "text/html", R"OTA(<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>OTA — Open Firenet</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#f0f2f5;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.card{background:#fff;border-radius:14px;padding:28px;max-width:380px;width:100%;box-shadow:0 4px 24px rgba(0,0,0,.1)}
h1{font-size:1.2em;font-weight:800;color:#b5351e;margin-bottom:4px}
.sub{font-size:12px;color:#aaa;margin-bottom:20px}
input[type=file]{width:100%;padding:10px;border:2px dashed #ddd;border-radius:9px;font-size:13px;margin-bottom:16px;cursor:pointer;background:#fafafa}
.btn{width:100%;padding:12px;background:#b5351e;color:#fff;border:none;border-radius:9px;font-size:14px;font-weight:700;cursor:pointer}
.btn:hover:not(:disabled){background:#e74c3c}
.btn:disabled{background:#ccc;cursor:not-allowed}
#st{margin-top:14px;font-size:13px;text-align:center;min-height:20px}
.back{font-size:12px;color:#0078d7;text-decoration:none;display:block;margin-top:16px;text-align:center}
.back:hover{text-decoration:underline}
</style></head><body>
<div class='card'>
<h1>OTA Update</h1>
<div class='sub'>Open Firenet — flash via WiFi</div>
<form method='POST' action='/update' enctype='multipart/form-data'>
<input type='file' name='firmware' accept='.bin' onchange='document.getElementById("btn").disabled=!this.value'>
<button type='submit' class='btn' id='btn' disabled onclick='document.getElementById("st").textContent="Uploading..."'>Flash</button>
</form>
<div id='st'></div>
<a class='back' href='/'>&#8592; Back</a>
</div>
</body></html>)OTA");
}

void handleOtaUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    addLog("OTA: start " + upload.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
      addLog("OTA begin error: " + String(Update.errorString()));
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
      addLog("OTA write error: " + String(Update.errorString()));
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true))
      addLog("OTA done: " + String(upload.totalSize) + "B — rebooting");
    else
      addLog("OTA end error: " + String(Update.errorString()));
  }
}

void handleResetWifi() {
  addLog("=== RESET WiFi → provisioning mode ===");
  prefs.remove("ssid"); prefs.remove("pass");
  prefs.remove("id");   prefs.remove("token");
  server.send(200, "text/plain", "WiFi credentials erased — rebooting...");
  delay(500);
  ESP.restart();
}

void handleRoot() {
  String wifiStatus = WiFi.status() == WL_CONNECTED
    ? ("Connected — " + WiFi.localIP().toString() + " (" + wifiSsid + ")")
    : (provisioningMode ? "En attente de configuration via l'écran du poêle"
                        : "Déconnecté (reconnexion...)");

  String html = R"(<!DOCTYPE html><html><head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Open Firenet</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#f0f2f5;min-height:100vh;color:#222}
.wrap{max-width:520px;margin:0 auto;padding:20px 16px}
header{background:linear-gradient(135deg,#b5351e 0%,#e74c3c 100%);border-radius:14px;padding:18px 22px;margin-bottom:16px;box-shadow:0 4px 18px rgba(183,53,30,.32);display:flex;align-items:center;justify-content:space-between}
.hinfo h1{color:#fff;font-size:1.5em;font-weight:800}
.hinfo .sub{color:rgba(255,255,255,.72);font-size:12px;margin-top:2px}
.lang-row{display:flex;gap:4px}
.lbtn{padding:4px 10px;border:1.5px solid rgba(255,255,255,.45);background:transparent;color:rgba(255,255,255,.75);border-radius:6px;cursor:pointer;font-size:12px;font-weight:700;transition:all .15s}
.lbtn.active{background:rgba(255,255,255,.22);color:#fff;border-color:rgba(255,255,255,.9)}
.card{background:#fff;border-radius:12px;padding:16px 18px;margin-bottom:14px;box-shadow:0 2px 10px rgba(0,0,0,.07)}
.ctitle{font-size:10.5px;font-weight:700;text-transform:uppercase;letter-spacing:1px;color:#bbb;margin-bottom:12px}
.ctrow{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px}
.ctrow .ctitle{margin-bottom:0}
.ok{background:#d4edda;color:#155724}.warn{background:#fff3cd;color:#856404}.err{background:#f8d7da;color:#721c24}
.badge{display:inline-block;padding:3px 11px;border-radius:20px;font-size:12px;font-weight:600}
.stbadge{display:inline-block;padding:3px 12px;border-radius:20px;font-size:12px;font-weight:700}
.son{background:#d4edda;color:#155724}.soff{background:#f0f0f0;color:#999}
table{width:100%;border-collapse:collapse;font-size:13px}
th{font-size:11px;text-transform:uppercase;letter-spacing:.5px;color:#bbb;font-weight:600;padding:6px 10px;border-bottom:2px solid #f3f3f3;text-align:left}
td{padding:8px 10px;border-bottom:1px solid #f5f5f5;color:#444}
tr:last-child td{border-bottom:none}
.pgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:16px}
.pbtn{padding:13px 8px;border:none;border-radius:10px;cursor:pointer;font-size:14px;font-weight:700;transition:filter .15s,opacity .15s}
.pbtn:hover:not(:disabled){filter:brightness(.9)}
.pbtn:disabled{cursor:not-allowed}
.gbtn{background:#27ae60;color:#fff}
.rbtn{background:#e74c3c;color:#fff}
.arow{display:flex;align-items:center;justify-content:space-between;padding:11px 0;border-bottom:1px solid #f5f5f5}
.albl{font-size:13px;color:#777;font-weight:500}
.actrl{display:flex;align-items:center;gap:12px}
.abtn{width:38px;height:38px;border:2px solid #e4e4e4;background:#fafafa;border-radius:9px;cursor:pointer;font-size:22px;font-weight:300;color:#555;display:flex;align-items:center;justify-content:center;transition:border-color .15s,background .15s}
.abtn:hover{border-color:#0078d7;background:#e7f1fd;color:#0078d7}
.aval{font-size:2em;font-weight:800;min-width:58px;text-align:center;color:#111}
.aunit{font-size:12px;color:#bbb;min-width:22px}
.mrow{display:flex;gap:8px;padding-top:14px}
.mbtn{flex:1;padding:10px 4px;border:2px solid #e4e4e4;background:#fafafa;border-radius:9px;cursor:pointer;font-size:13px;font-weight:600;color:#888;transition:all .15s}
.mbtn:hover{border-color:#0078d7;color:#0078d7;background:#fff}
.mbtn.active{background:#0078d7;border-color:#0078d7;color:#fff}
#fb{font-size:12px;min-height:18px;margin-top:10px;text-align:center;font-weight:600}
.craw input[type=text]{width:100%;padding:9px 12px;border:2px solid #e4e4e4;border-radius:9px;font-size:12px;font-family:monospace;color:#444;transition:border-color .15s;outline:none}
.craw input:focus{border-color:#0078d7}
.sbtn{width:100%;padding:10px;margin-top:8px;background:#0078d7;color:#fff;border:none;border-radius:9px;cursor:pointer;font-size:14px;font-weight:600;transition:background .15s}
.sbtn:hover{background:#0062b1}
.consignes{font-size:10.5px;color:#ccc;margin-top:12px;font-family:monospace;word-break:break-all;line-height:1.4}
.rlnk{font-size:11px;color:#e74c3c;text-decoration:none}
.rlnk:hover{text-decoration:underline}
.ltog{display:flex;align-items:center;justify-content:space-between;cursor:pointer;user-select:none;-webkit-user-select:none}
.ltog .arr{width:24px;height:24px;border-radius:50%;background:#f3f3f3;display:flex;align-items:center;justify-content:center;font-size:10px;color:#aaa;transition:transform .22s,background .15s}
.ltog:hover .arr{background:#e0eaf7;color:#0078d7}
.ltog.open .arr{transform:rotate(180deg)}
#logbody{display:none;margin-top:12px}
#logbody.open{display:block}
pre{background:#14141f;color:#8ab4f8;padding:12px;height:260px;overflow-y:scroll;font-size:10.5px;border-radius:9px;line-height:1.55;margin:0}
.lact{display:flex;justify-content:flex-end;margin-top:8px}
.bsm{font-size:11px;padding:4px 12px;background:#6c757d;color:#fff;border:none;border-radius:5px;cursor:pointer}
</style></head><body>
<div class='wrap'>
<header>
  <div class='hinfo'>
    <h1>Open Firenet</h1>
    <div class='sub' data-i18n='subtitle'></div>
  </div>
  <div class='lang-row'>
    <button class='lbtn' id='lang-fr' onclick='setLang("fr")'>FR</button>
    <button class='lbtn' id='lang-en' onclick='setLang("en")'>EN</button>
  </div>
</header>
<div class='card'>
  <div class='ctitle' data-i18n='network'></div>
  <div id='wsts'>)" + wifiStatus + R"(</div>
  <div id='provwarn'></div>
</div>
<div class='card'>
  <div class='ctitle' data-i18n='sensors'></div>
  <div id='sensors'><em style='color:#ccc;font-size:13px' data-i18n='waiting'></em></div>
</div>
<div class='card'>
  <div class='ctrow'>
    <div class='ctitle' data-i18n='control'></div>
    <span class='stbadge soff' id='stbadge'></span>
  </div>
  <div class='pgrid'>
    <button class='pbtn gbtn' id='btn-start' onclick='setPower(1)' data-i18n='start'></button>
    <button class='pbtn rbtn' id='btn-stop'  onclick='setPower(0)' data-i18n='stop'></button>
  </div>
  <div class='arow' id='adj-temp' style='display:none'>
    <span class='albl' data-i18n='temp'></span>
    <div class='actrl'>
      <button class='abtn' onclick='adjTemp(-1)'>&#8722;</button>
      <span class='aval' id='temp'>&#8211;</span>
      <span class='aunit'>°C</span>
      <button class='abtn' onclick='adjTemp(1)'>+</button>
    </div>
  </div>
  <div class='arow' id='adj-power'>
    <span class='albl' data-i18n='power'></span>
    <div class='actrl'>
      <button class='abtn' onclick='adjPow(-5)'>&#8722;</button>
      <span class='aval' id='pow'>&#8211;</span>
      <span class='aunit'>%</span>
      <button class='abtn' onclick='adjPow(5)'>+</button>
    </div>
  </div>
  <div class='mrow'>
    <button class='mbtn' id='m0' onclick='setMode(0)'></button>
    <button class='mbtn' id='m1' onclick='setMode(1)'></button>
    <button class='mbtn' id='m2' onclick='setMode(2)'></button>
  </div>
  <div id='fb'></div>
  <div class='consignes'><span data-i18n='setpoints'></span>&nbsp;: <span id='ctrl'>)" + desiredControls + R"(</span></div>
</div>
<div class='card craw'>
  <div class='ctrow'>
    <div class='ctitle' data-i18n='cmd'></div>
    <div style='display:flex;gap:12px'>
      <a class='rlnk' href='/update' data-i18n='ota'></a>
      <a class='rlnk' id='resetlnk' href='/reset-wifi' data-i18n='resetwifi'></a>
    </div>
  </div>
  <form onsubmit='sendCmd(event)'>
    <input type='text' id='cmdinput' placeholder='onOff=1; operatingMode=0; heatingPower=50; tempRoomTarget=220;'>
    <button type='submit' class='sbtn' data-i18n='send'></button>
  </form>
</div>
<div class='card'>
  <div class='ltog' id='ltog' onclick='toggleLog()'>
    <div class='ctitle' style='margin-bottom:0' data-i18n='log'></div>
    <span class='arr'>&#9660;</span>
  </div>
  <div id='logbody'>
    <pre id='log'></pre>
    <div class='lact'><button class='bsm' onclick='copyLog()' data-i18n='copy'></button></div>
  </div>
</div>
</div>
<script>
var ctrlState={onOff:0,operatingMode:0,heatingPower:50,tempRoomTarget:220};
var logOpen=false,ignoreCtrlUntil=0;
var isProv=)" + (provisioningMode ? "true" : "false") + R"(;
var LANG=localStorage.getItem('rika_lang')||'fr';
var I18N={
  fr:{subtitle:'Contrôle local du poêle',network:'Réseau WiFi',sensors:'Capteurs',control:'Contrôle',start:'Allumer',stop:'Éteindre',temp:'Température',power:'Puissance',mode:'Mode',manual:'Manuel',auto:'Auto',comfort:'Confort',cmd:'Commande directe',send:'Envoyer',resetwifi:'Réinit. WiFi',resetconfirm:'Effacer les credentials WiFi et redémarrer ?',log:'Log',copy:'Copier',setpoints:'Consignes',waiting:'En attente...',stOn:'Allumé',stOff:'Éteint',modeset:'Mode → ',tempset:'Temp → ',powset:'Puissance → ',sent:'✓ Envoyé',sensor_room:'Temp. ambiante',ota:'OTA',provwarn:'Allez dans <strong>Réglages → WiFi</strong> sur l&#39;écran du poêle pour configurer le WiFi.'},
  en:{subtitle:'Local stove control',network:'WiFi Network',sensors:'Sensors',control:'Control',start:'Start',stop:'Stop',temp:'Temperature',power:'Power',mode:'Mode',manual:'Manual',auto:'Auto',comfort:'Comfort',cmd:'Direct command',send:'Send',resetwifi:'Reset WiFi',resetconfirm:'Clear WiFi credentials and reboot?',log:'Log',copy:'Copy',setpoints:'Setpoints',waiting:'Waiting...',stOn:'On',stOff:'Off',modeset:'Mode → ',tempset:'Temp → ',powset:'Power → ',sent:'✓ Sent',sensor_room:'Room temp.',ota:'OTA',provwarn:'Go to <strong>Settings → WiFi</strong> on the stove screen to configure WiFi.'}
};
function t(k){return(I18N[LANG]||I18N.fr)[k]||k;}
function setLang(l){LANG=l;localStorage.setItem('rika_lang',l);applyLang();}
function applyLang(){
  document.querySelectorAll('[data-i18n]').forEach(function(el){el.textContent=t(el.getAttribute('data-i18n'));});
  var pw=document.getElementById('provwarn');
  if(pw)pw.innerHTML=isProv?'<p style="font-size:13px;color:#856404;margin-top:8px">'+t('provwarn')+'</p>':'';
  var rl=document.getElementById('resetlnk');
  if(rl)rl.onclick=function(){return confirm(t('resetconfirm'));};
  document.getElementById('lang-fr').className='lbtn'+(LANG==='fr'?' active':'');
  document.getElementById('lang-en').className='lbtn'+(LANG==='en'?' active':'');
  updateModeButtons();updateStateDisplay();
}
function parseCtrl(s){var r={};s.split(';').forEach(function(p){p=p.trim();var e=p.indexOf('=');if(e>0){var k=p.substring(0,e).trim(),v=parseInt(p.substring(e+1));if(!isNaN(v))r[k]=v;}});return r;}
function fb(msg,ok){var el=document.getElementById('fb');el.style.color=ok===false?'#e74c3c':'#27ae60';el.textContent=msg;setTimeout(function(){el.textContent='';},3000);}
function setPower(v){
  var nc='onOff='+v+'; operatingMode='+(ctrlState.operatingMode||0)+'; heatingPower='+(ctrlState.heatingPower||50)+'; tempRoomTarget='+(ctrlState.tempRoomTarget||220)+';';
  fetch('/api/controls',{method:'POST',body:nc}).then(function(){ctrlState.onOff=v;ignoreCtrlUntil=Date.now()+2000;fb(t(v?'stOn':'stOff'));updateDisplay();});
}
function adjTemp(d){
  fetch('/api/controls').then(function(r){return r.text();}).then(function(s){
    ctrlState=parseCtrl(s);
    var tv=Math.max(140,Math.min(280,(ctrlState.tempRoomTarget||220)+d*10));
    var nc='onOff='+ctrlState.onOff+'; operatingMode='+(ctrlState.operatingMode||0)+'; heatingPower='+(ctrlState.heatingPower||50)+'; tempRoomTarget='+tv+';';
    fetch('/api/controls',{method:'POST',body:nc}).then(function(){ctrlState.tempRoomTarget=tv;ignoreCtrlUntil=Date.now()+2000;fb(t('tempset')+(tv/10).toFixed(1)+'°C');document.getElementById('temp').textContent=(tv/10).toFixed(1);});
  });
}
function adjPow(d){
  fetch('/api/controls').then(function(r){return r.text();}).then(function(s){
    ctrlState=parseCtrl(s);
    var p=Math.max(50,Math.min(100,(ctrlState.heatingPower||50)+d));
    var nc='onOff='+ctrlState.onOff+'; operatingMode='+(ctrlState.operatingMode||0)+'; heatingPower='+p+'; tempRoomTarget='+(ctrlState.tempRoomTarget||220)+';';
    fetch('/api/controls',{method:'POST',body:nc}).then(function(){ctrlState.heatingPower=p;ignoreCtrlUntil=Date.now()+2000;fb(t('powset')+p+'%');document.getElementById('pow').textContent=p;});
  });
}
function updateDisplay(){
  var tv=ctrlState.tempRoomTarget||220;
  document.getElementById('temp').textContent=(tv/10).toFixed(1);
  document.getElementById('pow').textContent=ctrlState.heatingPower||'–';
  updateModeButtons();updateStateDisplay();updateAdjVisibility();
}
function updateStateDisplay(){
  var on=ctrlState.onOff===1;
  var b=document.getElementById('stbadge');
  b.textContent=t(on?'stOn':'stOff');b.className='stbadge '+(on?'son':'soff');
  var bs=document.getElementById('btn-start'),be=document.getElementById('btn-stop');
  bs.disabled=on;bs.style.opacity=on?'0.38':'1';
  be.disabled=!on;be.style.opacity=!on?'0.38':'1';
}
function updateAdjVisibility(){
  var c=ctrlState.operatingMode===2;
  document.getElementById('adj-temp').style.display=c?'':'none';
  document.getElementById('adj-power').style.display=c?'none':'';
}
function updateModeButtons(){
  var keys=['manual','auto','comfort'];
  [0,1,2].forEach(function(i){
    var b=document.getElementById('m'+i);
    if(b){b.className='mbtn'+(ctrlState.operatingMode===i?' active':'');b.textContent=t(keys[i]);}
  });
}
function setMode(m){
  fetch('/api/controls').then(function(r){return r.text();}).then(function(s){
    ctrlState=parseCtrl(s);
    var nc='onOff='+ctrlState.onOff+'; operatingMode='+m+'; heatingPower='+(ctrlState.heatingPower||50)+'; tempRoomTarget='+(ctrlState.tempRoomTarget||220)+';';
    fetch('/api/controls',{method:'POST',body:nc}).then(function(r){return r.text();}).then(function(){
      ctrlState.operatingMode=m;ignoreCtrlUntil=Date.now()+2000;
      fb(t('modeset')+t(['manual','auto','comfort'][m]||''));
      updateModeButtons();updateAdjVisibility();
    });
  });
}
function toggleLog(){
  logOpen=!logOpen;
  document.getElementById('logbody').className=logOpen?'open':'';
  document.getElementById('ltog').className='ltog'+(logOpen?' open':'');
  if(logOpen)fetchLog();
}
function fetchLog(){
  fetch('/log').then(function(r){return r.text();}).then(function(txt){
    var el=document.getElementById('log');
    var atBot=el.scrollHeight-el.scrollTop<=el.clientHeight+30;
    el.textContent=txt;if(atBot)el.scrollTop=el.scrollHeight;
  });
}
function sendCmd(e){
  e.preventDefault();
  var c=document.getElementById('cmdinput').value.trim();
  if(!c)return;
  fetch('/api/controls',{method:'POST',body:c}).then(function(r){return r.text();}).then(function(){fb(t('sent'));document.getElementById('ctrl').textContent=c;ctrlState=parseCtrl(c);updateDisplay();});
}
function fetchAll(){
  fetch('/api/sensors').then(function(r){return r.json();}).then(function(d){
    var rows='<table><tr><th>'+t('sensors')+'</th><th></th></tr>';
    if(d.f0!==undefined){var v0=parseInt(d.f0);rows+='<tr><td>'+t('sensor_room')+'</td><td>'+(v0>0?(v0/10).toFixed(1)+'°C':'–')+'</td></tr>';}
    for(var k in d){if(k==='f0')continue;var v=parseInt(d[k]);var disp=(v>0&&v<5000)?((v/10).toFixed(1)+'°C'):(d[k]);rows+='<tr><td>'+k+'</td><td>'+disp+'</td></tr>';}
    rows+='</table>';
    document.getElementById('sensors').innerHTML=Object.keys(d).length?rows:'<em style="color:#ccc;font-size:13px">'+t('waiting')+'</em>';
  }).catch(function(){});
  fetch('/api/controls').then(function(r){return r.text();}).then(function(s){
    if(Date.now()>ignoreCtrlUntil){document.getElementById('ctrl').textContent=s;ctrlState=parseCtrl(s);updateDisplay();}
  });
  if(logOpen)fetchLog();
}
function copyLog(){var txt=document.getElementById('log').textContent;var el=document.createElement('textarea');el.value=txt;document.body.appendChild(el);el.select();document.execCommand('copy');document.body.removeChild(el);}
setInterval(fetchAll,1500);fetchAll();applyLang();
</script></body></html>)";
  server.send(200, "text/html", html);
}

// ─────────────────────────────────────────────────────────────────────────────
// Drain USB pendant N ms
// ─────────────────────────────────────────────────────────────────────────────
void processStoveCommand(const String& raw);

// Synchronous read of fields 1-17 from a POST_CDCDEVICE_STATUS echo.
// Called immediately after receiving the header; reads fields in a tight
// loop before the next poll cycle can interfere.
void extractCredsFromStoveEcho() {
  String ssidHex, wpa2, id, token;
  int fi = 0;
  unsigned long deadline = millis() + 2500;
  while (fi < 20 && millis() < deadline) {
    if (!USBSerial.available()) {
      yield(); ArduinoOTA.handle(); server.handleClient();
      continue;
    }
    USBSerial.setTimeout(200);
    String line = USBSerial.readStringUntil('\n');
    while (line.length() > 0 &&
           (line[line.length()-1]=='\r' || line[line.length()-1]==';' || line[line.length()-1]==' '))
      line.remove(line.length()-1);
    // Stopper si on voit une nouvelle commande (ne pas la consommer)
    if (line.indexOf("POST_CDCDEVICE_STATUS") != -1 ||
        line.indexOf("GET_CDCDEVICE_STATUS")  != -1 ||
        line == "-------" || line == "OK" || line.indexOf('\x16') != -1) {
      processStoveCommand(line);
      break;
    }
    fi++;
    if (fi <= 5) addLog("DBG fi=" + String(fi) + " [" + line + "]");
    if      (fi == 3  && line.toInt() == 1) { scanRequested = true; addLog("scan_command=1 → scan requis"); }
    else if (fi == 13) id      = line;
    else if (fi == 14) token   = line;
    else if (fi == 16) ssidHex = line;
    else if (fi == 17) { wpa2 = line; break; }
  }
  addLog("CRED sync fi=" + String(fi) + " ssid=" + ssidHex + " wpa2len=" + String(wpa2.length()));
  if (ssidHex.length() > 0 && wpa2.length() > 0)
    saveAndConnect(ssidHex, wpa2,
                   id.length()    > 0 ? id    : stoveId,
                   token.length() > 0 ? token : stoveToken);
}

void drainFor(unsigned long ms) {
  unsigned long deadline = millis() + ms;
  while (millis() < deadline) {
    if (USBSerial.available()) {
      USBSerial.setTimeout(50);
      String raw = USBSerial.readStringUntil('\n');
      // Raw log before any processing (sensor debugging)
      {
        String dbg = raw;
        dbg.replace("\r", "");
        if (dbg.length() > 0)
          addLog("RAW: [" + dbg + "]");
      }
      while (raw.length() > 0 &&
             (raw[raw.length()-1]=='\r' || raw[raw.length()-1]==';' || raw[raw.length()-1]==' '))
        raw.remove(raw.length()-1);
      if (raw.length() > 0 || parsePh1Echo || pendingCredFrame)
        processStoveCommand(raw);
    }
    yield();
    ArduinoOTA.handle();
    server.handleClient();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Process lines received from the stove
// ─────────────────────────────────────────────────────────────────────────────
void processStoveCommand(const String& raw) {
  if (raw.length() == 0) return;

  // ── GET_CDCDEVICE_STATUS frame sent BY the stove (WiFi provisioning) ──────
  // The stove sends this frame after the user configures WiFi
  // from the stove screen: credentials are in fields 16 (ssid hex) and 17 (wpa2).
  if (raw.indexOf("GET_CDCDEVICE_STATUS") == 0 && raw.indexOf("=") >= 0) {
    addLog("<<< [STOVE→CRED] " + raw.substring(0, 40));
    pendingCredFrame = true;
    credFieldIdx = 0;
    credSsidHex = ""; credWpa2 = ""; credId = ""; credToken = "";
    return;
  }

  // ── Champs de la trame credentials (pendingCredFrame) ─────────────────────
  if (pendingCredFrame) {
    credFieldIdx++;
    if      (credFieldIdx == 3  && raw.toInt() == 1) { scanRequested = true; addLog("<<< scan_command=1 (stove GET_CDCDEVICE_STATUS)"); }
    else if (credFieldIdx == 13) credId      = raw;
    else if (credFieldIdx == 14) credToken   = raw;
    else if (credFieldIdx == 16) credSsidHex = raw;
    else if (credFieldIdx == 17) credWpa2    = raw;
    if (credFieldIdx >= 20) {
      pendingCredFrame = false;
      addLog("CRED frame complete: ssid=" + credSsidHex + " id=" + credId);
      if (credSsidHex.length() > 0 && credWpa2.length() > 0)
        saveAndConnect(credSsidHex, credWpa2, credId, credToken);
      else
        addLog("CRED: empty SSID or WPA2, ignored");
    }
    return;
  }

  // ── Parsing POST_CDCDEVICE_STATUS echo from stove (Phase 1 and Phase 2) ────
  // Les champs vides (id/token/ssid/wpa2/ip/mac blancs) arrivent comme lignes vides.
  // ATTENTION : si une nouvelle commande arrive (POST_CDCDEVICE_STATUS, -------,
  // OK), on ferme parsePh1Echo et on laisse le code normal la traiter.
  if (parsePh1Echo) {
    // Detect lines that are NOT fields (new commands)
    if (raw.indexOf("POST_CDCDEVICE_STATUS") != -1 ||
        raw.indexOf("GET_CDCDEVICE_STATUS")  != -1 ||
        raw == "-------" || raw == "OK" || raw.indexOf('\x16') != -1) {
      parsePh1Echo = false;
      addLog("PH1 echo ended early: " + ph1SsidHex);
      // Save credentials if found (stove echo containing its stored credentials)
      if (provisioningMode && ph1SsidHex.length() > 0 && ph1Wpa2.length() > 0)
        saveAndConnect(ph1SsidHex, ph1Wpa2, ph1Id, ph1Token);
      // Continue normal processing of this line (fall-through)
    } else {
      ph1FieldIdx++;
      if (ph1FieldIdx == 3 && raw.toInt() == 1) {
        scanRequested = true; addLog("scan_command=1 in echo → scan requested");
      }
      if      (ph1FieldIdx == 13) ph1Id      = raw;
      else if (ph1FieldIdx == 14) ph1Token   = raw;
      else if (ph1FieldIdx == 16) ph1SsidHex = raw;
      else if (ph1FieldIdx == 17) ph1Wpa2    = raw;
      if (ph1FieldIdx >= 20) {
        parsePh1Echo = false;
        addLog("PH1 echo: ssid=" + ph1SsidHex + " wpa2len=" + String(ph1Wpa2.length()) + " id=" + ph1Id);
        if (provisioningMode && ph1SsidHex.length() > 0 && ph1Wpa2.length() > 0) {
          addLog("PROV: stove has stored credentials → connecting");
          saveAndConnect(ph1SsidHex, ph1Wpa2, ph1Id, ph1Token);
        }
      }
      return;
    }
  }

  // ── Trame POST_SENSORS multi-ligne (champs "=val") ────────────────────────
  if (pendingPostSensors && raw.length() > 1 && raw[0] == '=') {
    String val = raw.substring(1); val.trim();
    if (val.length() > 0 && sensorCount < MAX_SENSORS) {
      String key = "f" + String(pendingSensorIdx);
      sensors[sensorCount].key = key;
      sensors[sensorCount].val = val;
      sensorCount++;
      String interp = "";
      int v = val.toInt();
      // f0 = sRoomTemp_ACT × 10 (empirically confirmed: 233=23.3°C)
      if (v > 50 && v < 500) interp = " [" + String(v/10) + "." + String(v%10) + "°C]";
      addLog("<<< SENSOR f" + String(pendingSensorIdx) + "=" + val + interp);
      pendingSensorIdx++;
    }
    return;
  }
  if (pendingPostSensors && (raw.length() == 0 || raw[0] != '=')) {
    pendingPostSensors = false;
  }

  // ── Log de la ligne (hors champs POST_SENSORS) ────────────────────────────
  addLog("<<< " + raw);

  // ── Reset USB (CDC probe) ─────────────────────────────────────────────────
  if (raw.indexOf('\x16') != -1) {
    mainLoopActive = phase2Done = rtcSynced = false;
    phase1SentAt = phase2GetSentAt = phase2EchoAt = lastPollMs = lastCDCKeepaliveMs = 0;
    phase1EchoCount = 0; parsePh1Echo = false; pendingCredFrame = false;
    delay(50);
    sendStove("GET_CDCDEVICE3_VERSION=0; BL=999; APP=201; REV=12201; DT=3;\n");
    return;
  }

  // ── Version firmware ──────────────────────────────────────────────────────
  if (raw.indexOf("GET_CDCDEVICE_VERSION_FINISHED") != -1) {
    addLog("--- VERSION_FINISHED → Phase 1 (POST_blank + GET_blank) ---");
    String p1  = "POST_CDCDEVICE_STATUS=0;\n";
    p1 += buildFields(false);
    p1 += "-------\n";
    p1 += "GET_CDCDEVICE_STATUS=0;\n";
    p1 += buildFields(false);
    p1 += "0\n0\n0\n";
    sendRaw(p1);
    addLog(">>> Phase 1 POST+GET blank [+" + String(p1.length()) + "B]");
    phase1SentAt = millis();
    return;
  }

  // ── Stove echo of our transmissions (POST_CDCDEVICE_STATUS received) ──────
  if (raw.indexOf("POST_CDCDEVICE_STATUS") != -1) {
    if (phase1SentAt > 0 && !phase2Done) {
      phase1EchoCount++;
      addLog("--- Phase 1 echo #" + String(phase1EchoCount) + " → parse fields");
      parsePh1Echo = true; ph1FieldIdx = 0;
      ph1SsidHex = ""; ph1Wpa2 = ""; ph1Id = ""; ph1Token = "";
      if (phase1EchoCount >= 2) {
        addLog("--- Phase 1 complete → Phase 2 in 200 ms ---");
        phase1SentAt = millis() - 800;
      }
    } else if (phase2Done && !mainLoopActive) {
      addLog("--- Phase 2 echo received → OK + MAIN LOOP in 600 ms ---");
      phase2EchoAt = millis();
    } else if (phase2Done && mainLoopActive) {
      addLog("--- CDC echo → sync read creds/scan ---");
      extractCredsFromStoveEcho();
    }
    return;
  }

  // ── Stove dump terminator ───────────────────────────────────────────────────
  if (raw == "-------") {
    sendRaw("OK\r\n"); addLog(">>> OK (ack ---)");
    return;
  }

  // ── Stove OK ack ────────────────────────────────────────────────────────────
  if (raw == "OK") {
    if (!mainLoopActive) {
      mainLoopActive = true;
      addLog("--- MAIN LOOP ACTIVE ---");
    }
    return;
  }

  // ── scan_command in stove echo → trigger WiFi scan ─────────────────────────
  // Detection via scan_command field (field 3) in Phase 1/2 echo.
  // Handled via GET_NETWORKS_FINISHED flow below.

  // ── GET_CONTROLS (stove → bridge) ──────────────────────────────────────────
  if (raw.indexOf("GET_CONTROLS") != -1) {
    sendGetControls();
    return;
  }

  // ── POST_CONTROLS ─────────────────────────────────────────────────────────
  if (raw.indexOf("POST_CONTROLS") != -1) {
    lastControlsRaw = raw;
    addLog("--- CONTROLS: " + raw.substring(0, min((int)raw.length(), 120)));
    // Parse positionally (shifted-named format when GC sent before GR)
    // Ordre wire: artefact(12201) onOff operatingMode heatingPower tempRoomTarget
    // The 5th value (tempRoomTarget) sometimes arrives on a 2nd line "=190;"
    pendingPostControls = true; postControlsPos = 0;
    stoveOnOff = stoveOpMode = stovePower = stoveTempTarget = -1;
    String tmp = raw;
    // Extract numeric values from the main line (ignore field names)
    int pos = tmp.indexOf("=0;"); if (pos < 0) pos = tmp.indexOf("=0; ");
    if (pos >= 0) tmp = tmp.substring(pos + 3);  // skip past "=0;"
    // Iterate over "name=val;" or "=val;" fields
    while (tmp.length() > 0) {
      tmp.trim();
      int eq = tmp.indexOf('=');
      if (eq < 0) break;
      int sc = tmp.indexOf(';', eq);
      // Last field may have no ';' (stripped upstream)
      int val = (sc >= 0) ? tmp.substring(eq + 1, sc).toInt() : tmp.substring(eq + 1).toInt();
      postControlsPos++;
      // position 1 = artefact GR (12201), 2=onOff, 3=opMode, 4=power, 5=target
      if      (postControlsPos == 2) stoveOnOff     = val;
      else if (postControlsPos == 3) stoveOpMode    = val;
      else if (postControlsPos == 4) stovePower      = val;
      else if (postControlsPos == 5) { stoveTempTarget = val; pendingPostControls = false; }
      if (sc < 0) break;  // last field without ';', end of string
      tmp = tmp.substring(sc + 1);
    }
    if (stoveTempTarget >= 0) {
      addLog("--- Stove controls: onOff=" + String(stoveOnOff) + " opMode=" + String(stoveOpMode) +
             " power=" + String(stovePower) + " target=" + String(stoveTempTarget) + " (=" + String(stoveTempTarget/10) + "." + String(stoveTempTarget%10) + "°C)");
      syncCtrlFromStove();
    }
    return;
  }

  // ── Continuation POST_CONTROLS (=190; = dernier champ tempRoomTarget) ─────
  if (pendingPostControls && raw.length() > 1 && raw[0] == '=') {
    pendingPostControls = false;
    stoveTempTarget = raw.substring(1).toInt();
    addLog("--- Stove controls: onOff=" + String(stoveOnOff) + " opMode=" + String(stoveOpMode) +
           " power=" + String(stovePower) + " target=" + String(stoveTempTarget) + " (=" + String(stoveTempTarget/10) + "." + String(stoveTempTarget%10) + "°C)");
    syncCtrlFromStove();
    return;
  }

  // ── POST_SENSORS ──────────────────────────────────────────────────────────
  if (raw.indexOf("POST_SENSORS") != -1) {
    lastSensorsRaw   = raw;
    sensorCount      = 0;
    pendingSensorIdx = 0;
    parseSensors(raw);
    if (sensorCount == 0) {
      pendingPostSensors = true;
      addLog("--- SENSORS (multi-ligne)");
    } else {
      String f = "";
      for (int i = 0; i < sensorCount; i++) f += " " + sensors[i].key + "=" + sensors[i].val + ";";
      addLog("--- SENSORS:" + f);
    }
    return;
  }

  // ── GET_NETWORKS_FINISHED ─────────────────────────────────────────────────
  if (raw.indexOf("GET_NETWORKS_FINISHED") != -1) {
    if (scanAwaitingGNF) {
      // GNF received in response to our GET_NETWORKS=1 → send sym=7 (display trigger)
      // Confirmed in official firmware: FUN_420097d0(0) called on scan GNF reception
      scanAwaitingGNF = false;
      delay(20);
      sendGetCDCStatus(true, 7);
      drainFor(300);
      return;
    }
    if (!pollInProgress && !scanBusy) {
      lastPollMs = millis(); lastSensorPollMs = lastPollMs;
      pendingControlsWrite = false;
      sendGetControls();
      delay(50);
      sendStove("GET_REVISION=0; revision=12201; frequency=30; \n");
      sendStove("GET_SENSORS=0; \n");
      drainFor(100);
      sendRaw("TRANSFER_COMPLETED\n");
      drainFor(2000);
      sendRaw("TRANSFER_COMPLETED\n");
      drainFor(500);
    }
    return;
  }

  if (raw.indexOf("revision=") != -1 || raw.indexOf("POST_FIRENET") != -1) return;
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  addLog("--- OPEN FIRENET ---");

  // WiFi event handler — log disconnects + exponential backoff to avoid flooding the AP
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      uint8_t reason = info.wifi_sta_disconnected.reason;
      // Raison 2=AUTH_EXPIRE, 4=ASSOC_EXPIRE, 8=ASSOC_FAIL, 15=4WAY_TIMEOUT(mauvais mdp), 200=BEACON_TIMEOUT, 201=NO_AP_FOUND
      char buf[80];
      snprintf(buf, sizeof(buf), "WiFi DISCONNECTED reason=%d (0x%02X) → retry in %lus",
               reason, reason, wifiRetryDelayMs / 1000);
      addLog(String(buf));
      wifiNextRetryMs = millis() + wifiRetryDelayMs;
      if (wifiRetryDelayMs < 600000) wifiRetryDelayMs = min(wifiRetryDelayMs * 2, 600000UL);
    } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
      addLog("WiFi STA_CONNECTED (association OK)");
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      wifiRetryDelayMs = 5000;  // reset backoff on successful connection
      addLog("WiFi GOT_IP: " + WiFi.localIP().toString());
    }
  });

  // Init WiFi STA
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  addLog("WiFi MAC: " + WiFi.macAddress());

  // ── Charger credentials depuis NVS ────────────────────────────────────────
  prefs.begin("rika", false);
  wifiSsid  = prefs.getString("ssid",  "");
  wifiPass  = prefs.getString("pass",  "");
  stoveId   = prefs.getString("id",    "0000000");
  stoveToken= prefs.getString("token", "00000000");

  // ── Validate stored SSID (must be printable ASCII) ─────────────────────────
  {
    bool ssidOk = wifiSsid.length() > 0;
    for (int i = 0; i < (int)wifiSsid.length() && ssidOk; i++) {
      if ((uint8_t)wifiSsid[i] < 0x20 || (uint8_t)wifiSsid[i] > 0x7E) ssidOk = false;
    }
    if (!ssidOk && wifiSsid.length() > 0) {
      addLog("NVS: SSID corrompu, effacement");
      prefs.remove("ssid"); prefs.remove("pass");
      wifiSsid = ""; wifiPass = "";
    }
  }

  // ── Charger consignes depuis NVS ──────────────────────────────────────────
  {
    String savedCtrl = prefs.getString("ctrl", "");
    if (savedCtrl.length() > 10) {
      // Migration : si l'ancienne valeur a tempRoomTarget < 100 c'est en °C directs
      // (pre-2026-05-28 bug) → reset to correct ×10 default
      int trtIdx = savedCtrl.indexOf("tempRoomTarget=");
      if (trtIdx >= 0) {
        int val = savedCtrl.substring(trtIdx + 15).toInt();
        if (val > 0 && val < 100) {
          addLog("Stored ctrl: tempRoomTarget=" + String(val) + " (legacy °C format) → reset to default");
          prefs.remove("ctrl");
        } else {
          desiredControls = savedCtrl;
        }
      } else {
        desiredControls = savedCtrl;
      }
    }
  }

  if (wifiSsid.length() > 0) {
    provisioningMode = false;
    wifiSsidHex = ssidToHex(wifiSsid);
    addLog("NVS: SSID=\"" + wifiSsid + "\" id=" + stoveId);

    WiFi.setTxPower(WIFI_POWER_17dBm);
    esp_wifi_set_max_tx_power(68);
    {
      wifi_config_t conf = {};
      memcpy(conf.sta.ssid,     wifiSsid.c_str(), min(wifiSsid.length(), (size_t)32));
      memcpy(conf.sta.password, wifiPass.c_str(), min(wifiPass.length(), (size_t)64));
      conf.sta.threshold.authmode  = WIFI_AUTH_OPEN;
      conf.sta.pmf_cfg.capable     = true;
      conf.sta.pmf_cfg.required    = false;
      esp_wifi_set_config(WIFI_IF_STA, &conf);
    }
    esp_wifi_set_max_tx_power(68);
    wifiConnectPendingAt = millis() + 500;  // non-blocking delay before connect
    addLog("WiFi WPA2/WPA3 connect (background): \"" + wifiSsid + "\"");
  } else {
    provisioningMode = true;
    WiFi.persistent(false);
    WiFi.disconnect(true, false);  // vider les creds internes ESP32 (stales)
    WiFi.mode(WIFI_STA);
    addLog("Provisioning mode: waiting for credentials from stove screen");
    // Proactive scan to populate the stove network list at boot
    WiFi.scanNetworks(true);
    scanBusy = true;
    addLog("Scan WiFi proactif (provisioning boot)");
  }

  // ── OTA ───────────────────────────────────────────────────────────────────
  ArduinoOTA.begin();

  // ── Web server ────────────────────────────────────────────────────────────
  server.on("/",              handleRoot);
  server.on("/log",           handleLog);
  server.on("/api/status",    handleApiStatus);
  server.on("/api/sensors",   handleApiSensors);
  server.on("/api/controls",  handleApiControls);
  server.on("/reset-wifi",    handleResetWifi);
  server.on("/update", HTTP_GET, handleOtaPage);
  server.on("/update", HTTP_POST,
    []() {
      server.sendHeader("Connection", "close");
      bool ok = !Update.hasError();
      server.send(ok ? 200 : 500, "text/plain",
                  ok ? "OK — rebooting..." : String("Error: ") + Update.errorString());
      delay(500);
      ESP.restart();
    },
    handleOtaUpload
  );
  // server.begin() called from loop() only when WiFi is connected

  // ── USB CDC ───────────────────────────────────────────────────────────────
  USB.VID(RIKA_VID);
  USB.PID(RIKA_PID);
  USB.productName("RIKA FireNet 2.0 USB-WiFi Stick");
  USBSerial.begin();
  USB.begin();

  addLog("Bridge ready. Waiting for stove...");
}

// ─────────────────────────────────────────────────────────────────────────────
// Boucle principale
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  unsigned long now = millis();

  // Reconnexion WiFi avec backoff exponentiel (5s→10s→20s→…→60s)
  // Avoids flooding the AP and triggering its rate-limiter (reason=2 loop)
  static bool wifiWas = false;
  bool wifiNow = (WiFi.status() == WL_CONNECTED);
  if (!wifiNow && !provisioningMode && wifiSsid.length() > 0
      && wifiNextRetryMs > 0 && now >= wifiNextRetryMs) {
    wifiNextRetryMs = 0;
    addLog("WiFi retry (delay=" + String(wifiRetryDelayMs/1000) + "s) → WPA2/WPA3 connect");
    WiFi.setTxPower(WIFI_POWER_17dBm);
    esp_wifi_set_max_tx_power(68);
    addLog("WiFi retry MAC: " + WiFi.macAddress());
    {
      wifi_config_t conf = {};
      memcpy(conf.sta.ssid,     wifiSsid.c_str(), min(wifiSsid.length(), (size_t)32));
      memcpy(conf.sta.password, wifiPass.c_str(), min(wifiPass.length(), (size_t)64));
      conf.sta.threshold.authmode  = WIFI_AUTH_OPEN;
      conf.sta.pmf_cfg.capable     = true;
      conf.sta.pmf_cfg.required    = false;
      esp_wifi_set_config(WIFI_IF_STA, &conf);
    }
    esp_wifi_set_max_tx_power(68);
    wifiConnectPendingAt = millis() + 500;  // non-blocking delay before connect
  }
  // Deferred connect (500ms after stop/start to let driver settle)
  if (wifiConnectPendingAt > 0 && now >= wifiConnectPendingAt) {
    wifiConnectPendingAt = 0;
    esp_wifi_connect();
  }
  if (wifiNow && !wifiWas) {
    esp_wifi_set_ps(WIFI_PS_NONE);
    wifiSsidHex = ssidToHex(wifiSsid);
    if (!ntpDone) startNTP();
    if (MDNS.begin("open-firenet"))
      addLog("mDNS: http://open-firenet.local");
    if (!serverStarted) { server.begin(); serverStarted = true; addLog("HTTP server started"); }
    addLog("WiFi connected: " + WiFi.localIP().toString() + " RSSI=" + String(WiFi.RSSI()) + "dBm");
    // Notify the stove immediately (symbol=4 connected) without waiting for the keepalive 30s
    if (mainLoopActive && rtcSynced) {
      sendGetCDCStatus(true);
      lastCDCKeepaliveMs = now;
    }
  }
  wifiWas = wifiNow;

  // Heartbeat
  if (now - lastHeartbeatMs >= 10000) {
    lastHeartbeatMs = now;
    addLog("--- alive loop=" + String(mainLoopActive) +
           " prov=" + String(provisioningMode) +
           " wifi=" + String(wifiNow) +
           " wst=" + String(WiFi.status()) + " ---");
  }

  // Phase 2 step 1: GET (full if credentials stored, blank in provisioning)
  // In provisioning mode we still send Phase 2 so the stove can
  // trigger a WiFi scan (scan_command=1) and send its credentials.
  if (phase1SentAt > 0 && phase2GetSentAt == 0 && !phase2Done && now - phase1SentAt >= 1000) {
    phase2GetSentAt = now;
    bool full = !provisioningMode;  // full as soon as credentials are stored
    addLog("--- Phase 2 step 1: GET " + String(full?"full":"blank(prov)") + " ---");
    sendGetCDCStatus(full);
  }

  // Phase 2 step 2: POST (full if credentials stored, blank in provisioning) 300 ms after GET
  if (phase2GetSentAt > 0 && !phase2Done && now - phase2GetSentAt >= 300) {
    phase1SentAt = 0; phase2GetSentAt = 0; phase2Done = true;
    bool full = !provisioningMode;
    addLog("--- Phase 2 step 2: POST " + String(full?"full":"blank(prov)") + " ---");
    sendPostCDCStatus(full);
  }

  // OK + MAIN LOOP 600 ms after Phase 2 echo
  if (phase2EchoAt > 0 && !mainLoopActive && now - phase2EchoAt >= 600) {
    phase2EchoAt = 0; mainLoopActive = true;
    lastPollMs = now; lastSensorPollMs = now;
    addLog("--- ACK OK → MAIN LOOP ---");
    sendRaw("OK\r\n");
  }

  // RTC + init
  if (mainLoopActive && !rtcSynced) {
    rtcSynced = true; lastCDCKeepaliveMs = now;
    String ts = buildRTCTimestamp();
    addLog("--- RTC: " + ts.substring(0, ts.indexOf('\r')));
    sendStove(ts);
    delay(200);
    sendRaw("GET_NETWORKS_FINISHED\n");
    addLog(">>> GET_NETWORKS_FINISHED (init)");
  }

  // CDC keepalive every 5 s — fast scan_command detection
  // Provisioning: POST blank → stove echoes stored credentials → extract
  // Connected   : GET+POST full → stove echoes POST_CDCDEVICE_STATUS with scan_command
  if (mainLoopActive && rtcSynced && now - lastCDCKeepaliveMs >= 5000) {
    lastCDCKeepaliveMs = now;
    if (provisioningMode) {
      sendPostCDCStatus(false);
      drainFor(300);
    } else {
      sendGetCDCStatus(true);
      delay(50);
      sendPostCDCStatus(true);
      drainFor(500);
    }
  }

  // Re-arm controls (after setpoint change from web UI)
  if (mainLoopActive && rtcSynced && needsRearm) {
    needsRearm = false; lastPollMs = now;
    pendingControlsWrite = true;
    prefs.putString("ctrl", desiredControls);
    sendRaw("GET_NETWORKS_FINISHED\n");
    addLog("--- Re-arm controls ---");
  }

  // WiFi scan (triggered by scan_command=1 in the stove echo)
  // Works even without a WiFi connection (scanning does not require association).
  if (scanRequested && !scanBusy) {
    if (cachedNetworksMsg.length() > 0) {
      // Cache available → immediate response, no scan wait
      scanRequested = false;
      addLog("--- WiFi scan: cache available → sending immediately ---");
      sendRaw(cachedNetworksMsg);
      addLog(">>> GET_NETWORKS=1; (cached)");
      scanAwaitingGNF = true;  // attendre GNF du stove pour envoyer sym=7
      // Start a background scan to refresh the cache
      WiFi.scanNetworks(true);
      scanBusy = true;
    } else {
      // Pas de cache → scan classique
      addLog("--- WiFi scan ---");
      WiFi.mode(WIFI_STA);
      WiFi.scanNetworks(true);
      scanBusy = true;
    }
  }
  if (scanBusy) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      scanBusy = false;
      lastProactiveScanMs = now;
      if (!mainLoopActive || !rtcSynced) {
        WiFi.scanDelete();
        addLog("Scan: " + String(n) + " networks — main loop not ready, ignored");
      } else {
        addLog("Scan: " + String(n) + " networks");
        int cnt = min(n, 16);
        String msg = "GET_NETWORKS=1;\n";
        for (int i = 0; i < cnt; i++) {
          String ssid = WiFi.SSID(i);
          String hex;
          for (int j = 0; j < (int)ssid.length(); j++) {
            char b[3]; sprintf(b, "%02X", (uint8_t)ssid[j]); hex += b;
          }
          msg += hex + "=" + String(WiFi.RSSI(i)) + "\n";
          addLog("  " + String(i) + ": " + ssid + " (" + String(WiFi.RSSI(i)) + "dBm)");
        }
        WiFi.scanDelete();
        cachedNetworksMsg = msg;  // cache for immediate future responses
        if (scanRequested) {
          // scan triggered by scan_command=1 → send now
          scanRequested = false;
          sendRaw(msg);
          addLog(">>> GET_NETWORKS=1; (" + String(cnt) + " networks) [from active scan]");
          scanAwaitingGNF = true;  // attendre GNF du stove pour envoyer sym=7
        } else {
          addLog("Proactive scan: cache updated (" + String(cnt) + " networks)");
        }
      }
    } else if (n == WIFI_SCAN_FAILED) {
      scanBusy = false;
      addLog("Scan FAIL");
    }
  }

  // Scan proactif toutes les 30 s pour avoir un cache frais
  if (mainLoopActive && rtcSynced && !scanBusy &&
      now - lastProactiveScanMs >= 30000) {
    lastProactiveScanMs = now;
    WiFi.scanNetworks(true);
    scanBusy = true;
    addLog("--- Proactive scan ---");
  }

  // Poll toutes les 15 s (backup si le stove n'envoie pas GET_NETWORKS_FINISHED)
  if (mainLoopActive && lastPollMs > 0 && now - lastPollMs >= 15000) {
    lastPollMs = now; lastSensorPollMs = now;
    pollInProgress = true;
    sendRaw("GET_NETWORKS_FINISHED\n");
    drainFor(300);
    pendingControlsWrite = false;
    sendGetControls();
    delay(50);
    sendStove("GET_REVISION=0; revision=12201; frequency=30; \n");
    sendStove("GET_SENSORS=0; \n");
    drainFor(100);
    sendRaw("TRANSFER_COMPLETED\n");
    drainFor(2000);
    sendRaw("TRANSFER_COMPLETED\n");
    drainFor(500);
    pollInProgress = false;
  }

  // Commandes PC via Serial (ttyACM0) : SETWIFI:ssid:pass
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("SETWIFI:")) {
      int c1 = cmd.indexOf(':', 8);
      if (c1 > 8) {
        String ssid = cmd.substring(8, c1);
        String pass = cmd.substring(c1 + 1);
        String hex;
        for (int i = 0; i < (int)ssid.length(); i++) {
          char b[3]; sprintf(b, "%02X", (uint8_t)ssid[i]); hex += b;
        }
        addLog("SETWIFI cmd: ssid=\"" + ssid + "\" pass=" + String(pass.length()) + "c");
        saveAndConnect(hex, pass, stoveId.length() ? stoveId : "3", stoveToken.length() ? stoveToken : "");
      }
    }
  }

  // Lecture USB CDC
  if (USBSerial.available()) {
    USBSerial.setTimeout(50);
    String raw = USBSerial.readStringUntil('\n');
    while (raw.length() > 0 &&
           (raw[raw.length()-1]=='\r' || raw[raw.length()-1]==';' || raw[raw.length()-1]==' '))
      raw.remove(raw.length()-1);
    processStoveCommand(raw);
    while (USBSerial.available()) {
      String extra = USBSerial.readStringUntil('\n');
      while (extra.length() > 0 &&
             (extra[extra.length()-1]=='\r' || extra[extra.length()-1]==';' || extra[extra.length()-1]==' '))
        extra.remove(extra.length()-1);
      processStoveCommand(extra);
    }
  }
}
