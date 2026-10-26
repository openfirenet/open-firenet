// Small colour screen of some ESP32-S3 boards (LilyGO T-Dongle-S3: ST7735, 160x80, backlight on GPIO38, active low).
// No library: a minimal ST7735 driver on the Arduino SPI class. Started only when the owner chose this board.
// Pins, init sequence, offsets (x 1, y 26 in landscape) and MADCTL come from LilyGO's own example, T-Dongle-S3
// examples/lcd (lcd.ino and bsp_lcd/esp_lcd_st7735.c).
//
// What it shows: the animated logo for a few seconds at start-up, then one piece of information per screen, in turn
// (BOOT button = next screen). An alert (stove error, hopper open, stove silent, Wi-Fi lost) takes the screen over.
// Everything is drawn by one task from a snapshot of the bridge's state that loop() hands over once a second.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <string.h>
#include <vector>
#include <initializer_list>
#include "firenet_display_config.h"
#include "firenet_display_logo.h"
#include "firenet_display_assets.h"
#include "firenet_display_text.h"

namespace firenet {

// What the screens show, filled by the sketch. A value that is not known is left at its "none" default.
struct DisplaySnapshot {
  static constexpr int NONE = -32768;
  bool linked = false;            // the stove has answered at least once since boot
  bool silent = false;            // ... but nothing has come from it for a while
  int  mainState = NONE;
  char stateLabel[24] = "";       // English name, shown when the language has no word for this state
  char stateKey[20] = "";         // the state as the API names it ("heating"): translated by the screen
  bool burning = false;
  int  room10 = NONE, target10 = NONE;   // tenths of a degree
  int  flame = NONE, powerPct = NONE;
  int  mode = -1;                 // 0 manual, 1 auto, 2 comfort
  bool comfort = false;           // comfort mode: the stove sets its own power
  int  serviceKg = NONE, hours = NONE;
  int  logHours = NONE;           // split log running time of a combined stove, in whole hours; NONE when the stove reports none
  long errCode = 0;
  bool hopperOpen = false;
  char ip[20] = "";
  char host[24] = "open-firenet.local";
  bool wifi = false;
  int  rssi = 0;
  bool mqttOn = false, mqttUp = false;
  char version[12] = "";
  bool updateAvailable = false;
  DisplayConfig config;           // which screens, in which order, how long each
};

class Display {
public:
  static constexpr int W = logo::SCREEN_W, H = logo::SCREEN_H;
  static constexpr uint32_t LOGO_MS = 5000, WAIT_MS = 45000, FRAME_MS = 50, PERIOD_MS = 3200;
  // Colour ramp of the logo, from yellow to dark orange (the logo's amber to orange-red).
  static constexpr uint8_t RAMP[4][3] = {{253, 224, 71}, {247, 168, 28}, {249, 115, 22}, {194, 65, 12}};

  // Starts the drawing task: it runs on its own at a steady 20 images a second, whatever loop() is busy with.
  void start() {
    if (task_) return;
    fb_ = (uint16_t*)malloc(W * H * sizeof(uint16_t));
    if (!fb_) return;
    // Core 0 (Wi-Fi) at the lowest priority above idle, never on the core of loop(): with a higher priority on loop()'s core the
    // stove link was starved (no consumption values after 6 minutes, seen on a stove), at the cost of a screen that may lag.
    xTaskCreatePinnedToCore(&Display::run, "fn_display", 6144, this, 1, &task_, 0);
  }

  // Stops the drawing task and gives its memory back (the picture stays on the panel); start() brings it back. The update needs
  // that memory: with the screen running, a download ran the bridge out of heap.
  void stop() {
    if (!task_) return;
    stopReq_ = true;
    for (int i = 0; i < 200 && task_; i++) delay(10);
  }

