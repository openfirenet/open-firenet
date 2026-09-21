// open-firenet.ino — Open-Firenet pour ESP32-S3 (reverse-engineering).
//
// Rôle : se substituer au dongle officiel. L'ESP32 est DEVICE USB CDC branché sur
// le poêle (hôte USB) et joue à la fois le dongle ET le serveur local Open-Firenet :
// il interroge le poêle en CDC, expose l'état par une interface web + API REST,
// et applique les consignes reçues. Toute la logique protocole prouvée est dans
// firenet_protocol.h / firenet_link.h (testés en g++).
//
// Carte : ESP32-S3. FQBN : esp32:esp32:esp32s3 avec USBMode=default (TinyUSB) et
// CDCOnBoot=cdc. Les logs de debug sortent sur UART0 (Serial0 / port COM/CH343).
//
// USB : VID 0x303A / PID 0x819A.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include "USB.h"
#include "USBCDC.h"
#include "tusb.h"   // écriture CDC directe, sans la condition DTR d'Arduino
#include "esp_wifi.h"  // connexion STA robuste (méthode open-firenet, coexistence USB)
#include "firenet_link.h"
#include "web_ui.h"

// Avec CDCOnBoot=default (désactivé), le core ne démarre pas l'USB de lui-même :
// on instancie le CDC et on fixe VID/PID AVANT USB.begin(). Serial = UART0 (debug).
USBCDC USBSerial;

// --------------------------------------------------------- version & config USB
#ifndef OPENFIRENET_VERSION
#define OPENFIRENET_VERSION "2.2.1"
#endif

// Identifiants USB Open-Firenet
#define OPENFIRENET_USB_VID 0x303A
#define OPENFIRENET_USB_PID 0x819A

// USBSerial = CDC TinyUSB (lien poêle) ; DBG = UART0 (port COM/CH343, logs).
#define DBG Serial
#define STOVE USBSerial

// --------------------------------------------------------------- WiFi / état
Preferences prefs;
WebServer   web(80);
DNSServer   dnsServer;
String      wifiSsid, wifiPass, apPass;
static uint32_t g_wifiConnectAt = 0;   // connect STA différé (méthode open-firenet)
static bool     g_isApMode = false;
static bool     g_staConnected = false;
static uint32_t g_staStart = 0;
bool        writeEnabled = true;    // Open-Firenet : consignes actives directement

// Lectures positionnelles. MÉCANISME PROUVÉ :
// le poêle émet UNE position par NOM enregistré dans GET_SENSORS.
// Sans nom, il n'émet que son jeu par défaut (1 capteur / 5 contrôles).
// -> pour lire les positions hautes il FAUT enregistrer autant de noms. Le poêle
// ignore le texte des noms, seule la position compte.
struct Reading { const char* wire; const char* label; long scale; };
static const Reading SENSORS[] = {          // PRIO1 f0..f12
  {"f0",  "Temperature ambiante", 10},      // sRoomTemp_ACT ×10
  {"f1",  "Temperature chambre combustion", 1}, // lFlameTemp_ACT
  {"f2",  "Code erreur actif", 1},          // ulError_ACT
  {"f3",  "Avertissement actif", 1},        // uiWarning_ACT
  {"f4",  "Code service", 1},               // usService_ACT
  {"f5",  "Moteur decharge (RPM)", 1},      // uiDischargeMotor_ACT
  {"f6",  "Vis pellets (RPM)", 1},          // uiInsertionMotor_ACT
  {"f7",  "Ventilateur combustion (RPM)", 1}, // uiIDFan_ACT
  {"f8",  "Position registres air", 1},     // uiAirFlaps_ACT
  {"f9",  "Heures pellets (min)", 1},       // ulRuntimePellets
  {"f10", "Heures buches (min)", 1},        // ulRuntimeLogs
  {"f11", "Consommation totale (kg)", 1},   // ulFeedRateTotal
  {"f12", "Marche/Arret", 1},               // bOnOff
};
static const Reading CONTROLS[] = {         // positions 0..4
  {"revision",    "Revision", 1},
  {"onOff",       "Marche/Arret", 1},
  {"mode",        "Mode regulation", 1},
  {"targetStage", "Etage cible", 1},
  {"roomTarget",  "Consigne ambiance", 10}, // dixiemes de degre
};
static const int N_SENS = sizeof(SENSORS)/sizeof(SENSORS[0]);
static const int N_CTRL = sizeof(CONTROLS)/sizeof(CONTROLS[0]);
static std::vector<std::string> SENSOR_NAMES;
static std::vector<std::string> CONTROL_NAMES;
static void buildNames() {
  // Registering more names has no benefit past 88: the stove's real internal
  // array caps out at 88 slots (confirmed live 2026-09-18, see PROTOCOL.md).
  for (int i = 0; i < 88; i++) SENSOR_NAMES.push_back(firenet::sensName(i));
  for (int i = 0; i < N_CTRL; i++) CONTROL_NAMES.push_back(CONTROLS[i].wire);
}

// --------------------------------------------------------- liaison protocole
firenet::DongleLink* g_link = nullptr;
static uint32_t lastPoll = 0;

static void txToStove(const uint8_t* d, size_t n) {
  // Le poêle (hôte USB Atmel AVR32) limite les transactions USB pleines à 4 (0x8004e568)
  // et attend des "short packets" (<64 octets, bit SHORTSIGN dans UPSTA0).
  // Le firmware officiel découpait ainsi chaque élément et flashait immédiatement (write+flush).
  // On découpe en paquets de 32 octets maximum (< 64), chacun émis en short packet.
  size_t off = 0;
  while (off < n) {
    size_t chunk = min((size_t)32, n - off);
    uint32_t start = millis();
    while (tud_cdc_n_write_available(0) < chunk && (millis() - start) < 200) {
      delay(1);
    }
    uint32_t w = tud_cdc_n_write(0, d + off, chunk);
    tud_cdc_n_write_flush(0);
    off += w;
    delay(2);
    if (w == 0) break;
  }
  if (off != n) {
    DBG.printf("[txToStove] ERR sent only %u/%u bytes!\n", (unsigned)off, (unsigned)n);
  } else if (n > 64) {
    DBG.printf("[txToStove] OK %u bytes sent (short packets)\n", (unsigned)off);
  }
}
static uint32_t nowMs() { return millis(); }

// ------------------------------------------------------------------- JSON helpers
static bool findJsonBool(const String& str, const String& key, bool& out) {
  int idx = str.indexOf("\"" + key + "\"");
  if (idx < 0) idx = str.indexOf("'" + key + "'");
  if (idx < 0) return false;
  int colon = str.indexOf(':', idx);
  if (colon < 0) return false;
  int start = colon + 1;
  while (start < str.length() && (str[start] == ' ' || str[start] == '\t')) start++;
  if (str.substring(start, start + 4).equalsIgnoreCase("true") || str[start] == '1') {
    out = true; return true;
  }
  if (str.substring(start, start + 5).equalsIgnoreCase("false") || str[start] == '0') {
    out = false; return true;
  }
  return false;
}

