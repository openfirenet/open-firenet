// firenet_api.h -- parsing of the control commands received by the web API (/api/controls, /api/control,
// /api/schedule). Arduino-free so it can be tested on the host (test/api_test.cpp).
//
// One table row per accepted parameter: the control it sets, how its value is converted to the stove unit, and every
// name it is accepted under (JSON key, form / query argument, or the "name" of a name/value pair). All the input forms
// the API has always accepted go through the same code.
#pragma once
#include "firenet_protocol.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace firenet {

enum class ParamKind {
  Bool,       // true/false, on/off (any case) or a number (non-zero = on)
  Int,        // integer as given
  Round,      // number rounded to an integer (bake temperature)
  Mode,       // "manual" / "auto" / "comfort" or 0 / 1 / 2
  Temp10,     // °C: below 50 it is taken as degrees and stored x10, otherwise as already x10 (target, setback)
  Frost10,    // °C: between 0 and 40 stored x10, otherwise as already x10 (frost protection temperature)
  Offset10,   // °C, always x10 (room_temperature_offset)
  OffsetAuto, // x10 if it has decimals and |v| < 4.5, else as given (roomTempOffset, historical form)
};

struct ControlParam { const char* control; ParamKind kind; const char* names; };  // names: space separated

static const ControlParam CONTROL_PARAMS[] = {
  {"onOff",                 ParamKind::Bool,       "on onOff"},
  {"mode",                  ParamKind::Mode,       "mode operatingMode"},
  {"roomTarget",            ParamKind::Temp10,     "target_temperature temperature tempRoomTarget roomTarget room"},
  {"targetStage",           ParamKind::Int,        "power_percent power heatingPower targetStage stage"},
  {"convectionFan1Active",  ParamKind::Bool,       "convectionFan1Active convection_fan1_active fan1Active fan1On fan1"},
  {"convectionFan1Level",   ParamKind::Int,        "convectionFan1Level convection_fan1_level fan1Level"},
  {"convectionFan1Area",    ParamKind::Int,        "convectionFan1Area convection_fan1_area fan1Area"},
  {"convectionFan2Active",  ParamKind::Bool,       "convectionFan2Active convection_fan2_active fan2Active fan2On fan2"},
  {"convectionFan2Level",   ParamKind::Int,        "convectionFan2Level convection_fan2_level fan2Level"},
  {"convectionFan2Area",    ParamKind::Int,        "convectionFan2Area convection_fan2_area fan2Area"},
  {"frostProtectionActive", ParamKind::Bool,       "frostProtectionActive frost_protection_active frostActive frostOn"},
  {"frostProtectionTemp",   ParamKind::Frost10,    "frostProtectionTemp frost_protection_temperature frost_protection_temp frostTemp tempFrost"},
  {"bakeTarget",            ParamKind::Round,      "bakeTarget bake_target_temperature bake_target bakeTemp bake"},
  {"roomTempOffset",        ParamKind::Offset10,   "room_temperature_offset room_temp_offset"},
  {"roomTempOffset",        ParamKind::OffsetAuto, "roomTempOffset tempOffset roomOffset offset"},
  {"ecoMode",               ParamKind::Bool,       "ecoMode eco_mode"},
  {"heatingTimesActive",    ParamKind::Bool,       "heatingTimesActive heating_times_active scheduleActive"},
  {"setBackTemp",           ParamKind::Temp10,     "setBackTemp setback_temperature setbackTemp tempEco"},
};

inline const ControlParam* findControlParam(const std::string& name) {
  if (name.empty()) return nullptr;
  for (const auto& p : CONTROL_PARAMS) {
    const char* s = p.names;
    while (*s) {
      const char* e = std::strchr(s, ' ');
      size_t n = e ? (size_t)(e - s) : std::strlen(s);
      if (n == name.size() && std::strncmp(s, name.c_str(), n) == 0) return &p;
      if (!e) break;
      s = e + 1;
    }
  }
  return nullptr;
}

// Converts a textual value to the stove unit of the parameter. Returns false for a value that cannot be read.
inline bool convertParam(ParamKind kind, const std::string& text, long& out) {
  if (kind == ParamKind::Bool) {
    std::string t = text;
    for (char& c : t) c = (char)std::tolower((unsigned char)c);
    if (t == "true" || t == "on") { out = 1; return true; }
    if (t == "false" || t == "off") { out = 0; return true; }
  }
  if (kind == ParamKind::Mode) {
    if (text == "manual") { out = 0; return true; }
    if (text == "auto") { out = 1; return true; }
    if (text == "comfort") { out = 2; return true; }
  }
  char* end = nullptr;
  double f = std::strtod(text.c_str(), &end);
  if (end == text.c_str()) return false;
  switch (kind) {
    case ParamKind::Bool:       out = (f != 0.0) ? 1 : 0; break;
    case ParamKind::Int:
    case ParamKind::Mode:       out = (long)f; break;
    case ParamKind::Round:      out = std::lround(f); break;
    case ParamKind::Temp10:     out = (f < 50.0) ? std::lround(f * 10.0) : (long)f; break;
    case ParamKind::Frost10:    out = (f > 0.0 && f < 40.0) ? std::lround(f * 10.0) : (long)f; break;
    case ParamKind::Offset10:   out = std::lround(f * 10.0); break;
    case ParamKind::OffsetAuto: out = (f > -4.5 && f < 4.5 && f != (double)(long)f) ? std::lround(f * 10.0) : std::lround(f); break;
  }
  return true;
}

