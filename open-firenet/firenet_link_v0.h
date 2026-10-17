// Link to a stove running firmware 2.25 (SUMO / SONO / INDUO): STX/ETX framed numeric dialect, read-only trial.
//
// What was read in the SUMO 2.25 main board (FINDINGS.md, "SUMO 2.25" sections; nothing here was tried on a stove yet):
//  - after the USB device is configured the stove sends 0x16 0x33 once (fn 0x80034b34);
//  - its frame decoder (fn 0x80034c34) collects bytes between STX (0x02) and ETX (0x03) while its receive state is 1
//    (probe sent) or 2 (an ACK 0x06 was seen); a frame of 3 bytes "NNN" is a READ, "NNN=value" is a WRITE;
//  - it answers STX digits ETX (value), ACK 0x06 or NAK 0x15.
// The meaning of the codes is NOT known (unverified labels only), so this class only reads three codes and logs
// the replies. It has no way to build a write frame on purpose.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace firenet {

namespace v0 {
constexpr uint8_t STX = 0x02, ETX = 0x03, ACK = 0x06, NAK = 0x15, SYN = 0x16;

// "STX NNN ETX": read of a 3-digit code. Returns an empty string for anything but 000..999.
inline std::string readFrame(int code) {
  if (code < 0 || code > 999) return std::string();
  char b[8];
  snprintf(b, sizeof b, "%c%03d%c", (char)STX, code, (char)ETX);
  return std::string(b, 5);
}

// Splits what the stove sends into ACK / NAK / STX..ETX frames. Bytes outside a frame are ignored.
class Decoder {
public:
  enum class Kind { Ack, Nak, Frame };
  using Sink = std::function<void(Kind, const std::string&)>;
  explicit Decoder(Sink s) : sink_(s) {}
  void feed(uint8_t b) {
    if (inFrame_) {
      if (b == ETX) { inFrame_ = false; sink_(Kind::Frame, buf_); buf_.clear(); return; }
      if (b == STX) { buf_.clear(); return; }   // a new STX restarts the frame
      if (buf_.size() < MAX_FRAME) buf_ += (char)b; else { inFrame_ = false; buf_.clear(); }
      return;
    }
    if (b == STX) { inFrame_ = true; buf_.clear(); }
    else if (b == ACK) sink_(Kind::Ack, std::string());
    else if (b == NAK) sink_(Kind::Nak, std::string());
  }
  static constexpr size_t MAX_FRAME = 64;
private:
  Sink sink_;
  bool inFrame_ = false;
  std::string buf_;
};
}  // namespace v0

// Read-only trial: when the stove has sent its 0x16 probe and the usual text dialect has not been acknowledged
// for ENGAGE_MS, answer with one ACK then read codes 001, 000, 002 in turn, a few rounds, and log every reply.
class LinkV0Probe {
public:
  using TxFn  = std::function<void(const uint8_t*, size_t)>;
  using NowFn = std::function<uint32_t()>;
  using DbgFn = std::function<void(const char*, const std::string&)>;
  static constexpr uint32_t ENGAGE_MS = 8000;   // after the first 0x16 with no text ACK
  static constexpr uint32_t READ_GAP_MS = 1500;
  static constexpr int ROUNDS = 4;              // 3 codes per round, then stop until the next 0x16
  static constexpr int CODES[3] = {1, 0, 2};

  LinkV0Probe(TxFn tx, NowFn now) : tx_(tx), now_(now), dec_([this](v0::Decoder::Kind k, const std::string& f) { onFrame(k, f); }) {}
  void onDebug(DbgFn f) { dbg_ = f; }

  // Every byte from the stove, before or after the text link has seen it.
  void onByte(uint8_t b) {
    if (b == v0::SYN) { if (!syn_seen_) { syn_seen_ = true; first_syn_ms_ = now_(); } started_ = false; steps_ = 0; engaged_ = false; return; }
    if (engaged_) dec_.feed(b);
  }
  // True while the text link must stay quiet so that its frames do not mix with ours.
  bool engaged() const { return engaged_; }
  uint32_t replies() const { return replies_; }

  // textAcked: the usual dialect got its version acknowledged (then this class stays out of the way).
  void poll(bool textAcked) {
    if (textAcked) { engaged_ = false; return; }
    uint32_t now = now_();
    if (!engaged_) {
      if (!syn_seen_ || started_ || (now - first_syn_ms_) < ENGAGE_MS) return;
      engaged_ = true; started_ = true; steps_ = 0; last_ms_ = now;
      send(std::string(1, (char)v0::ACK), "ack");
      return;
    }
    if (steps_ >= 3 * ROUNDS) { engaged_ = false; syn_seen_ = false; return; }
    if ((now - last_ms_) >= READ_GAP_MS) {
      last_ms_ = now;
      send(v0::readFrame(CODES[steps_ % 3]), "read");
      steps_++;
    }
  }

private:
  void send(const std::string& f, const char* what) {
    if (f.empty()) return;
    tx_(reinterpret_cast<const uint8_t*>(f.data()), f.size());
    if (dbg_) { char h[48]; snprintf(h, sizeof h, "v0 %s", what); dbg_(h, hex(f)); }
  }
  void onFrame(v0::Decoder::Kind k, const std::string& f) {
    replies_++;
    if (!dbg_) return;
    if (k == v0::Decoder::Kind::Ack) dbg_("v0 rx", "ACK");
    else if (k == v0::Decoder::Kind::Nak) dbg_("v0 rx", "NAK");
    else dbg_("v0 rx", "FRAME " + f);
  }
  static std::string hex(const std::string& s) {
    std::string o; char t[4];
    for (unsigned char c : s) { snprintf(t, sizeof t, "%02X ", c); o += t; }
    return o;
  }
  TxFn tx_; NowFn now_; DbgFn dbg_;
  v0::Decoder dec_;
  bool syn_seen_ = false, started_ = false, engaged_ = false;
  uint32_t first_syn_ms_ = 0, last_ms_ = 0, replies_ = 0;
  int steps_ = 0;
};

}  // namespace firenet
