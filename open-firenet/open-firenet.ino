// open-firenet.ino — Open Firenet pour ESP32-S3 (reverse-engineering).
//
// Rôle : se substituer au dongle officiel. L'ESP32 est DEVICE USB CDC branché sur
// le poêle (hôte USB) et joue à la fois le dongle ET le serveur local Open Firenet :
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
#include "firenet_web_guard.h"
#include "mbedtls/sha256.h"
#include "firenet_mqtt.h"
#include <deque>
#include "mqtt_client.h"   // esp-mqtt, Espressif's MQTT client shipped with the ESP32 core
#include "esp_crt_bundle.h" // public certificate authorities, to verify a broker reached over TLS
#include "web_ui.h"

// Types used in function signatures are declared here, ahead of every function: the Arduino builder inserts its
// generated prototypes before the first function of the sketch.
// Base settings sent to the stove by applyControlPairs().
struct AppliedControls { long on, mode, stage, room; bool sent; };   // sent: false when the command was refused

// MQTT settings (the logic is further down, in the MQTT section).
struct MqttSettings {
  bool enabled = false; String host; uint16_t port = 1883; String user, pass, base = "openfirenet"; bool discovery = false;
  bool tls = false;    // encrypted connection (MQTT over TLS, usually port 8883)
  String ca;           // optional: certificate (PEM) of the authority that signed the broker's certificate
};
static MqttSettings g_mqttCfg;
static volatile bool g_mqttConnected = false;
static String jsonHealth(bool forMqtt);   // defined with the bridge health code
static String g_otaHash;                  // SHA-256 of the update password, empty when none (see loadOtaPassword)
// Wi-Fi power saving (modem sleep), the owner's choice in the Bridge tab (issue #82). On, the default: the chip
// sleeps between the access point's beacons, so it runs cooler and draws less from the stove's USB port, and an
// answer can wait up to a quarter of a second. Off: it answers at once; for networks on which the bridge keeps
// dropping. The setting goes through the Wi-Fi library, which applies its own value when the station starts.
static bool g_wifiPowerSaving = true;
struct MqttCommand { std::string topic, payload; };

// Avec CDCOnBoot=default (désactivé), le core ne démarre pas l'USB de lui-même :
// on instancie le CDC et on fixe VID/PID AVANT USB.begin(). Serial = UART0 (debug).
USBCDC USBSerial;

// --------------------------------------------------------- version & config USB
#ifndef OPENFIRENET_VERSION
#define OPENFIRENET_VERSION "4.1.0"
#endif

// Identifiants USB Open Firenet
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
static volatile uint32_t g_wifiConnectAt = 0;   // connect STA différé (méthode open-firenet)
// Reconnection (issue #82). The library's own automatic reconnection is off (see setup), so each time the Wi-Fi
// drops or an attempt fails, the next attempt is scheduled here: after 1 s, then 5 s, then every 30 s, without
// ever giving up. The delay goes back to 1 s once an address is obtained.
static volatile uint32_t g_wifiRetryMs = 1000;
static volatile bool     g_leaveApMode = false;   // the Wi-Fi came back while the setup access point was up
static bool     g_isApMode = false;
static bool     g_staConnected = false;
static uint32_t g_staStart = 0;
bool        writeEnabled = true;    // Open Firenet : consignes actives directement

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
  // The stove (USB host, Atmel AVR32) expects short packets (< 64 bytes): frames are cut into pieces of at most
  // 32 bytes, each written and flushed at once, as the official stick did.
  // Exception: a version frame that fits in one short packet is sent whole. Cut in two (32 + 27 bytes, 2 ms
  // apart), a SONO 2.28 recorded another version than the one announced (APP=1, REV=0 instead of 112 / 13301:
  // read in its status reply) and then offered a stick update ("FIRENET UPDATE") as soon as it got our status;
  // sent whole, it records 112 / 13301 and links normally (issue #4, logs of 2026-10-06). How the stove mixes
  // the two pieces was not read in its code. A DOMO 2.29 records the right version either way (same day). The
  // INDUO 2.26 / 2.27 frame (74 bytes) does not fit and stays cut as before.
  if (n < 64 && n > 13 && memcmp(d, "GET_CDCDEVICE", 13) == 0 && memmem(d, n, "_VERSION=", 9) != nullptr) {
    uint32_t start = millis();
    while (tud_cdc_n_write_available(0) < n && (millis() - start) < 200) delay(1);
    uint32_t w = tud_cdc_n_write(0, d, n);
    tud_cdc_n_write_flush(0);
    if (w != n) DBG.printf("[txToStove] ERR version frame: sent only %u/%u bytes!\n", (unsigned)w, (unsigned)n);
    return;
  }
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
// Name, label and "is burning" of a main state code (with its sub-state, which tells "off" from "standby").
//
// Wood operation of a combined stove (issue #89), read on an INDUO on firmware 2.30: log check = 11 (after the door
// was opened in standby), 13 (after ignition), 14 (after control mode); split log mode = 20 and 21; 21 with
// sub-state 12 is shown by the stove as "put on split logs" or as "do not put log", which these two codes do not
// tell apart. Codes 16, 17 and 50 are log checks too in the table of the community integration for RIKA's cloud
// (Fockaert/rika-firenet-custom-component); nobody reported them from a stove yet.
// "Put on split logs" against "do not put logs": the mainboard does not tell them apart, the touch display does. Read
// in the touch application (2.28.500.02 fn 0x9d04f078, same constants in 2.29.534.04): it compares a value with 351
// and 300 -- 351 and above "split log mode", 300 to 350 "put on split logs", below 300 "do not put logs". That
// value is taken here to be the combustion chamber temperature, which the two files of issue #89 fit (559 in split
// log mode, 213 at "do not put log") without proving it. Not reproduced: the display's eco thresholds (331 / 280)
// and a flag that keeps "split log mode" between 300 and 350.
// Off against standby, read on a DOMO on firmware 2.29: main state 1 with sub-state 0 while the stove is switched
// off, sub-state 3 once it is switched on and waits for a heat demand.
static void mainStateNames(long mainSt, long subState, long flameTemp, const char*& stName, const char*& stLabel, bool& isBurning) {
  stName = "unknown"; stLabel = "Unknown"; isBurning = false;
  switch (mainSt) {
    case 0: stName = "off"; stLabel = "Off"; break;
    case 1:
      if (subState == 0) { stName = "off"; stLabel = "Off"; }
      else               { stName = "standby"; stLabel = "Standby"; }
      break;
    case 2: stName = "ignition"; stLabel = "Ignition"; isBurning = true; break;
    case 3: stName = "flame_start"; stLabel = "Flame Start"; isBurning = true; break;
    case 4: stName = "heating"; stLabel = "Heating"; isBurning = true; break;
    case 5: stName = "cleaning"; stLabel = "Grate Cleaning"; isBurning = true; break;
    case 6: stName = "burn_off"; stLabel = "Burn Off"; isBurning = true; break;
    case 7: stName = "splitlog"; stLabel = "Split Log"; isBurning = true; break;
    case 11: case 13: case 14: case 16: case 17: case 50:
      stName = "splitlog_check"; stLabel = "Split Log Check"; break;
    case 20: case 21:
      stName = "splitlog"; stLabel = "Split Log"; isBurning = true;
      if (mainSt == 21 && subState == 12) {
        if (flameTemp < 300)       { stName = "splitlog_no_refuel"; stLabel = "Do Not Put Logs"; }
        else if (flameTemp <= 350) { stName = "splitlog_refuel";    stLabel = "Put On Split Logs"; }
      }
      break;
  }
}

