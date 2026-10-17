#include "firenet_link_v0.h"
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
  for (int i = 0; i < LinkV0Probe::CODE_COUNT; i++) {
    clk += LinkV0Probe::READ_GAP_MS; p.poll(false);
    expect += v0::readFrame(LinkV0Probe::CODES[i]);
  }
  CH("reads every code of CODES in turn", wire == expect);
  CH("codes list starts 001, 000 and ends 040", LinkV0Probe::CODES[0] == 1 && LinkV0Probe::CODES[1] == 0 && LinkV0Probe::CODES[LinkV0Probe::CODE_COUNT - 1] == 40 && wire.size() == 5u * LinkV0Probe::CODE_COUNT);
  for (int b : std::initializer_list<int>{0x02, '2', '1', '5', 0x03}) p.onByte((uint8_t)b);
  CH("reply is logged", p.replies() == 1 && !log.empty() && log.back() == "v0 rx:FRAME 215");
  // the sent bytes never contain '=' (no write frame can be built)
  CH("never a write frame", wire.find('=') == std::string::npos);
  for (int i = 0; i < 60; i++) { clk += LinkV0Probe::READ_GAP_MS; p.poll(false); }
  CH("stops after ROUNDS", !p.engaged() && wire.size() == 1 * 0 + 5 * LinkV0Probe::CODE_COUNT * LinkV0Probe::ROUNDS);
  // text dialect acknowledged: stays out of the way
  LinkV0Probe q([&](const uint8_t*, size_t) { ko++; std::cout << "ECHEC sent while acked\n"; }, [&]() { return clk; });
  q.onByte(0x16); clk += 20000; q.poll(true); q.poll(true);
  CH("quiet once the text link is acknowledged", !q.engaged());

  std::cout << "link_v0_test: " << ok << " ok, " << ko << " ko\n";
  return ko ? 1 : 0;
}
