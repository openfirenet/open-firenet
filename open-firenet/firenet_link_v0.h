// Link to a stove running firmware 2.25 (SUMO / SONO / INDUO): STX/ETX framed numeric dialect, read-only trial.
//
// What was read in the SUMO 2.25 main board (FINDINGS.md, "SUMO 2.25" sections; nothing here was tried on a stove yet):
//  - after the USB device is configured the stove sends 0x16 0x33 once (fn 0x80034b34);
//  - its frame decoder (fn 0x80034c34) collects bytes between STX (0x02) and ETX (0x03) while its receive state is 1
//    (probe sent) or 2 (an ACK 0x06 was seen); a frame of 3 bytes "NNN" is a READ, "NNN=value" is a WRITE;
//  - it answers STX digits ETX (value), ACK 0x06 or NAK 0x15.
// The class reads codes and logs the replies. The only frames it can write are those of v0::writeFrame(): stop, mode, manual
// power, set points, frost protection and comfort power level, each within the range of the stove display (nothing was sent to a stove yet).
// The stove answers a write with nothing at all (not even an ACK / NAK, fn 0x80034c34 only empties its buffer after
// fn 0x80034d80), so every write is followed by a read of the same code, which is the only confirmation.
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <functional>
#include <string>
#include <vector>

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

// Whether a start (023=1) may be sent: the stove is off or waiting (main state 1, or 11 as the stove's own start code accepts), with no
// error, a cold flame sensor (the stove itself refuses an ignition above 139 degrees, 0x80023022) and its "on" flag not set yet.
// The numbers are those of the values the bridge has read back from the stove.
inline bool startAllowed(long errMask, long mainState, long subState, long flameTemp, long onOff) {
  return errMask == 0 && (mainState == 1 || mainState == 11) && subState <= 1 && flameTemp < 100 && onOff == 0;
}