  // For a board with a screen the owner does not want lit: the backlight off and the panel asleep, nothing else. Without this the
  // panel stays lit and grey, since nothing tells it otherwise.
  void blank() {
    pinMode(CS, OUTPUT); pinMode(DC, OUTPUT); pinMode(RST, OUTPUT); pinMode(BL, OUTPUT);
    digitalWrite(BL, HIGH);                                   // backlight off (active low)
    digitalWrite(CS, HIGH);
    spi_.begin(SCK, -1, MOSI, -1);
    digitalWrite(RST, LOW); delay(20); digitalWrite(RST, HIGH); delay(120);
    spi_.beginTransaction(SPISettings(27000000, MSBFIRST, SPI_MODE0));
    cmd(0x28);                                                // display off
    cmd(0x10);                                                // sleep in
    spi_.endTransaction();
  }

  // From loop(), about once a second.
  void update(const DisplaySnapshot& s) {
    portENTER_CRITICAL(&mux_);
    snap_ = s;
    portEXIT_CRITICAL(&mux_);
  }

private:
  // Same values as DisplayConfig::Id (the order on the screen comes from the setting, not from here).
  enum Screen { S_STATE, S_ROOM, S_FLAME, S_POWER, S_IP, S_PELLETS, S_HOURS, S_LOG_HOURS, S_WIFI, S_MQTT, S_VERSION, S_LOGO, S_COUNT };
  static_assert((int)S_COUNT == (int)DisplayConfig::COUNT, "screens and their setting must list the same screens");
  enum Alert { A_NONE, A_ERROR, A_HOPPER, A_SILENT, A_NOWIFI };

  static constexpr int SCK = 5, MOSI = 3, CS = 4, DC = 2, RST = 1, BL = 38, BUTTON = 0;
  static constexpr int X_OFF = 1, Y_OFF = 26;               // glass offsets in landscape
  static constexpr uint8_t MADCTL = 0x08 | 0x20 | 0x80;     // BGR, swap x/y, mirror y
  static constexpr uint8_t MADCTL_TURNED = 0x08 | 0x20 | 0x40;   // the same turned by 180 degrees: mirror x instead of y (the glass offsets are the same both ways)

  struct Rgb { uint8_t r, g, b; };
  static constexpr Rgb C_BG{12, 15, 23}, C_WHITE{241, 245, 249}, C_DIM{148, 163, 184}, C_ORANGE{249, 115, 22},
                       C_AMBER{245, 158, 11}, C_GREEN{52, 211, 153}, C_RED{239, 68, 68}, C_ROSE{251, 113, 133},
                       C_SKY{56, 189, 248}, C_YELLOW{250, 204, 21}, C_INDIGO{129, 140, 248}, C_GREY{100, 116, 139};

