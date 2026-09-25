// firenet_protocol.h — cœur du protocole CDC Open-Firenet, sans dépendance Arduino.
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
static const int    BL_VERSION     = 101;      // Firenet V1: 101
static const int    APP_VERSION    = 112;      // Firenet V1: 112 (0x70, validated by stove FUN_8004ab40)
static const int    APP_REVISION   = 360;      // Firenet V1: 360
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
  CTRL_HEAT_ACTIVE  = 21,// stove[0x29] heating times active
  CTRL_SETBACK_TEMP = 22,// stove[0x67] set-back temperature ×10
  CTRL_MULTIAIR1_ON = 23,// stove[0x68]
  CTRL_MULTIAIR1_LV = 24,// stove[0x69]
  CTRL_FROST_ON     = 29,// stove[0x6E]
  CTRL_FROST_TEMP   = 30,// stove[0x6F] ×10
  CTRL_ROOM_OFFSET  = 31,// stove[0x65] ×10
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
static const char* CONTROL_LABELS[] = {
  /*0*/"revision", /*1*/"onOff", /*2*/"mode", /*3*/"targetStage",
  /*4*/"roomTarget",                       // ×10 (§13, CTRL_ROOM_TARGET_SCALE)
};
static const int NUM_CONTROL_LABELS = 5;

// index = position du capteur ; couvre les positions prouvées jusqu'à 54 (55 au total).
static const char* SENSOR_LABELS[] = {
  /*0*/"roomTemp",      /*1*/"flame",       /*2*/"",           /*3*/"errMask32",
  /*4*/"errSub",        /*5*/"stateMask",   /*6*/"",           /*7*/"augerSet",
  /*8*/"",              /*9*/"idFanMeas",   /*10*/"idFanSet",  /*11*/"",
  /*12*/"",             /*13*/"",           /*14*/"",          /*15*/"",
  /*16*/"",             /*17*/"",           /*18*/"",          /*19*/"",
  /*20*/"",             /*21*/"",           /*22*/"",          /*23*/"hopperLidClosed",
  /*24*/"",             /*25*/"",           /*26*/"",          /*27*/"boardSensor",
  /*28*/"stageCur1",    /*29*/"stageTgt2",  /*30*/"stageCur",  /*31*/"mainState",
  /*32*/"subState",     /*33*/"rssi",       /*34*/"",          /*35*/"fabNumber",
  /*36*/"model",        /*37*/"language",   /*38*/"appVerBoard",
  /*39*/"",             /*40*/"appVersion", /*41*/"",          /*42*/"",
  /*43*/"blVersion",    /*44*/"firmwareBuild",/*45*/"subVersion",/*46*/"appRevision",
  /*47*/"pelletHours",  /*48*/"",           /*49*/"pelletsTotal",/*50*/"serviceCountdown",
  /*51*/"serviceOffset",/*52*/"serviceMinutes",
  // 53-54 confirmed 2026-09-18 by direct comparison against the stove's own
  // Info > Paramètres screen (real hardware match, not binary-only inference).
  /*53*/"ignitionCount",/*54*/"onOffCycles",
};
static const int NUM_SENSOR_LABELS = 55;

// nom émis pour une position (libellé prouvé, sinon "sNN"/"cNN")
inline std::string ctrlName(int i) {
  if (i < NUM_CONTROL_LABELS && CONTROL_LABELS[i][0]) return CONTROL_LABELS[i];
  char b[8]; snprintf(b, sizeof b, "c%02d", i); return b;
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