static bool findJsonFloat(const String& str, const String& key, float& out) {
  int idx = str.indexOf("\"" + key + "\"");
  if (idx < 0) idx = str.indexOf("'" + key + "'");
  if (idx < 0) idx = str.indexOf(key + "=");
  if (idx < 0) return false;
  int sep = str.indexOf(':', idx);
  if (sep < 0 || (str.indexOf('=', idx) > 0 && str.indexOf('=', idx) < sep)) sep = str.indexOf('=', idx);
  if (sep < 0) return false;
  int start = sep + 1;
  while (start < str.length() && (str[start] == ' ' || str[start] == '"' || str[start] == '\'')) start++;
  int end = start;
  while (end < str.length() && (isDigit(str[end]) || str[end] == '.' || str[end] == '-')) end++;
  if (end > start) {
    out = str.substring(start, end).toFloat();
    return true;
  }
  return false;
}

static bool findJsonString(const String& str, const String& key, String& out) {
  int idx = str.indexOf("\"" + key + "\"");
  if (idx < 0) idx = str.indexOf("'" + key + "'");
  if (idx < 0) return false;
  int colon = str.indexOf(':', idx);
  if (colon < 0) return false;
  int start = str.indexOf('"', colon);
  if (start < 0) return false;
  int end = str.indexOf('"', start + 1);
  if (end < 0) return false;
  out = str.substring(start + 1, end);
  return true;
}

static const char* getStoveModelName(long modelId) {
  switch (modelId) {
    case 1:  return "INDUO";
    case 2:  return "TOPO";
    case 3:  return "ROCO";
    case 4:  return "ROCO MULTIAIR";
    case 5:  return "ROCO RAO";
    case 6:  return "KAPO";
    case 7:  return "MIRO";
    case 8:  return "COMO";
    case 9:  return "REVO";
    case 10: return "INTERNO";
    case 11: return "FILO";
    case 12: return "SUMO";
    case 13: return "DOMO";
    case 14: return "CORSO";
    case 15: return "INDUO II";
    case 16: return "REVIVO";
    case 17: return "PARO";
    case 18: return "LIVO";
    case 19: return "COMO II";
    case 20: return "REVO II";
    case 21: return "COSMO";
    case 22: return "SONO";
    case 23: return "DOMO BACK";
    case 24: return "PK E";
    case 25: return "SUMO MULTIAIR";
    case 26: return "CONNECT";
    default: return "RIKA";
  }
}