  static uint16_t rgb565(Rgb c) { return ((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3); }
  static Rgb unpack(uint16_t p) { return Rgb{(uint8_t)((p >> 8) & 0xF8), (uint8_t)((p >> 3) & 0xFC), (uint8_t)((p << 3) & 0xF8)}; }
  void fill(Rgb c) { uint16_t v = rgb565(c); for (int i = 0; i < W * H; i++) fb_[i] = v; }

  // ---- panel -------------------------------------------------------------------------------------------------------
  void cmd(uint8_t c) { digitalWrite(DC, LOW); digitalWrite(CS, LOW); spi_.transfer(c); digitalWrite(CS, HIGH); }
  void data(const uint8_t* d, size_t n) { digitalWrite(DC, HIGH); digitalWrite(CS, LOW); spi_.writeBytes(d, n); digitalWrite(CS, HIGH); }
  void cmdData(uint8_t c, std::initializer_list<uint8_t> d) { cmd(c); std::vector<uint8_t> v(d); if (!v.empty()) data(v.data(), v.size()); }

  void initPanel() {
    spi_.beginTransaction(SPISettings(27000000, MSBFIRST, SPI_MODE0));
    cmd(0x01); delay(150);                                  // software reset
    cmd(0x11); delay(120);                                  // sleep out
    cmdData(0xB1, {0x05, 0x3A, 0x3A});                      // frame rate, normal mode
    cmdData(0xB2, {0x05, 0x3A, 0x3A});                      // idle mode
    cmdData(0xB3, {0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A});    // partial mode
    cmdData(0xB4, {0x03});                                  // inversion control
    cmdData(0xC0, {0x62, 0x02, 0x04});                      // power control
    cmdData(0xC1, {0xC0});
    cmdData(0xC2, {0x0D, 0x00});
    cmdData(0xC3, {0x8D, 0x6A});
    cmdData(0xC4, {0x8D, 0xEE});
    cmdData(0xC5, {0x0E});                                  // VCOM
    cmd(0x21);                                              // inversion on (this panel needs it)
    cmdData(0x3A, {0x05});                                  // 16 bits per pixel
    cmdData(0xE0, {0x10, 0x0E, 0x02, 0x03, 0x0E, 0x07, 0x02, 0x07, 0x0A, 0x12, 0x27, 0x37, 0x00, 0x0D, 0x0E, 0x10});
    cmdData(0xE1, {0x10, 0x0E, 0x03, 0x03, 0x0F, 0x06, 0x02, 0x08, 0x0A, 0x13, 0x26, 0x36, 0x00, 0x0D, 0x0E, 0x10});
    cmd(0x13); delay(10);                                   // normal display mode
    cmdData(0x36, {MADCTL});                                // landscape, BGR
    turned_ = false;
    cmd(0x29); delay(100);                                  // display on
    spi_.endTransaction();
  }

  // Rotates the picture by 180 degrees (or back) without redrawing anything: only the scan direction of the panel changes.
  void setTurned(bool turned) {
    if (turned == turned_) return;
    turned_ = turned;
    spi_.beginTransaction(SPISettings(27000000, MSBFIRST, SPI_MODE0));
    cmdData(0x36, {turned ? MADCTL_TURNED : MADCTL});
    spi_.endTransaction();
  }

  // Whole frame to the panel: window (with the offsets of the glass), then the 16-bit pixels, high byte first.
  void push() {
    spi_.beginTransaction(SPISettings(27000000, MSBFIRST, SPI_MODE0));
    cmdData(0x2A, {0, (uint8_t)X_OFF, 0, (uint8_t)(X_OFF + W - 1)});
    cmdData(0x2B, {0, (uint8_t)Y_OFF, 0, (uint8_t)(Y_OFF + H - 1)});
    cmd(0x2C);
    digitalWrite(DC, HIGH); digitalWrite(CS, LOW);
    spi_.writePixels(fb_, W * H * sizeof(uint16_t));        // swaps each pixel to high byte first
    digitalWrite(CS, HIGH);
    spi_.endTransaction();
  }

  void begin() {
    pinMode(CS, OUTPUT); pinMode(DC, OUTPUT); pinMode(RST, OUTPUT);
    digitalWrite(CS, HIGH);
    pinMode(BL, OUTPUT); digitalWrite(BL, HIGH);            // backlight off until the first image is in place
    pinMode(BUTTON, INPUT_PULLUP);
    spi_.begin(SCK, -1, MOSI, -1);
    digitalWrite(RST, LOW); delay(20); digitalWrite(RST, HIGH); delay(120);
    initPanel();
    fill(C_BG); push();
    ledcAttach(BL, 5000, 8);
    setBacklight(DisplayConfig::BRIGHT_DEFAULT);
  }

  // Backlight in percent. Active low: a higher duty is a dimmer screen. It is most of the screen's current, hence the setting.
  void setBacklight(uint8_t pct) {
    if (pct == pct_) return;
    pct_ = pct;
    ledcWrite(BL, 255 - (uint32_t)pct * 255 / 100);
  }

  // ---- drawing -----------------------------------------------------------------------------------------------------
  void blend(int x, int y, uint8_t a, Rgb c) {
    if (!a || x < 0 || y < 0 || x >= W || y >= H) return;
    uint16_t& px = fb_[y * W + x];
    if (a == 255) { px = rgb565(c); return; }
    Rgb b = unpack(px);
    px = rgb565(Rgb{(uint8_t)((c.r * a + b.r * (255 - a)) / 255), (uint8_t)((c.g * a + b.g * (255 - a)) / 255), (uint8_t)((c.b * a + b.b * (255 - a)) / 255)});
  }
  void mask(int x, int y, int w, int h, const uint8_t* m, Rgb c) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) blend(x + i, y + j, m[j * w + i], c);
  }
  void icon(const uint8_t* m, int x, int y, Rgb c) { mask(x, y, screen::ICON_SIZE, screen::ICON_SIZE, m, c); }