// "STX NNN=value ETX": write of one setting. Only these codes are ever built, each with the values the stove display
// allows (the stove itself checks almost nothing: 025 takes 0..255, 026 and 081 only keep each other 2 degrees apart):
//   023 on / off:  0 (stop) or 1 (start). The start path of the stove (fn 0x80024488) does not look at its error word
//                  (0x3594+4) and sets the "on" flag before anything is checked, so a start is only sent when
//                  startAllowed() says the stove is cold, idle and without error. With an error the stove clears the flag
//                  at once and goes to burn-off (state 6, fn 0x800240cc, 0x800240e4 and 0x80024238), so a start made then does nothing.
//   024 mode:      0 manual, 1 automatic, 2 comfort (the stove itself refuses more than 2).
//   025 power:     30..100 percent in steps of 5 (manual mode).
//   026 set point: whole degrees, 14..28 (comfort mode), the range of the page and of /api/controls for the other firmware versions.
//   080 weekly schedule on / off: 0 or 1 (plain flag, read back like the others).
//   065 .. 078 start and end of the daily range of each weekday (Sunday first), in minutes since midnight: 0..1440 (the first range only,
//                  the second range of a day has no numeric code, issue #110). Both 0 means no range for that day.
//   081 reduced set point: whole degrees, 12..22 (the stove itself keeps it at least 2 degrees under 026, 0x80034e5e).
//   088 frost protection on / off: 0 or 1. 089 its temperature: whole degrees, 4..10. Both are plain byte stores in the
//                  stove (0x80035362, 0x8003537a, into 0x6824+0x6e / +0x6f); the frost protection starts the stove by itself
//                  when the room is colder than 089, which is what it is for (0x80022fa4).
//   092 power level of the comfort mode: 1..4 (the range of the stove display, "Leistungsbedarf").
// Returns an empty string for any other code or value (091, the room sensor offset, is read but not written: it was only ever
// read as 0, so its meaning is not proven).
inline std::string writeFrame(int code, long value) {
  bool ok = false;
  switch (code) {
    case 23: ok = (value == 0 || value == 1); break;
    case 24: ok = (value >= 0 && value <= 2); break;
    case 25: ok = (value >= 30 && value <= 100 && value % 5 == 0); break;
    case 26: ok = (value >= 14 && value <= 28); break;
    case 80: ok = (value == 0 || value == 1); break;
    case 65: case 66: case 67: case 68: case 69: case 70: case 71:
    case 72: case 73: case 74: case 75: case 76: case 77: case 78: ok = (value >= 0 && value <= 1440); break;
    case 81: ok = (value >= 12 && value <= 22); break;
    case 88: ok = (value == 0 || value == 1); break;
    case 89: ok = (value >= 4 && value <= 10); break;
    case 92: ok = (value >= 1 && value <= 4); break;
  }
  if (!ok) return std::string();
  char b[16];
  int n = snprintf(b, sizeof b, "%c%03d=%ld%c", (char)STX, code, value, (char)ETX);
  return std::string(b, (size_t)n);
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
// for ENGAGE_MS, answer with one ACK then read the live codes and a few settings per cycle, and log every reply.
class LinkV0Probe {
public:
  using TxFn  = std::function<void(const uint8_t*, size_t)>;
  using NowFn = std::function<uint32_t()>;
  using DbgFn = std::function<void(const char*, const std::string&)>;
  static constexpr uint32_t ENGAGE_MS = 8000;   // after the first 0x16 with no text ACK
  static constexpr uint32_t READ_GAP_MS = 250;    // the stove answers in about 40 ms (issue #110 logs); 1500 was a cautious first value
  static constexpr int ROUNDS = 10;             // all codes per round (about 6 s), then stop until the next 0x16 when nothing ever answered
  // Live codes, read in every cycle. From the stove's own application (SUMO 2.25 disassembly) and from what it answered on a stove
  // (issue #110): 001 = flame temperature and 000 = room temperature (integers), 012 = grate contact (very probable), 016/017 = external
  // request and pressure switch, 027/028 = main state and sub state, 005 = auger in per mille, 019 = auger current, 007 = flue fan.
  // 013, 029 and 032 are digital inputs not identified yet; 002 to 004 are error values (0 at rest).
  static constexpr int CODE_COUNT = 16;
  static constexpr int CODES[CODE_COUNT] = {27, 28, 1, 0, 12, 13, 16, 17, 29, 32, 2, 3, 4, 5, 7, 19};
  // Settings and texts of the stove, read a few at a time after the live codes, from the first to the last and again (they do not
  // change by themselves; only a change is logged): 023 to 026, 034 to 046 (struct fields), 047 to 092 (parameters, with seven pairs of
  // shorts that look like one time range per weekday), 900 to 917 (texts such as versions and the stove clock).
  static constexpr int CFG_PER_CYCLE = 8;
  static constexpr int CFG_COUNT = 77;
  static constexpr int CFG[CFG_COUNT] = {
    23, 24, 25, 26, 34, 35, 37, 38, 39, 41, 43, 44, 46,
    47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78,
    79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92,
    900, 901, 902, 903, 904, 905, 906, 907, 908, 909, 910, 911, 912, 913, 914, 915, 916, 917};
  static constexpr int CYCLE_STEPS = CODE_COUNT + CFG_PER_CYCLE;
  static int codeAt(int step) {
    int cycle = step / CYCLE_STEPS, p = step % CYCLE_STEPS;
    return p < CODE_COUNT ? CODES[p] : CFG[(cycle * CFG_PER_CYCLE + (p - CODE_COUNT)) % CFG_COUNT];
  }

  LinkV0Probe(TxFn tx, NowFn now) : tx_(tx), now_(now), dec_([this](v0::Decoder::Kind k, const std::string& f) { onFrame(k, f); }) {}
  using ValFn = std::function<void(int, const std::string&)>;
  void onDebug(DbgFn f) { dbg_ = f; }
  void onValue(ValFn f) { val_ = f; }   // every value answered (code, text), changed or not

  // Every byte from the stove, before or after the text link has seen it.
  void onByte(uint8_t b) {
    if (b == v0::SYN) { if (!syn_seen_) { syn_seen_ = true; first_syn_ms_ = now_(); } started_ = false; steps_ = 0; engaged_ = false; pending_.clear(); readback_ = -1; return; }
    if (engaged_) dec_.feed(b);
  }
  // True while the text link must stay quiet so that its frames do not mix with ours.
  bool engaged() const { return engaged_; }
  uint32_t replies() const { return replies_; }

  // Queues one write (see v0::writeFrame). False when the code or value is not allowed, or when the link is not up:
  // the stove must have answered at least one read, so that the ACK that its receiver needs has been seen.
  bool requestWrite(int code, long value) {
    if (!engaged_ || replies_ == 0 || pending_.size() >= MAX_PENDING) return false;
    std::string f = v0::writeFrame(code, value);
    if (f.empty()) return false;
    pending_.push_back({code, f});
    return true;
  }

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
    // Without any reply, give up after ROUNDS. Once the stove answers, keep reading in turn so that the log follows the stove.
    if (steps_ >= CYCLE_STEPS * ROUNDS && replies_ == 0) { engaged_ = false; syn_seen_ = false; return; }
    if ((now - last_ms_) >= READ_GAP_MS) {
      last_ms_ = now;
      if (readback_ >= 0) {
        // The read that follows a write: the answer is logged even when the value did not change.
        last_code_ = readback_; readback_ = -1;
        last_.erase(last_code_);
        send(v0::readFrame(last_code_), "read", false);
      } else if (!pending_.empty()) {
        Pending w = pending_.front(); pending_.erase(pending_.begin());
        send(w.frame, "write");
        readback_ = w.code;
      } else {
        last_code_ = codeAt(steps_);
        send(v0::readFrame(last_code_), "read", false);
        steps_++;
      }
    }
  }

private:
  void send(const std::string& f, const char* what, bool log = true) {
    if (f.empty()) return;
    tx_(reinterpret_cast<const uint8_t*>(f.data()), f.size());
    if (log && dbg_) { char h[48]; snprintf(h, sizeof h, "v0 %s", what); dbg_(h, hex(f)); }
  }
  void onFrame(v0::Decoder::Kind k, const std::string& f) {
    replies_++;
    if (k == v0::Decoder::Kind::Frame && last_code_ >= 0 && val_) val_(last_code_, f);
    if (!dbg_) return;
    if (k == v0::Decoder::Kind::Ack) dbg_("v0 rx", "ACK");
    else if (k == v0::Decoder::Kind::Nak) dbg_("v0 rx", "NAK");
    else {
      // Log a value only when it differs from the last one of that code, so the exchange log (a small ring) keeps the whole
      // start-up of the stove instead of filling with identical lines.
      auto it = last_.find(last_code_);
      if (it != last_.end() && it->second == f) return;
      last_[last_code_] = f;
      char h[16]; snprintf(h, sizeof h, "%03d=", last_code_);
      dbg_("v0 rx", std::string(h) + f);
    }
  }
  static std::string hex(const std::string& s) {
    std::string o; char t[4];
    for (unsigned char c : s) { snprintf(t, sizeof t, "%02X ", c); o += t; }
    return o;
  }
  struct Pending { int code; std::string frame; };
  static constexpr size_t MAX_PENDING = 8;
  std::vector<Pending> pending_;
  int readback_ = -1;
  TxFn tx_; NowFn now_; DbgFn dbg_; ValFn val_;
  v0::Decoder dec_;
  bool syn_seen_ = false, started_ = false, engaged_ = false;
  uint32_t first_syn_ms_ = 0, last_ms_ = 0, replies_ = 0;
  int steps_ = 0;
  int last_code_ = -1;
  std::map<int, std::string> last_;
};

}  // namespace firenet
