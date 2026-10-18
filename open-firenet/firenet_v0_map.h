// Puts what a stove on firmware 2.25 answers (numeric codes, see firenet_link_v0.h) into the same model as the other
// firmware versions, under the same names, so that the page, the REST API, MQTT and Home Assistant show it without change.
// Read only: nothing here can build a command. The meaning of each code is given where it is known.
#pragma once
#include <cstdlib>
#include <string>
#include "firenet_link.h"

namespace firenet {
namespace v0map {

// Parses "123" or "-5"; false for anything else (the 9xx codes answer with text).
inline bool parseInt(const std::string& s, long& out) {
  if (s.empty() || s.size() > 11) return false;
  size_t i = (s[0] == '-') ? 1 : 0;
  if (i >= s.size()) return false;
  for (size_t k = i; k < s.size(); k++) if (s[k] < '0' || s[k] > '9') return false;
  out = strtol(s.c_str(), nullptr, 10);
  return true;
}

// Sensors. Read on a stove (issue #110): 000 room temperature (whole degrees), 001 flame temperature, 012 grate contact (very
// probable), 027 / 028 main state and sub state (the sub state sequence of an ignition matches the other firmware versions).
// By comparison with the other firmware versions, not proven one by one: 002 to 004 errors, 005 auger in per mille, 007 flue fan
// measured, 019 auger current. 013, 016, 017, 029 and 032 are digital inputs not identified yet (external request and pressure
// switch are two of 016 / 017): kept under their code.
inline const char* sensorName(int code) {
  switch (code) {
    case 0: return "roomTemp";
    case 1: return "flame";
    case 2: return "errMask32";
    case 3: return "errSub";
    case 4: return "stateMask";
    case 5: return "augerSet";
    case 7: return "idFanMeas";
    case 12: return "gridContact";
    case 13: return "in013";
    case 16: return "in016";
    case 17: return "in017";
    case 19: return "augerCurrent";
    case 27: return "mainState";
    case 28: return "subState";
    case 29: return "in029";
    case 32: return "in032";
  }
  return nullptr;
}

// Settings read back from the stove's configuration (names by comparison with the other firmware versions, NOT confirmed yet by a
// reading on a stove): 023 on / off, 024 mode, 025 power stage, 026 room set point (whole degrees), 081 reduced set point.
inline const char* controlName(int code) {
  switch (code) {
    case 23: return "onOff";
    case 24: return "mode";
    case 25: return "targetStage";
    case 26: return "roomTarget";
    case 81: return "setBackTemp";
  }
  return nullptr;
}

// Temperatures of the other firmware versions are in tenths of a degree for the room, the stove of 2.25 gives whole degrees.
inline long scaled(int code, long v) { return (code == 0 || code == 26 || code == 81) ? v * 10 : v; }

// Returns true when the value was stored. Marks the stove as reachable through the numeric link.
inline bool apply(StoveModel& m, int code, const std::string& text, uint32_t now) {
  long v;
  if (!parseInt(text, v)) return false;
  m.v0_linked = true; m.last_rx_ms = now; m.frames_in++;
  if (const char* n = sensorName(code)) {
    m.sensors[n] = scaled(code, v);
    if (code == 27) m.main_state = (int)v;
    if (code == 28) m.sub_state = (int)v;
    return true;
  }
  if (const char* n = controlName(code)) { m.controls[n] = scaled(code, v); return true; }
  return false;
}

}  // namespace v0map
}  // namespace firenet