// The "stove", "sensors" and "controls" objects, shared by /api/state and by MQTT (same names, same units).
static String jsonStoveSections() {
  const auto& m = g_link->model();

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
  // Stove identity: only what the stove itself reported. Before its first sensor post these are null (they used to
  // default to a DOMO 2.29, which looked like real data on a stove that was not linked at all).
  long modelId  = sensorValue(m, "model", -1);
  long appVer   = sensorValue(m, "appVerBoard", -1);
  long buildVer = sensorValue(m, "firmwareBuild", -1);
  long warnCode = sensorValue(m, "statusWarning", 0);

  // Air flap values are per mille: the stove screen labels them "Luftklappen [‰]" and shows the raw value (50 for
  // raw 50 = 5.0 %, photo of an INDUO 2.27, issue #4), so they are divided by 10 to publish a percentage. A missing
  // record, e.g. on a stove whose names are not registered yet, is published as null.
  char airFlapsS[16] = "null", airFlapsTgtS[16] = "null";
  auto itAF = m.sensors.find("airFlaps");
  if (itAF != m.sensors.end()) snprintf(airFlapsS, sizeof airFlapsS, "%.1f", itAF->second / 10.0f);
  auto itAT = m.sensors.find("airFlapsTarget");
  if (itAT != m.sensors.end()) snprintf(airFlapsTgtS, sizeof airFlapsTgtS, "%.1f", itAT->second / 10.0f);
  // Without a RIKA room sensor the stove reports the constant 1024 (0x400) as room temperature (read in the 2.27
  // disassembly: record 0 is set to 0x400 when no sensor is present); publish null instead of 102.4 °C.
  bool roomSensor = (rTemp != 1024);

  char modelS[12] = "null", modelNameS[40] = "null", mbVerS[16] = "null", buildS[16] = "null";
  if (modelId >= 0) {
    snprintf(modelS, sizeof modelS, "%ld", modelId);
    snprintf(modelNameS, sizeof modelNameS, "\"%s\"", getStoveModelName(modelId));
  }
  if (appVer >= 0) snprintf(mbVerS, sizeof mbVerS, "\"%ld.%02ld\"", appVer / 100, appVer % 100);
  if (buildVer >= 0) snprintf(buildS, sizeof buildS, "\"%ld\"", buildVer);

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

  const char* stName; const char* stLabel; bool isBurning;
  mainStateNames(mainSt, sState, fTemp, stName, stLabel, isBurning);

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

  char buf[1500];
  snprintf(buf, sizeof(buf),
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
      "\"model\":%s,"
      "\"model_name\":%s,"
      "\"mainboard_version\":%s,"
      "\"firmware_build\":%s"
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
    "}",
    stName, mainSt, stLabel, sState,
    isBurning ? "true" : "false",
    errMask != 0 ? "true" : "false",
    errMask, errSub, warnCode,
    modelS, modelNameS, mbVerS, buildS,
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

  return String(buf);
}

static String jsonState() {
  const auto& m = g_link->model();
  const char* stName; const char* stLabel; bool isBurning;
  mainStateNames(sensorValue(m, "mainState", 1), sensorValue(m, "subState", 0), sensorValue(m, "flame", 0), stName, stLabel, isBurning);

  char buf[600];
  snprintf(buf, sizeof(buf),
    "{"
    "\"device\":{"
      "\"name\":\"Open Firenet\","
      "\"version\":\"" OPENFIRENET_VERSION "\","
      "\"app_version\":\"" OPENFIRENET_VERSION "\","
      "\"firmware_version\":\"" OPENFIRENET_VERSION "\","
      "\"ip\":\"%s\","
      "\"mac\":\"%s\","
      "\"wifi_ssid\":\"%s\","
      "\"wifi_rssi\":%d,"
      "\"uptime_seconds\":%lu,"
      "\"free_heap\":%u,"
      "\"ota_slot_bytes\":%u,"
      "\"ota_password_set\":%s,"
      "\"connected\":%s"
    "},",
    (g_isApMode?WiFi.softAPIP():WiFi.localIP()).toString().c_str(),
    WiFi.macAddress().c_str(),
    jsonEscape(WiFi.SSID()).c_str(),   // a network name may contain quotes
    WiFi.RSSI(),
    millis() / 1000UL,
    ESP.getFreeHeap(),
    // Size of the partition a wireless update is written to: a firmware larger than this cannot be installed
    // over Wi-Fi (1,310,720 bytes on a board flashed with Arduino's default partition scheme).
    (unsigned)ESP.getFreeSketchSpace(),
    g_otaHash.length() == 64 ? "true" : "false",
    m.version_ack ? "true" : "false"
  );

  String j = String(buf) + jsonStoveSections() + ",";
  j += "\"wifi_mode\":\"" + String(g_isApMode?"AP":"STA") + "\",";
  j += "\"ip\":\"" + (g_isApMode?WiFi.softAPIP():WiFi.localIP()).toString() + "\",";
  j += "\"wifi_connected\":" + String(WiFi.status()==WL_CONNECTED?"true":"false") + ",";
  j += "\"wifi_power_saving\":" + String(g_wifiPowerSaving?"true":"false") + ",";
  j += "\"uptime_seconds\":" + String(millis() / 1000UL) + ",";
  j += "\"write_enabled\":true,";
  j += "\"version_ack\":" + String(m.version_ack ? "true" : "false") + ",";
  // version_profile: which family's probe got acked (DongleLink::DETECT_V3=0, DETECT_V28=1, DETECT_V1=2); a
  // detected 2.28 still runs the DOMO/V3 protocol (generation=1), this label is for display only.
  const char* verFrameLabel = m.version_profile == 0 ? "V3" : (m.version_profile == 1 ? "V28" : (m.version_profile == 2 ? "V1" : "?"));
  j += "\"version_frame\":\"" + String(verFrameLabel) + "\",";
  j += "\"generation\":" + String(m.generation) + ",";
  // USB link diagnostics: host_connected = a USB host (the stove) has enumerated the bridge on its native USB port;
  // rx_bytes = bytes received from the stove since boot. Both stay false / 0 when the stove is plugged into the
  // board's UART/COM port, when the cable has no data wires, or when the stove is off.
  j += "\"usb\":{\"host_connected\":" + String((bool)USB ? "true" : "false") + ",\"rx_bytes\":" + String(m.rx_bytes) + "},";
  j += "\"mqtt\":{\"enabled\":" + String(g_mqttCfg.enabled ? "true" : "false") + ",\"connected\":" + String(g_mqttConnected ? "true" : "false") + "},";
  j += jsonHealth(false) + ",";
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
    j += "\"" + jsonEscape(String(kv.first.c_str())) + "\":\"" + jsonEscape(val) + "\"";
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

static void handleState()   { web.send(200, "application/json", jsonState()); }
static void handleVersion() {
  char buf[220];
  snprintf(buf, sizeof(buf),
    "{\"app\":\"Open Firenet\",\"version\":\"" OPENFIRENET_VERSION "\",\"build_date\":\"%s\",\"build_time\":\"%s\",\"target\":\"ESP32-S3\"}",
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

// ---------------------------------------------------------------- bridge health (issue #74)
// Cause of the last restart. The chip tells power-on, brown-out, crash and watchdog by itself; for a restart the
// firmware asks for, it only says "software". The reason is then left in memory that survives a software restart
// but not a power cut (RTC, no flash wear), with a marker so that leftovers are never read as a reason.
enum RestartNote : uint32_t { NOTE_NONE = 0, NOTE_USER, NOTE_WIFI_CHANGE, NOTE_UPDATE, NOTE_STOVE_SILENT };
static const uint32_t RESTART_NOTE_MAGIC = 0x0F17E7A5;
RTC_NOINIT_ATTR static uint32_t g_restartNoteMagic;
RTC_NOINIT_ATTR static uint32_t g_restartNote;
static const char* g_restartReason = "unknown";
static volatile uint32_t g_wifiDisconnects = 0;   // Wi-Fi drops after a successful connection, since boot
static volatile bool     g_wifiUp = false;
static volatile uint32_t g_mqttDisconnects = 0;   // broker connections lost, since boot

// (uint32_t, not RestartNote: the Arduino build puts its generated prototypes above the enum)
static void noteRestart(uint32_t note) { g_restartNote = note; g_restartNoteMagic = RESTART_NOTE_MAGIC; }
static void restartBecause(uint32_t note) { noteRestart(note); ESP.restart(); }

// Called once at boot, before anything can ask for a restart.
static void readRestartReason() {
  uint32_t note = (g_restartNoteMagic == RESTART_NOTE_MAGIC) ? g_restartNote : (uint32_t)NOTE_NONE;
  g_restartNoteMagic = 0; g_restartNote = NOTE_NONE;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  g_restartReason = "power_on"; break;
    case ESP_RST_BROWNOUT: g_restartReason = "brownout"; break;
    case ESP_RST_PANIC:    g_restartReason = "crash"; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      g_restartReason = "watchdog"; break;
    case ESP_RST_EXT:      g_restartReason = "reset_button"; break;
    case ESP_RST_USB:      g_restartReason = "usb"; break;
    case ESP_RST_SW:
      g_restartReason = note == NOTE_USER ? "user" : note == NOTE_WIFI_CHANGE ? "wifi_change"
                      : note == NOTE_UPDATE ? "update" : note == NOTE_STOVE_SILENT ? "stove_silent" : "software";
      break;
    default:               g_restartReason = "unknown"; break;
  }
}

// The "health" object of /api/state. `forMqtt` leaves out the value that changes every second (age of the last
// frame) and rounds the temperature, so that the published state only changes when something happened.
static String jsonHealth(bool forMqtt) {
  const auto& m = g_link->model();
  float t = temperatureRead();
  String j = "\"health\":{";
  j += "\"restart_reason\":\"" + String(g_restartReason) + "\",";
  j += "\"min_free_heap\":" + String((unsigned)ESP.getMinFreeHeap()) + ",";
  j += "\"chip_temperature\":" + (isnan(t) ? String("null") : (forMqtt ? String((int)lroundf(t)) : String(t, 1))) + ",";
  j += "\"wifi_disconnects\":" + String((unsigned)g_wifiDisconnects) + ",";
  if (!forMqtt) {
    j += "\"stove_last_frame_seconds\":" + (m.rx_bytes == 0 ? String("null") : String((millis() - m.last_rx_ms) / 1000UL)) + ",";
  }
  j += "\"stove_detections\":" + String(m.detections) + ",";
  j += "\"stove_link_losses\":" + String(m.link_losses) + ",";
  j += "\"mqtt_disconnects\":" + String((unsigned)g_mqttDisconnects);
  j += "}";
  return j;
}

static void handleRestart() {
  web.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
  delay(300); restartBecause(NOTE_USER);
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
    "<title>Open Firenet</title></head>"
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
  delay(300); restartBecause(NOTE_WIFI_CHANGE);
}
// Optional password for wireless updates (issue #77). It can only be set or removed over the USB serial port
// (SETOTAPASS, sent by the installer), never over the network: whoever holds the bridge decides, and a device on
// the network cannot set one first to lock the owner out. Only its SHA-256 is kept, which is what the update
// library works with. A full flash over USB erases it.
static void loadOtaPassword() {
  prefs.begin("firenet", true);
  g_otaHash = prefs.getString("ota_hash", "");
  prefs.end();
  if (g_otaHash.length() != 64) g_otaHash = "";
}
static String sha256Hex(const String& text) {
  uint8_t digest[32];
  mbedtls_sha256((const unsigned char*)text.c_str(), text.length(), digest, 0);
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  return String(hex);
}

// POST /api/forget -> efface le WiFi, repasse en AP au prochain boot
static void handleForget() {
  // The update password stays: erasing it from the network would defeat it.
  prefs.begin("firenet", false); prefs.clear();
  if (g_otaHash.length() == 64) prefs.putString("ota_hash", g_otaHash);
  prefs.end();
  web.send(200,"application/json","{\"ok\":true}");
  delay(300); restartBecause(NOTE_WIFI_CHANGE);
}

// Option C — provisioning par commande série (UART0 DBG et CDC TinyUSB STOVE) :
//   SETWIFI:<ssid>:<password>
// Le SSID s'arrête au premier ':' ; tout le reste est le mot de passe (donc un
// mot de passe contenant ':' est accepté). Enregistre en NVS puis redémarre en STA.
//   SETOTAPASS:<password>   sets the password asked for wireless updates; an empty password removes it.
static bool applySetOtaPass(const String& line, Print& out) {
  if (!line.startsWith("SETOTAPASS:")) return false;
  String pass = line.substring(11);
  prefs.begin("firenet", false);
  if (pass.length() > 0) prefs.putString("ota_hash", sha256Hex(pass)); else prefs.remove("ota_hash");
  prefs.end();
  const char* what = pass.length() > 0 ? "password set" : "password removed";
  DBG.printf("[ota] SETOTAPASS OK %s -> reboot\n", what);
  if ((Print*)&out != (Print*)&DBG) { out.printf("[ota] SETOTAPASS OK %s -> reboot\r\n", what); out.flush(); }
  delay(200); restartBecause(NOTE_USER);
  return true;
}

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
      delay(200); restartBecause(NOTE_WIFI_CHANGE);
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
        if (!applySetOtaPass(dbgLine, DBG)) applySetWifi(dbgLine, DBG);
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
        if (!applySetOtaPass(stoveLine, STOVE)) applySetWifi(stoveLine, STOVE);
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
  // With a saved network the bridge keeps trying to join it while the setup access point is up: after a power
  // cut the home router takes minutes to come back, long after the bridge has started (issue #82). Without one
  // (first setup), only the access point.
  WiFi.mode(wifiSsid.length() ? WIFI_AP_STA : WIFI_AP);
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
    json += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
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
// 32 KB: the ring is reserved in RAM at boot, so it is taken from the free memory at all times. 96 KB left a DOMO
// 2.29 with only ~25 KB of free heap at its lowest; 48 KB left 14 KB during a TLS connection to an MQTT broker.
// Repeated lines are merged, so 32 KB still holds a few minutes of normal traffic.
static const size_t LOG_RING_BYTES = 32 * 1024;
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

// The API answers its own page and clients that are not browsers; a page of another website gets a 403 before
// any handler runs (see firenet_web_guard.h, issue #77). No Access-Control-Allow-Origin is sent any more.
// Domains added by the owner in the Bridge tab (section Access), kept in the settings.
static std::vector<std::string> g_extraHosts;
static void applyWifiPowerSaving() {
  WiFi.setSleep(g_wifiPowerSaving ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
}
static void loadExtraHosts() {
  prefs.begin("firenet", true);
  g_extraHosts = firenet::webguard::parseExtraHosts(prefs.getString("web_hosts", "").c_str());
  prefs.end();
}

static bool webGuard(WebServer& server, Middleware::Callback next) {
  std::string path = server.uri().c_str();
  std::string host = server.hostHeader().c_str();
  std::string origin = server.header("Origin").c_str();
  if (firenet::webguard::requestAllowed(path, host, origin, g_extraHosts)) return next();
  DBG.printf("[web] refused %s (Host \"%s\", Origin \"%s\")\n", path.c_str(), host.c_str(), origin.c_str());
  std::string ip = (g_isApMode ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str();
  server.send(403, "application/json", firenet::webguard::refusalJson(host, origin, ip, g_extraHosts).c_str());
  return true;
}

// GET /api/access: the domains added by the owner, and the name this request was addressed to.
// POST /api/access: extra_hosts (JSON or form), free text with one domain per line; an empty text removes them all.
// GET/POST /api/wifi_power_saving -> the setting above; a change applies at once, without a restart.
static void handleWifiPowerSaving() {
  if (web.method() == HTTP_POST) {
    std::vector<std::pair<std::string, std::string>> kv;
    firenet::jsonPairs(web.hasArg("plain") ? web.arg("plain").c_str() : "", kv);
    for (int i = 0; i < web.args(); i++) if (web.argName(i) != "plain") kv.push_back({web.argName(i).c_str(), web.arg(i).c_str()});
    for (const auto& p : kv) if (p.first == "enabled") {
      g_wifiPowerSaving = (p.second == "true" || p.second == "1");
      prefs.begin("firenet", false);
      prefs.putBool("wifi_ps", g_wifiPowerSaving);
      prefs.end();
      applyWifiPowerSaving();
    }
  }
  web.send(200, "application/json", String("{\"ok\":true,\"enabled\":") + (g_wifiPowerSaving ? "true" : "false") + "}");
}

static void handleAccess() {
  if (web.method() == HTTP_POST) {
    std::vector<std::pair<std::string, std::string>> kv;
    firenet::jsonPairs(web.hasArg("plain") ? web.arg("plain").c_str() : "", kv);
    for (int i = 0; i < web.args(); i++) if (web.argName(i) != "plain") kv.push_back({web.argName(i).c_str(), web.arg(i).c_str()});
    for (const auto& p : kv) if (p.first == "extra_hosts") {
      String text = p.second.c_str();
      text.replace("\\n", "\n"); text.replace("\\r", "");          // JSON carries the line breaks as "\n"
      auto hosts = firenet::webguard::parseExtraHosts(text.c_str());
      prefs.begin("firenet", false);
      prefs.putString("web_hosts", firenet::webguard::joinExtraHosts(hosts).c_str());
      prefs.end();
      g_extraHosts = hosts;
    }
  }
  String j = "{\"ok\":true,\"extra_hosts\":[";
  for (size_t i = 0; i < g_extraHosts.size(); i++) j += String(i ? "," : "") + "\"" + g_extraHosts[i].c_str() + "\"";
  j += "],\"max_extra_hosts\":" + String((unsigned)firenet::webguard::MAX_EXTRA_HOSTS);
  j += ",\"host\":\"" + jsonEscape(String(firenet::webguard::hostName(web.hostHeader().c_str()).c_str())) + "\"}";
  web.send(200, "application/json", j);
}

// Applies commanded name/value pairs (any name accepted by firenet_api.h) to the stove; shared by the REST API and
// MQTT. The base settings it ends up sending are returned for the answer.
static AppliedControls applyControlPairs(const std::vector<std::pair<std::string, std::string>>& pairs) {
  const auto& m = g_link->model();
  // A command is the stove's current settings plus the commanded ones: nothing is sent before the stove has posted
  // its settings under their names (a few seconds after the link comes up), see Model::controls_synced.
  if (!m.version_ack || !m.controls_synced) return {0, 0, 0, 0, false};
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
  return {finalOn, finalMode, finalStage, finalRoom, true};
}

// GET /api/controls & POST /api/controls (V2 JSON + legacy compat)
static void handleApiControls() {
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

  // Commands only on POST and PUT. Any other method is refused: a browser sends HEAD, for one, without saying
  // which page asks, so it would get past the check on the page of origin (issue #77).
  if (web.method() != HTTP_POST && web.method() != HTTP_PUT) {
    web.sendHeader("Allow", "GET, POST, PUT");
    web.send(405, "application/json", "{\"ok\":false,\"error\":\"method_not_allowed\"}");
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
  const AppliedControls r = applyControlPairs(pairs);
  if (!r.sent) {
    web.sendHeader("Retry-After", "5");
    web.send(503, "application/json", "{\"ok\":false,\"error\":\"stove_not_ready\",\"message\":\"The stove is not linked, or has not sent its settings yet. Nothing was sent; try again in a few seconds.\"}");
    return;
  }
  long finalOn = r.on, finalMode = r.mode, finalStage = r.stage, finalRoom = r.room;

  const char* modeName = (finalMode == 0) ? "manual" : ((finalMode == 1) ? "auto" : "comfort");
  char resBuf[256];
  snprintf(resBuf, sizeof(resBuf),
    "{\"ok\":true,\"on\":%s,\"mode\":\"%s\",\"mode_code\":%ld,\"target_temperature\":%.1f,\"power_percent\":%ld}",
    finalOn == 1 ? "true" : "false", modeName, finalMode, finalRoom / 10.0f, finalStage);
  web.send(200, "application/json", resBuf);
}

// ------------------------------------------------------------------------ MQTT
// Optional, off by default (issue #52). Topics, under a configurable base ("openfirenet"):
//   <base>/availability        online / offline (retained, last will)
//   <base>/state               JSON: the "device", "stove", "sensors" and "controls" objects of /api/state (retained)
//   <base>/<section>/<name>    the same values, one per topic (retained), e.g. <base>/sensors/room_temperature
//   <base>/set                 command: JSON or "k=v;" text, as POST /api/controls
//   <base>/set/<name>          command: one value, e.g. <base>/set/target_temperature 21
// The client is esp-mqtt. It runs in its own task (connection, reconnection every 10 s, keep-alive), so a broker
// that is down or slow never holds the main loop, which also runs the stove link. The two sides meet in two places:
// outgoing messages are put in g_mqttOutbox and sent by a small publisher task, and incoming commands are put in
// g_mqttInbox by the client's task and applied by the main loop.
// (esp_mqtt_client_enqueue() is not used: the client sends about one queued message per second and drops those
// older than 30 s, which lost most of a full refresh on a real stove. esp_mqtt_client_publish() sends at once but
// may wait on a stalled broker, hence the separate task.)
static esp_mqtt_client_handle_t g_mqttClient = nullptr;
static volatile bool g_mqttFresh = false;     // a session just opened: everything is published again
static volatile int g_mqttError = 0;          // 0 none, -1 broker unreachable, -3 connection lost, > 0 refusal code
static bool g_mqttReload = false;             // settings changed: restart the client with the new ones
static std::string g_mqttBase;                // base topic of the running client
static std::string g_mqttCa;                  // authority certificate of the running client (TLS)
static volatile int g_mqttTlsError = 0;       // last error code of the TLS layer (mbedTLS), 0 when none
static std::mutex g_mqttMx;
static std::deque<MqttCommand> g_mqttInbox;
static std::deque<std::pair<std::string, std::string>> g_mqttOutbox;   // topic, payload (all retained, QoS 0)
// The queue is limited in bytes: a full refresh (about 50 values and 25 discovery messages, 30 kB in all) used to
// be queued at once, which left a DOMO with less than 2 kB of free memory right after a TLS connection.
static const size_t MQTT_OUTBOX_MAX_BYTES = 3072;
static size_t g_mqttOutboxBytes = 0;
// Queues a message (g_mqttMx held by the caller). An empty queue takes any message, however long.
static bool mqttOutboxPush(std::string topic, std::string payload) {
  size_t n = topic.size() + payload.size();
  if (!g_mqttOutbox.empty() && g_mqttOutboxBytes + n > MQTT_OUTBOX_MAX_BYTES) return false;
  g_mqttOutboxBytes += n;
  g_mqttOutbox.push_back({std::move(topic), std::move(payload)});
  return true;
}
static std::mutex g_mqttClientMx;             // held while publishing, and while the client is stopped and destroyed
static TaskHandle_t g_mqttPublisher = nullptr;
static std::map<std::string, std::string> g_mqttSent;   // last value published per topic
static String g_mqttLastState;
static uint32_t g_mqttLastStateMs = 0;
// Home Assistant discovery (optional): the configuration messages are sent a few at a time after each connection,
// and again when the stove model becomes known. g_mqttDiscRemove sends the empty messages that delete the entities,
// once, after discovery was switched off.
static size_t g_mqttDiscNext = SIZE_MAX;      // next entity to announce; SIZE_MAX = nothing to send
static bool g_mqttDiscRemove = false;
static String g_mqttDiscModel;                // model name the entities were announced with

// Runs in the client's task: no access to the stove link from here.
static void mqttEvent(void*, esp_event_base_t, int32_t id, void* data) {
  static std::string rxTopic, rxData;
  static bool rxRetained = false;
  auto* e = (esp_mqtt_event_handle_t)data;
  if (e->client != g_mqttClient) return;                     // a client being stopped (settings changed)
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
      esp_mqtt_client_publish(e->client, (g_mqttBase + "/availability").c_str(), "online", 0, 1, 1);
      esp_mqtt_client_subscribe(e->client, (g_mqttBase + "/set").c_str(), 1);
      esp_mqtt_client_subscribe(e->client, (g_mqttBase + "/set/#").c_str(), 1);
      g_mqttError = 0; g_mqttTlsError = 0; g_mqttFresh = true; g_mqttConnected = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (g_mqttConnected) { g_mqttError = -3; g_mqttDisconnects++; }
      g_mqttConnected = false;
      break;
    case MQTT_EVENT_ERROR:
      // The code of mbedTLS, as a positive number (it is reported so on a DOMO: 0x3000, 0x7280).
      g_mqttTlsError = e->error_handle ? abs(e->error_handle->esp_tls_stack_err) : 0;
      if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        g_mqttError = (int)e->error_handle->connect_return_code;
      } else if (g_mqttCfg.tls && e->error_handle &&
                 (e->error_handle->esp_tls_cert_verify_flags != 0 || g_mqttTlsError == 0x2700 || g_mqttTlsError == 0x3000)) {
        // Certificate verification failed (0x2700), or refused by the check against the public authorities
        // (0x3000, measured on a DOMO with a broker signed by an authority of its own and none given).
        g_mqttError = -5;
      } else if (g_mqttCfg.tls && g_mqttTlsError != 0) {
        g_mqttError = -6;                                        // the TLS dialogue failed for another reason (0x7280: plain port)
      } else {
        g_mqttError = -1;
      }
      break;
    case MQTT_EVENT_DATA: {
      // A message larger than the client's buffer arrives in several events; the topic comes with the first one.
      if (e->current_data_offset == 0) { rxTopic.assign(e->topic, e->topic_len); rxData.clear(); rxRetained = e->retain; }
      if (e->total_data_len > 1024) break;                       // no command is that long
      rxData.append(e->data, e->data_len);
      if ((int)rxData.size() < e->total_data_len) break;
      // A retained command would be replayed at every reconnection (e.g. switch the stove on again): ignored.
      if (rxRetained) break;
      std::lock_guard<std::mutex> lk(g_mqttMx);
      if (g_mqttInbox.size() < 8) g_mqttInbox.push_back({rxTopic, rxData});
      break;
    }
    default: break;
  }
}

// Publisher task: sends what the main loop queued. A message that cannot be sent empties the queue and asks for a
// full refresh, so the "last value published" table never claims something that did not go out.
static void mqttPublisherTask(void*) {
  for (;;) {
    std::pair<std::string, std::string> msg;
    bool have = false;
    {
      std::lock_guard<std::mutex> lk(g_mqttMx);
      if (!g_mqttOutbox.empty()) {
        msg = std::move(g_mqttOutbox.front()); g_mqttOutbox.pop_front(); have = true;
        g_mqttOutboxBytes -= msg.first.size() + msg.second.size();
      }
    }
    if (!have) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    bool sent = false;
    {
      std::lock_guard<std::mutex> lk(g_mqttClientMx);
      if (g_mqttClient && g_mqttConnected)
        sent = esp_mqtt_client_publish(g_mqttClient, msg.first.c_str(), msg.second.data(), (int)msg.second.size(), 0, 1) >= 0;
    }
    if (!sent) {
      std::lock_guard<std::mutex> lk(g_mqttMx);
      g_mqttOutbox.clear(); g_mqttOutboxBytes = 0;
      g_mqttFresh = true;
    }
  }
}

static void mqttApply(const MqttCommand& c) {
  std::string name;
  if (!firenet::mqtt::commandTopic(g_mqttBase, c.topic, name)) return;
  std::vector<std::pair<std::string, std::string>> pairs;
  if (!name.empty()) pairs.push_back({name, c.payload});
  else if (c.payload.find('{') != std::string::npos) firenet::jsonPairs(c.payload, pairs);
  else firenet::textPairs(c.payload, pairs);
  if (firenet::parseControlCommands(pairs).empty()) {
    DBG.printf("[mqtt] nothing to apply in %s\n", c.topic.c_str());
    return;
  }
  if (!applyControlPairs(pairs).sent) {
    DBG.println("[mqtt] command ignored: the stove is not linked or has not sent its settings yet");
    return;
  }
  DBG.printf("[mqtt] command applied from %s\n", c.topic.c_str());
}

static void mqttLoadSettings() {
  prefs.begin("firenet", true);
  g_mqttCfg.enabled = prefs.getBool("mq_on", false);
  g_mqttCfg.host = prefs.getString("mq_host", "");
  g_mqttCfg.port = prefs.getUShort("mq_port", 1883);
  g_mqttCfg.user = prefs.getString("mq_user", "");
  g_mqttCfg.pass = prefs.getString("mq_pass", "");
  g_mqttCfg.base = prefs.getString("mq_base", "openfirenet");
  g_mqttCfg.discovery = prefs.getBool("mq_disc", false);
  g_mqttCfg.tls = prefs.getBool("mq_tls", false);
  g_mqttCfg.ca = prefs.getString("mq_ca", "");
  prefs.end();
  if (g_mqttCfg.base.isEmpty()) g_mqttCfg.base = "openfirenet";
}

static void mqttStart() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char id[32]; snprintf(id, sizeof id, "open-firenet-%02x%02x%02x", mac[3], mac[4], mac[5]);
  g_mqttBase = g_mqttCfg.base.c_str();
  const std::string will = g_mqttBase + "/availability";
  esp_mqtt_client_config_t c = {};                           // every string is copied by esp_mqtt_client_init()
  c.broker.address.hostname = g_mqttCfg.host.c_str();
  c.broker.address.port = g_mqttCfg.port;
  c.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  if (g_mqttCfg.tls) {
    c.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
    if (g_mqttCfg.ca.isEmpty()) {
      // No certificate given: the broker must present one signed by a public authority, for the name it is
      // reached by (a hosted broker, or a home broker with a Let's Encrypt certificate).
      c.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    } else {
      // A home broker with its own authority: its certificate must be signed by the one given here. The name is
      // not checked, as such a broker is usually reached by its IP address, which its certificate does not carry.
      g_mqttCa = g_mqttCfg.ca.c_str();                       // kept alive: the client does not copy it
      c.broker.verification.certificate = g_mqttCa.c_str();
      c.broker.verification.skip_cert_common_name_check = true;
    }
    c.task.stack_size = 8192;                                // the TLS handshake runs in the client's task
  }
  c.credentials.client_id = id;
  if (!g_mqttCfg.user.isEmpty()) {
    c.credentials.username = g_mqttCfg.user.c_str();
    if (!g_mqttCfg.pass.isEmpty()) c.credentials.authentication.password = g_mqttCfg.pass.c_str();
  }
  c.session.last_will.topic = will.c_str();
  c.session.last_will.msg = "offline";
  c.session.last_will.qos = 1;
  c.session.last_will.retain = 1;
  c.session.keepalive = 30;
  // A TLS connection attempt takes about 30 kB of memory for a few seconds (free memory seen as low as 12 kB on a
  // DOMO while a refused broker was retried every 10 s): the attempts are spaced out.
  c.network.reconnect_timeout_ms = g_mqttCfg.tls ? 30000 : 10000;
  c.network.timeout_ms = g_mqttCfg.tls ? 6000 : 3000;       // a TLS handshake takes a few seconds on this chip
  c.buffer.size = 1024;                                      // longer messages are sent in several parts
  g_mqttError = 0; g_mqttConnected = false;
  g_mqttClient = esp_mqtt_client_init(&c);
  if (!g_mqttClient) { DBG.println("[mqtt] client init failed"); return; }
  esp_mqtt_client_register_event(g_mqttClient, MQTT_EVENT_ANY, mqttEvent, nullptr);
  esp_mqtt_client_start(g_mqttClient);
  if (!g_mqttPublisher) xTaskCreate(mqttPublisherTask, "mqtt-publish", 4096, nullptr, 1, &g_mqttPublisher);
  DBG.printf("[mqtt] client started, broker %s:%u\n", g_mqttCfg.host.c_str(), (unsigned)g_mqttCfg.port);
}

// Stops and frees a client in a task of its own: stopping waits for the client's task to end, which takes up to the
// network timeout when a connection attempt is in progress (6 s with TLS). Done in the main loop, that froze the
// web page and the dialogue with the stove for as long.
static volatile bool g_mqttStopping = false;
static void mqttStopTask(void* arg) {
  esp_mqtt_client_handle_t old = (esp_mqtt_client_handle_t)arg;
  esp_mqtt_client_stop(old);
  esp_mqtt_client_destroy(old);
  g_mqttStopping = false;                                    // a new client may start: the memory is back
  vTaskDelete(nullptr);
}

// Only called when the settings change.
static void mqttStop() {
  if (!g_mqttClient) return;
  esp_mqtt_client_handle_t old;
  {
    std::lock_guard<std::mutex> lk(g_mqttClientMx);
    if (g_mqttConnected) esp_mqtt_client_publish(g_mqttClient, (g_mqttBase + "/availability").c_str(), "offline", 0, 1, 1);
    old = g_mqttClient;
    g_mqttClient = nullptr; g_mqttConnected = false;
    g_mqttError = 0; g_mqttTlsError = 0;                     // the status of the previous settings no longer applies
  }
  {
    std::lock_guard<std::mutex> lk(g_mqttMx);
    g_mqttInbox.clear(); g_mqttOutbox.clear(); g_mqttOutboxBytes = 0;
  }
  g_mqttStopping = true;
  if (xTaskCreate(mqttStopTask, "mqtt-stop", 4096, old, 1, nullptr) != pdPASS) {
    esp_mqtt_client_stop(old); esp_mqtt_client_destroy(old); g_mqttStopping = false;   // no task: do it here
  }
}

static const char* mqttStatus() {
  if (!g_mqttCfg.enabled || g_mqttCfg.host.isEmpty()) return "disabled";
  if (g_mqttConnected) return "connected";
  if (WiFi.status() != WL_CONNECTED) return "waiting for Wi-Fi";
  switch (g_mqttError) {
    case 0: return "connecting";
    case -1: return "broker unreachable";
    case -3: return "connection lost";
    case -5: return "certificate not trusted";
    case -6: return "secure connection failed";
    case 4: case 5: return "refused: wrong user or password";
    default: return "refused by the broker";
  }
}

static bool mqttQueue(const std::string& leaf, const char* data, size_t len) {
  std::lock_guard<std::mutex> lk(g_mqttMx);
  return mqttOutboxPush(g_mqttBase + "/" + leaf, std::string(data, len));   // false: tried again at the next pass
}

// Queues up to four discovery messages per call (see g_mqttDiscNext).
static void mqttDiscoveryStep() {
  if (g_mqttDiscNext == SIZE_MAX) return;
  uint8_t mac[6]; WiFi.macAddress(mac);
  char id[32]; snprintf(id, sizeof id, "openfirenet_%02x%02x%02x", mac[3], mac[4], mac[5]);
  firenet::mqtt::DiscoveryDevice dev;
  dev.id = id;
  dev.base = g_mqttBase;
  dev.model = g_mqttDiscModel.c_str();
  dev.version = OPENFIRENET_VERSION;
  dev.url = std::string("http://") + WiFi.localIP().toString().c_str();
  std::string topic, payload;
  for (int n = 0; n < 4; n++) {
    if (!firenet::mqtt::discoveryEntity(g_mqttDiscNext, dev, g_mqttDiscRemove, topic, payload)) {
      g_mqttDiscNext = SIZE_MAX; g_mqttDiscRemove = false;
      return;
    }
    std::lock_guard<std::mutex> lk(g_mqttMx);
    if (!mqttOutboxPush(topic, payload)) return;                   // tried again at the next pass
    g_mqttDiscNext++;
  }
}

// Queues what changed: the JSON state at most every 5 s (and at least every 60 s), then up to 25 single values per
// call, so a full refresh is spread over a few seconds.
static void mqttPublish() {
  const auto& m = g_link->model();
  bool linked = m.version_ack && !m.sensors_pos.empty();
  if (g_mqttCfg.discovery) {
    // Announce again once the stove has told its model, so that the device shows it.
    long modelId = sensorValue(m, "model", -1);
    String model = (linked && modelId >= 0) ? String(getStoveModelName(modelId)) : String();
    if (model != g_mqttDiscModel && !g_mqttDiscRemove) { g_mqttDiscModel = model; g_mqttDiscNext = 0; }
  }
  mqttDiscoveryStep();
  char dev[160];
  snprintf(dev, sizeof dev, "{\"device\":{\"version\":\"" OPENFIRENET_VERSION "\",\"connected\":%s,\"ip\":\"%s\"}",
           linked ? "true" : "false", WiFi.localIP().toString().c_str());
  // Before the stove is linked its values are not known: only the "device" object is published.
  String state = String(dev) + "," + jsonHealth(true) + (linked ? "," + jsonStoveSections() : String()) + "}";
  uint32_t now = millis();
  bool changed = state != g_mqttLastState;
  if ((changed && (g_mqttLastState.isEmpty() || now - g_mqttLastStateMs >= 5000)) || now - g_mqttLastStateMs >= 60000) {
    if (!mqttQueue("state", state.c_str(), state.length())) return;
    g_mqttLastState = state; g_mqttLastStateMs = now;
  }
  std::vector<std::pair<std::string, std::string>> values;
  firenet::mqtt::flattenSections(state.c_str(), values);
  int budget = 25;
  for (const auto& v : values) {
    auto it = g_mqttSent.find(v.first);
    if (it != g_mqttSent.end() && it->second == v.second) continue;
    if (!mqttQueue(v.first, v.second.data(), v.second.size())) return;
    g_mqttSent[v.first] = v.second;
    if (--budget == 0) break;
  }
}

static void mqttLoop() {
  if (g_mqttReload) {
    g_mqttReload = false;
    bool hadDiscovery = g_mqttCfg.discovery;
    mqttStop();
    mqttLoadSettings();
    // Discovery switched off: the entities are deleted from Home Assistant at the next connection.
    if (hadDiscovery && !g_mqttCfg.discovery) { g_mqttDiscRemove = true; g_mqttDiscNext = 0; }
  }
  if (!g_mqttClient) {
    if (g_mqttStopping) return;                              // the previous client is still being freed
    // Started once Wi-Fi is up; from then on the client reconnects by itself.
    if (g_mqttCfg.enabled && !g_mqttCfg.host.isEmpty() && !g_isApMode && WiFi.status() == WL_CONNECTED) mqttStart();
    return;
  }
  for (;;) {
    MqttCommand c;
    {
      std::lock_guard<std::mutex> lk(g_mqttMx);
      if (g_mqttInbox.empty()) break;
      c = g_mqttInbox.front(); g_mqttInbox.pop_front();
    }
    mqttApply(c);
  }
  if (g_mqttFresh) {
    g_mqttFresh = false; g_mqttSent.clear(); g_mqttLastState = ""; DBG.println("[mqtt] full refresh");
    if (g_mqttCfg.discovery) { g_mqttDiscNext = 0; g_mqttDiscRemove = false; }
    else if (!g_mqttDiscRemove) g_mqttDiscNext = SIZE_MAX;
  }
  static uint32_t lastPublish = 0;
  if (g_mqttConnected && millis() - lastPublish >= 1000) { lastPublish = millis(); mqttPublish(); }
}

static String jsonEscape(const String& in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

// GET /api/mqtt: settings (the password is never returned) and connection status.
// POST /api/mqtt: enabled, host, port, user, password, base_topic, discovery, tls, ca_certificate (JSON or form). A field left out keeps its value.
static void handleMqtt() {
  if (web.method() == HTTP_OPTIONS) { web.send(204); return; }
  if (web.method() == HTTP_POST) {
    std::vector<std::pair<std::string, std::string>> kv;
    firenet::jsonPairs(web.hasArg("plain") ? web.arg("plain").c_str() : "", kv);
    for (int i = 0; i < web.args(); i++) if (web.argName(i) != "plain") kv.push_back({web.argName(i).c_str(), web.arg(i).c_str()});
    // The certificate is checked first: nothing is saved when it is refused.
    bool hasCa = false; String pem;
    for (const auto& p : kv) if (p.first == "ca_certificate") {
      // JSON carries the line breaks of the PEM text as "\n"; an empty value erases the certificate.
      hasCa = true; pem = p.second.c_str();
      pem.replace("\\n", "\n"); pem.replace("\\r", ""); pem.replace("\r", ""); pem.trim();
    }
    if (hasCa && !pem.isEmpty() && !(pem.startsWith("-----BEGIN CERTIFICATE-----") && pem.endsWith("-----END CERTIFICATE-----") && pem.length() < 3800)) {
      web.send(400, "application/json", "{\"ok\":false,\"error\":\"ca_certificate is not a PEM certificate (or is longer than 3800 characters)\"}");
      return;
    }
    prefs.begin("firenet", false);
    if (hasCa) prefs.putString("mq_ca", pem);
    for (const auto& p : kv) {
      String v = p.second.c_str(); v.trim();
      if (p.first == "enabled") prefs.putBool("mq_on", v == "true" || v == "1" || v == "on");
      else if (p.first == "host") prefs.putString("mq_host", v.substring(0, 95));
      else if (p.first == "port") { long n = v.toInt(); prefs.putUShort("mq_port", (n > 0 && n < 65536) ? (uint16_t)n : 1883); }
      else if (p.first == "user") prefs.putString("mq_user", v.substring(0, 64));
      else if (p.first == "password") prefs.putString("mq_pass", String(p.second.c_str()).substring(0, 64));
      else if (p.first == "discovery") prefs.putBool("mq_disc", v == "true" || v == "1" || v == "on");
      else if (p.first == "tls") prefs.putBool("mq_tls", v == "true" || v == "1" || v == "on");
      else if (p.first == "base_topic") {
        while (v.endsWith("/")) v.remove(v.length() - 1);
        if (v.indexOf('#') < 0 && v.indexOf('+') < 0) prefs.putString("mq_base", v.isEmpty() ? String("openfirenet") : v.substring(0, 64));
      }
    }
    prefs.end();
    g_mqttReload = true;
  }
  // Right after a POST the answer shows the saved settings; the status follows at the next GET.
  MqttSettings c = g_mqttCfg;
  if (g_mqttReload) {
    prefs.begin("firenet", true);
    c.enabled = prefs.getBool("mq_on", false); c.host = prefs.getString("mq_host", ""); c.port = prefs.getUShort("mq_port", 1883);
    c.user = prefs.getString("mq_user", ""); c.pass = prefs.getString("mq_pass", ""); c.base = prefs.getString("mq_base", "openfirenet");
    c.discovery = prefs.getBool("mq_disc", false);
    c.tls = prefs.getBool("mq_tls", false); c.ca = prefs.getString("mq_ca", "");
    prefs.end();
  }
  String j = "{\"enabled\":" + String(c.enabled ? "true" : "false") + ",\"host\":\"" + jsonEscape(c.host) + "\",\"port\":" + String(c.port)
    + ",\"user\":\"" + jsonEscape(c.user) + "\",\"password_set\":" + String(c.pass.isEmpty() ? "false" : "true")
    + ",\"base_topic\":\"" + jsonEscape(c.base) + "\",\"discovery\":" + String(c.discovery ? "true" : "false")
    + ",\"tls\":" + String(c.tls ? "true" : "false") + ",\"ca_set\":" + String(c.ca.isEmpty() ? "false" : "true")
    + ",\"tls_error\":" + String(g_mqttReload ? 0 : (int)g_mqttTlsError) + ",\"connected\":" + String(!g_mqttReload && g_mqttConnected ? "true" : "false")
    + ",\"status\":\"" + String(g_mqttReload ? (c.enabled && !c.host.isEmpty() ? "connecting" : "disabled") : mqttStatus()) + "\"}";
  web.send(200, "application/json", j);
}

// GET & POST /api/schedule
static void handleApiSchedule() {
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
  readRestartReason();
  DBG.begin(115200);
  DBG.println("\n[Open Firenet] boot");
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
  g_wifiPowerSaving = prefs.getBool("wifi_ps", true);   // read before the Wi-Fi starts, see applyWifiPowerSaving
  prefs.end();

  if (wifiSsid.length()) {
    g_link->setCredentials(wifiSsid.c_str(), wifiPass.c_str());
    g_isApMode = false;
    g_staStart = millis();
    // Connexion STA robuste — méthode open-firenet (fonctionne en coexistence USB
    // natif TinyUSB) : TX power max, config bas niveau + connect
    // différé. WiFi.begin() seul échoue (status=6 / no assoc).
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    // Power saving as the owner chose it, through the library: on the "station started" event it applies its
    // own setting, which undoes a direct esp_wifi_set_ps() made here (issue #82).
    applyWifiPowerSaving();
    WiFi.mode(WIFI_STA);
    WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info){
      if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        DBG.printf("[wifi] DISCONNECTED reason=%d, next attempt in %u ms\n", info.wifi_sta_disconnected.reason, (unsigned)g_wifiRetryMs);
        if (g_wifiUp) { g_wifiUp = false; g_wifiDisconnects++; }
        uint32_t at = millis() + g_wifiRetryMs;
        g_wifiConnectAt = at ? at : 1;                       // 0 means "nothing scheduled"
        g_wifiRetryMs = g_wifiRetryMs < 5000 ? 5000 : 30000;
      } else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        g_staConnected = true; g_wifiUp = true;
        g_wifiRetryMs = 1000;
        if (g_isApMode) g_leaveApMode = true;                // done in loop(), not in the Wi-Fi event task
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
      // With a password, only an encrypted network is accepted: an open access point (or an old WEP one) that
      // shows the same name is not joined. Without a password the network is meant to be open.
      conf.sta.threshold.authmode = wifiPass.length() > 0 ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
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
  loadOtaPassword();
  if (g_otaHash.length() == 64) ArduinoOTA.setPasswordHash(g_otaHash.c_str());
  ArduinoOTA.onEnd([]() { noteRestart(NOTE_UPDATE); });   // the update library restarts the chip by itself
  ArduinoOTA.begin();

  if (MDNS.begin("open-firenet")) {
    MDNS.addService("http", "tcp", 80);
    DBG.println("[mdns] http://open-firenet.local");
  }

  web.on("/", handleRoot);
  web.on("/api/version", handleVersion);
  web.on("/api/state", handleState);
  web.on("/api/control", handleApiControls);
  web.on("/api/controls", handleApiControls);
  web.on("/api/schedule", handleApiSchedule);
  web.on("/api/restart", HTTP_POST, handleRestart);
  web.on("/api/txgap", handleTxGap);
  web.on("/api/mqtt", handleMqtt);
  web.on("/api/access", handleAccess);
  web.on("/api/wifi_power_saving", handleWifiPowerSaving);
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

  mqttLoadSettings();
  static const char* WEB_HEADERS[] = {"Origin"};
  web.collectHeaders(WEB_HEADERS, 1);
  loadExtraHosts();
  web.addMiddleware(webGuard);
  web.begin();
  DBG.println("[web] démarré (avec portail captif + compatibilité open-firenet)");
}


// ---------------------------------------------------------------------- loop
void loop() {
  // 0) connect WiFi différé (laisse le driver se poser après config bas niveau)
  if (g_wifiConnectAt && (int32_t)(millis() - g_wifiConnectAt) >= 0) {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
      g_wifiConnectAt = millis() + 3000;                     // a network scan is running (setup page): after it
    } else {
      g_wifiConnectAt = 0;
      esp_wifi_connect();
      DBG.println("[wifi] esp_wifi_connect()");
    }
  }
  // The home network answered while the setup access point was up: the bridge is back on it, setup mode ends.
  if (g_leaveApMode) {
    g_leaveApMode = false;
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    g_isApMode = false;
    DBG.printf("[wifi] back on the home network (%s): setup access point closed\n", WiFi.localIP().toString().c_str());
  }

  // Secours : si échec de connexion STA après 20s, basculer en AP pour permettre la configuration
  if (!g_isApMode && !g_staConnected && g_staStart && (millis() - g_staStart > 20000)) {
    DBG.println("[wifi] no connection after 20 s: setup access point started, still trying the saved network");
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
    delay(50); restartBecause(NOTE_STOVE_SILENT);
  }

  // 2) cycle de lecture périodique une fois la version acquittée
  if (g_link->model().version_ack && g_link->txIdle() && millis() - lastPoll > 2000) {
    lastPoll = millis();
    if (WiFi.status() == WL_CONNECTED) g_link->setRssi(WiFi.RSSI());  // RSSI réel (§7.4)

    if (g_link->induoDialect()) {
      // Firmware 2.26/2.27 (V1): names registered once, status every ~20s. A detected 2.28 runs the DOMO loop
      // below, the one a real LIVO 2.28 works with (see induoDialect()).
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
  mqttLoop();
}
