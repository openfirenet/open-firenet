// Which screens the small display of the bridge shows, in which order, and for how long. Kept apart from the drawing code so
// that every board answers /api/display and the rule can be tested on a computer.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace firenet {

class DisplayConfig {
public:
  enum Id : uint8_t { STATE, ROOM, FLAME, POWER, IP, PELLETS, HOURS, WIFI, MQTT, VERSION, LOGO, COUNT };
  static constexpr uint8_t SECONDS_MIN = 2, SECONDS_MAX = 30, SECONDS_DEFAULT = 4;
  static constexpr uint8_t BRIGHT_MIN = 5, BRIGHT_MAX = 100, BRIGHT_DEFAULT = 50, DIM_LEVEL_MAX = 50, DIM_LEVEL_DEFAULT = 10;
  static constexpr uint16_t DIM_AFTER_MIN = 30, DIM_AFTER_MAX = 3600, DIM_AFTER_DEFAULT = 120;

  static const char* idName(int i) {
    static const char* const N[COUNT] = {"state", "room", "flame", "power", "ip", "pellets", "hours", "wifi", "mqtt", "version", "logo"};
    return i >= 0 && i < COUNT ? N[i] : "";
  }
  static int idOf(const std::string& n) { for (int i = 0; i < COUNT; i++) if (n == idName(i)) return i; return -1; }

  // Boards with a screen the firmware knows how to drive: "tdongle" runs it, "tdongle_off" only blanks it (backlight off). "none" is the default: the pins of another board may be wired to
  // something else, so a screen is only driven when the owner says there is one.
  static bool validBoard(const std::string& b) { return b == "none" || b == "tdongle" || b == "tdongle_off"; }

  DisplayConfig() { reset(); }
  void reset() {
    for (int i = 0; i < COUNT; i++) { order[i] = (uint8_t)i; on[i] = true; }
    seconds = SECONDS_DEFAULT; brightness = BRIGHT_DEFAULT; dim = false; dimAfter = DIM_AFTER_DEFAULT; dimLevel = DIM_LEVEL_DEFAULT;
  }

  uint8_t order[COUNT];     // every screen once, in the order they are shown
  bool on[COUNT];           // indexed by screen id, not by position
  uint8_t seconds;
  uint8_t brightness;       // backlight, percent
  bool dim;                 // dim the backlight after a while without a press or an alert
  uint16_t dimAfter;        // seconds
  uint8_t dimLevel;         // backlight while dimmed, percent (0 = off)

  // {"seconds":4,"screens":[{"id":"state","enabled":true}, ...]}; `seconds` and `screens` are both optional. The screens
  // listed come first, in that order; the ones left out follow in their previous order, unchanged. Returns false (and
  // leaves *this untouched) on an unknown id, a repeated id or a `seconds` out of range.
  bool apply(const std::string& body) {
    DisplayConfig c = *this;
    size_t s = body.find("\"seconds\"");
    if (s != std::string::npos) {
      size_t colon = body.find(':', s);
      if (colon == std::string::npos) return false;
      int v = atoi(body.c_str() + colon + 1);
      if (v < SECONDS_MIN || v > SECONDS_MAX) return false;
      c.seconds = (uint8_t)v;
    }
    auto number = [&](const char* key, long lo, long hi, long& out) {   // false only when present and out of range
      size_t k = body.find(std::string("\"") + key + "\"");
      if (k == std::string::npos) return true;
      size_t colon = body.find(':', k);
      if (colon == std::string::npos) return false;
      long v = atol(body.c_str() + colon + 1);
      if (v < lo || v > hi) return false;
      out = v; return true;
    };
    long v;
    v = c.brightness; if (!number("brightness", BRIGHT_MIN, BRIGHT_MAX, v)) return false; c.brightness = (uint8_t)v;
    v = c.dimAfter;   if (!number("dim_after", DIM_AFTER_MIN, DIM_AFTER_MAX, v)) return false; c.dimAfter = (uint16_t)v;
    v = c.dimLevel;   if (!number("dim_level", 0, DIM_LEVEL_MAX, v)) return false; c.dimLevel = (uint8_t)v;
    size_t d = body.find("\"dim\"");
    if (d != std::string::npos) {
      size_t colon = body.find(':', d), val = colon == std::string::npos ? colon : body.find_first_not_of(" \t\r\n", colon + 1);
      if (val == std::string::npos) return false;
      if (body.compare(val, 4, "true") == 0) c.dim = true; else if (body.compare(val, 5, "false") == 0) c.dim = false; else return false;
    }
    std::vector<uint8_t> listed;
    size_t pos = 0;
    for (;;) {
      size_t k = body.find("\"id\"", pos);
      if (k == std::string::npos) break;
      size_t q1 = body.find('"', body.find(':', k) + 1);
      if (q1 == std::string::npos) return false;
      size_t q2 = body.find('"', q1 + 1);
      if (q2 == std::string::npos) return false;
      int id = idOf(body.substr(q1 + 1, q2 - q1 - 1));
      if (id < 0) return false;
      for (uint8_t l : listed) if (l == id) return false;
      size_t next = body.find("\"id\"", q2);
      size_t e = body.find("\"enabled\"", q2);
      if (e != std::string::npos && (next == std::string::npos || e < next)) {
        size_t colon = body.find(':', e);
        size_t v = body.find_first_not_of(" \t\r\n", colon + 1);
        if (colon == std::string::npos || v == std::string::npos) return false;
        if (body.compare(v, 4, "true") == 0) c.on[id] = true;
        else if (body.compare(v, 5, "false") == 0) c.on[id] = false;
        else return false;
      }
      listed.push_back((uint8_t)id);
      pos = q2 + 1;
    }
    if (!listed.empty()) {
      int n = 0;
      for (uint8_t l : listed) c.order[n++] = l;
      for (int i = 0; i < COUNT; i++) {
        bool in = false; for (uint8_t l : listed) if (l == order[i]) in = true;
        if (!in) c.order[n++] = order[i];
      }
    }
    *this = c;
    return true;
  }