  // Texts are UTF-8 in the source; the fonts hold Latin-1 (the accents), so a two-byte sequence C3 xx is read as one character.
  static unsigned char nextChar(const char*& s) {
    unsigned char u = (unsigned char)*s++;
    if (u == 0xC3 && *s) return (unsigned char)(0xC0 + ((unsigned char)*s++ - 0x80));
    return u;
  }
  static const screen::Glyph* glyph(const screen::Font& f, unsigned char u) {
    if (u < f.first || u > f.last) u = ' ';
    return &f.glyphs[u - f.first];
  }
  int textWidth(const screen::Font& f, const char* s) { int w = 0; while (*s) w += glyph(f, nextChar(s))->adv; return w; }
  // x is the left edge of the text, top the top of the line (ascender).
  void text(const screen::Font& f, int x, int top, const char* s, Rgb c) {
    while (*s) {
      const screen::Glyph* g = glyph(f, nextChar(s));
      if (g->w) mask(x + g->x, top + g->y, g->w, g->h, f.data + g->off, c);
      x += g->adv;
    }
  }
  // Medium font when the line fits in `avail` pixels, else the small one (state names such as "Grate Cleaning").
  void textFit(int x, int top, const char* s, Rgb c, int avail) {
    using namespace screen;
    if (textWidth(FONT_MEDIUM, s) <= avail) text(FONT_MEDIUM, x, top, s, c);
    else text(FONT_SMALL, x, top + (FONT_MEDIUM.height - FONT_SMALL.height) / 2, s, c);
  }
  // Cuts `t` into lines of the small font no wider than `w`, at spaces; returns the number of lines (at most `max`).
  int wrapText(const char* t, int w, char out[][40], int max) {
    int n = 0; char cur[40] = ""; const char* p = t;
    while (*p) {
      const char* q = p; while (*q && *q != ' ') q++;
      char word[40]; int wl = (int)(q - p); if (wl > 39) wl = 39; memcpy(word, p, wl); word[wl] = 0;
      char trial[40]; snprintf(trial, sizeof trial, "%s%s%s", cur, cur[0] ? " " : "", word);
      if (cur[0] && textWidth(screen::FONT_SMALL, trial) > w) { if (n < max) snprintf(out[n++], 40, "%s", cur); snprintf(cur, sizeof cur, "%s", word); }
      else snprintf(cur, sizeof cur, "%s", trial);
      p = *q ? q + 1 : q;
    }
    if (cur[0] && n < max) snprintf(out[n++], 40, "%s", cur);
    return n;
  }
  // A line of text beside an icon: the medium font when it fits in `avail` pixels, else the small one, else the small one on two or three lines.
  void textBlock(int x, int top, const char* s, Rgb c, int avail) {
    using namespace screen;
    if (textWidth(FONT_MEDIUM, s) <= avail) { text(FONT_MEDIUM, x, top, s, c); return; }
    if (textWidth(FONT_SMALL, s) <= avail) { text(FONT_SMALL, x, top + (FONT_MEDIUM.height - FONT_SMALL.height) / 2, s, c); return; }
    char ln[3][40]; int n = wrapText(s, avail, ln, 3);
    int y = top + (FONT_MEDIUM.height - (n > 1 ? n * 15 : FONT_SMALL.height)) / 2;
    if (y < 22) y = 22;                                       // below the caption
    for (int i = 0; i < n; i++) { text(FONT_SMALL, x, y, ln[i], c); y += 15; }
  }
  void textCentre(const screen::Font& f, int cx, int top, const char* s, Rgb c) { text(f, cx - textWidth(f, s) / 2, top, s, c); }

  // A target temperature, in tenths: whole degrees as a rule ("20~"), with the tenth only when there is one ("20.5~").
  static void tempText(char* out, size_t n, int t10, const char* prefix) {
    if (t10 % 10 == 0) snprintf(out, n, "%s%d~", prefix, t10 / 10);
    else snprintf(out, n, "%s%d.%d~", prefix, t10 / 10, abs(t10) % 10);
  }

