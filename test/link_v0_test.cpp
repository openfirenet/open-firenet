#include "firenet_link_v0.h"
#include "firenet_v0_map.h"
#include <iostream>
using namespace firenet;

int main() {
  int ok = 0, ko = 0;
  auto CH = [&](const char* n, bool c) { if (c) ok++; else { ko++; std::cout << "ECHEC " << n << "\n"; } };

  // codec: read frame
  CH("read frame 001", v0::readFrame(1) == std::string("\x02" "001" "\x03", 5));
  CH("read frame 000", v0::readFrame(0) == std::string("\x02" "000" "\x03", 5));
  CH("read frame out of range", v0::readFrame(1000).empty() && v0::readFrame(-1).empty());

  // decoder
  std::vector<std::pair<int, std::string>> ev;
  v0::Decoder d([&](v0::Decoder::Kind k, const std::string& f) { ev.push_back({(int)k, f}); });
  for (int b : std::initializer_list<int>{0x06, 0x15}) d.feed((uint8_t)b);
  CH("ack and nak", ev.size() == 2 && ev[0].first == (int)v0::Decoder::Kind::Ack && ev[1].first == (int)v0::Decoder::Kind::Nak);
  ev.clear();
  for (int b : std::initializer_list<int>{0x02, '2', '3', '6', 0x03}) d.feed((uint8_t)b);
  CH("value frame", ev.size() == 1 && ev[0].first == (int)v0::Decoder::Kind::Frame && ev[0].second == "236");
  ev.clear();
  for (int b : std::initializer_list<int>{'x', 'y', 0x02, '1', 0x02, '5', 0x03}) d.feed((uint8_t)b);
  CH("garbage outside frame ignored, STX restarts the frame", ev.size() == 1 && ev[0].second == "5");
  ev.clear();
  d.feed(0x02);
  for (int i = 0; i < 70; i++) d.feed('7');
  d.feed(0x03);
  CH("overlong frame dropped", ev.empty());
  for (int b : std::initializer_list<int>{0x02, '9', 0x03}) d.feed((uint8_t)b);
  CH("decoder recovers after an overlong frame", ev.size() == 1 && ev[0].second == "9");

  // probe trial
  std::string wire; uint32_t clk = 1000;
  LinkV0Probe p([&](const uint8_t* b, size_t n) { wire.append((const char*)b, n); }, [&]() { return clk; });
  std::vector<std::string> log;
  p.onDebug([&](const char* dir, const std::string& f) { log.push_back(std::string(dir) + ":" + f); });
  p.poll(false); clk += 20000;
  p.poll(false);
  CH("silent without a 0x16 probe", wire.empty() && !p.engaged());
  p.onByte(0x16); p.onByte('3');
  clk += 1000; p.poll(false);
  CH("waits ENGAGE_MS after the probe", wire.empty() && !p.engaged());
  clk += LinkV0Probe::ENGAGE_MS; p.poll(false);
  CH("engages with a single ACK", p.engaged() && wire == std::string(1, '\x06'));
  wire.clear();
  p.poll(false);
  CH("no read before READ_GAP_MS", wire.empty());
  std::string expect;
  for (int i = 0; i < LinkV0Probe::CYCLE_STEPS; i++) {
    clk += LinkV0Probe::READ_GAP_MS; p.poll(false);
    expect += v0::readFrame(LinkV0Probe::codeAt(i));
  }
  CH("reads every code of CODES in turn", wire == expect);
  CH("a cycle is the live codes then 8 settings, all 3 digits", LinkV0Probe::CODES[0] == 27 && LinkV0Probe::CODES[1] == 28 && LinkV0Probe::codeAt(LinkV0Probe::CODE_COUNT) == 23 && wire.size() == 5u * LinkV0Probe::CYCLE_STEPS);
  {
    bool all = true;
    for (int k = 0; k < LinkV0Probe::CFG_COUNT; k++) {
      bool found = false;
      for (int st = 0; st < LinkV0Probe::CYCLE_STEPS * 12; st++) if (LinkV0Probe::codeAt(st) == LinkV0Probe::CFG[k]) found = true;
      all = all && found && LinkV0Probe::CFG[k] > 0 && LinkV0Probe::CFG[k] < 1000;
    }
    CH("every setting code comes up within 12 cycles", all);
  }
  for (int b : std::initializer_list<int>{0x02, '2', '1', '5', 0x03}) p.onByte((uint8_t)b);
  CH("reply is logged", p.replies() == 1 && !log.empty() && log.back() == "v0 rx:038=215");
  // nothing was asked to be written: the sent bytes never contain '='
  CH("no write frame unless asked", wire.find('=') == std::string::npos);
  // same value again: not logged twice; a new value is
  size_t nlog = log.size();
  for (int b : std::initializer_list<int>{0x02, '2', '1', '5', 0x03}) p.onByte((uint8_t)b);
  CH("an unchanged value is not logged again", log.size() == nlog);
  for (int b : std::initializer_list<int>{0x02, '2', '1', '6', 0x03}) p.onByte((uint8_t)b);
  CH("a changed value is logged", log.size() == nlog + 1 && log.back() == "v0 rx:038=216");
  // the stove answered: reading goes on past ROUNDS, the text link stays paused
  for (int i = 0; i < LinkV0Probe::CYCLE_STEPS * LinkV0Probe::ROUNDS + 60; i++) { clk += LinkV0Probe::READ_GAP_MS; p.poll(false); }
  CH("keeps reading while the stove answers", p.engaged() && wire.size() > 5u * LinkV0Probe::CYCLE_STEPS * LinkV0Probe::ROUNDS);
  // no answer at all: stops after ROUNDS
  std::string wire2; uint32_t clk2 = 1000;
  LinkV0Probe m([&](const uint8_t* b, size_t n) { wire2.append((const char*)b, n); }, [&]() { return clk2; });
  m.onByte(0x16); clk2 += LinkV0Probe::ENGAGE_MS; m.poll(false);
  for (int i = 0; i < LinkV0Probe::CYCLE_STEPS * LinkV0Probe::ROUNDS + 80; i++) { clk2 += LinkV0Probe::READ_GAP_MS; m.poll(false); }
  CH("stops after ROUNDS without any reply", !m.engaged() && wire2.size() == 1 + 5u * LinkV0Probe::CYCLE_STEPS * LinkV0Probe::ROUNDS);
  // text dialect acknowledged: stays out of the way
  LinkV0Probe q([&](const uint8_t*, size_t) { ko++; std::cout << "ECHEC sent while acked\n"; }, [&]() { return clk; });
  q.onByte(0x16); clk += 20000; q.poll(true); q.poll(true);
  CH("quiet once the text link is acknowledged", !q.engaged());

  // values into the shared model
  {
    StoveModel m;
    CH("room temperature is stored in tenths", v0map::apply(m, 0, "18", 5) && m.sensors["roomTemp"] == 180 && m.v0_linked && m.last_rx_ms == 5);
    CH("flame temperature as is", v0map::apply(m, 1, "276", 6) && m.sensors["flame"] == 276);
    CH("main and sub state", v0map::apply(m, 27, "2", 7) && v0map::apply(m, 28, "9", 8) && m.sensors["mainState"] == 2 && m.sensors["subState"] == 9 && m.main_state == 2 && m.sub_state == 9);
    CH("schedule switch", v0map::apply(m, 80, "1", 9) && m.controls["heatingTimesActive"] == 1);
    CH("daily ranges, Sunday first, in minutes", v0map::apply(m, 65, "1020", 9) && v0map::apply(m, 66, "1320", 9) && v0map::apply(m, 77, "60", 9) && v0map::apply(m, 78, "120", 9) &&
       m.v0_sched[0] == 1020 && m.v0_sched[1] == 1320 && m.v0_sched[12] == 60 && m.v0_sched[13] == 120 && !m.controls.count("heatTimeSun1"));
    CH("pressure switch and unidentified inputs", v0map::apply(m, 12, "1", 9) && m.sensors["pressureSwitch"] == 1 && v0map::apply(m, 16, "1", 9) && m.sensors["in016"] == 1);
    CH("comfort power level and room offset are named", v0map::apply(m, 92, "3", 10) && m.controls["roomSensorPower"] == 3 && v0map::apply(m, 91, "-10", 10) && m.controls["roomTempOffset"] == -10);
    CH("settings go to the controls", v0map::apply(m, 26, "21", 10) && m.controls["roomTarget"] == 210 && v0map::apply(m, 24, "2", 10) && m.controls["mode"] == 2);
    CH("frost protection and set back", v0map::apply(m, 88, "1", 10) && m.controls["frostProtectionActive"] == 1 && v0map::apply(m, 89, "5", 10) && m.controls["frostProtectionTemp"] == 50 && v0map::apply(m, 81, "17", 10) && m.controls["setBackTemp"] == 170);
    CH("text and unknown codes are not stored", !v0map::apply(m, 901, "SUMO", 11) && !v0map::apply(m, 6, "0", 11) && m.sensors.count("in006") == 0);
    CH("negative values parse", v0map::apply(m, 7, "-3", 12) && m.sensors["idFanMeas"] == -3);
    CH("the text link stays not acknowledged", !m.version_ack && !m.controls_synced);
  }
  {
    std::string w2; uint32_t c2 = 1000; LinkV0Probe pr([&](const uint8_t* b, size_t n) { w2.append((const char*)b, n); }, [&]() { return c2; });
    std::vector<std::pair<int, std::string>> got;
    pr.onValue([&](int c, const std::string& t) { got.push_back({c, t}); });
    pr.onByte(0x16); c2 += LinkV0Probe::ENGAGE_MS; pr.poll(false);
    c2 += LinkV0Probe::READ_GAP_MS; pr.poll(false);
    for (int b : std::initializer_list<int>{0x02, '1', '2', 0x03}) pr.onByte((uint8_t)b);
    CH("a reply is delivered with the code that was asked", got.size() == 1 && got[0].first == 27 && got[0].second == "12");
  }

  // writes: only the allowed codes and values are ever built
  {
    CH("write frame 026", v0::writeFrame(26, 21) == std::string("\x02" "026=21" "\x03", 8));
    CH("write frame 025", v0::writeFrame(25, 50) == std::string("\x02" "025=50" "\x03", 8));
    CH("write frame 024", v0::writeFrame(24, 2) == std::string("\x02" "024=2" "\x03", 7));
    CH("write frame 023 stop", v0::writeFrame(23, 0) == std::string("\x02" "023=0" "\x03", 7));
    CH("start frame 023=1, but no other value", v0::writeFrame(23, 1) == std::string("\x02" "023=1" "\x03", 7) && v0::writeFrame(23, 2).empty() && v0::writeFrame(23, 255).empty());
    CH("a start is allowed only on a cold, idle stove without error",
       v0::startAllowed(0, 1, 0, 20, 0) && v0::startAllowed(0, 11, 1, 20, 0) &&
       !v0::startAllowed(1, 1, 0, 20, 0) &&        // an error
       !v0::startAllowed(0, 2, 2, 20, 0) && !v0::startAllowed(0, 4, 1, 200, 0) && !v0::startAllowed(0, 6, 1, 20, 0) &&   // not off or waiting
       !v0::startAllowed(0, 1, 2, 20, 0) &&        // sub state above 1
       !v0::startAllowed(0, 1, 0, 100, 0) && !v0::startAllowed(0, 1, 0, 300, 0) &&   // not cold
       !v0::startAllowed(0, 1, 0, 20, 1));         // already on
    CH("power only 30..100 in steps of 5", v0::writeFrame(25, 25).empty() && v0::writeFrame(25, 105).empty() && v0::writeFrame(25, 53).empty() && !v0::writeFrame(25, 30).empty() && !v0::writeFrame(25, 100).empty());
    CH("mode only 0..2", v0::writeFrame(24, 3).empty() && v0::writeFrame(24, -1).empty());
    CH("set point only 14..28", v0::writeFrame(26, 13).empty() && v0::writeFrame(26, 29).empty() && !v0::writeFrame(26, 14).empty() && !v0::writeFrame(26, 28).empty());
    CH("write frame 081 088 089 092", v0::writeFrame(81, 17) == std::string("\x02" "081=17" "\x03", 8) && v0::writeFrame(88, 1) == std::string("\x02" "088=1" "\x03", 7) &&
       v0::writeFrame(89, 5) == std::string("\x02" "089=5" "\x03", 7) && v0::writeFrame(92, 3) == std::string("\x02" "092=3" "\x03", 7));
    CH("schedule switch 080: 0 or 1", v0::writeFrame(80, 0) == std::string("\x02" "080=0" "\x03", 7) && v0::writeFrame(80, 1) == std::string("\x02" "080=1" "\x03", 7) && v0::writeFrame(80, 2).empty() && v0::writeFrame(80, -1).empty());
    CH("reduced set point only 12..22", v0::writeFrame(81, 11).empty() && v0::writeFrame(81, 23).empty() && !v0::writeFrame(81, 12).empty() && !v0::writeFrame(81, 22).empty());
    CH("frost protection: 0 or 1, temperature 4..10", v0::writeFrame(88, 2).empty() && v0::writeFrame(88, -1).empty() && v0::writeFrame(89, 3).empty() && v0::writeFrame(89, 11).empty() && !v0::writeFrame(89, 4).empty() && !v0::writeFrame(89, 10).empty());
    CH("comfort power level only 1..4", v0::writeFrame(92, 0).empty() && v0::writeFrame(92, 5).empty() && !v0::writeFrame(92, 1).empty() && !v0::writeFrame(92, 4).empty());
    CH("dangerous and other codes are never built", v0::writeFrame(27, 1).empty() && v0::writeFrame(28, 1).empty() && v0::writeFrame(82, 17).empty() && v0::writeFrame(91, 1).empty() && v0::writeFrame(65, 1020).empty() && v0::writeFrame(900, 1).empty() && v0::writeFrame(-1, 1).empty());

    std::string w; uint32_t c = 1000; LinkV0Probe pw([&](const uint8_t* b, size_t n) { w.append((const char*)b, n); }, [&]() { return c; });
    std::vector<std::string> lg; pw.onDebug([&](const char* d, const std::string& f) { lg.push_back(std::string(d) + ":" + f); });
    CH("no write before the link is up", !pw.requestWrite(26, 21));
    pw.onByte(0x16); c += LinkV0Probe::ENGAGE_MS; pw.poll(false);
    CH("no write before the stove answered", !pw.requestWrite(26, 21));
    c += LinkV0Probe::READ_GAP_MS; pw.poll(false);
    for (int b : std::initializer_list<int>{0x02, '1', '2', 0x03}) pw.onByte((uint8_t)b);
    CH("a refused value is not queued", !pw.requestWrite(25, 53) && !pw.requestWrite(23, 2));
    CH("an allowed value is queued", pw.requestWrite(26, 21));
    w.clear();
    c += LinkV0Probe::READ_GAP_MS; pw.poll(false);
    CH("the write is sent first", w == v0::writeFrame(26, 21));
    CH("the write is logged", !lg.empty() && lg.back() == "v0 write:02 30 32 36 3D 32 31 03 ");
    w.clear();
    c += LinkV0Probe::READ_GAP_MS; pw.poll(false);
    CH("then the same code is read back", w == v0::readFrame(26));
    for (int b : std::initializer_list<int>{0x02, '2', '1', 0x03}) pw.onByte((uint8_t)b);
    CH("the read back is logged", lg.back() == "v0 rx:026=21");
    for (int b : std::initializer_list<int>{0x02, '2', '1', 0x03}) pw.onByte((uint8_t)b);
    size_t before = lg.size();
    for (int b : std::initializer_list<int>{0x02, '2', '1', 0x03}) pw.onByte((uint8_t)b);
    CH("an unchanged value is still not logged twice", lg.size() == before);
    // a new 0x16 (USB reconnected) drops what was waiting
    CH("queue accepts several", pw.requestWrite(25, 60) && pw.requestWrite(24, 0));
    pw.onByte(0x16);
    w.clear(); c += LinkV0Probe::ENGAGE_MS; pw.poll(false);
    c += LinkV0Probe::READ_GAP_MS; pw.poll(false);
    CH("pending writes are dropped on reconnection", w.find('=') == std::string::npos);
  }

  std::cout << "link_v0_test: " << ok << " ok, " << ko << " ko\n";
  return ko ? 1 : 0;
}