  // `active`: the display is running now; `board`: the choice stored (it applies at the next restart); `restartNeeded`: they differ.
  std::string json(bool active, const std::string& board = "none", bool restartNeeded = false) const {
    std::string j = std::string("{\"available\":") + (active ? "true" : "false") + ",\"board\":\"" + board + "\",\"boards\":[\"none\",\"tdongle\",\"tdongle_off\"],\"restart_needed\":" +
                    (restartNeeded ? "true" : "false") + ",\"seconds\":" + std::to_string(seconds) + ",\"brightness\":" + std::to_string(brightness) +
                    ",\"dim\":" + (dim ? "true" : "false") + ",\"dim_after\":" + std::to_string(dimAfter) + ",\"dim_level\":" + std::to_string(dimLevel) + ",\"screens\":[";
    for (int i = 0; i < COUNT; i++) {
      if (i) j += ",";
      j += std::string("{\"id\":\"") + idName(order[i]) + "\",\"enabled\":" + (on[order[i]] ? "true" : "false") + "}";
    }
    return j + "]}";
  }

  // Stored as "state:1,room:0,..." (order = position in the list).
  std::string toPrefs() const {
    std::string s;
    for (int i = 0; i < COUNT; i++) { if (i) s += ","; s += idName(order[i]); s += on[order[i]] ? ":1" : ":0"; }
    return s + ";" + std::to_string(seconds) + ";" + std::to_string(brightness) + ";" + (dim ? "1" : "0") + ";" + std::to_string(dimAfter) + ";" + std::to_string(dimLevel);
  }
  void fromPrefs(const std::string& s) {
    DisplayConfig c;
    size_t semi = s.find(';');
    std::string list = s.substr(0, semi);
    if (semi != std::string::npos) {                       // ";seconds;brightness;dim;dimAfter;dimLevel", the last four absent in an older text
      std::vector<long> f; size_t p = semi + 1;
      while (p <= s.size() && f.size() < 5) { size_t e = s.find(';', p); if (e == std::string::npos) e = s.size(); f.push_back(atol(s.substr(p, e - p).c_str())); p = e + 1; }
      if (f.size() > 0 && f[0] >= SECONDS_MIN && f[0] <= SECONDS_MAX) c.seconds = (uint8_t)f[0];
      if (f.size() > 1 && f[1] >= BRIGHT_MIN && f[1] <= BRIGHT_MAX) c.brightness = (uint8_t)f[1];
      if (f.size() > 2) c.dim = f[2] != 0;
      if (f.size() > 3 && f[3] >= DIM_AFTER_MIN && f[3] <= DIM_AFTER_MAX) c.dimAfter = (uint16_t)f[3];
      if (f.size() > 4 && f[4] >= 0 && f[4] <= DIM_LEVEL_MAX) c.dimLevel = (uint8_t)f[4];
    }
    std::vector<uint8_t> seen; size_t p = 0; int n = 0;
    while (p < list.size()) {
      size_t comma = list.find(',', p); if (comma == std::string::npos) comma = list.size();
      std::string item = list.substr(p, comma - p); p = comma + 1;
      size_t colon = item.find(':');
      int id = idOf(item.substr(0, colon));
      bool dup = false; for (uint8_t x : seen) if (x == id) dup = true;
      if (id < 0 || dup) continue;
      seen.push_back((uint8_t)id); c.order[n++] = (uint8_t)id; c.on[id] = colon == std::string::npos || item.substr(colon + 1) != "0";
    }
    for (int i = 0; i < COUNT; i++) { bool in = false; for (uint8_t x : seen) if (x == i) in = true; if (!in) c.order[n++] = (uint8_t)i; }   // screens added by a later version
    *this = c;
  }
};

}  // namespace firenet
