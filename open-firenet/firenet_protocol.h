// firenet_protocol.h — cœur du protocole CDC Open Firenet, sans dépendance Arduino.
// Chaque constante/fonction cite la section de PROTOCOL.md qui la démontre.
// Compilable et testable en g++ puis inclus tel quel par le firmware ESP32.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace firenet {

// ----------------------------------------------------------------- §4.1 filtre
inline bool byteAccepted(uint8_t b) {
  // (b > 0x1F ou b dans la liste) et b < 0x7F  — FUN_8001c284 / FUN_42008efc
  bool extra = (b==0x02||b==0x03||b==0x06||b==0x09||b==0x0A||b==0x0D||b==0x15);
  return (b > 0x1F || extra) && b < 0x7F;
}

// ------------------------------------------------------------- §4.2 / §12 / §5
static const size_t DONGLE_RX_SIZE = 0x1000;   // 4096, notre rôle = dongle
static const int    DT             = 1;        // Firenet V1: DT=1 (plain text SSID, no OTA fields)

// ------------------------------------------------------------- §5 champs status
// ordre exact sur le fil ; 't'=texte 'b'=u8 'w'=u16
struct Field { const char* name; char type; };
static const Field CDC_FIELDS[] = {
  {"monitoring",'b'},{"on_off",'b'},{"scan_command",'b'},{"init_command",'b'},
  {"initialised",'b'},{"symbol",'b'},{"error",'w'},{"bl_version",'w'},
  {"app_version",'w'},{"app_revision",'w'},{"spwf_version",'w'},{"rssi",'b'},
  {"id",'t'},{"token",'t'},{"protocol",'t'},{"ssid",'t'},{"wpa2",'t'},
  {"ip",'t'},{"mac",'t'},{"update_dialogue",'b'},{"ota_update_revision",'w'},
  {"ota_update_progress",'b'},{"ota_update_error",'b'},
};
static const int NUM_FIELDS = 19;              // Firenet V1: 19 fields (0 to 18, mac)

