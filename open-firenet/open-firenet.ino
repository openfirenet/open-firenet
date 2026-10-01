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

#include <mutex>
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
#include "firenet_api.h"
#include "web_ui.h"

// Avec CDCOnBoot=default (désactivé), le core ne démarre pas l'USB de lui-même :
// on instancie le CDC et on fixe VID/PID AVANT USB.begin(). Serial = UART0 (debug).
USBCDC USBSerial;

// --------------------------------------------------------- version & config USB
#ifndef OPENFIRENET_VERSION
#define OPENFIRENET_VERSION "3.0.0"
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
static std::vector<std::string> SENSOR_NAMES;
static std::vector<std::string> CONTROL_NAMES;
static void buildNames() {
  // Registering more names has no benefit past 88: the stove's real internal
  // array caps out at 88 slots (confirmed live 2026-09-18, see PROTOCOL.md).
  for (int i = 0; i < 88; i++) SENSOR_NAMES.push_back(firenet::sensName(i));
  // DOMO / 2.29 registration: control records 0..31 (up to roomTempOffset), as before.
  for (int i = 0; i < 32; i++) CONTROL_NAMES.push_back(firenet::ctrlName(i));
}

// Current value of a sensor / control: by name (every record is named once registered), else by its position in the
// DOMO-indexed vectors, else the given default.
static long sensorValue(const firenet::StoveModel& m, const char* name, long def) {
  auto it = m.sensors.find(name); if (it != m.sensors.end()) return it->second;
  int i = firenet::sensIndexByName(name);
  return (i >= 0 && (size_t)i < m.sensors_pos.size()) ? m.sensors_pos[i] : def;
}
static long controlValue(const firenet::StoveModel& m, const char* name, long def) {
  auto it = m.controls.find(name); if (it != m.controls.end()) return it->second;
  int i = firenet::ctrlIndexByName(name);
  return (i >= 0 && (size_t)i < m.controls_pos.size()) ? m.controls_pos[i] : def;
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
    case 29: return "PRIMO MULTIAIR";
    default: return "RIKA";
  }
}