// Every "key": value pair of a JSON text, nested or not (the historical parser searched keys anywhere in the body,
// e.g. inside the "slots" object of /api/schedule). String values are returned without their quotes.
inline void jsonPairs(const std::string& json, std::vector<std::pair<std::string, std::string>>& out) {
  size_t i = 0;
  while ((i = json.find('"', i)) != std::string::npos) {
    size_t kEnd = json.find('"', i + 1);
    if (kEnd == std::string::npos) return;
    std::string key = json.substr(i + 1, kEnd - i - 1);
    size_t p = kEnd + 1;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) p++;
    if (p >= json.size() || json[p] != ':') { i = kEnd + 1; continue; }   // a string value, not a key
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) p++;
    if (p < json.size() && json[p] == '"') {
      size_t vEnd = json.find('"', p + 1);
      if (vEnd == std::string::npos) return;
      out.push_back({key, json.substr(p + 1, vEnd - p - 1)});
      i = vEnd + 1;
    } else if (p < json.size() && json[p] == '{') {
      i = p;                                                                 // descend into the nested object
    } else {
      size_t vEnd = json.find_first_of(",}] \t\r\n", p);
      if (vEnd == std::string::npos) vEnd = json.size();
      out.push_back({key, json.substr(p, vEnd - p)});
      i = vEnd;
    }
  }
}

// Historical plain-text form "onOff=1; operatingMode=2; heatingPower=70; tempRoomTarget=215;" ('&' also separates).
inline void textPairs(const std::string& text, std::vector<std::pair<std::string, std::string>>& out) {
  size_t i = 0;
  while (i < text.size()) {
    size_t e = text.find_first_of(";&", i);
    if (e == std::string::npos) e = text.size();
    std::string item = text.substr(i, e - i);
    size_t eq = item.find('=');
    if (eq != std::string::npos) {
      auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
      };
      out.push_back({trim(item.substr(0, eq)), trim(item.substr(eq + 1))});
    }
    i = e + 1;
  }
}

// Commanded values, keyed by control name (stove units). Later pairs override earlier ones. Heating time slots are
// accepted under their control names (heatTimeMon1 .. heatTimeSun2).
inline std::map<std::string, long> parseControlCommands(const std::vector<std::pair<std::string, std::string>>& pairs) {
  std::map<std::string, long> cmd;
  for (const auto& kv : pairs) {
    long v = 0;
    if (const ControlParam* p = findControlParam(kv.first)) {
      if (convertParam(p->kind, kv.second, v)) cmd[p->control] = v;
      continue;
    }
    for (int i = 7; i <= 20; i++) {
      if (kv.first == ctrlName(i)) { if (convertParam(ParamKind::Int, kv.second, v)) cmd[kv.first] = v; break; }
    }
  }
  return cmd;
}

// A text made safe to put between the quotes of a JSON string: quotes and backslashes escaped, control characters
// dropped, and every byte that is not part of a well-formed UTF-8 sequence replaced by U+FFFD. The stove sends some
// texts as raw bytes (its status fields; a 0xFF as "ssid" was seen on firmware 2.28, open-firenet-ha issue #38),
// and a single such byte made the whole answer of /api/state undecodable by strict JSON readers.
inline std::string jsonText(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  const size_t n = in.size();
  for (size_t i = 0; i < n;) {
    const unsigned char c = (unsigned char)in[i];
    if (c < 0x20) { i++; continue; }
    if (c == '"' || c == '\\') { out += '\\'; out += (char)c; i++; continue; }
    if (c < 0x80) { out += (char)c; i++; continue; }
    // Length of the sequence this lead byte announces; 0 for a byte that cannot start one.
    size_t len = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    bool ok = len != 0 && i + len <= n;
    for (size_t k = 1; ok && k < len; k++) ok = ((unsigned char)in[i + k] & 0xC0) == 0x80;
    if (ok && len >= 3) {
      const unsigned char c1 = (unsigned char)in[i + 1];
      if (c == 0xE0 && c1 < 0xA0) ok = false;        // overlong
      else if (c == 0xED && c1 > 0x9F) ok = false;   // surrogates
      else if (c == 0xF0 && c1 < 0x90) ok = false;   // overlong
      else if (c == 0xF4 && c1 > 0x8F) ok = false;   // above U+10FFFF
    }
    if (ok) { out.append(in, i, len); i += len; }
    else    { out += "\xEF\xBF\xBD"; i++; }
  }
  return out;
}

}  // namespace firenet