  Rgb wave(float t, float pos) const {                       // colour along the ramp, yellow to dark orange, pos 0..1
    float p = 0.5f + 0.5f * sinf(6.2831853f * (t - pos * 0.6f));
    p *= 3; int i = (int)p; if (i > 2) i = 2; float f = p - i;
    auto mix = [&](int k) { return (uint8_t)(RAMP[i][k] + (RAMP[i + 1][k] - RAMP[i][k]) * f + 0.5f); };
    return Rgb{mix(0), mix(1), mix(2)};
  }

  void drawLogo(float t) {
    fill(C_BG);
    Rgb lut[64];
    for (int k = 0; k < 64; k++) lut[k] = wave(t, k / 63.0f);
    const logo::Layer* layers[3] = {&logo::EMBLEM, &logo::OPEN, &logo::FIRENET};
    for (int n = 0; n < 3; n++) {
      const logo::Layer& L = *layers[n];
      for (int j = 0; j < L.h; j++)
        for (int i = 0; i < L.w; i++) {
          uint8_t a = L.mask[j * L.w + i];
          if (!a) continue;
          Rgb c = n == 1 ? C_WHITE : lut[n == 0 ? 63 - (j * 63) / L.h : (i * 63) / L.w];
          blend(L.x + i, L.y + j, a, c);
        }
    }
  }

  // The flame follows the stove as on the web page: outline-like amber pulse at ignition, the colours of the logo when
  // heating, dimmer at burn-off, grey when off.
  void flameIcon(const DisplaySnapshot& s, float t, int x, int y) {
    Rgb c = C_GREY;
    if (s.mainState == 2) c = Rgb{247, 168, 28};
    else if (s.mainState == 3 || s.mainState == 4 || s.mainState == 5 || s.mainState == 7) {
      Rgb lut[3] = {wave(t, 0.0f), wave(t, 0.5f), wave(t, 1.0f)}; c = lut[1];
    } else if (s.mainState == 6) c = Rgb{194, 65, 12};
    if (s.mainState == 2 || s.mainState == 6) {              // pulse
      float k = 0.65f + 0.35f * (0.5f + 0.5f * sinf(6.2831853f * t * 2));
      c = Rgb{(uint8_t)(c.r * k), (uint8_t)(c.g * k), (uint8_t)(c.b * k)};
    }
    icon(screen::ICON_FLAME, x, y, c);
  }

  // One piece of information: icon on the left, a caption above and the value below it.
  void info(const uint8_t* ic, Rgb icc, const char* caption, const char* value, const screen::Font& vf, Rgb vc, const char* unit = nullptr) {
    const int iy = (H - screen::ICON_SIZE) / 2, tx = 6 + screen::ICON_SIZE + 10;
    if (ic) icon(ic, 6, iy, icc);
    text(screen::FONT_SMALL, tx, 6, caption, C_DIM);
    const screen::Font& vfit = textWidth(vf, value) > W - tx - 2 ? screen::FONT_SMALL : vf;     // a long word ("Hors ligne"): the small font
    int top = 24 + (screen::FONT_BIG.height - vfit.height) / 2;
    text(vfit, tx, top, value, vc);
    if (unit) text(screen::FONT_SMALL, tx + textWidth(vfit, value) + 3, top + vfit.ascent - screen::FONT_SMALL.ascent, unit, C_DIM);
  }