// ------------------------------------------------------------------- API web V2
static String jsonState() {
  const auto& m = g_link->model();

  const bool v1 = (m.generation == 2);   // defaults below only matter before the first stove frame
  long rTemp    = sensorValue(m, "roomTemp", 0);
  long fTemp    = sensorValue(m, "flame", 0);
  long bTemp    = sensorValue(m, "boardSensor", 0);
  long mainSt   = sensorValue(m, "mainState", 1);
  long sState   = sensorValue(m, "subState", 0);
  long pTotal   = sensorValue(m, "pelletsTotal", 0);
  long pHours   = sensorValue(m, "pelletHours", 0);
  long sCount   = sensorValue(m, "serviceCountdown", 700);
  long idFan    = sensorValue(m, "idFanMeas", 0);
  long auger    = sensorValue(m, "augerSet", 0);
  long errMask  = sensorValue(m, "errMask32", 0);
  long errSub   = sensorValue(m, "errSub", 0);
  long modelId  = sensorValue(m, "model", v1 ? 1 : 13);          // 1 = INDUO, 13 = DOMO
  long appVer   = sensorValue(m, "appVerBoard", v1 ? 227 : 229);
  long buildVer = sensorValue(m, "firmwareBuild", v1 ? 44501 : 58512);
  long warnCode = sensorValue(m, "statusWarning", 0);

  // Air flap values are tenths of a percent (the Rika cloud integrations divide them by 10). A missing record, e.g. on a
  // stove whose names are not registered yet, is published as null.
  char airFlapsS[16] = "null", airFlapsTgtS[16] = "null";
  auto itAF = m.sensors.find("airFlaps");
  if (itAF != m.sensors.end()) snprintf(airFlapsS, sizeof airFlapsS, "%.1f", itAF->second / 10.0f);
  auto itAT = m.sensors.find("airFlapsTarget");
  if (itAT != m.sensors.end()) snprintf(airFlapsTgtS, sizeof airFlapsTgtS, "%.1f", itAT->second / 10.0f);
  // Without a RIKA room sensor the stove reports the constant 1024 (0x400) as room temperature (read in the 2.27
  // disassembly: record 0 is set to 0x400 when no sensor is present); publish null instead of 102.4 °C.
  bool roomSensor = (rTemp != 1024);

  const char* modelName = getStoveModelName(modelId);

  long curOn     = controlValue(m, "onOff", 0);
  long curMode   = controlValue(m, "mode", 2);
  long curStage  = controlValue(m, "targetStage", 70);
  long curRoom   = controlValue(m, "roomTarget", 200);
  long fan1On    = controlValue(m, "convectionFan1Active", 0);
  long fan1Level = controlValue(m, "convectionFan1Level", 0);
  long fan1Area  = controlValue(m, "convectionFan1Area", 0);
  long fan2On    = controlValue(m, "convectionFan2Active", 0);
  long fan2Level = controlValue(m, "convectionFan2Level", 0);
  long fan2Area  = controlValue(m, "convectionFan2Area", 0);

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

  long htActive    = controlValue(m, "heatingTimesActive", 0);
  long sbTemp      = controlValue(m, "setBackTemp", 160);
  long frostActive = controlValue(m, "frostProtectionActive", 0);
  long frostTemp   = controlValue(m, "frostProtectionTemp", 50);  if (frostTemp <= 0) frostTemp = 50;
  long bakeTarget  = controlValue(m, "bakeTarget", 180);          if (bakeTarget <= 0) bakeTarget = 180;
  long tempOffset  = controlValue(m, "roomTempOffset", 0);
  float tempOffsetF = tempOffset / 10.0f;
  // Eco mode (control ecoMode, DOMO record 6) and whether the stove allows it (sensor ecoModePossible).
  long ecoMode     = controlValue(m, "ecoMode", 0);
  long ecoPossible = sensorValue(m, "ecoModePossible", 0);

  float rTempF = rTemp / 10.0f;
  float rTargetF = curRoom / 10.0f;
  float sbTempF = sbTemp / 10.0f;
  float frostTempF = frostTemp / 10.0f;
  float fTempF = (float)fTemp;
  float bTempF = (float)bTemp;

  char roomTempS[16] = "null";
  if (roomSensor) snprintf(roomTempS, sizeof roomTempS, "%.1f", rTempF);

  char buf[2000];
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
      "\"warning_code\":%ld,"
      "\"model\":%ld,"
      "\"model_name\":\"%s\","
      "\"mainboard_version\":\"%ld.%02ld\","
      "\"firmware_build\":\"%ld\""
    "},"
    "\"sensors\":{"
      "\"room_temperature\":%s,"
      "\"room_sensor_connected\":%s,"
      "\"combustion_temperature\":%.1f,"
      "\"board_temperature\":%.1f,"
      "\"pellets_total_kg\":%ld,"
      "\"pellet_hours\":%ld,"
      "\"service_countdown_kg\":%ld,"
      "\"fan_speed_rpm\":%ld,"
      "\"auger_speed_rpm\":%ld,"
      "\"air_flaps_percent\":%s,"
      "\"air_flaps_target_percent\":%s"
    "},"
    "\"controls\":{"
      "\"on\":%s,"
      "\"mode\":\"%s\","
      "\"mode_code\":%ld,"
      "\"target_temperature\":%.1f,"
      "\"power_percent\":%ld,"
      "\"heating_times_active\":%s,"
      "\"setback_temperature\":%.1f,"
      "\"convection_fan1_active\":%s,"
      "\"convection_fan1_level\":%ld,"
      "\"convection_fan1_area\":%ld,"
      "\"convection_fan2_active\":%s,"
      "\"convection_fan2_level\":%ld,"
      "\"convection_fan2_area\":%ld,"
      "\"frost_protection_active\":%s,"
      "\"frost_protection_temperature\":%.1f,"
      "\"bake_target_temperature\":%ld,"
      "\"room_temperature_offset\":%.1f,"
      "\"eco_mode\":%s,"
      "\"eco_mode_possible\":%s"
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
    errMask, errSub, warnCode,
    modelId, modelName, appVer / 100, appVer % 100, buildVer,
    roomTempS, roomSensor ? "true" : "false", fTempF, bTempF, pTotal, pHours, sCount, idFan, auger,
    airFlapsS, airFlapsTgtS,
    (curOn == 1) ? "true" : "false",
    modeName, curMode, rTargetF, curStage,
    (htActive == 1) ? "true" : "false", sbTempF,
    (fan1On == 1) ? "true" : "false", fan1Level, fan1Area,
    (fan2On == 1) ? "true" : "false", fan2Level, fan2Area,
    (frostActive == 1) ? "true" : "false", frostTempF,
    bakeTarget,
    tempOffsetF,
    ecoMode ? "true" : "false", ecoPossible ? "true" : "false"
  );

  String j = String(buf);
  j += "\"wifi_mode\":\"" + String(WiFi.getMode()==WIFI_AP?"AP":"STA") + "\",";
  j += "\"ip\":\"" + (WiFi.getMode()==WIFI_AP?WiFi.softAPIP():WiFi.localIP()).toString() + "\",";
  j += "\"wifi_connected\":" + String(WiFi.status()==WL_CONNECTED?"true":"false") + ",";
  j += "\"uptime_seconds\":" + String(millis() / 1000UL) + ",";
  j += "\"write_enabled\":true,";
  j += "\"version_ack\":" + String(m.version_ack ? "true" : "false") + ",";
  // version_profile: which family's probe got acked (DongleLink::DETECT_V3=0, DETECT_V28=1, DETECT_V1=2); a
  // detected 2.28 still runs the DOMO/V3 protocol (generation=1), this label is for display only.
  const char* verFrameLabel = m.version_profile == 0 ? "V3" : (m.version_profile == 1 ? "V28" : (m.version_profile == 2 ? "V1" : "?"));
  j += "\"version_frame\":\"" + String(verFrameLabel) + "\",";
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
    String val = (kv.first == "wpa2" && !kv.second.empty() && kv.second != "0") ? "********" : String(kv.second.c_str());
    j += "\"" + String(kv.first.c_str()) + "\":\"" + val + "\"";
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
// The page is stored gzip-compressed (web_ui.h, generated from web/index.html) and decompressed by the browser.
static void handleRoot() {
  web.sendHeader("Content-Encoding", "gzip");
  web.send_P(200, "text/html", (PGM_P)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}

// GET/POST /api/txgap : délai entre trames envoyées au poêle (ms), borné à 50..600.
// Pris en compte dès la prochaine évaluation de la file d'émission ; conservé en NVS.
static void handleTxGap() {
  sendCors();
  if (web.method() == HTTP_OPTIONS) { web.send(204); return; }
  if (web.method() == HTTP_POST) {
    long ms = 0;
    std::vector<std::pair<std::string, std::string>> kv;
    firenet::jsonPairs(web.hasArg("plain") ? web.arg("plain").c_str() : "", kv);
    for (const auto& p : kv) if (p.first == "ms") ms = atol(p.second.c_str());
    if (ms <= 0 && web.hasArg("ms")) ms = web.arg("ms").toInt();
    if (ms > 0) {
      g_link->setTxGapMs((uint32_t)(ms < 0 ? 0 : ms));
      prefs.begin("firenet", false);
      prefs.putUInt("txgap", g_link->txGapMs());
      prefs.end();
    }
  }
  char buf[64];
  snprintf(buf, sizeof buf, "{\"ms\":%u,\"default\":%u,\"min\":%u,\"max\":%u}",
           (unsigned)g_link->txGapMs(), (unsigned)firenet::DongleLink::TX_GAP_MS,
           (unsigned)firenet::DongleLink::TX_GAP_MIN_MS, (unsigned)firenet::DongleLink::TX_GAP_MAX_MS);
  web.send(200, "application/json", buf);
}

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
// Journal : tampon circulaire statique (aucune allocation, aucune copie → pas de
// fragmentation du tas). Les lignes identiques consécutives sont regroupées en une seule
// ligne « xN » pour qu'une rafale ne chasse pas le reste du journal.
// 48 KB: the ring is reserved in RAM at boot; 96 KB left a DOMO 2.29 with only ~25 KB of free heap at its lowest
// (repeated lines are merged, so 48 KB still holds several minutes of traffic).
static const size_t LOG_RING_BYTES = 96 * 1024;  // TEMPORARY for the 2.28 test (issue #4): keep the start of the link attempt; back to 48 KB before merging
static char     g_logRing[LOG_RING_BYTES];
static uint64_t g_logTotal = 0;             // octets écrits depuis le boot (position absolue)
static std::mutex g_logMx;                  // logEntry() est aussi appelé depuis des callbacks USB
static String   g_pendKey;                  // "dir\x01msg" de la ligne en attente de regroupement
static String   g_pendLine;                 // "[ms][dir] msg" de sa première occurrence
static uint32_t g_pendCount = 0, g_pendLastMs = 0;

static void logRingWrite(const char* p, size_t n) {
  for (size_t i = 0; i < n; ) {
    size_t at = (size_t)(g_logTotal % LOG_RING_BYTES);
    size_t k = LOG_RING_BYTES - at; if (k > n - i) k = n - i;
    memcpy(g_logRing + at, p + i, k);
    g_logTotal += k; i += k;
  }
}
static void logFlushPendingLocked() {
  if (!g_pendCount) return;
  logRingWrite(g_pendLine.c_str(), g_pendLine.length());
  if (g_pendCount > 1) {
    char t[48]; int n = snprintf(t, sizeof t, "  x%u (last=%u)", (unsigned)g_pendCount, (unsigned)g_pendLastMs);
    logRingWrite(t, (size_t)n);
  }
  logRingWrite("\n", 1);
  g_pendCount = 0;
}

static void logEntry(const char* dir, const std::string& msg) {
  // Concaténation directe (pas de tampon fixe) : une longue trame n'est jamais tronquée.
  uint32_t ms = (uint32_t)millis();
  String key = String(dir) + "\x01" + msg.c_str();
  std::lock_guard<std::mutex> lk(g_logMx);
  if (g_pendCount && key == g_pendKey) { g_pendCount++; g_pendLastMs = ms; return; }
  logFlushPendingLocked();
  g_pendKey = key;
  g_pendLine = "[" + String((unsigned long)ms) + "][" + dir + "] " + msg.c_str();
  g_pendCount = 1; g_pendLastMs = ms;
}

// ── USB control-channel diagnostics (DTR/RTS, line coding) ────────────────
// Testing hypothesis (HA community forum + issue #4): the 0x16→0x33 probe
// loop some stoves get stuck in (firmware ~2.25-2.28) repeats identically
// even with zero reply from the dongle, which points away from CDC *data*
// content and toward the USB *control* transfers (SET_CONTROL_LINE_STATE /
// SET_LINE_CODING) that nothing in this codebase has ever logged before.
static void onUsbCdcLineState(void* arg, esp_event_base_t base, int32_t id, void* data) {
  auto* p = (arduino_usb_cdc_event_data_t*)data;
  char b[48];
  snprintf(b, sizeof b, "line_state dtr=%d rts=%d", p->line_state.dtr, p->line_state.rts);
  logEntry("usb", b);
}
static void onUsbCdcLineCoding(void* arg, esp_event_base_t base, int32_t id, void* data) {
  auto* p = (arduino_usb_cdc_event_data_t*)data;
  char b[80];
  snprintf(b, sizeof b, "line_coding baud=%lu stop=%u parity=%u bits=%u",
           (unsigned long)p->line_coding.bit_rate, p->line_coding.stop_bits,
           p->line_coding.parity, p->line_coding.data_bits);
  logEntry("usb", b);
}

static void sendCors() {
  web.sendHeader("Access-Control-Allow-Origin", "*");
  web.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  web.sendHeader("Access-Control-Allow-Headers", "*");
}

// GET /api/controls & POST /api/controls (V2 JSON + legacy compat)
static void handleApiControls() {
  sendCors();
  if (web.method() == HTTP_OPTIONS) { web.send(204); return; }
  const auto& m = g_link->model();

  if (web.method() == HTTP_GET) {
    long curOn = controlValue(m, "onOff", 0), curMode = controlValue(m, "mode", 2);
    long curStage = controlValue(m, "targetStage", 70), curRoom = controlValue(m, "roomTarget", 200);
    long htActive = controlValue(m, "heatingTimesActive", 0), sbTemp = controlValue(m, "setBackTemp", 160);
    long frostActive = controlValue(m, "frostProtectionActive", 0);
    long frostTemp = controlValue(m, "frostProtectionTemp", 50);  if (frostTemp <= 0) frostTemp = 50;
    long bakeTarget = controlValue(m, "bakeTarget", 180);         if (bakeTarget <= 0) bakeTarget = 180;
    long tempOffset = controlValue(m, "roomTempOffset", 0), eco = controlValue(m, "ecoMode", 0);
    const char* modeName = (curMode == 0) ? "manual" : ((curMode == 1) ? "auto" : "comfort");
    char buf[1024];
    snprintf(buf, sizeof(buf),
      "{\"on\":%s,\"mode\":\"%s\",\"mode_code\":%ld,\"target_temperature\":%.1f,\"power_percent\":%ld,"
      "\"onOff\":%ld,\"operatingMode\":%ld,\"heatingPower\":%ld,\"tempRoomTarget\":%ld,"
      "\"heatingTimesActive\":%ld,\"heating_times_active\":%s,\"setBackTemp\":%ld,\"setback_temperature\":%.1f,"
      "\"convectionFan1Active\":%ld,\"convectionFan1Level\":%ld,\"convectionFan1Area\":%ld,"
      "\"convectionFan2Active\":%ld,\"convectionFan2Level\":%ld,\"convectionFan2Area\":%ld,"
      "\"frostProtectionActive\":%ld,\"frost_protection_active\":%s,\"frostProtectionTemp\":%ld,\"frost_protection_temperature\":%.1f,"
      "\"bakeTarget\":%ld,\"bake_target_temperature\":%ld,\"roomTempOffset\":%ld,\"room_temperature_offset\":%.1f,"
      "\"ecoMode\":%ld,\"eco_mode\":%s}",
      curOn == 1 ? "true" : "false", modeName, curMode, curRoom / 10.0f, curStage,
      curOn, curMode, curStage, curRoom,
      htActive, htActive == 1 ? "true" : "false", sbTemp, sbTemp / 10.0f,
      controlValue(m, "convectionFan1Active", 0), controlValue(m, "convectionFan1Level", 0), controlValue(m, "convectionFan1Area", 0),
      controlValue(m, "convectionFan2Active", 0), controlValue(m, "convectionFan2Level", 0), controlValue(m, "convectionFan2Area", 0),
      frostActive, frostActive == 1 ? "true" : "false", frostTemp, frostTemp / 10.0f,
      bakeTarget, bakeTarget, tempOffset, tempOffset / 10.0f,
      eco, eco ? "true" : "false");
    web.send(200, "application/json", buf);
    return;
  }

  // POST / PUT: every accepted form (JSON body, "k=v;" text, form or query arguments, name/value pair) is reduced to
  // name/value pairs and parsed by the same table (firenet_api.h). Later forms override earlier ones.
  std::string body = (web.hasArg("plain") ? web.arg("plain") : (web.hasArg("cmd") ? web.arg("cmd") : String())).c_str();
  std::vector<std::pair<std::string, std::string>> pairs;
  if (body.find('{') != std::string::npos) firenet::jsonPairs(body, pairs); else firenet::textPairs(body, pairs);
  for (int i = 0; i < web.args(); i++) {
    String n = web.argName(i);
    if (n == "plain" || n == "cmd" || n == "name" || n == "value") continue;
    pairs.push_back({n.c_str(), web.arg(i).c_str()});
  }
  if (web.hasArg("name") && web.hasArg("value")) pairs.push_back({web.arg("name").c_str(), web.arg("value").c_str()});
  const auto cmd = firenet::parseControlCommands(pairs);
  auto has = [&](const char* c) { return cmd.find(c) != cmd.end(); };
  auto get = [&](const char* c, long def) { auto it = cmd.find(c); return it != cmd.end() ? it->second : controlValue(m, c, def); };

  // Read-modify-write: the base settings and MultiAir are always sent, the others only when commanded.
  long finalOn = get("onOff", 0), finalMode = get("mode", 2), finalStage = get("targetStage", 70), finalRoom = get("roomTarget", 200);
  long fan1On = get("convectionFan1Active", 0), fan2On = get("convectionFan2Active", 0);
  long fan1Level = get("convectionFan1Level", 0), fan2Level = get("convectionFan2Level", 0);
  if (fan1Level < 0) fan1Level = controlValue(m, "convectionFan1Level", 0);
  if (fan2Level < 0) fan2Level = controlValue(m, "convectionFan2Level", 0);
  long fan1Area = get("convectionFan1Area", 0), fan2Area = get("convectionFan2Area", 0);
  if (fan1Area < -30 || fan1Area > 30) fan1Area = controlValue(m, "convectionFan1Area", 0);
  if (fan2Area < -30 || fan2Area > 30) fan2Area = controlValue(m, "convectionFan2Area", 0);

  std::vector<std::pair<std::string, long>> full = {
    {"revision", (long)m.revision}, {"onOff", finalOn}, {"mode", finalMode}, {"targetStage", finalStage}, {"roomTarget", finalRoom},
    {"convectionFan1Active", fan1On}, {"convectionFan1Level", fan1Level}, {"convectionFan1Area", fan1Area},
    {"convectionFan2Active", fan2On}, {"convectionFan2Level", fan2Level}, {"convectionFan2Area", fan2Area},
  };
  for (const char* c : {"heatingTimesActive", "setBackTemp", "frostProtectionActive", "frostProtectionTemp", "ecoMode"}) {
    if (has(c) && cmd.at(c) >= 0) full.push_back({c, cmd.at(c)});
  }
  if (has("bakeTarget") && cmd.at("bakeTarget") >= 0) {
    long b = cmd.at("bakeTarget"); full.push_back({"bakeTarget", b < 130 ? 130 : (b > 340 ? 340 : b)});
  }
  if (has("roomTempOffset")) {
    long o = cmd.at("roomTempOffset"); full.push_back({"roomTempOffset", o < -40 ? -40 : (o > 40 ? 40 : o)});
  }
  for (int i = 7; i <= 20; i++) {
    std::string k = firenet::ctrlName(i);
    auto it = cmd.find(k);
    if (it != cmd.end() && it->second >= 0) full.push_back({k, it->second});
  }

  g_link->applyControls(full);
  lastPoll = millis();

  const char* modeName = (finalMode == 0) ? "manual" : ((finalMode == 1) ? "auto" : "comfort");
  char resBuf[256];
  snprintf(resBuf, sizeof(resBuf),
    "{\"ok\":true,\"on\":%s,\"mode\":\"%s\",\"mode_code\":%ld,\"target_temperature\":%.1f,\"power_percent\":%ld}",
    finalOn == 1 ? "true" : "false", modeName, finalMode, finalRoom / 10.0f, finalStage);
  web.send(200, "application/json", resBuf);
}

// GET & POST /api/schedule
static void handleApiSchedule() {
  sendCors();
  if (web.method() == HTTP_OPTIONS) { web.send(204); return; }
  const auto& m = g_link->model();

  if (web.method() == HTTP_GET) {
    long htActive = 0, sbTemp = 160;
    auto itH = m.controls.find("heatingTimesActive"); if (itH != m.controls.end()) htActive = itH->second;
    else if (m.controls_pos.size() > 21) htActive = m.controls_pos[21];

    auto itS = m.controls.find("setBackTemp"); if (itS != m.controls.end()) sbTemp = itS->second;
    else if (m.controls_pos.size() > 22) sbTemp = m.controls_pos[22];

    String json = "{\"ok\":true,\"active\":" + String(htActive == 1 ? "true" : "false") + ",";
    json += "\"heatingTimesActive\":" + String(htActive) + ",";
    json += "\"setback_temperature\":" + String(sbTemp / 10.0f, 1) + ",";
    json += "\"setBackTemp\":" + String(sbTemp) + ",";
    json += "\"slots\":{";
    for (int i = 0; i < 14; i++) {
      std::string key = firenet::ctrlName(7 + i);
      long val = 0;
      auto it = m.controls.find(key); if (it != m.controls.end()) val = it->second;
      else if (m.controls_pos.size() > (size_t)(7 + i)) val = m.controls_pos[7 + i];
      if (i > 0) json += ",";
      json += "\"" + String(key.c_str()) + "\":" + String(val);
    }
    json += "}}";
    web.send(200, "application/json", json);
    return;
  }

  // POST / PUT: delegate to handleApiControls
  handleApiControls();
}

// GET /log (compatibilité open-firenet) — envoyé par morceaux depuis le tampon circulaire.
static void handleLog() {
  sendCors();
  uint64_t total, pos;
  {
    std::lock_guard<std::mutex> lk(g_logMx);
    logFlushPendingLocked();
    total = g_logTotal;
    pos = total > LOG_RING_BYTES ? total - LOG_RING_BYTES : 0;
  }
  char hdr[160];
  int hn = snprintf(hdr, sizeof hdr,
      "=== journal: uptime=%lus, %llu octets ecrits, %s, tas libre=%u (min %u) ===\n",
      (unsigned long)(millis() / 1000), (unsigned long long)total,
      pos ? "ANCIEN CONTENU ECRASE" : "complet depuis le boot",
      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200, "text/plain", "");
  web.sendContent(hdr, (size_t)hn);
  char chunk[1024];
  bool first = (pos != 0);       // on peut démarrer au milieu d'une ligne : la sauter
  while (pos < total) {
    size_t n;
    {
      std::lock_guard<std::mutex> lk(g_logMx);
      if (g_logTotal > LOG_RING_BYTES && pos < g_logTotal - LOG_RING_BYTES) pos = g_logTotal - LOG_RING_BYTES;
      n = (size_t)(total - pos); if (n > sizeof chunk) n = sizeof chunk;
      size_t at = (size_t)(pos % LOG_RING_BYTES);
      if (n > LOG_RING_BYTES - at) n = LOG_RING_BYTES - at;
      memcpy(chunk, g_logRing + at, n);
    }
    pos += n;
    size_t off = 0;
    if (first) { const char* nl = (const char*)memchr(chunk, '\n', n); off = nl ? (size_t)(nl - chunk) + 1 : n; if (nl) first = false; }
    if (n > off) web.sendContent(chunk + off, n - off);
  }
  if (total == 0) web.sendContent("Pas de logs recents.\n");
  web.sendContent("");
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
  USB.productName("Open-Firenet (V1)");
  USB.serialNumber("23176212");
  STOVE.begin();                   // CDC TinyUSB vers le poêle
  // This device stays permanently wired into the stove: the Arduino
  // "1200-baud / DTR touch reset" watcher (on by default, reboot_enable=true
  // in USBCDC) serves no purpose here, and an unexpected DTR/RTS sequence
  // from the stove's USB driver could trigger it by accident -- silently
  // dropping the ESP32 into the bootloader mid-handshake, which would look
  // exactly like an infinite probe loop from the stove's side.
  STOVE.enableReboot(false);
  STOVE.onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT, onUsbCdcLineState);
  STOVE.onEvent(ARDUINO_USB_CDC_LINE_CODING_EVENT, onUsbCdcLineCoding);
  USB.begin();

  g_link = new firenet::DongleLink(txToStove, nowMs);
  g_link->onDebug([](const char* dir, const std::string& f){
    if (strcmp(dir, "drop") == 0) {
      logEntry("drop", f);
      return;
    }
    std::string safe = firenet::sanitizeForLog(f);
    DBG.printf("[%s %u] ", dir, (unsigned)safe.size());
    for (char c : safe) { if (c=='\n') DBG.print("\\n"); else if (c=='\r') DBG.print("\\r");
                       else if (c>=32 && c<127) DBG.print(c); else DBG.print('.'); }
    DBG.println();
    logEntry(dir, safe);
  });

  prefs.begin("firenet", true);
  g_link->setTxGapMs(prefs.getUInt("txgap", firenet::DongleLink::TX_GAP_MS));
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  prefs.end();

  if (wifiSsid.length()) {
    g_link->setCredentials(wifiSsid.c_str(), wifiPass.c_str());
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
        g_link->setCredentials(wifiSsid.c_str(), wifiPass.c_str(),
                               WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
        if (g_link->model().version_ack) {
          g_link->pushStatus();
        }
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
  web.on("/api/schedule", handleApiSchedule);
  web.on("/api/restart", handleRestart);
  web.on("/api/txgap", handleTxGap);
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
  web.on("/log", handleLog);

  web.begin();
  DBG.println("[web] démarré (avec portail captif + compatibilité open-firenet)");
}


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

  // 2) cycle de lecture périodique une fois la version acquittée
  if (g_link->model().version_ack && g_link->txIdle() && millis() - lastPoll > 2000) {
    lastPoll = millis();
    if (WiFi.status() == WL_CONNECTED) g_link->setRssi(WiFi.RSSI());  // RSSI réel (§7.4)

    if (g_link->induoDialect()) {
      // INDUO family (V1 = 2.26/2.27, and a detected INDUO II 2.28): names registered once, status every ~20s.
      // Keyed on induoDialect(), not generation: a detected 2.28 has generation 1 (DOMO tables) but must not run
      // the DOMO loop below, whose phase 3 re-sends the full status every 2s.
      static uint32_t v1Cycle = 0;
      v1Cycle++;
      if (v1Cycle % 10 == 0) {
        // About every 20 s: refresh the status. The PRIO 2 records arrive by themselves at every 30th
        // GET_REVISION; pollPrio2Sensors() sends nothing (a GET_SENSORS frame would empty the registered names).
        g_link->pollPrio2Sensors();
        g_link->requestStatus();
      } else {
        // Routine (toutes les 2s) : PRIO 1 capteurs + contrôles
        g_link->pollSensors();
        g_link->pollControls();
      }
    } else {
      // V3 (DOMO V2.29+) : enregistrement préalable des sentinelles
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
  }

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
    DBG.printf("[hb] dropped=%u cdc_connected=%d txfree=%d\n",
               (unsigned)g_link->dropped(), (bool)STOVE, STOVE.availableForWrite());
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
