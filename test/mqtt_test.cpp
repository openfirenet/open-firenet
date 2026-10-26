// Host tests of the MQTT helpers (open-firenet/firenet_mqtt.h).
#include "firenet_mqtt.h"
#include <iostream>
#include <map>
using namespace firenet;

static int ok = 0, ko = 0;
static void CH(const char* n, bool c) { if (c) ok++; else { ko++; std::cout << "ECHEC " << n << "\n"; } }

int main() {
  // --- flattening of the state JSON into one topic per value
  {
    std::vector<std::pair<std::string, std::string>> f;
    mqtt::flattenSections("{\"stove\":{\"state\":\"heating\",\"state_code\":4,\"model\":null,\"state_label\":\"Grate Cleaning\"},"
                          "\"sensors\":{\"room_temperature\":21.5,\"deep\":{\"x\":1},\"last\":-3},\"top\":7,\"controls\":{\"on\":true},"
                          "\"schedule\":{\"heatTimeMon1\":6000800,\"heatTimeSun2\":0}}", f);
    std::map<std::string, std::string> m(f.begin(), f.end());
    CH("flatten: string without quotes", m["stove/state"] == "heating");
    CH("flatten: number, null, boolean, negative", m["stove/state_code"] == "4" && m["stove/model"] == "null" && m["controls/on"] == "true" && m["sensors/last"] == "-3");
    CH("flatten: string with a space", m["stove/state_label"] == "Grate Cleaning");
    CH("flatten: float", m["sensors/room_temperature"] == "21.5");
    CH("flatten: schedule slots", m["schedule/heatTimeMon1"] == "6000800" && m["schedule/heatTimeSun2"] == "0");
    CH("flatten: deeper levels and top-level scalars ignored", f.size() == 9 && !m.count("sensors/x") && !m.count("deep/x") && !m.count("top"));
  }

  // --- walk without copies, and the hashes of what was published
  {
    std::string json = "{\"stove\":{\"state\":\"heating\",\"n\":4},\"sensors\":{\"t\":21.5,\"deep\":{\"x\":1}},\"top\":7}";
    std::vector<std::string> seen;
    mqtt::forEachValue(json, [&](std::string_view s, std::string_view k, std::string_view v) { seen.push_back(std::string(s) + "/" + std::string(k) + "=" + std::string(v)); return true; });
    CH("walk: order and content", seen.size() == 3 && seen[0] == "stove/state=heating" && seen[1] == "stove/n=4" && seen[2] == "sensors/t=21.5");
    int n = 0;
    mqtt::forEachValue(json, [&](std::string_view, std::string_view, std::string_view) { return ++n < 2; });
    CH("walk: stops when the callback says so", n == 2);
    mqtt::SentValues sv;
    uint64_t t1 = mqtt::SentValues::topicHash("stove", "state"), t2 = mqtt::SentValues::topicHash("stove", "n");
    CH("sent: nothing known at first", !sv.same(t1, mqtt::SentValues::valueHash("heating")));
    sv.set(t2, mqtt::SentValues::valueHash("4")); sv.set(t1, mqtt::SentValues::valueHash("heating"));
    CH("sent: same value", sv.same(t1, mqtt::SentValues::valueHash("heating")) && sv.same(t2, mqtt::SentValues::valueHash("4")));
    CH("sent: other value", !sv.same(t1, mqtt::SentValues::valueHash("off")));
    sv.set(t1, mqtt::SentValues::valueHash("off"));
    CH("sent: updated, still two topics", sv.same(t1, mqtt::SentValues::valueHash("off")) && sv.size() == 2);
    CH("sent: section and key are not interchangeable", mqtt::SentValues::topicHash("a", "bc") != mqtt::SentValues::topicHash("ab", "c"));
    sv.clear();
    CH("sent: cleared", sv.size() == 0 && !sv.same(t1, mqtt::SentValues::valueHash("off")));
  }

  // --- command topics
  {
    std::string name = "x";
    CH("command: <base>/set carries a whole payload", mqtt::commandTopic("openfirenet", "openfirenet/set", name) && name.empty());
    CH("command: <base>/set/<name>", mqtt::commandTopic("openfirenet", "openfirenet/set/target_temperature", name) && name == "target_temperature");
    CH("command: base with a slash", mqtt::commandTopic("home/stove", "home/stove/set/on", name) && name == "on");
    CH("command: a state topic is not a command", !mqtt::commandTopic("openfirenet", "openfirenet/state", name));
    CH("command: <base>/set/ without a name is not a command", !mqtt::commandTopic("openfirenet", "openfirenet/set/", name));
    CH("command: another base is not a command", !mqtt::commandTopic("openfirenet", "openfirenet2/set/on", name));
    CH("command: <base>/settings is not a command", !mqtt::commandTopic("openfirenet", "openfirenet/settings", name));
  }

  // --- Home Assistant discovery
  {
    mqtt::DiscoveryDevice dev{"openfirenet_45eac8", "home/stove", "DOMO", "3.4.0", "http://192.168.1.50"};
    std::string topic, payload;
    size_t n = mqtt::discoveryCount();
    CH("discovery: at least 20 entities", n >= 20);
    CH("discovery: nothing past the last entity", !mqtt::discoveryEntity(n, dev, false, topic, payload));
    std::map<std::string, int> topics, ids;
    bool allFit = true, allBalanced = true, allTilde = true, allAvail = true;
    for (size_t i = 0; i < n; i++) {
      mqtt::discoveryEntity(i, dev, false, topic, payload);
      topics[topic]++;
      size_t u = payload.find("\"unique_id\":\""); ids[payload.substr(u + 13, payload.find('"', u + 13) - u - 13)]++;
      if (payload.size() > 1500) allFit = false;
      int depth = 0, brackets = 0; bool inStr = false;                       // braces and brackets balanced outside strings
      for (size_t k = 0; k < payload.size(); k++) {
        char c = payload[k];
        if (c == '"' && (k == 0 || payload[k - 1] != '\\')) inStr = !inStr;
        if (inStr) continue;
        depth += (c == '{') - (c == '}');
        brackets += (c == '[') - (c == ']');
      }
      if (depth != 0 || brackets != 0 || inStr || payload.find(",}") != std::string::npos || payload.find(",]") != std::string::npos) allBalanced = false;
      if (payload.find("\"~\":\"home/stove\"") == std::string::npos) allTilde = false;
      if (payload.find("\"availability\":[{\"topic\":\"~/availability\"}") == std::string::npos) allAvail = false;
    }
    CH("discovery: one topic per entity", topics.size() == n);
    CH("discovery: unique ids are unique", ids.size() == n);
    CH("discovery: messages fit in the MQTT buffer", allFit);
    CH("discovery: well-formed JSON (balanced, no trailing comma)", allBalanced);
    CH("discovery: base topic given once, as ~", allTilde);
    CH("discovery: availability on every entity", allAvail);
    mqtt::discoveryEntity(0, dev, false, topic, payload);
    CH("discovery: first entity is the climate, under the device id", topic == "homeassistant/climate/openfirenet_45eac8/stove/config");
    CH("discovery: the climate carries the device description", payload.find("\"manufacturer\":\"RIKA\",\"model\":\"DOMO\",\"sw_version\":\"3.4.0\"") != std::string::npos);
    CH("discovery: the climate is named after the device", payload.find("\"name\":null") != std::string::npos);
    mqtt::discoveryEntity(1, dev, false, topic, payload);
    CH("discovery: every entity describes the device", payload.find("\"device\":{\"identifiers\":[\"openfirenet_45eac8\"],\"name\":\"Open Firenet\"") != std::string::npos);
    mqtt::discoveryEntity(n - 1, dev, false, topic, payload);
    CH("discovery: the link sensor does not depend on the link to be available", topic.find("/connected/config") != std::string::npos && payload.find("payload_not_available") == std::string::npos);
    mqtt::discoveryEntity(3, dev, true, topic, payload);
    CH("discovery: removal is an empty message on the same topic", payload.empty() && topic.find("homeassistant/") == 0);
    mqtt::DiscoveryDevice unknown{"openfirenet_45eac8", "openfirenet", "", "", ""};
    mqtt::discoveryEntity(0, unknown, false, topic, payload);
    CH("discovery: model unknown yet", payload.find("\"model\":\"Pellet stove\"}}") != std::string::npos);
  }

  std::cout << "mqtt: " << ok << " ok, " << ko << " failed\n";
  return ko ? 1 : 0;
}