// ------------------------------------------------------------- §5.3 codec hexa
inline char hexNibble(int n) {                 // FUN_42009574 : '#' hors plage
  static const char* H = "0123456789ABCDEF";
  return (n >= 0 && n < 16) ? H[n] : '#';
}
inline std::string hexEncode(const std::string& s) {   // FUN_42009590
  std::string o;
  for (unsigned char c : s) { o += hexNibble(c >> 4); o += hexNibble(c & 0xF); }
  return o;
}
inline int hexVal(char c) {                    // FUN_80012124 : -1 si invalide
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
inline std::string hexDecode(const std::string& s, int limit = 31) {  // FUN_42009528
  std::string o;
  for (size_t i = 0; i + 1 < s.size(); i += 2) {
    if ((int)o.size() >= limit) break;
    o += (char)(((hexVal(s[i]) & 0xF) << 4) | (hexVal(s[i+1]) & 0xF));
  }
  return o;
}

// A text that is hex-encoded bytes: not empty, an even number of characters, hex digits only.
inline bool isHexText(const std::string& s) {
  if (s.empty() || s.size() % 2) return false;
  for (char c : s) if (hexVal(c) < 0) return false;
  return true;
}
// hexDecode() for a text that may not be hex at all: such a text is returned as it is. Decoding a plain Wi-Fi name
// turned it into garbage bytes: "iot" became the single byte 0xFF (open-firenet-ha issue #38).
inline std::string hexDecodeIfHex(const std::string& s) {
  return isHexText(s) ? hexDecode(s) : s;
}

// --------------------------------------------------- §7.2 tokeniseur littéral
// FUN_4200B5F8 : cherche la CHAÎNE delim ; jetons vides conservés ; limite out.
// Renvoie les valeurs d'une trame status (2 sauts "=;\n", 1 saut "\n", boucle "\n").
inline std::vector<std::string> parseStatusFrame(const std::string& buf,
                                                 int maxFields = NUM_FIELDS) {
  size_t pos = 0;
  auto tok = [&](const char* d) -> int {       // avance pos si trouvé, renvoie code
    size_t idx = buf.find(d, pos);
    if (idx == std::string::npos) return 1;
    pos = idx + strlen(d);
    return 0;
  };
  tok("=;\n"); tok("=;\n"); tok("\n");          // saute jusqu'à la 1re valeur
  std::vector<std::string> out;
  while ((int)out.size() <= maxFields) {
    size_t idx = buf.find('\n', pos);
    if (idx == std::string::npos) break;
    out.push_back(buf.substr(pos, idx - pos));
    pos = idx + 1;
  }
  return out;
}

// ------------------------------------------------------------- §5.4 Log sanitization
// Redacts the WiFi password (field 17 wpa2) in status frames
// to prevent accidental exposure of private credentials when sharing diagnostic logs.
inline std::string sanitizeForLog(const std::string& msg) {
  if (msg.find("STATUS=0;") == std::string::npos &&
      msg.find("STATUS") == std::string::npos) {
    return msg;
  }
  std::string out;
  out.reserve(msg.size());
  size_t start = 0;
  int lineIdx = 0;
  while (start < msg.size()) {
    size_t next = msg.find('\n', start);
    std::string line = (next == std::string::npos) ? msg.substr(start)
                                                   : msg.substr(start, next - start);
    start = (next == std::string::npos) ? msg.size() : next + 1;

    bool hasCr = (!line.empty() && line.back() == '\r');
    if (hasCr) line.pop_back();

    if (lineIdx == 17 && !line.empty() && line != "0") {
      line = "********";
    }

    out += line;
    if (hasCr) out += '\r';
    if (next != std::string::npos) out += '\n';
    lineIdx++;
  }
  return out;
}

// -------------------------------------------------- §13 positions des controls
// (le poêle travaille par position ; le nom est une étiquette libre)
enum Ctrl {
  CTRL_REVISION = 0,   // comparé dans GET_REVISION
  CTRL_ON_OFF   = 1,   // stove[0x02]  Stove on/off [1/0]
  CTRL_MODE     = 2,   // stove[0x03]  Regulation Mode [0-3]
  CTRL_TARGET_STAGE = 3, // stove[0x06] Target stage [30-100]
  CTRL_ROOM_TARGET  = 4, // stove[0x64] Room target Temperature ×10
  CTRL_BAKE_TARGET  = 5, // stove[0xfa] Bake target temperature
  CTRL_HEAT_ACTIVE  = 21,// stove[0x29] heating times active
  CTRL_SETBACK_TEMP = 22,// stove[0x67] set-back temperature ×10
  CTRL_MULTIAIR1_ON = 23,// stove[0x68] convectionFan1Active [0/1]
  CTRL_MULTIAIR1_LV = 24,// stove[0x69] convectionFan1Level [0-5] (0=auto)
  CTRL_MULTIAIR1_AR = 25,// stove[0x6a] convectionFan1Area [-30..+30]
  CTRL_MULTIAIR2_ON = 26,// stove[0x6b] convectionFan2Active [0/1]
  CTRL_MULTIAIR2_LV = 27,// stove[0x6c] convectionFan2Level [0-5] (0=auto)
  CTRL_MULTIAIR2_AR = 28,// stove[0x6d] convectionFan2Area [-30..+30]
  CTRL_FROST_ON     = 29,// stove[0x6e] frostProtectionActive [0/1]
  CTRL_FROST_TEMP   = 30,// stove[0x6f] ×10
  CTRL_ROOM_OFFSET  = 31,// stove[0x65] ×10
  CTRL_ROOM_POWER   = 32,// stove[0x66]
};
static const int CTRL_ROOM_TARGET_SCALE = 10;  // FUN_80010e8c : *10 (§13)

// -------------------------------------------------- §14 positions des sensors
enum Sens {
  SENS_ROOM_TEMP  = 0,   // dixièmes de °C, 1024 = sonde invalide
  SENS_FLAME      = 1,
  SENS_ERR_MASK32 = 3,   // masque d'erreurs 32 bits (§14.3)
  SENS_ERR_SUB    = 4,
  SENS_STATE_MASK = 5,
  SENS_AUGER_SET  = 7,
  SENS_IDFAN_MEAS = 9,   // vitesse mesurée ventilateur ID
  SENS_IDFAN_SET  = 10,
  SENS_MAIN_STATE = 31,
  SENS_SUB_STATE  = 32,
  SENS_RSSI       = 33,
  SENS_APP_VER    = 38,
  SENS_STAGE_CUR  = 30,
};

// ------------------------------------------------- libellés positionnels prouvés
// Étiquettes lisibles associées aux positions (§13 controls, §14 sensors). Le nom
// est libre sur le fil ; seule la position a un sens matériel. "" = position non
// identifiée (le firmware émet alors "sNN"/"cNN"). Sources : §13, §14, §14.3.
// index = control record in the DOMO / INDUO II table (38 records; the INDUO 2.26/2.27 table is the same without
// record 5, see v1ToDomoCtrlIndex). The comment is the official name of the record, from the order of the Rika cloud
// "controls" object (same method as the sensors, see SENSOR_LABELS); records 33..37 are the five debug words that
// are also sensor records debug0..debug4 (2.27 disassembly: same variables *(0x420c + 0xd0 .. 0xe0)).
static const char* CONTROL_LABELS[] = {
  /*0*/"revision",              // revision
  /*1*/"onOff",                 // onOff
  /*2*/"mode",                  // operatingMode
  /*3*/"targetStage",           // heatingPower
  /*4*/"roomTarget",            // targetTemperature (x10)
  /*5*/"bakeTarget",            // bakeTemperature (DOMO BACK / 2.28 layout only)
  /*6*/"ecoMode",               // ecoMode
  /*7*/"heatTimeMon1",          // heatingTimeMon1
  /*8*/"heatTimeMon2",          // heatingTimeMon2
  /*9*/"heatTimeTue1",          // heatingTimeTue1
  /*10*/"heatTimeTue2",         // heatingTimeTue2
  /*11*/"heatTimeWed1",         // heatingTimeWed1
  /*12*/"heatTimeWed2",         // heatingTimeWed2
  /*13*/"heatTimeThu1",         // heatingTimeThu1
  /*14*/"heatTimeThu2",         // heatingTimeThu2
  /*15*/"heatTimeFri1",         // heatingTimeFri1
  /*16*/"heatTimeFri2",         // heatingTimeFri2
  /*17*/"heatTimeSat1",         // heatingTimeSat1
  /*18*/"heatTimeSat2",         // heatingTimeSat2
  /*19*/"heatTimeSun1",         // heatingTimeSun1
  /*20*/"heatTimeSun2",         // heatingTimeSun2
  /*21*/"heatingTimesActive",   // heatingTimesActiveForComfort
  /*22*/"setBackTemp",          // setBackTemperature
  /*23*/"convectionFan1Active", // convectionFan1Active
  /*24*/"convectionFan1Level",  // convectionFan1Level
  /*25*/"convectionFan1Area",   // convectionFan1Area
  /*26*/"convectionFan2Active", // convectionFan2Active
  /*27*/"convectionFan2Level",  // convectionFan2Level
  /*28*/"convectionFan2Area",   // convectionFan2Area
  /*29*/"frostProtectionActive",// frostProtectionActive
  /*30*/"frostProtectionTemp",  // frostProtectionTemperature
  /*31*/"roomTempOffset",       // temperatureOffset
  /*32*/"roomSensorPower",      // RoomPowerRequest
  /*33*/"debug0",               // debug0
  /*34*/"debug1",               // debug1
  /*35*/"debug2",               // debug2
  /*36*/"debug3",               // debug3
  /*37*/"debug4",               // debug4
};
static const int NUM_CONTROL_LABELS = 38;

// index = sensor position in the DOMO / INDUO II table (88 records; the INDUO 2.26/2.27 table is the same without
// record 2, see v1ToDomoIndex). The comment is the official name of the record: the official key does not hold the
// names, it fetches them from the Rika server and registers them with the stove, so the order of the cloud "sensors"
// object is the record order (full cloud dump in natural order, 87 names, checked position by position against every
// record already identified and against the disassembly; record 2 = inputBakeTemperature from a DOMO/2.28 dump).
// The wire names are kept short on purpose: the stove copies each registered name without a length check into a
// 32-byte field (name at +0, value at +0x20), and receives at most 2048 bytes per frame, while some official names
// are 35 characters long. Names already used by the firmware or the Home Assistant integration are kept as they were.
static const char* SENSOR_LABELS[] = {
  /*0*/"roomTemp",          // inputRoomTemperature
  /*1*/"flame",             // inputFlameTemperature
  /*2*/"bakeTemp",          // inputBakeTemperature
  /*3*/"errMask32",         // statusError
  /*4*/"errSub",            // statusSubError
  /*5*/"statusWarning",     // statusWarning
  /*6*/"statusService",     // statusService
  /*7*/"augerSet",          // outputDischargeMotor
  /*8*/"augerCurrent",      // outputDischargeCurrent
  /*9*/"idFanMeas",         // outputIDFan
  /*10*/"idFanSet",         // outputIDFanTarget
  /*11*/"insertionMotor",   // outputInsertionMotor
  /*12*/"insertionCurrent", // outputInsertionCurrent
  /*13*/"airFlaps",         // outputAirFlaps
  /*14*/"airFlapsTarget",   // outputAirFlapsTargetPosition
  /*15*/"burnBackMagnet",   // outputBurnBackFlapMagnet
  /*16*/"gridMotor",        // outputGridMotor
  /*17*/"ignition",         // outputIgnition
  /*18*/"tempLimiter",      // inputUpperTemperatureLimiter
  /*19*/"pressureSwitch",   // inputPressureSwitch
  /*20*/"pressureSensor",   // inputPressureSensor
  /*21*/"gridContact",      // inputGridContact
  /*22*/"door",             // inputDoor
  /*23*/"hopperLidClosed",  // inputCover
  /*24*/"externalRequest",  // inputExternalRequest
  /*25*/"burnBackSwitch",   // inputBurnBackFlapSwitch
  /*26*/"flueGasSwitch",    // inputFlueGasFlapSwitch
  /*27*/"boardSensor",      // inputBoardTemperature
  /*28*/"stageCur1",        // inputCurrentStage
  /*29*/"stageTgt2",        // inputTargetStagePID
  /*30*/"stageCur",         // inputCurrentStagePID
  /*31*/"mainState",        // statusMainState
  /*32*/"subState",         // statusSubState
  /*33*/"rssi",             // statusWifiStrength
  /*34*/"ecoModePossible",  // parameterEcoModePossible
  /*35*/"fabNumber",        // parameterFabricationNumber
  /*36*/"model",            // parameterStoveTypeNumber
  /*37*/"language",         // parameterLanguageNumber
  /*38*/"appVerBoard",      // parameterVersionMainBoard
  /*39*/"tftVersion",       // parameterVersionTFT
  /*40*/"appVersion",       // parameterVersionWiFi
  /*41*/"blVerBoard",       // parameterVersionMainBoardBootLoader
  /*42*/"blVerTft",         // parameterVersionTFTBootLoader
  /*43*/"blVersion",        // parameterVersionWiFiBootLoader
  /*44*/"firmwareBuild",    // parameterVersionMainBoardSub
  /*45*/"tftBuild",         // parameterVersionTFTSub
  /*46*/"appRevision",      // parameterVersionWiFiSub
  /*47*/"pelletHours",      // parameterRuntimePellets
  /*48*/"logRuntime",       // parameterRuntimeLogs
  /*49*/"pelletsTotal",     // parameterFeedRateTotal
  /*50*/"serviceCountdown", // parameterFeedRateService
  /*51*/"serviceOffset",    // parameterServiceCountdownKg
  /*52*/"serviceMinutes",   // parameterServiceCountdownTime
  /*53*/"ignitionCount",    // parameterIgnitionCount
  /*54*/"onOffCycles",      // parameterOnOffCycleCount
  /*55*/"flameSensorOffset",// parameterFlameSensorOffset
  /*56*/"pressureOffset",   // parameterPressureSensorOffset
  /*57*/"errCount0",        // parameterErrorCount0
  /*58*/"errCount1",        // parameterErrorCount1
  /*59*/"errCount2",        // parameterErrorCount2
  /*60*/"errCount3",        // parameterErrorCount3
  /*61*/"errCount4",        // parameterErrorCount4
  /*62*/"errCount5",        // parameterErrorCount5
  /*63*/"errCount6",        // parameterErrorCount6
  /*64*/"errCount7",        // parameterErrorCount7
  /*65*/"errCount8",        // parameterErrorCount8
  /*66*/"errCount9",        // parameterErrorCount9
  /*67*/"errCount10",       // parameterErrorCount10
  /*68*/"errCount11",       // parameterErrorCount11
  /*69*/"errCount12",       // parameterErrorCount12
  /*70*/"errCount13",       // parameterErrorCount13
  /*71*/"errCount14",       // parameterErrorCount14
  /*72*/"errCount15",       // parameterErrorCount15
  /*73*/"errCount16",       // parameterErrorCount16
  /*74*/"errCount17",       // parameterErrorCount17
  /*75*/"errCount18",       // parameterErrorCount18
  /*76*/"errCount19",       // parameterErrorCount19
  /*77*/"heatTimesNotProg", // statusHeatingTimesNotProgrammed
  /*78*/"frostStarted",     // statusFrostStarted
  /*79*/"spiralTuning",     // parameterSpiralMotorsTuning
  /*80*/"idFanTuning",      // parameterIDFanTuning
  /*81*/"cleanInterval",    // parameterCleanIntervalBig
  /*82*/"kgTillCleaning",   // parameterKgTillCleaning
  /*83*/"debug0",           // parameterDebug0
  /*84*/"debug1",           // parameterDebug1
  /*85*/"debug2",           // parameterDebug2
  /*86*/"debug3",           // parameterDebug3
  /*87*/"debug4",           // parameterDebug4
};
static const int NUM_SENSOR_LABELS = 88;

// nom émis pour une position (libellé prouvé, sinon "sNN"/"cNN")
// INDUO V2.26 / V2.27 (generation 2): the controls table of the stove is the DOMO / INDUO II one without the record at
// index 5 (the 2.28 has an extra constant record there, disassembly of both firmwares): V1 record p is DOMO control p
// for p < 5 and p + 1 for p >= 5 (V1 records 6..19 = the 14 heating times = DOMO 7..20, and so on up to record 36).
inline int v1ToDomoCtrlIndex(int p) { return p < 5 ? p : p + 1; }

inline std::string ctrlName(int i, int generation = 0) {
  if (generation == 2) i = v1ToDomoCtrlIndex(i);   // i is then a V1 (2.27) record
  if (i < NUM_CONTROL_LABELS && CONTROL_LABELS[i][0]) return CONTROL_LABELS[i];
  char b[8]; snprintf(b, sizeof b, "c%02d", i); return b;
}

// Index (DOMO index space) of a control name echoed by the stove, or -1: a label of the table or "cNN".
inline int ctrlIndexByName(const std::string& n) {
  if (n.empty()) return -1;
  for (int i = 0; i < NUM_CONTROL_LABELS; i++)
    if (CONTROL_LABELS[i][0] && n == CONTROL_LABELS[i]) return i;
  if (n.size() >= 2 && n.size() <= 4 && n[0] == 'c') {
    int v = 0;
    for (size_t k = 1; k < n.size(); k++) { if (n[k] < '0' || n[k] > '9') return -1; v = v * 10 + (n[k] - '0'); }
    return v;
  }
  return -1;
}
// INDUO V2.26 / V2.27 (generation 2): the stove's sensor table is the DOMO / INDUO II one without the record
// at index 2 (disassembly of the three firmwares, joined through the TFT display numbers: 2.27 position p is
// position p for p < 2 and p + 1 for p >= 2 of the DOMO table above; confirmed against a live DOMO for the
// positions with a label). The labels of the DOMO table are therefore reused for V1.
inline int v1ToDomoIndex(int p) { return p < 2 ? p : p + 1; }
inline int domoToV1Index(int d) { return d < 2 ? d : (d == 2 ? -1 : d - 1); }

inline std::string sensName(int i, int generation = 0) {
  if (generation == 2) i = v1ToDomoIndex(i);   // i is then a V1 (2.27) position
  if (i < NUM_SENSOR_LABELS && SENSOR_LABELS[i][0]) return SENSOR_LABELS[i];
  char b[8]; snprintf(b, sizeof b, "s%02d", i); return b;
}

// Position (DOMO index space) of a sensor name echoed by the stove, or -1: a label of the table or "sNN".
inline int sensIndexByName(const std::string& n) {
  if (n.empty()) return -1;
  for (int i = 0; i < NUM_SENSOR_LABELS; i++)
    if (SENSOR_LABELS[i][0] && n == SENSOR_LABELS[i]) return i;
  if (n.size() >= 2 && n.size() <= 4 && n[0] == 's') {
    int v = 0;
    for (size_t k = 1; k < n.size(); k++) { if (n[k] < '0' || n[k] > '9') return -1; v = v * 10 + (n[k] - '0'); }
    return v;
  }
  return -1;
}

} // namespace firenet