// ------------------------------------------------------------------- API web V2
static String jsonState() {
  const auto& m = g_link->model();

  long rTemp = (m.sensors_pos.size() > 0) ? m.sensors_pos[0] : 0;
  auto itR = m.sensors.find("roomTemp"); if (itR != m.sensors.end()) rTemp = itR->second;

  long fTemp = (m.sensors_pos.size() > 1) ? m.sensors_pos[1] : 0;
  auto itF = m.sensors.find("flame"); if (itF != m.sensors.end()) fTemp = itF->second;

  long bTemp = (m.sensors_pos.size() > 27) ? m.sensors_pos[27] : 0;
  auto itB = m.sensors.find("boardSensor"); if (itB != m.sensors.end()) bTemp = itB->second;

  long mainSt = (m.sensors_pos.size() > 31) ? m.sensors_pos[31] : 1;
  auto itMS = m.sensors.find("mainState"); if (itMS != m.sensors.end()) mainSt = itMS->second;

  long sState = (m.sensors_pos.size() > 32) ? m.sensors_pos[32] : 0;
  auto itSS = m.sensors.find("subState"); if (itSS != m.sensors.end()) sState = itSS->second;

  long pTotal = (m.sensors_pos.size() > 49) ? m.sensors_pos[49] : 0;
  auto itPT = m.sensors.find("pelletsTotal"); if (itPT != m.sensors.end()) pTotal = itPT->second;

  long pHours = (m.sensors_pos.size() > 47) ? m.sensors_pos[47] : 0;
  auto itPH = m.sensors.find("pelletHours"); if (itPH != m.sensors.end()) pHours = itPH->second;

  long sCount = (m.sensors_pos.size() > 50) ? m.sensors_pos[50] : 700;
  auto itSC = m.sensors.find("serviceCountdown"); if (itSC != m.sensors.end()) sCount = itSC->second;

  long idFan = (m.sensors_pos.size() > 9) ? m.sensors_pos[9] : 0;
  auto itFan = m.sensors.find("idFanMeas"); if (itFan != m.sensors.end()) idFan = itFan->second;

  long auger = (m.sensors_pos.size() > 7) ? m.sensors_pos[7] : 0;
  auto itAug = m.sensors.find("augerSet"); if (itAug != m.sensors.end()) auger = itAug->second;

  long errMask = (m.sensors_pos.size() > 3) ? m.sensors_pos[3] : 0;
  auto itEM = m.sensors.find("errMask32"); if (itEM != m.sensors.end()) errMask = itEM->second;

  long errSub = (m.sensors_pos.size() > 4) ? m.sensors_pos[4] : 0;
  auto itES = m.sensors.find("errSub"); if (itES != m.sensors.end()) errSub = itES->second;

  long modelId = (m.sensors_pos.size() > 36) ? m.sensors_pos[36] : 13;
  auto itMod = m.sensors.find("model"); if (itMod != m.sensors.end()) modelId = itMod->second;
  const char* modelName = getStoveModelName(modelId);

  long appVer = (m.sensors_pos.size() > 38) ? m.sensors_pos[38] : 229;
  auto itAV = m.sensors.find("appVerBoard"); if (itAV != m.sensors.end()) appVer = itAV->second;

  long buildVer = (m.sensors_pos.size() > 44) ? m.sensors_pos[44] : 58512;
  auto itBV = m.sensors.find("firmwareBuild"); if (itBV != m.sensors.end()) buildVer = itBV->second;

  long curOn = 0, curMode = 2, curStage = 70, curRoom = 200;
  auto itOn = m.controls.find("onOff"); if (itOn != m.controls.end()) curOn = itOn->second;
  else if (m.controls_pos.size() >= 5) curOn = m.controls_pos[1];

  auto itMode = m.controls.find("mode"); if (itMode != m.controls.end()) curMode = itMode->second;
  else if (m.controls_pos.size() >= 5) curMode = m.controls_pos[2];

  auto itStage = m.controls.find("targetStage"); if (itStage != m.controls.end()) curStage = itStage->second;
  else if (m.controls_pos.size() >= 5) curStage = m.controls_pos[3];

  auto itRoom = m.controls.find("roomTarget"); if (itRoom != m.controls.end()) curRoom = itRoom->second;
  else if (m.controls_pos.size() >= 5) curRoom = m.controls_pos[4];

  const char* stName = "unknown";
  const char* stLabel = "Unknown";
  bool isBurning = false;
  switch (mainSt) {
    case 0: stName = "off"; stLabel = "Off"; isBurning = false; break;
    case 1: stName = "standby"; stLabel = "Standby"; isBurning = false; break;
    case 2: stName = "ignition"; stLabel = "Ignition"; isBurning = true; break;
    case 3: stName = "flame_start"; stLabel = "Flame Start"; isBurning = true; break;
    case 4: stName = "heating"; stLabel = "Heating"; isBurning = true; break;
    case 5: stName = "cleaning"; stLabel = "Grate Cleaning"; isBurning = true; break;
    case 6: stName = "burn_off"; stLabel = "Burn Off"; isBurning = true; break;
    case 7: stName = "splitlog"; stLabel = "Split Log"; isBurning = true; break;
  }

  const char* modeName = "comfort";
  switch (curMode) {
    case 0: modeName = "manual"; break;
    case 1: modeName = "auto"; break;
    case 2: modeName = "comfort"; break;
  }

  float rTempF = rTemp / 10.0f;
  float rTargetF = curRoom / 10.0f;
  float fTempF = (float)fTemp;
  float bTempF = (float)bTemp;

  char buf[1400];
  snprintf(buf, sizeof(buf),
    "{"
    "\"device\":{"
      "\"name\":\"Open-Firenet\","
      "\"version\":\"" OPENFIRENET_VERSION "\","
      "\"app_version\":\"" OPENFIRENET_VERSION "\","
      "\"firmware_version\":\"" OPENFIRENET_VERSION "\","
      "\"ip\":\"%s\","
      "\"mac\":\"%s\","
      "\"wifi_ssid\":\"%s\","
      "\"wifi_rssi\":%d,"
      "\"uptime_seconds\":%lu,"
      "\"free_heap\":%u,"
      "\"connected\":%s"
    "},"
    "\"stove\":{"
      "\"state\":\"%s\","
      "\"state_code\":%ld,"
      "\"state_label\":\"%s\","
      "\"sub_state\":%ld,"
      "\"is_burning\":%s,"
      "\"has_error\":%s,"
      "\"error_code\":%ld,"
      "\"error_sub\":%ld,"
      "\"model\":%ld,"
      "\"model_name\":\"%s\","
      "\"mainboard_version\":\"%ld.%02ld\","
      "\"firmware_build\":\"%ld\""
    "},"
    "\"sensors\":{"
      "\"room_temperature\":%.1f,"
      "\"combustion_temperature\":%.1f,"
      "\"board_temperature\":%.1f,"
      "\"pellets_total_kg\":%ld,"
      "\"pellet_hours\":%ld,"
      "\"service_countdown_kg\":%ld,"
      "\"fan_speed_rpm\":%ld,"
      "\"auger_speed_rpm\":%ld"
    "},"
    "\"controls\":{"
      "\"on\":%s,"
      "\"mode\":\"%s\","
      "\"mode_code\":%ld,"
      "\"target_temperature\":%.1f,"
      "\"power_percent\":%ld"
    "},",
    (WiFi.getMode()==WIFI_AP?WiFi.softAPIP():WiFi.localIP()).toString().c_str(),
    WiFi.macAddress().c_str(),
    WiFi.SSID().c_str(),
    WiFi.RSSI(),
    millis() / 1000UL,
    ESP.getFreeHeap(),
    m.version_ack ? "true" : "false",
    stName, mainSt, stLabel, sState,
    isBurning ? "true" : "false",
    errMask != 0 ? "true" : "false",
    errMask, errSub,
    modelId, modelName, appVer / 100, appVer % 100, buildVer,
    rTempF, fTempF, bTempF, pTotal, pHours, sCount, idFan, auger,
    (curOn == 1) ? "true" : "false",
    modeName, curMode, rTargetF, curStage
  );

  String j = String(buf);
  j += "\"wifi_mode\":\"" + String(WiFi.getMode()==WIFI_AP?"AP":"STA") + "\",";
  j += "\"ip\":\"" + (WiFi.getMode()==WIFI_AP?WiFi.softAPIP():WiFi.localIP()).toString() + "\",";
  j += "\"wifi_connected\":" + String(WiFi.status()==WL_CONNECTED?"true":"false") + ",";
  j += "\"uptime_seconds\":" + String(millis() / 1000UL) + ",";
  j += "\"write_enabled\":true,";
  j += "\"version_ack\":" + String(m.version_ack ? "true" : "false") + ",";
  j += "\"generation\":" + String(m.generation) + ",";
  j += "\"frames_in\":" + String(m.frames_in) + ",";
  j += "\"frames_out\":" + String(m.frames_out) + ",";
  j += "\"revision\":" + String((long)m.revision) + ",";
  j += "\"state_label\":\"" + String(stLabel) + "\",";

  // raw_sensors pour le tableau complet
  j += "\"raw_sensors\":{";
  bool first = true;
  for (auto& kv : m.sensors) {
    if (!first) j += ","; first = false;
    j += "\"" + String(kv.first.c_str()) + "\":" + String(kv.second);
  }
  j += "},";

  // legacy status
  j += "\"status\":{";
  first = true;
  for (auto& kv : m.status) {
    if (!first) j += ","; first = false;
    j += "\"" + String(kv.first.c_str()) + "\":\"" + String(kv.second.c_str()) + "\"";
  }
  j += "},";

  // legacy controls_pos / sensors_pos
  j += "\"sensors_pos\":[";
  first = true;
  for (long v : m.sensors_pos) { if (!first) j += ","; first = false; j += String(v); }
  j += "],\"controls_pos\":[";
  first = true;
  for (long v : m.controls_pos) { if (!first) j += ","; first = false; j += String(v); }
  j += "]}";

  return j;
}

static void handleState()   { sendCors(); web.send(200, "application/json", jsonState()); }
static void handleVersion() {
  sendCors();
  char buf[220];
  snprintf(buf, sizeof(buf),
    "{\"app\":\"Open-Firenet\",\"version\":\"" OPENFIRENET_VERSION "\",\"build_date\":\"%s\",\"build_time\":\"%s\",\"target\":\"ESP32-S3\"}",
    __DATE__, __TIME__
  );
  web.send(200, "application/json", buf);
}
static void handleRoot()    { web.send_P(200, "text/html", INDEX_HTML); }
static void handleArm()    { sendCors(); web.send(200, "application/json", "{\"write\":true}"); }

static void handleRestart() {
  sendCors();
  web.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
  delay(300); ESP.restart();
}