  bool alertHolds(Alert a, const DisplaySnapshot& s) const {
    switch (a) {
      case A_ERROR:  return s.errCode != 0;
      case A_HOPPER: return s.hopperOpen;
      case A_SILENT: return s.silent;
      case A_NOWIFI: return !s.wifi;
      default: return false;
    }
  }
  bool screenHas(Screen sc, const DisplaySnapshot& s) const {
    switch (sc) {
      case S_STATE:   return s.mainState != DisplaySnapshot::NONE;
      case S_ROOM:    return s.room10 != DisplaySnapshot::NONE;
      case S_FLAME:   return s.flame != DisplaySnapshot::NONE;
      case S_POWER:   return s.powerPct != DisplaySnapshot::NONE || (s.comfort && s.target10 != DisplaySnapshot::NONE);
      case S_IP:      return s.ip[0] != 0;
      case S_PELLETS: return s.serviceKg != DisplaySnapshot::NONE;
      case S_HOURS:   return s.hours != DisplaySnapshot::NONE;
      case S_LOG_HOURS: return s.logHours != DisplaySnapshot::NONE;
      case S_WIFI:    return s.wifi;
      case S_MQTT:    return s.mqttOn;
      case S_VERSION: return s.version[0] != 0;
      case S_LOGO:    return true;
      default: return false;
    }
  }

  void drawScreen(Screen sc, const DisplaySnapshot& s, float t) {
    fill(C_BG);
    char v[32];
    using namespace screen;
    const int lg = s.config.language;
    auto TXT = [lg](screentext::Word w) { return screentext::word(lg, w); };
    switch (sc) {
      case S_STATE:
        flameIcon(s, t, 6, (H - ICON_SIZE) / 2);
        text(FONT_SMALL, 6 + ICON_SIZE + 10, 6, TXT(screentext::W_STOVE), C_DIM);
        textBlock(6 + ICON_SIZE + 10, 28, screentext::stateName(lg, s.stateKey, s.stateLabel), C_WHITE, W - 6 - (6 + ICON_SIZE + 10));
        break;
      case S_ROOM:
        snprintf(v, sizeof v, "%d.%d~", s.room10 / 10, abs(s.room10) % 10);
        info(ICON_THERMOMETER, C_ROSE, TXT(screentext::W_ROOM), v, FONT_BIG, C_WHITE);
        if (s.target10 != DisplaySnapshot::NONE) {           // "-> 20~" at the right of the caption
          tempText(v, sizeof v, s.target10, "\x7f ");
          text(FONT_SMALL, W - 6 - textWidth(FONT_SMALL, v), 6, v, C_SKY);
        }
        break;
      case S_FLAME:
        snprintf(v, sizeof v, "%d~", s.flame);
        info(ICON_FLAME, C_ORANGE, TXT(screentext::W_FLAME), v, FONT_BIG, C_WHITE);
        break;
      case S_POWER:
        // Comfort mode: the stove sets its own power, so the target temperature is what makes sense; power in auto and manual.
        if (s.comfort && s.target10 != DisplaySnapshot::NONE) {
          tempText(v, sizeof v, s.target10, "");
          info(ICON_GAUGE, C_YELLOW, TXT(screentext::W_COMFORT), v, FONT_BIG, C_WHITE);
        } else {
          snprintf(v, sizeof v, "%d%%", s.powerPct);
          info(ICON_GAUGE, C_YELLOW, s.mode == 0 ? TXT(screentext::W_MANUAL) : s.mode == 1 ? TXT(screentext::W_AUTO) : TXT(screentext::W_POWER), v, FONT_BIG, C_WHITE);
        }
        break;
      case S_IP: {
        // One line beside the icon, in the largest font that fits (an address such as 192.168.100.200 is wide).
        const int ix = 4, tx = ix + ICON_SIZE + 4;
        icon(ICON_HOUSE, ix, (H - ICON_SIZE) / 2, C_INDIGO);
        text(FONT_SMALL, tx, 6, TXT(screentext::W_ADDRESS), C_DIM);
        const Font& f = textWidth(FONT_ADDRESS, s.ip) <= W - tx - 1 ? FONT_ADDRESS : FONT_SMALL;
        text(f, tx, 24 + (FONT_BIG.height - f.height) / 2, s.ip, C_WHITE);
        break;
      }
      case S_PELLETS:
        snprintf(v, sizeof v, "%d", s.serviceKg);
        info(ICON_PACKAGE, C_AMBER, TXT(screentext::W_SERVICE_IN), v, FONT_BIG, C_WHITE, "kg");
        break;
      case S_HOURS:
        snprintf(v, sizeof v, "%d", s.hours);
        info(ICON_TIMER, C_AMBER, TXT(screentext::W_RUNNING), v, FONT_BIG, C_WHITE, "h");
        break;
      case S_LOG_HOURS:
        snprintf(v, sizeof v, "%d", s.logHours);
        info(ICON_TIMER, C_AMBER, TXT(screentext::W_RUNNING_LOGS), v, FONT_BIG, C_WHITE, "h");
        break;
      case S_WIFI: {
        snprintf(v, sizeof v, "%d", s.rssi);
        info(ICON_WIFI, C_GREEN, TXT(screentext::W_WIFI), v, FONT_BIG, C_WHITE, "dBm");
        break;
      }
      case S_MQTT:
        info(ICON_PLUG_ZAP, s.mqttUp ? C_GREEN : C_AMBER, TXT(screentext::W_MQTT), s.mqttUp ? TXT(screentext::W_ONLINE) : TXT(screentext::W_OFFLINE), FONT_MEDIUM, s.mqttUp ? C_GREEN : C_AMBER);
        break;
      case S_VERSION:
        snprintf(v, sizeof v, "v%s", s.version);
        info(s.updateAvailable ? ICON_REFRESH_CW : ICON_PACKAGE, s.updateAvailable ? C_AMBER : C_INDIGO, s.updateAvailable ? TXT(screentext::W_UPDATE) : TXT(screentext::W_FIRMWARE), v, FONT_MEDIUM, C_WHITE);
        break;
      case S_LOGO: drawLogo(t); break;
      default: break;
    }
  }

