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

// Sensors. Read on a stove (issue #110): 000 room temperature (whole degrees), 001 flame temperature, 027 / 028 main state and sub state (the sub state sequence of an ignition matches the other firmware versions).
// By comparison with the other firmware versions, not proven one by one: 002 to 004 errors, 005 auger in per mille, 007 flue fan
// measured, 019 auger current. 012 pressure switch, 013 grate contact and 032 ignition element were matched with the stove display (issue #110 photos).
// 016, 017 and 029 are not identified yet (external request is one of 016 / 017): kept under their code.
inline const char* sensorName(int code) {
  switch (code) {
    case 0: return "roomTemp";
    case 1: return "flame";
    case 2: return "errMask32";
    case 3: return "errSub";
    case 4: return "stateMask";
    case 5: return "augerSet";
    case 7: return "idFanMeas";
    case 12: return "pressureSwitch";
    case 13: return "gridContact";
    case 16: return "in016";
    case 17: return "in017";
    case 19: return "augerCurrent";
    case 27: return "mainState";
    case 28: return "subState";
    case 29: return "in029";
    case 32: return "ignition";
  }
  return nullptr;
}

// Settings read back from the stove's configuration. Confirmed against the stove display and the code of firmware 2.25 (issue #110):
// 024 mode (0 manual, 1 automatic, 2 comfort), 026 room set point of the comfort mode, 081 reduced set point (the stove keeps it at least
// 2 degrees under 026, code at 0x80034e5e), 088 / 089 frost protection on / switch-on temperature (whole degrees).
// 023 on / off (0 to 1 when the stove was started) and 025 power in percent of the manual mode (60 to 50 when reset) were seen changing
// with the user's actions in the log of issue #110. 092 is the power level of the comfort mode, 1 to 4 (3 to 2 seen at the time of the
// change on the display); it is mapped to roomSensorPower (the official name of the record). 091 (room sensor offset, "Temperaturanpassung") was read only as 0; the room temperature 000
// went 17, 18, 17 within 15 s of the time the offset was set to +1.0 and back, and the function at 0x800256f4 returns a signed byte
// divided by 10, so it is probably in tenths of a degree: mapped to roomTempOffset (read only, never written), but still
// only ever read as 0, so unproven.
inline const char* controlName(int code) {
  switch (code) {
    case 23: return "onOff";
    case 24: return "mode";
    case 25: return "targetStage";
    case 26: return "roomTarget";
    case 81: return "setBackTemp";
    case 88: return "frostProtectionActive";
    case 89: return "frostProtectionTemp";
    case 91: return "roomTempOffset";
    case 92: return "roomSensorPower";
  }
  return nullptr;
}

// Temperatures of the other firmware versions are in tenths of a degree for the room, the stove of 2.25 gives whole degrees.
inline long scaled(int code, long v) { return (code == 0 || code == 26 || code == 81 || code == 89) ? v * 10 : v; }

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
