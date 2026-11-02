#include "firenet_display_config.h"
#include <iostream>
using firenet::DisplayConfig;

int main() {
  int ok = 0, ko = 0;
  auto CH = [&](const char* n, bool c) { if (c) ok++; else { ko++; std::cout << "ECHEC " << n << "\n"; } };

  DisplayConfig c;
  CH("default order and all on", std::string(c.json(true)).find("{\"id\":\"state\",\"enabled\":true},{\"id\":\"room\"") != std::string::npos && c.seconds == 4);
  CH("json says whether a display exists", c.json(false).rfind("{\"available\":false,", 0) == 0 && c.json(true).rfind("{\"available\":true,", 0) == 0);

  CH("the split log hours screen is known, after the pellet hours", DisplayConfig::idOf("log_hours") == DisplayConfig::LOG_HOURS && DisplayConfig::LOG_HOURS == DisplayConfig::HOURS + 1 && c.on[DisplayConfig::LOG_HOURS]);
  {
    DisplayConfig old; old.fromPrefs("state:1,room:1,flame:1,power:1,ip:1,pellets:1,hours:0,wifi:1,mqtt:1,version:1,logo:1;4;50");
    CH("a text saved before the split log screen still loads, with that screen last and on", old.order[DisplayConfig::COUNT - 1] == DisplayConfig::LOG_HOURS && old.on[DisplayConfig::LOG_HOURS] && !old.on[DisplayConfig::HOURS]);
  }
  CH("only known boards are valid", DisplayConfig::validBoard("none") && DisplayConfig::validBoard("tdongle") && DisplayConfig::validBoard("tdongle_off") && !DisplayConfig::validBoard("") && !DisplayConfig::validBoard("TDONGLE") && !DisplayConfig::validBoard("lilygo"));
  CH("json carries the board and whether a restart is needed",
     c.json(false, "tdongle", true).find("\"board\":\"tdongle\"") != std::string::npos && c.json(false, "tdongle", true).find("\"restart_needed\":true") != std::string::npos &&
     c.json(true).find("\"board\":\"none\"") != std::string::npos && c.json(true).find("\"restart_needed\":false") != std::string::npos);

  CH("a listed screen moves first and can be switched off",
     c.apply("{\"screens\":[{\"id\":\"ip\",\"enabled\":true},{\"id\":\"room\",\"enabled\":false}]}") &&
     c.order[0] == DisplayConfig::IP && c.order[1] == DisplayConfig::ROOM && !c.on[DisplayConfig::ROOM]);
  CH("screens left out follow in their previous order", c.order[2] == DisplayConfig::STATE && c.order[3] == DisplayConfig::FLAME && c.order[DisplayConfig::COUNT - 1] == DisplayConfig::LOGO);
  CH("every screen is still there once", [&] { int seen[DisplayConfig::COUNT] = {0}; for (int i = 0; i < DisplayConfig::COUNT; i++) seen[c.order[i]]++; for (int v : seen) if (v != 1) return false; return true; }());

  DisplayConfig before = c;
  CH("an unknown id is refused and changes nothing", !c.apply("{\"screens\":[{\"id\":\"nope\",\"enabled\":true}]}") && c.toPrefs() == before.toPrefs());
  CH("a repeated id is refused", !c.apply("{\"screens\":[{\"id\":\"ip\"},{\"id\":\"ip\"}]}") && c.toPrefs() == before.toPrefs());
  CH("a bad enabled value is refused", !c.apply("{\"screens\":[{\"id\":\"ip\",\"enabled\":maybe}]}") && c.toPrefs() == before.toPrefs());
  CH("seconds out of range are refused", !c.apply("{\"seconds\":1}") && !c.apply("{\"seconds\":31}") && c.seconds == 4);
  CH("seconds alone are taken", c.apply("{\"seconds\":10}") && c.seconds == 10 && c.order[0] == DisplayConfig::IP);
  CH("enabled may be left out: the screen keeps its state", c.apply("{\"screens\":[{\"id\":\"room\"}]}") && !c.on[DisplayConfig::ROOM] && c.order[0] == DisplayConfig::ROOM);

  DisplayConfig d; d.fromPrefs(c.toPrefs());
  CH("stored text gives the same setting back", d.toPrefs() == c.toPrefs() && d.seconds == 10);
  DisplayConfig e; e.fromPrefs("ip:0,wifi:1;99");
  CH("a short or older stored text is completed", e.order[0] == DisplayConfig::IP && !e.on[DisplayConfig::IP] && e.order[1] == DisplayConfig::WIFI && e.on[DisplayConfig::STATE] && e.seconds == 4 &&
     [&] { int seen[DisplayConfig::COUNT] = {0}; for (int i = 0; i < DisplayConfig::COUNT; i++) seen[e.order[i]]++; for (int v : seen) if (v != 1) return false; return true; }());
  DisplayConfig f; f.fromPrefs("garbage,,::;");
  CH("garbage in storage gives the default", f.toPrefs() == DisplayConfig().toPrefs());

  {
    DisplayConfig b;
    CH("backlight defaults: half, no dimming", b.brightness == 50 && !b.dim && b.dimAfter == 120 && b.dimLevel == 10);
    CH("backlight settings are taken", b.apply("{\"brightness\":80,\"dim\":true,\"dim_after\":300,\"dim_level\":0}") && b.brightness == 80 && b.dim && b.dimAfter == 300 && b.dimLevel == 0);
    DisplayConfig keep = b;
    CH("out-of-range backlight values are refused and change nothing", !b.apply("{\"brightness\":2}") && !b.apply("{\"brightness\":101}") && !b.apply("{\"dim_after\":5}") && !b.apply("{\"dim_level\":80}") && !b.apply("{\"dim\":maybe}") && b.toPrefs() == keep.toPrefs());
    CH("dim alone does not touch dim_after", b.apply("{\"dim\":false}") && !b.dim && b.dimAfter == 300);
    DisplayConfig r; r.fromPrefs(b.toPrefs());
    CH("backlight settings survive storage", r.brightness == 80 && !r.dim && r.dimAfter == 300 && r.dimLevel == 0);
    DisplayConfig o; o.fromPrefs("ip:1,wifi:0;6");
    CH("an older stored text keeps the backlight defaults", o.seconds == 6 && o.brightness == 50 && !o.dim && o.dimAfter == 120);
    DisplayConfig ro;
    CH("rotation is off by default and can be turned on", !ro.rotate && ro.apply("{\"rotate\":true}") && ro.rotate && ro.json(true).find("\"rotate\":true") != std::string::npos);
    DisplayConfig ro2; ro2.fromPrefs(ro.toPrefs());
    CH("rotation survives storage", ro2.rotate);
    CH("a bad rotate value is refused and changes nothing", !ro.apply("{\"rotate\":maybe}") && ro.rotate);
    CH("rotating off again", ro.apply("{\"rotate\":false}") && !ro.rotate);
    DisplayConfig ro3; ro3.fromPrefs("ip:1;4;50;0;120;10");
    CH("an older stored text is not rotated", !ro3.rotate);
    DisplayConfig lg;
    CH("no language chosen at first", lg.language == 0 && lg.json(true).find("\"language\":\"\"") != std::string::npos);
    CH("a language is taken", lg.apply("{\"language\":\"fr\"}") && lg.language == 2 && lg.json(true).find("\"language\":\"fr\"") != std::string::npos);
    CH("an unknown language is refused and changes nothing", !lg.apply("{\"language\":\"xx\"}") && !lg.apply("{\"language\":fr}") && lg.language == 2);
    CH("Spanish is a screen language", lg.apply("{\"language\":\"es\"}") && lg.language == 5 && lg.json(true).find("\"language\":\"es\"") != std::string::npos);
    DisplayConfig lgEs; lgEs.fromPrefs(lg.toPrefs());
    CH("Spanish survives storage", lgEs.language == 5);
    lg.apply("{\"language\":\"fr\"}");
    DisplayConfig lg2; lg2.fromPrefs(lg.toPrefs());
    CH("the language survives storage", lg2.language == 2);
    DisplayConfig lg3; lg3.fromPrefs("ip:1;4;50;0;120;10;1");
    CH("an older stored text has no language", lg3.language == 0 && lg3.rotate);
    CH("json carries the backlight", b.json(true).find("\"brightness\":80,\"dim\":false,\"dim_after\":300,\"dim_level\":0") != std::string::npos);
  }

  std::cout << "display_config_test: " << ok << " ok, " << ko << " ko\n";
  return ko ? 1 : 0;
}