  void drawAlert(Alert a, const DisplaySnapshot& s, float t) {
    using namespace screen;
    fill(Rgb{40, 14, 18});                                   // dark red, so that it cannot be taken for a normal screen
    const int lg = s.config.language; auto TXT = [lg](screentext::Word w) { return screentext::word(lg, w); };
    const char* cap = ""; const char* line = ""; const uint8_t* ic = ICON_TRIANGLE_ALERT; Rgb col = C_RED; char v[24];
    switch (a) {
      case A_ERROR:  cap = TXT(screentext::W_STOVE_ERROR); snprintf(v, sizeof v, "%s %ld", TXT(screentext::W_CODE), s.errCode); line = v; ic = ICON_CIRCLE_ALERT; break;
      case A_HOPPER: cap = TXT(screentext::W_ALERT); line = TXT(screentext::W_HOPPER_OPEN); ic = ICON_LOCK_OPEN; col = C_AMBER; break;
      case A_SILENT: cap = TXT(screentext::W_ALERT); line = TXT(screentext::W_NO_STOVE); ic = ICON_UNPLUG; col = C_AMBER; break;
      case A_NOWIFI: cap = TXT(screentext::W_ALERT); line = TXT(screentext::W_NO_WIFI); ic = ICON_WIFI_OFF; col = C_AMBER; break;
      default: break;
    }
    float k = 0.7f + 0.3f * (0.5f + 0.5f * sinf(6.2831853f * t * 2));
    icon(ic, 6, (H - ICON_SIZE) / 2, Rgb{(uint8_t)(col.r * k), (uint8_t)(col.g * k), (uint8_t)(col.b * k)});
    text(FONT_SMALL, 6 + ICON_SIZE + 10, 8, cap, C_DIM);
    textBlock(6 + ICON_SIZE + 10, 30, line, C_WHITE, W - 6 - (6 + ICON_SIZE + 10));
  }

  // ---- task --------------------------------------------------------------------------------------------------------
  static void run(void* self) { static_cast<Display*>(self)->loop(); }