// POST /api/wifi  ssid=<..>&pass=<..>  -> enregistre et redémarre en STA.
// Répond en HTML (et non JSON) : la page est soumise par un <form> natif afin de
// fonctionner dans les navigateurs de portail captif (macOS/iOS) qui bloquent fetch()
// et suppriment confirm()/alert() (issue #8). La navigation affiche cette page de
// confirmation, puis la carte redémarre.
static void handleWifi() {
  if (!web.hasArg("ssid") || web.arg("ssid").length() == 0) {
    web.send(400, "text/html; charset=utf-8",
      "<!doctype html><meta charset=\"utf-8\">"
      "<body style=\"font-family:sans-serif;padding:24px\">"
      "<h2>SSID manquant / Missing SSID</h2><p><a href=\"/\">&larr; Retour / Back</a></p>");
    return;
  }
  prefs.begin("firenet", false);
  prefs.putString("ssid", web.arg("ssid"));
  prefs.putString("pass", web.hasArg("pass") ? web.arg("pass") : "");
  prefs.end();
  web.send(200, "text/html; charset=utf-8",
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Open-Firenet</title></head>"
    "<body style=\"margin:0;background:#0c0f17;color:#f1f5f9;font-family:sans-serif;"
    "display:flex;align-items:center;justify-content:center;min-height:100vh;"
    "padding:24px;box-sizing:border-box\">"
    "<div style=\"background:#161b26;border:1px solid #232a3b;border-radius:16px;"
    "padding:32px;max-width:440px;width:100%;text-align:center\">"
    "<div style=\"font-size:48px;margin-bottom:16px\">&#128260;</div>"
    "<h2 style=\"margin:0 0 12px\">Red&eacute;marrage&hellip; / Rebooting&hellip;</h2>"
    "<p style=\"color:#94a3b8;line-height:1.6;margin:0 0 24px\">"
    "R&eacute;seau enregistr&eacute;. Reconnectez votre appareil &agrave; votre WiFi "
    "habituel, puis ouvrez :<br>Settings saved. Reconnect your device to your home WiFi, "
    "then open:</p>"
    "<a href=\"http://open-firenet.local\" style=\"display:inline-block;width:100%;"
    "box-sizing:border-box;background:#38bdf8;color:#0c0f17;font-weight:700;padding:14px;"
    "border-radius:10px;text-decoration:none\">http://open-firenet.local</a></div></body></html>");
  delay(300); ESP.restart();
}
// POST /api/forget -> efface le WiFi, repasse en AP au prochain boot
static void handleForget() {
  prefs.begin("firenet", false); prefs.clear(); prefs.end();
  web.send(200,"application/json","{\"ok\":true}");
  delay(300); ESP.restart();
}

// Option C — provisioning par commande série (UART0 DBG et CDC TinyUSB STOVE) :
//   SETWIFI:<ssid>:<password>
// Le SSID s'arrête au premier ':' ; tout le reste est le mot de passe (donc un
// mot de passe contenant ':' est accepté). Enregistre en NVS puis redémarre en STA.
static bool applySetWifi(const String& line, Print& out) {
  if (line.startsWith("SETWIFI:")) {
    String rest = line.substring(8);       // après "SETWIFI:"
    int sep = rest.indexOf(':');
    if (sep > 0) {
      String ssid = rest.substring(0, sep);
      String pass = rest.substring(sep + 1);
      prefs.begin("firenet", false);
      prefs.putString("ssid", ssid);
      prefs.putString("pass", pass);
      prefs.end();
      DBG.printf("[wifi] SETWIFI OK ssid=\"%s\" -> reboot STA\n", ssid.c_str());
      if ((Print*)&out != (Print*)&DBG) {
        out.printf("[wifi] SETWIFI OK ssid=\"%s\" -> reboot STA\r\n", ssid.c_str());
        out.flush();
      }
      delay(200); ESP.restart();
      return true;
    } else {
      DBG.println("[wifi] SETWIFI: format attendu -> SETWIFI:<ssid>:<password>");
      if ((Print*)&out != (Print*)&DBG) {
        out.println("[wifi] SETWIFI: format attendu -> SETWIFI:<ssid>:<password>");
        out.flush();
      }
    }
  }
  return false;
}

static void handleSerialProvisioning() {
  static String dbgLine;
  while (DBG.available()) {
    char c = (char)DBG.read();
    if (c == '\n' || c == '\r') {
      if (dbgLine.length() > 0) {
        applySetWifi(dbgLine, DBG);
        dbgLine = "";
      }
    } else if (dbgLine.length() < 160) {
      dbgLine += c;
    }
  }

  static String stoveLine;
  while (STOVE.available()) {
    uint8_t b = (uint8_t)STOVE.read();
    if (g_link) g_link->onByte(b);
    char c = (char)b;
    if (c == '\n' || c == '\r') {
      if (stoveLine.length() > 0) {
        applySetWifi(stoveLine, STOVE);
        stoveLine = "";
      }
    } else {
      if (stoveLine.length() == 0) {
        if (c == 'S') stoveLine += c;
      } else if (stoveLine.length() < 160) {
        stoveLine += c;
        if (stoveLine.length() == 8 && stoveLine != "SETWIFI:") {
          stoveLine = "";
        }
      }
    }
  }
}

static void startApMode() {
  g_isApMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Open-Firenet-Setup");   // réseau ouvert (sans mot de passe)
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  DBG.printf("[wifi] AP Open-Firenet-Setup (DNS captif actif) IP: %s\n",
             WiFi.softAPIP().toString().c_str());
}

static void handleCaptiveRedirect() {
  if (g_isApMode) {
    web.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    web.send(302, "text/plain", "");
  } else {
    web.sendHeader("Location", String("http://") + WiFi.localIP().toString() + "/", true);
    web.send(302, "text/plain", "");
  }
}

// GET /api/scan -> scanne les réseaux 2.4 GHz et renvoie un tableau JSON
static void handleScan() {
  sendCors();
  int n = WiFi.scanComplete();
  if (n == -2) {
    WiFi.scanNetworks(true);
    web.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }
  if (n == -1) {
    web.send(202, "application/json", "{\"status\":\"scanning\"}");
    return;
  }

  String json = "[";
  std::vector<String> seen;
  int count = 0;
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    bool dup = false;
    for (const auto& s : seen) { if (s == ssid) { dup = true; break; } }
    if (dup) continue;
    seen.push_back(ssid);

    if (count > 0) json += ",";
    json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    count++;
  }
  json += "]";
  WiFi.scanDelete();
  web.send(200, "application/json", json);
}

// ------------------------------------------------ API compatibilité open-firenet & Home Assistant
static const size_t LOG_MAX_BYTES = 24576;  // 24 KB (well within stable free heap margin)
static const size_t LOG_TRIM_BYTES = 6144;  // 6 KB trimmed on overflow
static String g_recentLogs = "";

