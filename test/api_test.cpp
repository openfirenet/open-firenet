// Host tests of the web API command parsing (open-firenet/firenet_api.h).
#include "firenet_api.h"
#include <iostream>
using namespace firenet;

static int ok = 0, ko = 0;
static void CH(const char* n, bool c) { if (c) ok++; else { ko++; std::cout << "ECHEC " << n << "\n"; } }

static std::map<std::string, long> fromJson(const std::string& j) {
  std::vector<std::pair<std::string, std::string>> p; jsonPairs(j, p); return parseControlCommands(p);
}

int main() {
  // --- texts put into JSON answers are always valid UTF-8 (open-firenet-ha issue #38)
  CH("json text: plain text unchanged", jsonText("iot-2.4G") == "iot-2.4G");
  CH("json text: quote and backslash escaped", jsonText("a\"b\\c") == "a\\\"b\\\\c");
  CH("json text: control characters dropped", jsonText(std::string("a\x01\n\tb")) == "ab");
  CH("json text: valid accents kept", jsonText("Caf\xC3\xA9 \xE2\x82\xAC") == "Caf\xC3\xA9 \xE2\x82\xAC");
  CH("json text: a lone 0xFF is replaced", jsonText(std::string("\xFF")) == "\xEF\xBF\xBD");
  CH("json text: a cut sequence is replaced", jsonText(std::string("ab\xC3")) == "ab\xEF\xBF\xBD");
  CH("json text: a stray continuation byte is replaced", jsonText(std::string("a\x80z")) == "a\xEF\xBF\xBDz");
  CH("json text: an overlong sequence is replaced", jsonText(std::string("\xC0\xAF")) == "\xEF\xBF\xBD\xEF\xBF\xBD");

  // Home Assistant integration payloads (JSON, °C floats, booleans)
  auto a = fromJson("{\"target_temperature\": 21.5}");
  CH("HA target temperature 21.5 °C -> 215", a.at("roomTarget") == 215);
  a = fromJson("{\"power_percent\": 65}");
  CH("HA power 65 %", a.at("targetStage") == 65);
  a = fromJson("{\"roomSensorPower\": 3}");
  CH("comfort power level 3", a.at("roomSensorPower") == 3);
  a = fromJson("{\"on\": true}");
  CH("HA on true", a.at("onOff") == 1);
  a = fromJson("{\"mode\": \"auto\"}");
  CH("HA mode auto -> 1", a.at("mode") == 1);
  a = fromJson("{\"frostProtectionTemp\": 5, \"frostProtectionActive\": false}");
  CH("HA frost 5 °C -> 50, active false -> 0", a.at("frostProtectionTemp") == 50 && a.at("frostProtectionActive") == 0);
  a = fromJson("{\"room_temperature_offset\": -0.5, \"setback_temperature\": 16.0, \"bakeTarget\": 200}");
  CH("HA offset -0.5 -> -5, setback 16 -> 160, bake 200", a.at("roomTempOffset") == -5 && a.at("setBackTemp") == 160 && a.at("bakeTarget") == 200);
  a = fromJson("{\"convectionFan2Active\": true, \"convectionFan2Level\": 3, \"convectionFan2Area\": -10}");
  CH("HA MultiAir 2 on / level 3 / area -10", a.at("convectionFan2Active") == 1 && a.at("convectionFan2Level") == 3 && a.at("convectionFan2Area") == -10);
  { std::vector<std::pair<std::string, std::string>> p = {{"on", "ON"}, {"eco_mode", "off"}, {"mode", "auto"}, {"target_temperature", "21.5"}};
    auto m = parseControlCommands(p);   // MQTT single-value topics: the payload is the value as text
    CH("MQTT values: ON, off, auto, 21.5", m.at("onOff") == 1 && m.at("ecoMode") == 0 && m.at("mode") == 1 && m.at("roomTarget") == 215); }
  a = fromJson("{\"ecoMode\": true}");
  CH("eco mode true", a.at("ecoMode") == 1);

  // Web page schedule (/api/schedule): values already x10, heating slots by control name
  a = fromJson("{\"heatingTimesActive\": 1, \"setBackTemp\": 160, \"heatTimeMon1\": 3600540, \"heatTimeSun2\": 0}");
  CH("schedule: active, setback already x10, slots", a.at("heatingTimesActive") == 1 && a.at("setBackTemp") == 160 &&
     a.at("heatTimeMon1") == 3600540 && a.at("heatTimeSun2") == 0);
  a = fromJson("{\"active\": true, \"slots\": {\"heatTimeTue1\": 42, \"heatTimeTue2\": 7}}");
  CH("nested object keys are found (slots)", a.at("heatTimeTue1") == 42 && a.at("heatTimeTue2") == 7 && !a.count("onOff"));

  // Web page single name/value pairs and form arguments (strings)
  std::vector<std::pair<std::string, std::string>> f = {{"name_unused", "x"}, {"on", "true"}, {"temperature", "22"}, {"fan1", "1"}};
  a = parseControlCommands(f);
  CH("form: on=true, temperature=22 -> 220, fan1=1", a.at("onOff") == 1 && a.at("roomTarget") == 220 && a.at("convectionFan1Active") == 1);
  a = parseControlCommands({{"roomTarget", "215"}, {"operatingMode", "0"}});
  CH("form: value already x10 kept, mode 0", a.at("roomTarget") == 215 && a.at("mode") == 0);

  // Historical text form
  std::vector<std::pair<std::string, std::string>> t; textPairs("onOff=1; operatingMode=2; heatingPower=70; tempRoomTarget=215;", t);
  a = parseControlCommands(t);
  CH("text form onOff/operatingMode/heatingPower/tempRoomTarget", a.at("onOff") == 1 && a.at("mode") == 2 && a.at("targetStage") == 70 && a.at("roomTarget") == 215);

  // roomTempOffset historical form: decimals -> x10, integers already x10
  a = parseControlCommands({{"roomTempOffset", "-0.5"}});   CH("roomTempOffset -0.5 -> -5", a.at("roomTempOffset") == -5);
  a = parseControlCommands({{"roomTempOffset", "-20"}});    CH("roomTempOffset -20 kept", a.at("roomTempOffset") == -20);

  // later pairs override earlier ones; unknown keys and unreadable values are ignored
  a = parseControlCommands({{"power", "40"}, {"heatingPower", "55"}, {"foo", "1"}, {"mode", "nonsense"}});
  CH("later pair wins, unknown / unreadable ignored", a.at("targetStage") == 55 && !a.count("foo") && !a.count("mode"));

  std::cout << ok << " api checks ok, " << ko << " failures\n";
  return ko ? 1 : 0;
}