  void loop() {
    begin();
    TickType_t wake = xTaskGetTickCount();
    uint32_t t0 = millis(), shown = t0, alertShown = t0;
    int cur = -1, alertIdx = 0;
    bool wasDown = false;
    uint32_t lastActivity = t0;                              // last press, alert, change of the stove's state or of the settings
    uint8_t seenBright = 0; bool seenDim = false; uint16_t seenAfter = 0;
    char seenState[20] = "";
    for (;;) {
      if (stopReq_) { stopReq_ = false; free(fb_); fb_ = nullptr; task_ = nullptr; vTaskDelete(nullptr); }
      uint32_t now = millis();
      float t = (now % PERIOD_MS) / (float)PERIOD_MS;
      DisplaySnapshot s;
      portENTER_CRITICAL(&mux_); s = snap_; portEXIT_CRITICAL(&mux_);
      const uint32_t screenMs = s.config.seconds * 1000UL;
      bool down = digitalRead(BUTTON) == LOW;
      bool pressed = down && !wasDown; wasDown = down;
      const bool pressedRaw = pressed;

      // Backlight: the set level, lowered after a while without a press or an alert when the owner asked for it.
      bool alertNow = alertHolds(A_ERROR, s) || alertHolds(A_HOPPER, s) || alertHolds(A_SILENT, s) || alertHolds(A_NOWIFI, s);
      const DisplayConfig& cf = s.config;
      bool wasDimmed = cf.dim && (now - lastActivity) > (uint32_t)cf.dimAfter * 1000UL;
      if (wasDimmed && pressed) pressed = false;               // the press that wakes the screen is only a wake-up, it does not skip a screen
      bool stateChanged = seenState[0] && s.stateKey[0] && strcmp(seenState, s.stateKey) != 0;   // the stove starts, stops...
      if (s.stateKey[0]) { strncpy(seenState, s.stateKey, sizeof seenState - 1); seenState[sizeof seenState - 1] = 0; }
      if (pressedRaw || alertNow || stateChanged || cf.brightness != seenBright || cf.dim != seenDim || cf.dimAfter != seenAfter) lastActivity = now;
      seenBright = cf.brightness; seenDim = cf.dim; seenAfter = cf.dimAfter;
      bool dimmed = cf.dim && (now - lastActivity) > (uint32_t)cf.dimAfter * 1000UL;
      setTurned(cf.rotate);
      setBacklight(dimmed ? (cf.dimLevel < cf.brightness ? cf.dimLevel : cf.brightness) : cf.brightness);

      // The logo stays until the stove has told its state (at most WAIT_MS after start: a bridge with no stove shows its
      // own screens then), so that no screen is drawn from half the values.
      bool stoveReady = s.mainState != DisplaySnapshot::NONE && s.flame != DisplaySnapshot::NONE;
      if (now - t0 < LOGO_MS || (!stoveReady && now - t0 < WAIT_MS)) {
        drawLogo(t);
      } else {
        // Alerts first: the ones that hold take the screen in turn; a press on BOOT moves to the next.
        Alert hold[4]; int n = 0;
        for (Alert a : {A_ERROR, A_HOPPER, A_SILENT, A_NOWIFI}) if (alertHolds(a, s)) hold[n++] = a;
        if (n) {
          if (pressed || now - alertShown >= screenMs) { alertIdx++; alertShown = now; }
          drawAlert(hold[alertIdx % n], s, t);
        } else {
          // The screens that have data, in order; the next one every SCREEN_MS or on a press.
          int count = 0; Screen list[S_COUNT];
          for (int i = 0; i < S_COUNT; i++) { int id = s.config.order[i]; if (s.config.on[id] && screenHas((Screen)id, s)) list[count++] = (Screen)id; }
          if (count) {
            if (cur < 0 || pressed || now - shown >= screenMs) { cur = (cur + 1) % count; shown = now; }
            if (cur >= count) cur = 0;
            drawScreen(list[cur], s, t);
          } else {
            drawLogo(t);                                     // nothing to tell yet
          }
        }
      }
      push();
      vTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS));
    }
  }

  SPIClass spi_{FSPI};
  uint16_t* fb_ = nullptr;                                  // allocated when the display starts: a board without one pays nothing
  TaskHandle_t task_ = nullptr;
  volatile bool stopReq_ = false;
  bool turned_ = false;                                     // the panel scans for the 180 degrees picture
  uint8_t pct_ = 255;                                       // backlight now, percent (255 = not set yet)
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  DisplaySnapshot snap_;
};

}  // namespace firenet