static void logEntry(const char* dir, const std::string& msg) {
  // Build the line with direct concatenation (no fixed-size buffer) so long frames
  // (e.g. GET_SENSORS/POST_SENSORS with many fields) are never silently truncated.
  String line = "[" + String((unsigned long)millis()) + "][" + dir + "] " + msg.c_str() + "\n";
  if (g_recentLogs.length() > LOG_MAX_BYTES) {
    g_recentLogs = g_recentLogs.substring(LOG_TRIM_BYTES);
  }
  g_recentLogs += line;
}

static void sendCors() {
  web.sendHeader("Access-Control-Allow-Origin", "*");
  web.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  web.sendHeader("Access-Control-Allow-Headers", "*");
}

// GET /api/status (compatibilité open-firenet)
static void handleApiStatus() {
  sendCors();
  const auto& m = g_link->model();
  long curOn = 0, curMode = 2, curStage = 70, curRoom = 200;
  auto itOn = m.controls.find("onOff"); if (itOn != m.controls.end()) curOn = itOn->second;
  auto itMode = m.controls.find("mode"); if (itMode != m.controls.end()) curMode = itMode->second;
  auto itStage = m.controls.find("targetStage"); if (itStage != m.controls.end()) curStage = itStage->second;
  auto itRoom = m.controls.find("roomTarget"); if (itRoom != m.controls.end()) curRoom = itRoom->second;

  char ctrlStr[160];
  snprintf(ctrlStr, sizeof ctrlStr, "onOff=%ld; operatingMode=%ld; heatingPower=%ld; tempRoomTarget=%ld;",
           curOn, curMode, curStage, curRoom);

  String json = "{";
  json += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ip\":\"" + (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
  json += "\"ssid\":\"" + wifiSsid + "\",";
  json += "\"provisioning\":" + String(WiFi.getMode() == WIFI_AP ? "true" : "false") + ",";
  json += "\"mainLoop\":" + String(m.version_ack ? "true" : "false") + ",";
  json += "\"pauseCdc\":false,";
  json += "\"controls\":\"" + String(ctrlStr) + "\",";
  json += "\"revisionFrequency\":60";
  json += "}";
  web.send(200, "application/json", json);
}

// GET /api/sensors (compatibilité open-firenet & Home Assistant)
static void handleApiSensors() {
  sendCors();
  const auto& m = g_link->model();
  String json = "{";
  bool first = true;
  auto addKV = [&](const String& k, const String& v) {
    if (!first) json += ",";
    json += "\"" + k + "\":\"" + v + "\"";
    first = false;
  };

  long rTemp = (m.sensors_pos.size() > 0) ? m.sensors_pos[0] : 0;
  auto itR = m.sensors.find("roomTemp"); if (itR != m.sensors.end()) rTemp = itR->second;

  long fTemp = (m.sensors_pos.size() > 1) ? m.sensors_pos[1] : 0;
  auto itF = m.sensors.find("flame"); if (itF != m.sensors.end()) fTemp = itF->second;

  long mState = (m.sensors_pos.size() > 31) ? m.sensors_pos[31] : 1;
  auto itMS = m.sensors.find("mainState"); if (itMS != m.sensors.end()) mState = itMS->second;

  long sState = (m.sensors_pos.size() > 32) ? m.sensors_pos[32] : 0;
  auto itSS = m.sensors.find("subState"); if (itSS != m.sensors.end()) sState = itSS->second;

  long pTotal = (m.sensors_pos.size() > 49) ? m.sensors_pos[49] : 0;
  auto itPT = m.sensors.find("pelletsTotal"); if (itPT != m.sensors.end()) pTotal = itPT->second;

  long pHours = (m.sensors_pos.size() > 47) ? m.sensors_pos[47] : 0;
  auto itPH = m.sensors.find("pelletHours"); if (itPH != m.sensors.end()) pHours = itPH->second;

  long sCount = (m.sensors_pos.size() > 50) ? m.sensors_pos[50] : 700;
  auto itSC = m.sensors.find("serviceCountdown"); if (itSC != m.sensors.end()) sCount = itSC->second;

  long idFan = (m.sensors_pos.size() > 9) ? m.sensors_pos[9] : 0;
  auto itFan = m.sensors.find("idFanMeas"); if (itFan != m.sensors.end()) idFan = itFan->second;

  // Clé 'f0' essentielle pour les configurations Home Assistant (value_json.f0)
  addKV("f0", String(rTemp));
  addKV("roomTemp", String(rTemp));
  addKV("flameTemp", String(fTemp));
  addKV("combustionChamberTemp", String(fTemp));
  addKV("mainState", String(mState));
  addKV("subState", String(sState));
  addKV("feedRateTotal", String(pTotal));
  addKV("pelletsTotal", String(pTotal));
  addKV("runtimePellets", String(pHours));
  addKV("pelletHours", String(pHours));
  addKV("serviceCountdown", String(sCount));
  addKV("serviceCountdownKg", String(sCount));
  addKV("idFan", String(idFan));

  long modelId = (m.sensors_pos.size() > 36) ? m.sensors_pos[36] : 13;
  auto itMod = m.sensors.find("model"); if (itMod != m.sensors.end()) modelId = itMod->second;
  addKV("model", String(modelId));
  addKV("modelName", getStoveModelName(modelId));

  // Contrôles en lecture
  long curOn = 0, curMode = 2, curStage = 70, curRoom = 200;
  auto itOn = m.controls.find("onOff"); if (itOn != m.controls.end()) curOn = itOn->second;
  auto itMode = m.controls.find("mode"); if (itMode != m.controls.end()) curMode = itMode->second;
  auto itStage = m.controls.find("targetStage"); if (itStage != m.controls.end()) curStage = itStage->second;
  auto itRoom = m.controls.find("roomTarget"); if (itRoom != m.controls.end()) curRoom = itRoom->second;

  addKV("stoveOnOff", String(curOn));
  addKV("stoveOpMode", String(curMode));
  addKV("stovePower", String(curStage));
  addKV("stoveTempTarget", String(curRoom));

  // Diagnostic ESP32 (compatibilité PR #1)
  addKV("uptime", String(millis() / 1000));
  addKV("firmware", "2.0.0");
  addKV("internalTemp", String(temperatureRead(), 1));
  if (WiFi.status() == WL_CONNECTED) {
    addKV("mac", WiFi.macAddress());
    addKV("rssi", String(WiFi.RSSI()));
    addKV("ssid", wifiSsid);
    addKV("ip", WiFi.localIP().toString());
  }
  json += "}";
  web.send(200, "application/json", json);
}

// GET /api/controls & POST /api/controls (V2 JSON + legacy compat)
static void handleApiControls() {
  sendCors();
  if (web.method() == HTTP_OPTIONS) { web.send(204); return; }

  if (web.method() == HTTP_GET) {
    const auto& m = g_link->model();
    long curOn = 0, curMode = 2, curStage = 70, curRoom = 200;
    auto itOn = m.controls.find("onOff"); if (itOn != m.controls.end()) curOn = itOn->second;
    else if (m.controls_pos.size() > 1) curOn = m.controls_pos[1];
    auto itMode = m.controls.find("mode"); if (itMode != m.controls.end()) curMode = itMode->second;
    else if (m.controls_pos.size() > 2) curMode = m.controls_pos[2];
    auto itStage = m.controls.find("targetStage"); if (itStage != m.controls.end()) curStage = itStage->second;
    else if (m.controls_pos.size() > 3) curStage = m.controls_pos[3];
    auto itRoom = m.controls.find("roomTarget"); if (itRoom != m.controls.end()) curRoom = itRoom->second;
    else if (m.controls_pos.size() > 4) curRoom = m.controls_pos[4];

    const char* modeName = (curMode == 0) ? "manual" : ((curMode == 1) ? "auto" : "comfort");
    float rTargetF = curRoom / 10.0f;

    char buf[300];
    snprintf(buf, sizeof(buf),
      "{"
      "\"on\":%s,"
      "\"mode\":\"%s\","
      "\"mode_code\":%ld,"
      "\"target_temperature\":%.1f,"
      "\"power_percent\":%ld,"
      "\"onOff\":%ld,"
      "\"operatingMode\":%ld,"
      "\"heatingPower\":%ld,"
      "\"tempRoomTarget\":%ld"
      "}",
      (curOn == 1) ? "true" : "false",
      modeName, curMode, rTargetF, curStage,
      curOn, curMode, curStage, curRoom
    );
    web.send(200, "application/json", buf);
    return;
  }

  // POST / PUT : modification de consigne
  String raw = web.hasArg("plain") ? web.arg("plain") : (web.hasArg("cmd") ? web.arg("cmd") : "");
  long newOn = -1, newMode = -1, newStage = -1, newRoom = -1;

  // 1) Analyse JSON
  bool bVal = false;
  if (findJsonBool(raw, "on", bVal) || findJsonBool(raw, "onOff", bVal)) {
    newOn = bVal ? 1 : 0;
  }
  String sMode;
  if (findJsonString(raw, "mode", sMode) || findJsonString(raw, "operatingMode", sMode)) {
    sMode.toLowerCase();
    if (sMode == "manual") newMode = 0;
    else if (sMode == "auto") newMode = 1;
    else if (sMode == "comfort") newMode = 2;
  }
  float fVal;
  if (findJsonFloat(raw, "target_temperature", fVal) ||
      findJsonFloat(raw, "temperature", fVal) ||
      findJsonFloat(raw, "tempRoomTarget", fVal) ||
      findJsonFloat(raw, "roomTarget", fVal)) {
    newRoom = (fVal < 50.0f) ? (long)round(fVal * 10.0f) : (long)fVal;
  }
  if (findJsonFloat(raw, "power_percent", fVal) ||
      findJsonFloat(raw, "power", fVal) ||
      findJsonFloat(raw, "heatingPower", fVal) ||
      findJsonFloat(raw, "targetStage", fVal)) {
    newStage = (long)fVal;
  }
  if (newOn < 0 && (findJsonFloat(raw, "onOff", fVal) || findJsonFloat(raw, "on", fVal))) newOn = (long)fVal;
  if (newMode < 0 && (findJsonFloat(raw, "mode", fVal) || findJsonFloat(raw, "operatingMode", fVal))) newMode = (long)fVal;

  // 2) Form arguments / Query arguments
  if (web.hasArg("on")) {
    String s = web.arg("on");
    newOn = (s == "true" || s == "1") ? 1 : 0;
  }
  if (web.hasArg("onOff")) newOn = web.arg("onOff").toInt();
  if (web.hasArg("mode")) {
    String s = web.arg("mode");
    if (s == "manual") newMode = 0;
    else if (s == "auto") newMode = 1;
    else if (s == "comfort") newMode = 2;
    else newMode = s.toInt();
  }
  if (web.hasArg("operatingMode")) newMode = web.arg("operatingMode").toInt();
  if (web.hasArg("target_temperature")) {
    float f = web.arg("target_temperature").toFloat();
    newRoom = (f < 50.0f) ? (long)round(f * 10.0f) : (long)f;
  }
  if (web.hasArg("temperature")) {
    float f = web.arg("temperature").toFloat();
    newRoom = (f < 50.0f) ? (long)round(f * 10.0f) : (long)f;
  }
  if (web.hasArg("tempRoomTarget")) {
    float f = web.arg("tempRoomTarget").toFloat();
    newRoom = (f < 50.0f) ? (long)round(f * 10.0f) : (long)f;
  }
  if (web.hasArg("roomTarget")) {
    float f = web.arg("roomTarget").toFloat();
    newRoom = (f < 50.0f) ? (long)round(f * 10.0f) : (long)f;
  }
  if (web.hasArg("power_percent")) newStage = web.arg("power_percent").toInt();
  if (web.hasArg("power")) newStage = web.arg("power").toInt();
  if (web.hasArg("heatingPower")) newStage = web.arg("heatingPower").toInt();
  if (web.hasArg("targetStage")) newStage = web.arg("targetStage").toInt();

  // 3) Form single name/value (utilisé par steppers & sliders web UI)
  if (web.hasArg("name") && web.hasArg("value")) {
    String n = web.arg("name");
    String v = web.arg("value");
    if (n == "on" || n == "onOff") newOn = (v == "true" || v == "1") ? 1 : 0;
    else if (n == "mode" || n == "operatingMode") {
      if (v == "manual") newMode = 0;
      else if (v == "auto") newMode = 1;
      else if (v == "comfort") newMode = 2;
      else newMode = v.toInt();
    } else if (n == "roomTarget" || n == "target_temperature" || n == "temperature" || n == "tempRoomTarget" || n == "room") {
      float f = v.toFloat();
      newRoom = (f < 50.0f) ? (long)round(f * 10.0f) : (long)f;
    } else if (n == "targetStage" || n == "power_percent" || n == "power" || n == "heatingPower" || n == "stage") {
      newStage = v.toInt();
    }
  }

  // 4) Format legacy point-virgule (si texte brut)
  if (newOn < 0 && raw.indexOf("onOff=") >= 0) {
    auto extract = [&](const String& key) -> long {
      int idx = raw.indexOf(key + "=");
      if (idx < 0) return -1;
      int eq = raw.indexOf('=', idx);
      if (eq < 0) return -1;
      int sc = raw.indexOf(';', eq);
      if (sc < 0) sc = raw.indexOf('&', eq);
      if (sc < 0) sc = raw.length();
      return raw.substring(eq + 1, sc).toInt();
    };
    newOn = extract("onOff");
    if (newMode < 0) newMode = extract("operatingMode");
    if (newStage < 0) newStage = extract("heatingPower");
    if (newRoom < 0) newRoom = extract("tempRoomTarget");
  }

  // Récupérer les valeurs actuelles pour read-modify-write
  const auto& m = g_link->model();
  long curOn = 0, curMode = 2, curStage = 70, curRoom = 200;
  auto itOn = m.controls.find("onOff"); if (itOn != m.controls.end()) curOn = itOn->second;
  else if (m.controls_pos.size() >= 5) curOn = m.controls_pos[1];
  auto itMode = m.controls.find("mode"); if (itMode != m.controls.end()) curMode = itMode->second;
  else if (m.controls_pos.size() >= 5) curMode = m.controls_pos[2];
  auto itStage = m.controls.find("targetStage"); if (itStage != m.controls.end()) curStage = itStage->second;
  else if (m.controls_pos.size() >= 5) curStage = m.controls_pos[3];
  auto itRoom = m.controls.find("roomTarget"); if (itRoom != m.controls.end()) curRoom = itRoom->second;
  else if (m.controls_pos.size() >= 5) curRoom = m.controls_pos[4];

  long finalOn = (newOn >= 0) ? newOn : curOn;
  long finalMode = (newMode >= 0) ? newMode : curMode;
  long finalStage = (newStage >= 0) ? newStage : curStage;
  long finalRoom = (newRoom >= 0) ? newRoom : curRoom;

  std::vector<std::pair<std::string,long>> full;
  full.push_back({"revision", (long)m.revision});
  full.push_back({"onOff", finalOn});
  full.push_back({"mode", finalMode});
  full.push_back({"targetStage", finalStage});
  full.push_back({"roomTarget", finalRoom});

  g_link->applyControls(full);
  lastPoll = millis();

  const char* modeName = (finalMode == 0) ? "manual" : ((finalMode == 1) ? "auto" : "comfort");
  float rTargetF = finalRoom / 10.0f;
  char resBuf[300];
  snprintf(resBuf, sizeof(resBuf),
    "{"
    "\"ok\":true,"
    "\"on\":%s,"
    "\"mode\":\"%s\","
    "\"mode_code\":%ld,"
    "\"target_temperature\":%.1f,"
    "\"power_percent\":%ld"
    "}",
    (finalOn == 1) ? "true" : "false",
    modeName, finalMode, rTargetF, finalStage
  );
  web.send(200, "application/json", resBuf);
}

// GET /log (compatibilité open-firenet)
static void handleLog() {
  sendCors();
  web.send(200, "text/plain", g_recentLogs.length() ? g_recentLogs : "Pas de logs recents.\n");
}

// --------------------------------------------------------------------- setup
void setup() {
  DBG.begin(115200);
  DBG.println("\n[Open-Firenet] boot");
  buildNames();

  // USB CDC avec les identifiants fixés AVANT begin
  USB.VID(OPENFIRENET_USB_VID);
  USB.PID(OPENFIRENET_USB_PID);
  USB.manufacturerName("Open-Firenet");
  USB.productName("Open-Firenet 2");
  USB.serialNumber("23176212");
  STOVE.begin();                   // CDC TinyUSB vers le poêle
  USB.begin();

  g_link = new firenet::DongleLink(txToStove, nowMs);
  g_link->onDebug([](const char* dir, const std::string& f){
    if (strcmp(dir, "drop") == 0) return;    // trace rx AND tx (frame diagnostics)
    std::string safe = firenet::sanitizeForLog(f);
    DBG.printf("[%s %u] ", dir, (unsigned)safe.size());
    for (char c : safe) { if (c=='\n') DBG.print("\\n"); else if (c=='\r') DBG.print("\\r");
                       else if (c>=32 && c<127) DBG.print(c); else DBG.print('.'); }
    DBG.println();
    logEntry(dir, safe);
  });

  prefs.begin("firenet", true);
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  prefs.end();

  if (wifiSsid.length()) {
    g_isApMode = false;
    g_staStart = millis();
    // Connexion STA robuste — méthode open-firenet (fonctionne en coexistence USB
    // natif TinyUSB) : power-save OFF, TX power max, config bas niveau + connect
    // différé. WiFi.begin() seul échoue (status=6 / no assoc).
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info){
      if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
        DBG.printf("[wifi] DISCONNECTED reason=%d\n", info.wifi_sta_disconnected.reason);
      else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        g_staConnected = true;
        DBG.printf("[wifi] GOT_IP %s\n", WiFi.localIP().toString().c_str());
      }
    });
    WiFi.setTxPower(WIFI_POWER_17dBm);
    esp_wifi_set_max_tx_power(68);
    {
      wifi_config_t conf = {};
      memcpy(conf.sta.ssid,     wifiSsid.c_str(), min((size_t)wifiSsid.length(), (size_t)32));
      memcpy(conf.sta.password, wifiPass.c_str(), min((size_t)wifiPass.length(), (size_t)64));
      conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
      conf.sta.pmf_cfg.capable    = true;
      conf.sta.pmf_cfg.required   = false;
      esp_wifi_set_config(WIFI_IF_STA, &conf);
    }
    esp_wifi_set_max_tx_power(68);
    g_wifiConnectAt = millis() + 500;   // connect différé (laisse le driver se poser)
    DBG.printf("[wifi] STA (background) -> %s\n", wifiSsid.c_str());
  } else {
    startApMode();
  }

  ArduinoOTA.setHostname("open-firenet");
  ArduinoOTA.begin();

  if (MDNS.begin("open-firenet")) {
    MDNS.addService("http", "tcp", 80);
    DBG.println("[mdns] http://open-firenet.local");
  }

  web.enableCORS(true);
  web.on("/", handleRoot);
  web.on("/api/version", handleVersion);
  web.on("/api/state", handleState);
  web.on("/api/control", handleApiControls);
  web.on("/api/controls", handleApiControls);
  web.on("/api/restart", handleRestart);
  web.on("/restart", handleRestart);
  web.on("/api/arm", handleArm);
  web.on("/api/wifi", HTTP_POST, handleWifi);
  web.on("/api/forget", HTTP_POST, handleForget);
  web.on("/api/scan", handleScan);

  // Détection Portail Captif (iOS, Android, Windows)
  web.on("/hotspot-detect.html", handleCaptiveRedirect);
  web.on("/library/test/success.html", handleCaptiveRedirect);
  web.on("/generate_204", handleCaptiveRedirect);
  web.on("/gen_204", handleCaptiveRedirect);
  web.on("/connecttest.txt", handleCaptiveRedirect);
  web.on("/ncsi.txt", handleCaptiveRedirect);

  web.onNotFound([](){
    if (g_isApMode) {
      handleCaptiveRedirect();
      return;
    }
    web.send(404, "text/plain", "Not Found");
  });

  // Routes compatibilité open-firenet & Home Assistant
  web.on("/api/status", handleApiStatus);
  web.on("/api/sensors", handleApiSensors);
  web.on("/reset-wifi", handleForget);
  web.on("/log", handleLog);

  web.begin();
  DBG.println("[web] démarré (avec portail captif + compatibilité open-firenet)");
}

// Mode chasse PRIO2 : pompe GET_REVISION + TRANSFER_COMPLETED en continu, SANS
// nommer de capteurs, pour laisser le poêle émettre ses trames curées (PRIO2).
// On compte les GET_REVISION pour corréler le déclenchement.
#define HUNT_PRIO2 0
static uint32_t g_grCount = 0;

// ---------------------------------------------------------------------- loop
void loop() {
  // 0) connect WiFi différé (laisse le driver se poser après config bas niveau)
  if (g_wifiConnectAt && millis() >= g_wifiConnectAt) {
    g_wifiConnectAt = 0;
    esp_wifi_connect();
    DBG.println("[wifi] esp_wifi_connect()");
  }

  // Secours : si échec de connexion STA après 20s, basculer en AP pour permettre la configuration
  if (!g_isApMode && !g_staConnected && g_staStart && (millis() - g_staStart > 20000)) {
    DBG.println("[wifi] Échec connexion STA (20s) -> Démarrage AP de secours");
    g_staStart = 0;
    startApMode();
  }

  if (g_isApMode) {
    dnsServer.processNextRequest();
  }

  // 0b) provisioning série (Option C) : commande SETWIFI:<ssid>:<pass> sur UART0 (DBG) et CDC TinyUSB (STOVE)
  handleSerialProvisioning();

  // 1) traiter les trames du poêle
  g_link->poll();

  // 1b) Watchdog RX : une fois la version acquittée, le poêle répond en continu
  // (POST_CDCDEVICE_STATUS à chaque cycle). Si plus AUCUN octet reçu pendant
  // RX_TIMEOUT_MS alors qu'on émet toujours, le lien est figé -> on redémarre
  // pour forcer la ré-énumération USB et un nouveau handshake.
  static const uint32_t RX_TIMEOUT_MS = 60000;
  if (g_link->model().version_ack &&
      (millis() - g_link->model().last_rx_ms) > RX_TIMEOUT_MS) {
    DBG.printf("[wd] aucun RX depuis %lus -> ESP.restart()\n", RX_TIMEOUT_MS / 1000);
    delay(50); ESP.restart();
  }

#if HUNT_PRIO2
  if (g_link->model().version_ack) {
    static bool precond = false;
    if (!precond) {
      // pré-condition (§6.3 l.465) : GET_CONTROLS ET GET_SENSORS reçus >=1 fois
      g_link->requestStatus();
      g_link->pollControls({});          // GET_CONTROLS=0 (vide)
      g_link->pollSensors({});           // GET_SENSORS=0 (vide) -> laisse le poêle libre
      precond = true;
    } else if (g_link->txIdle()) {
      // pompe : GR incrémente sensor_prio2_cnt ; TC draine les POST en attente.
      if (WiFi.status() == WL_CONNECTED) g_link->setRssi(WiFi.RSSI());
      g_link->sendRevision();
      g_link->transferCompleted();
      g_link->transferCompleted();
      g_grCount++;
      // relance périodiquement le slot status (~toutes les 40 pompes)
      if (g_grCount % 40 == 0) g_link->requestStatus();
    }
  }
#else
  // 2) cycle de lecture périodique une fois la version acquittée
  if (g_link->model().version_ack && g_link->txIdle() && millis() - lastPoll > 2000) {
    lastPoll = millis();
    if (WiFi.status() == WL_CONNECTED) g_link->setRssi(WiFi.RSSI());  // RSSI réel (§7.4)

    static bool controlsRegistered = false;
    if (g_link->model().sensors_pos.size() < 50) {
      // Phase 1 : enregistrer la table complète de 53 capteurs dans le poêle
      g_link->pollSensors(SENSOR_NAMES);
    } else if (!controlsRegistered) {
      // Phase 2 : enregistrer la table des controls
      g_link->pollControls(CONTROL_NAMES);
      controlsRegistered = true;
    } else {
      // Phase 3 : routine d'interrogation cadencée
      g_link->requestStatus();
      g_link->sendRevision();
      g_link->transferCompleted();
      g_link->transferCompleted();
    }
  }
#endif

  // 3) battement de cœur sur le port COM (diagnostic terrain)
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 3000) {
    lastBeat = millis();
    const auto& m = g_link->model();
    DBG.printf("[hb] ack=%d gen=%d in=%u out=%u rev=%ld sensors=%u controls=%u rssi=%d\n",
               m.version_ack, m.generation, m.frames_in, m.frames_out,
               (long)m.revision, (unsigned)m.sensors.size(),
               (unsigned)m.controls.size(),
               WiFi.status()==WL_CONNECTED ? WiFi.RSSI() : 0);
    DBG.printf("[hb] dropped=%u cdc_connected=%d txfree=%d grCount=%u\n",
               (unsigned)g_link->dropped(), (bool)STOVE, STOVE.availableForWrite(),
               (unsigned)g_grCount);
    DBG.printf("[wifi] status=%d ip=%s rssi=%d ssid=%s\n",
               (int)WiFi.status(), WiFi.localIP().toString().c_str(),
               (int)WiFi.RSSI(), WiFi.SSID().c_str());
    // suivi santé (fuite/fragmentation heap, âge du dernier RX) — diagnostic longue durée
    DBG.printf("[sys] uptime=%lus heap=%u maxblock=%u rxAge=%lums\n",
               (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(),
               (unsigned)ESP.getMaxAllocHeap(),
               (unsigned long)(millis() - g_link->model().last_rx_ms));
    // dump positionnel brut : index=valeur, pour calibrer §14 sur poêle réel
    DBG.printf("[sp] n=%u:", (unsigned)m.sensors_pos.size());
    for (size_t i = 0; i < m.sensors_pos.size(); i++) DBG.printf(" %u=%ld", (unsigned)i, m.sensors_pos[i]);
    DBG.printf("\n[cp] n=%u:", (unsigned)m.controls_pos.size());
    for (size_t i = 0; i < m.controls_pos.size(); i++) DBG.printf(" %u=%ld", (unsigned)i, m.controls_pos[i]);
    DBG.print("\n");
  }

  ArduinoOTA.handle();
  web.handleClient();
}
