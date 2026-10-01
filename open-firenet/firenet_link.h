// firenet_link.h — couche liaison CDC côté dongle Open-Firenet, au-dessus de firenet_protocol.h.
// Sans dépendance Arduino : testable en g++. Le firmware fournit deux callbacks
// (émettre des octets, temps en ms) ; toute la logique protocole est ici.
#pragma once
#include "firenet_protocol.h"
#include <map>
#include <deque>
#include <vector>
#include <functional>

namespace firenet {

struct Entry { std::string name; long value = 0; };

// Modèle d'état reconstitué à partir des trames du poêle.
struct StoveModel {
  // cdc_status renvoyé par POST_CDCDEVICE_STATUS (§5)
  std::map<std::string,std::string> status;   // nom -> valeur brute
  // jeux complets nom->valeur, fusionnés depuis les POST_* différentiels (§8.4)
  std::map<std::string,long> controls;
  std::map<std::string,long> sensors;
  // dump POSITIONNEL du dernier POST_* (§8.5 : GET_*=0 -> "REFRESH ALL", valeurs
  // émises SANS nom, dans l'ordre des index 0..N). Indispensable pour lire les
  // positions hautes (heures pellets, conso, entretien) qui n'ont pas de nom.
  std::vector<long> controls_pos;
  std::vector<long> sensors_pos;
  long revision = 0;
  int  main_state = 0, sub_state = 0;
  bool version_ack = false;                    // GET_CDCDEVICE_VERSION_FINISHED reçu
  int  generation = 0;                         // 1 = CDCDEVICE, 2 = FIRENET (§6.4)
  int  version_profile = -1;                   // acknowledged version frame: -1 none, 0 = V3, 1 = V1
  uint32_t frames_in = 0, frames_out = 0, last_rx_ms = 0;
};

class DongleLink {
public:
  // V1 positions registered in GET_SENSORS: 0..86 = every record the stove's PRIO2 fill writes (2.27 fn 0x8004b390: records 0..86);
  // the DOMO table has 88 positions (record 2 of the 2.28 is never written). Only the first 55 DOMO positions have a label, the rest are sNN.
  static constexpr int V1_SENSOR_COUNT = 87;
  // V1 control records registered in GET_CONTROLS: 0..36 = every record of the 2.27 controls table (FINDINGS "2.27
  // controls table: record index -> destination"; at least 37 records). The DOMO / INDUO II labels are used, record 5 skipped.
  static constexpr int V1_CONTROL_COUNT = 37;
  // INDUO II 2.28: same registration mechanism as V1 (disassembly: same decoder family, "GET_SENSORS handler
  // ... *0x1ab0=0 unconditionally" applies to fn 0x8001b25c too, see "2.28 vs 2.27: same protocol machinery"),
  // but the table itself is DOMO-identical (no shift -- only the 2.27 table has one fewer record). Counts cover
  // DOMO positions 0..87 / 0..37 directly (V1's counts cover the same DOMO range, but through the shifted index).
  static constexpr int V28_SENSOR_COUNT = 88;
  static constexpr int V28_CONTROL_COUNT = 38;
  using TxFn  = std::function<void(const uint8_t*, size_t)>;
  using NowFn = std::function<uint32_t()>;

  DongleLink(TxFn tx, NowFn now) : tx_(tx), now_(now) {}
  using DbgFn = std::function<void(const char*, const std::string&)>;
  void onDebug(DbgFn f) { dbg_ = f; }        // (sens, contenu) : "rx"/"tx"/"drop"
  uint32_t dropped() const { return dropped_; }
  uint32_t synCount() const { return syn_count_; }

  // --- réception : appeler avec chaque octet reçu du poêle -------------------
  void onByte(uint8_t b) {
    if (b == 0x16) {
      // Octet de sonde SYN : émis par le poêle au boot (VA 0x80039f74).
      // On répond sans attendre que la file TX soit vide (le watchdog poêle *0x1ac4
      // expire après ~12 s), mais au plus une fois par TX_GAP_MS : sans plafond,
      // chaque octet 0x16 déclenchait une trame complète (rafale ~8 ms, 23/09).
      uint32_t now = now_();
      if (syn_count_ == 0) first_syn_ms_ = now;
      syn_count_++;
      last_syn_ms_ = now;
      if (!model_.version_ack && (!syn_replied_ || (now - last_syn_reply_ms_) >= TX_GAP_MS)) {
        txq_.clear();          // abandonner toute trame en attente — la version prime
        sendVersion();
        last_tx_ms_ = 0;       // émettre sans délai dès le prochain poll()
        syn_replied_ = true;
        last_syn_reply_ms_ = now;
      }
      return;
    }
    if (!byteAccepted(b)) { dropped_++; if (dbg_){char h[6];snprintf(h,6,"%02X",b);dbg_("drop",h);} return; }  // §4.1
    if (rx_.size() < DONGLE_RX_SIZE) rx_ += (char)b;
    model_.last_rx_ms = now_();
    silence_pending_ = true;
  }

  // --- à appeler dans loop() : traite la trame après un silence -------------
  void poll() {
    if (syn_count_ != syn_reported_ && (now_() - last_syn_report_ms_) >= SYN_REPORT_MS) {
      if (dbg_) {
        char h[96];
        snprintf(h, sizeof h, "0x16 x%u (+%u) first=%u last=%u",
                 (unsigned)syn_count_, (unsigned)(syn_count_ - syn_reported_),
                 (unsigned)first_syn_ms_, (unsigned)last_syn_ms_);
        dbg_("syn", h);
      }
      syn_reported_ = syn_count_;
      last_syn_report_ms_ = now_();
    }
    if (silence_pending_ && (now_() - model_.last_rx_ms) >= SILENCE_MS) {
      silence_pending_ = false;
      if (!rx_.empty()) { dispatch(rx_); rx_.clear(); }
    }
    if (!model_.version_ack && txq_.empty() &&
        (!version_sent_ || (now_() - last_version_ms_) >= VERSION_RETRY_MS)) {
      sendVersion();
      version_sent_ = true;
      last_version_ms_ = now_();
    }
    // émission cadencée : une trame par TX_GAP_MS
    if (!txq_.empty() && (now_() - last_tx_ms_) >= tx_gap_ms_) emitOne();
  }
  bool txIdle() const { return txq_.empty(); }
  size_t txPending() const { return txq_.size(); }

  // --- émissions (rôle dongle) ----------------------------------------------
  // Auto-detection: three stove families are probed in turn, one frame at a time, each with its own APP so
  // a real stove of that family validates correctly on the first try (see stageProfile). Probe order: DOMO/V3
  // first (unchanged from main's production behaviour: existing DOMO users see no change and reach ACK on
  // their very first try), then INDUO II 2.28 (bare "GET_CDCDEVICE_VERSION=0; ..." with NO "GET_WIFI_VERSION"
  // prefix and APP=112), then INDUO 2.26/2.27 (V1, hardware-validated with Cyril, issue #4).
  // Why the 2.28 probe is read as safe on a real 2.27/2.26: the strstr chain of fn 0x8004beac only recognises
  // "GET_WIFI_VERSION", "POST_FIRENET_STATUS", "GET_FIRENET_STATUS", "GET_NETWORKS", "TRANSFER_COMPLETED",
  // "GET_REVISION", "GET_CONTROLS", "GET_SENSORS" (none is a substring of our bare CDCDEVICE probe), and no
  // "GET_CDCDEVICE" string exists anywhere in the 2.27 image; read directly (run 2026-09-28): when the LAST
  // check in that chain (GET_SENSORS, 0x8004ccee) also fails to match, execution falls to the shared exit at
  // 0x8004cea6 (clears the 2048-byte command buffer and its write index, logs one line, returns) -- the exact
  // same cleanup every OTHER command (matched or not) already falls through to, so it carries no side effect,
  // no error state, no counter change. NOT verified the same way on a real INDUO II 2.28 or DOMO (no 2.28
  // hardware, DOMO firmware not disassembled): the probe's inertness on a mismatched family and its APP=112
  // validation on a real 2.28 rest on the 2.27 proof plus reading the 2.28 chain (`GET_WIFI_VERSION=0` first,
  // else `GET_CDCDEVICE_VERSION`, then fn 0x800431f0 checks APP==112), not on a live test.
  // A detected 2.28 has generation 1 because its sensor/control table is DOMO-identical position for position
  // (no shift; only the 2.27 table is shifted, see v1ToDomoIndex/v1ToDomoCtrlIndex), but it speaks the FIRENET
  // dialect of the 2.26/2.27 for everything else (induoDialect()). Which probe answered is kept in
  // model_.version_profile (0=DOMO/V3, 1=INDUO II 2.28, 2=INDUO V1).
  // DT=0 in the 2.28 probe, read in the 2.28 firmware: the GET_CDCDEVICE_VERSION branch stores atoi(DT) in the
  // byte *(0x5bcc+0xc9) (0x8001b714), and that byte only selects the status dialect -- 0: GET_FIRENET_STATUS /
  // POST_FIRENET_STATUS (0x8001b73c, 0x8001b816, 0x8001b954), otherwise the GET/POST_CDCDEVICE_STATUS ones -- plus
  // a special case for DT == 2 (version check APP == 1 instead of 112, 0x8004321c). With DT=1 the stove ignored our
  // FIRENET status and ping and never linked (RIKA SONO 2.28, issue #4); DT=0 makes it use the FIRENET dialect,
  // whose status parse table is the 2.27 one.
  enum { DETECT_V3 = 0, DETECT_V28 = 1, DETECT_V1 = 2, DETECT_STAGE_COUNT = 3 };
  struct VersionProfile { const char* prefix; int bl; int app; int rev; int dt; };
  static const VersionProfile& stageProfile(int stage) {
    static const VersionProfile v3  = {"GET_CDCDEVICE3_VERSION=0; ", 999, 201, 12201, 3};
    static const VersionProfile v28 = {"GET_CDCDEVICE_VERSION=0; ", 101, 112, 13301, 0};
    static const VersionProfile v1  = {"GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; ", 101, 111, 360, 1};
    return stage == DETECT_V3 ? v3 : (stage == DETECT_V28 ? v28 : v1);
  }
  // dt() reflects the LOCKED generation (sticky across a reset, see dispatch()), not the stage currently being
  // probed for re-detection: only pushStatus()/hexEncode callers use it, and they only run once acked.
  // Keyed on version_profile (V3/V28/V1), not the coarse `generation` field: a detected 2.28 shares generation=1
  // with a real DOMO (same sensor/control table, see stageProfile), but NOT the DOMO's status wire format --
  // read on a real RIKA SONO 2.28 (issue #4, darkranger555, 2026-09-30): sent the CDC dialect with a hex ssid
  // and protocol="3" (the DOMO format) after the earlier fix already added the early status request, and the
  // stove still never answered, then reset the session -- so the DOMO wire format itself was the remaining
  // problem, not just its timing. A 2.28 now gets the exact same status format as V1 (FIRENET dialect, plain
  // ssid, protocol="1"), which is also what its own historical key used for the version frame (FINDINGS "the
  // official frame for the INDUO II 2.28 era is GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; ... DT=1;"). UNTESTED:
  // this is the next thing to confirm on real hardware, not proven by disassembly (2.28's status handler was
  // never fully read, only its recognized command keywords).
  int dt() const { return model_.version_profile == DETECT_V3 ? 3 : 1; }
  bool induoDialect() const { return model_.version_profile == DETECT_V1 || model_.version_profile == DETECT_V28; }

  // Advances to the next family after STAGE_TIMEOUT_MS of silence on the current one. Time-based rather than a
  // per-call counter: sendVersion() can fire more than once for a single real event (the immediate SYN-byte
  // reply in onByte() AND the delayed dispatch() re-arm both fire for one "\x16 3" probe), so counting calls
  // would burn through a stage's tries far faster than the retries it was meant to allow.
  static const uint32_t STAGE_TIMEOUT_MS = 3000;   // 3x VERSION_RETRY_MS (declared later in the class)
  void maybeAdvanceStage() {
    if (now_() - stage_start_ms_ >= STAGE_TIMEOUT_MS) {
      detect_stage_ = (detect_stage_ + 1) % DETECT_STAGE_COUNT;
      stage_start_ms_ = now_();
    }
  }
  void sendVersion() {
    v1_sensors_registered_ = false;         // the stove forgets nothing on its side, but the link restarted: register again
    v1_controls_registered_ = false;
    if (!model_.version_ack) maybeAdvanceStage();
    const VersionProfile& p = stageProfile(detect_stage_);
    char b[96];
    snprintf(b, sizeof b, "%sBL=%d; APP=%d; REV=%d; DT=%d; ", p.prefix, p.bl, p.app, p.rev, p.dt);
    send(b);
  }

  void setCredentials(const std::string& ssid, const std::string& pass,
                      const std::string& ip = "", const std::string& mac = "") {
    ssid_ = ssid; pass_ = pass; ip_ = ip; mac_ = mac;
  }

  // Test-only: jump straight to a detection stage instead of cycling through the earlier ones. Nothing in the
  // firmware calls this; it only exists so tests of one stove family do not have to replay the full 3-stage,
  // multi-second detection sequence (STAGE_MAX_TRIES x VERSION_RETRY_MS per stage) to reach it.
  void debugSetStage(int stage) { detect_stage_ = stage; stage_start_ms_ = now_(); }

  void requestStatus() {
    if (induoDialect()) {
      pushStatus();
      // Liveness ping: once the sensor/control names are registered, an INDUO-family stove only posts records
      // that changed, so a stove idling in standby can stay silent for more than a minute (full PRIO2 refresh
      // every 30 GET_REVISION) and the firmware's 60s no-RX watchdog restarted the ESP32 every ~65s (issue #4,
      // Cyril's 2026-09-29 logs: last RX at ~7.6s, restart at ~67.6s, no error on the stove). A bare
      // POST_FIRENET_STATUS is a request the stove always answers with its full status (read in the 2.27
      // decoder, branch 0x8004c2d4..0x8004c470, FINDINGS run #20; the official key sends it too). Not yet
      // confirmed on hardware; if the stove did not answer, behaviour would be the same as before.
      send("POST_FIRENET_STATUS");
      if (model_.version_profile == DETECT_V28) send("POST_CDCDEVICE_STATUS");  // TEMPORARY 2.28 diagnostic, see the ack
    } else {
      send("POST_CDCDEVICE_STATUS");
    }
  }

  // Firenet V1 status: EXACTLY 19 fields (0 to 18, ending with mac, no OTA fields).
  // bl=101, app=111, rev=360, spwf=0, symbol=4, initialised=1.
  void pushStatus(const std::string& ssidClear = "", const std::string& wpa2 = "",
                  const std::string& ip = "", const std::string& mac = "",
                  int rssi = -55, const std::string& idArg = "",
                  const std::string& token = "00000000") {
    // Le poêle INDUO valide l'ID et le token de la trame de statut (désassemblage 2.27,
    // fn 0x8001d324, code d'erreur 0x1b = "UW27") : ID = exactement 8 chiffres,
    // token = exactement 8 caractères imprimables (0x21..0x7E). "0000000" (7) => UW27.
    // The 2.28 needs the same 8-digit ID (read directly, "on the 2.28 the ID must be 8 digits ... in both
    // dialects, exactly as on the 2.27"), even though it is reached here through generation=1 (DOMO dialect):
    // keyed off version_profile, not generation, so a real DOMO (profile V3) keeps its proven 7-digit default.
    bool induoFamily = model_.version_profile == DETECT_V1 || model_.version_profile == DETECT_V28;
    const std::string id = !idArg.empty() ? idArg : (induoFamily ? "00000000" : "0000000");
    std::string s_ssid = ssidClear.empty() ? ssid_ : ssidClear;
    std::string s_pass = wpa2.empty() ? pass_ : wpa2;
    std::string s_ip   = ip.empty() ? ip_ : ip;
    std::string s_mac  = mac.empty() ? mac_ : mac;

    std::string ssid = (dt() == 3) ? hexEncode(s_ssid) : s_ssid;
    std::string f = induoDialect() ? "GET_FIRENET_STATUS=0;\n"
                                     : "GET_CDCDEVICE_STATUS=0;\n";
    // Echo the bl/app/rev of the profile that actually got acked (model_.version_profile), not necessarily the
    // one currently being (re-)probed: a 2.28 echoes 101/112/13301, a DOMO 999/201/12201, distinctly.
    const VersionProfile& ackedProfile = stageProfile(model_.version_profile >= 0 ? model_.version_profile : DETECT_V3);
    char rssis[8], apps[8], bls[8], revs[8];
    snprintf(rssis, sizeof rssis, "%d", rssi);
    snprintf(apps, sizeof apps, "%d", ackedProfile.app);
    snprintf(bls, sizeof bls, "%d", ackedProfile.bl);
    snprintf(revs, sizeof revs, "%d", ackedProfile.rev);
    const char* vals[19] = {
      "0","1","0","0","1","4","0",          // monitoring,on_off,scan,init,initialised,symbol,error
      bls, apps, revs, "0", rssis,           // bl,app,rev,spwf,rssi
      id.c_str(), token.c_str(), (induoDialect() ? "1" : "3"), // id,token,protocol
      ssid.c_str(), s_pass.c_str(),         // ssid,wpa2
      s_ip.c_str(), s_mac.c_str()};         // ip,mac
    for (int i = 0; i < 19; i++) { f += vals[i]; f += '\n'; }
    if (model_.generation != 2) {
      f += "0\n0\n0\n";
    }
    send(f);
  }

  // Reads the sensors. V1 (INDUO 2.27): the stove keeps the names of the last GET_SENSORS frame as the labels of
  // its records 0..N-1 and echoes them in POST_SENSORS ("name=value; "); a GET_SENSORS frame WITHOUT names resets
  // that list to empty (disassembly of the 2.27 handler, 0x8004ccfc). So the list is registered once (flag 0 =
  // "refresh all": every registered record is sent) and afterwards only GET_REVISION + TRANSFER_COMPLETED are
  // sent: the stove prepares its data in the GET_REVISION handler and emits it in the TRANSFER_COMPLETED one
  // (one POST per TRANSFER_COMPLETED, controls first); it re-sends a record only when its value changed.
  void pollSensors(const std::vector<std::string>& names = {}) {
    if (induoDialect()) {
      if (!v1_sensors_registered_) registerV1Sensors();
      sendRevision();
      transferCompleted();
      transferCompleted();
    } else {
      sendTable("GET_SENSORS", names, 0);
      sendRevision();                       // déclenche la sélection (§8.3)
      transferCompleted();                  // -> POST_CONTROLS si en attente
      transferCompleted();                  // -> POST_SENSORS
    }
  }
  // V1: the PRIO 2 records arrive by themselves at every 30th GET_REVISION (all registered records are then
  // refreshed). Sending "GET_SENSORS=2;" would only empty the registered list, so nothing is sent here.
  void pollPrio2Sensors() {}
  // Registers the sensor names once: V1 (2.27) positions under the DOMO-shifted labels (V1_SENSOR_COUNT, shift
  // via sensName(p,2)); a detected 2.28 under the unshifted DOMO labels directly (V28_SENSOR_COUNT, sensName(p,0)).
  void registerV1Sensors() {
    bool v1 = model_.version_profile == DETECT_V1;
    int count = v1 ? V1_SENSOR_COUNT : V28_SENSOR_COUNT;
    int shift = v1 ? DETECT_V1 : 0;   // sensName's shift selector: 2 = 2.27 shift, 0 = no shift (2.28/DOMO space)
    std::string f = "GET_SENSORS=0; ";
    for (int p = 0; p < count; p++) { f += sensName(p, shift); f += "=0; "; }
    send(f);
    v1_sensors_registered_ = true;
  }
  // Registers V1 control records 0..V1_CONTROL_COUNT-1 under the labels of the DOMO table (ctrlName(p, 2)). Flag 0 makes the
  // stove post every registered record once, then only the changed ones. The registration stores the values sent in the
  // records and the stove reloads them from its own variables at the next GET_REVISION: that GET_REVISION is queued right
  // behind the frame so that no later command can apply the placeholder values. Record 0 (the revision) gets the real one.
  void registerV1Controls() {
    bool v1 = model_.version_profile == DETECT_V1;
    int count = v1 ? V1_CONTROL_COUNT : V28_CONTROL_COUNT;
    int shift = v1 ? DETECT_V1 : 0;
    std::string f = "GET_CONTROLS=0; ";
    char v[16];
    for (int p = 0; p < count; p++) {
      f += ctrlName(p, shift);
      snprintf(v, sizeof v, "=%ld; ", p == 0 ? (long)model_.revision : 0L);
      f += v;
    }
    send(f);
    sendRevision();
    transferCompleted();
    v1_controls_registered_ = true;
  }
  void pollControls(const std::vector<std::string>& names = {}) {
    if (induoDialect()) {
      // Once registered the stove posts the changed controls on its own at the TRANSFER_COMPLETED of the sensor poll.
      if (!v1_controls_registered_) registerV1Controls();
    } else {
      sendTable("GET_CONTROLS", names, 0);
      sendRevision();
      transferCompleted();
      transferCompleted();
    }
  }
  // §13.2 : applique un jeu COMPLET de controls ordonné par position matérielle (§13).
  void applyControls(const std::vector<std::pair<std::string,long>>& full) {
    // drainer d'éventuels POST en attente avant de commander
    transferCompleted();
    transferCompleted();

    long onOff = 0;
    long mode = 2;
    long targetStage = 70;
    long roomTarget = 200;

    // Récupérer les valeurs courantes du modèle
    auto itOn = model_.controls.find("onOff");
    if (itOn != model_.controls.end()) onOff = itOn->second;
    else if (model_.controls_pos.size() > 1) onOff = model_.controls_pos[1];

    auto itMode = model_.controls.find("mode");
    if (itMode != model_.controls.end()) mode = itMode->second;
    else if (model_.controls_pos.size() > 2) mode = model_.controls_pos[2];

    auto itStage = model_.controls.find("targetStage");
    if (itStage != model_.controls.end()) targetStage = itStage->second;
    else if ((itStage = model_.controls.find("stage")) != model_.controls.end()) targetStage = itStage->second;
    else if (model_.controls_pos.size() > 3) targetStage = model_.controls_pos[3];

    auto itRoom = model_.controls.find("roomTarget");
    if (itRoom != model_.controls.end()) roomTarget = itRoom->second;
    else if ((itRoom = model_.controls.find("room")) != model_.controls.end()) roomTarget = itRoom->second;
    else if (model_.controls_pos.size() > 4) roomTarget = model_.controls_pos[4];

    long fan1On = 0, fan1Level = 0, fan1Area = 0;
    long fan2On = 0, fan2Level = 0, fan2Area = 0;
    auto itF1O = model_.controls.find("convectionFan1Active");
    if (itF1O != model_.controls.end()) fan1On = itF1O->second;
    else if (model_.controls_pos.size() > 23) fan1On = model_.controls_pos[23];

    auto itF1L = model_.controls.find("convectionFan1Level");
    if (itF1L != model_.controls.end()) fan1Level = itF1L->second;
    else if (model_.controls_pos.size() > 24) fan1Level = model_.controls_pos[24];

    auto itF1A = model_.controls.find("convectionFan1Area");
    if (itF1A != model_.controls.end()) fan1Area = itF1A->second;
    else if (model_.controls_pos.size() > 25) fan1Area = model_.controls_pos[25];

    auto itF2O = model_.controls.find("convectionFan2Active");
    if (itF2O != model_.controls.end()) fan2On = itF2O->second;
    else if (model_.controls_pos.size() > 26) fan2On = model_.controls_pos[26];

    auto itF2L = model_.controls.find("convectionFan2Level");
    if (itF2L != model_.controls.end()) fan2Level = itF2L->second;
    else if (model_.controls_pos.size() > 27) fan2Level = model_.controls_pos[27];

    auto itF2A = model_.controls.find("convectionFan2Area");
    if (itF2A != model_.controls.end()) fan2Area = itF2A->second;
    else if (model_.controls_pos.size() > 28) fan2Area = model_.controls_pos[28];

    long frostActive = 0, frostTemp = 50;
    auto itFA = model_.controls.find("frostProtectionActive");
    if (itFA != model_.controls.end()) frostActive = itFA->second;
    else if (model_.controls_pos.size() > 29) frostActive = model_.controls_pos[29];

    auto itFT = model_.controls.find("frostProtectionTemp");
    if (itFT != model_.controls.end()) frostTemp = itFT->second;
    else if (model_.controls_pos.size() > 30 && model_.controls_pos[30] > 0) frostTemp = model_.controls_pos[30];

    long bakeTarget = 180;
    auto itBT = model_.controls.find("bakeTarget");
    if (itBT != model_.controls.end()) bakeTarget = itBT->second;
    else if (model_.controls_pos.size() > 5 && model_.controls_pos[5] > 0) bakeTarget = model_.controls_pos[5];

    long tempOffset = 0;
    auto itTO = model_.controls.find("roomTempOffset");
    if (itTO != model_.controls.end()) tempOffset = itTO->second;
    else if (model_.controls_pos.size() > 31) tempOffset = model_.controls_pos[31];

    bool hasMultiAirCmd = false;
    bool hasScheduleCmd = false;
    bool hasFrostCmd = false;
    bool hasBakeCmd = false;
    // Eco mode (official control ecoMode, record 6 of the DOMO table): kept from the stove unless commanded.
    long ecoMode = 0;
    auto itEco = model_.controls.find("ecoMode");
    if (itEco != model_.controls.end()) ecoMode = itEco->second;
    else if (model_.controls_pos.size() > 6) ecoMode = model_.controls_pos[6];
    bool hasEcoCmd = false;
    bool hasTempOffsetCmd = false;

    // Mettre à jour avec les valeurs passées dans `full`
    for (const auto& kv : full) {
      if (kv.first == "onOff" || kv.first == "on") onOff = kv.second;
      else if (kv.first == "mode") mode = kv.second;
      else if (kv.first == "targetStage" || kv.first == "stage" || kv.first == "power") targetStage = kv.second;
      else if (kv.first == "roomTarget" || kv.first == "room" || kv.first == "temperature") roomTarget = kv.second;
      else if (kv.first == "convectionFan1Active" || kv.first == "fan1On" || kv.first == "fan1Active" || kv.first == "fan1") {
        fan1On = kv.second; hasMultiAirCmd = true;
      }
      else if (kv.first == "convectionFan1Level" || kv.first == "fan1Level") {
        fan1Level = kv.second; hasMultiAirCmd = true;
      }
      else if (kv.first == "convectionFan1Area" || kv.first == "fan1Area") {
        fan1Area = kv.second; hasMultiAirCmd = true;
      }
      else if (kv.first == "convectionFan2Active" || kv.first == "fan2On" || kv.first == "fan2Active" || kv.first == "fan2") {
        fan2On = kv.second; hasMultiAirCmd = true;
      }
      else if (kv.first == "convectionFan2Level" || kv.first == "fan2Level") {
        fan2Level = kv.second; hasMultiAirCmd = true;
      }
      else if (kv.first == "convectionFan2Area" || kv.first == "fan2Area") {
        fan2Area = kv.second; hasMultiAirCmd = true;
      }
      else if (kv.first == "heatingTimesActive" || kv.first == "scheduleActive") {
        if (model_.controls_pos.size() < 29) model_.controls_pos.resize(29, 0);
        long hta = kv.second ? 1 : 0;
        model_.controls["heatingTimesActive"] = hta;
        model_.controls_pos[21] = hta;
        hasScheduleCmd = true;
      }
      else if (kv.first == "setBackTemp" || kv.first == "setback_temperature" || kv.first == "setbackTemp" || kv.first == "tempEco") {
        if (model_.controls_pos.size() < 29) model_.controls_pos.resize(29, 0);
        long sbt = kv.second;
        if (sbt < 50) sbt *= 10;
        if (sbt < 100) sbt = 100;
        if (sbt > 250) sbt = 250;
        model_.controls["setBackTemp"] = sbt;
        model_.controls_pos[22] = sbt;
        hasScheduleCmd = true;
      }
      else if (kv.first == "frostProtectionActive" || kv.first == "frost_protection_active" || kv.first == "frostActive" || kv.first == "frostOn") {
        frostActive = kv.second ? 1 : 0;
        hasFrostCmd = true;
      }
      else if (kv.first == "frostProtectionTemp" || kv.first == "frost_protection_temp" || kv.first == "frost_protection_temperature" || kv.first == "frostTemp" || kv.first == "tempFrost") {
        long ft = kv.second;
        if (ft > 0 && ft < 40) ft *= 10;
        if (ft < 40) ft = 40;
        if (ft > 100) ft = 100;
        frostTemp = ft;
        hasFrostCmd = true;
      }
      else if (kv.first == "ecoMode" || kv.first == "eco_mode") {
        ecoMode = kv.second ? 1 : 0;
        hasEcoCmd = true;
      }
      else if (kv.first == "bakeTarget" || kv.first == "bake_target_temperature" || kv.first == "bake_target" || kv.first == "bakeTemp" || kv.first == "bake") {
        bakeTarget = kv.second;
        hasBakeCmd = true;
      }
      else if (kv.first == "room_temperature_offset" || kv.first == "room_temp_offset") {
        long ro = kv.second;
        if (ro >= -4 && ro <= 4 && ro != 0) ro *= 10;
        tempOffset = ro;
        hasTempOffsetCmd = true;
      }
      else if (kv.first == "roomTempOffset" || kv.first == "tempOffset" || kv.first == "roomOffset" || kv.first == "offset") {
        tempOffset = kv.second;
        hasTempOffsetCmd = true;
      }
      else {
        for (int i = 7; i <= 20; i++) {
          if (kv.first == ctrlName(i)) {
            if (model_.controls_pos.size() < 29) model_.controls_pos.resize(29, 0);
            model_.controls_pos[i] = kv.second;
            model_.controls[kv.first] = kv.second;
            hasScheduleCmd = true;
            break;
          }
        }
      }
    }

    // Garde-fous poêle :
    if (mode < 0) mode = 0;
    if (mode > 2) mode = 2;
    if (onOff < 0) onOff = 0;
    if (onOff > 1) onOff = 1;
    if (targetStage < 30) targetStage = 30;
    if (targetStage > 100) targetStage = 100;
    if (roomTarget < 50) roomTarget *= 10;
    if (roomTarget < 140) roomTarget = 140;
    if (roomTarget > 280) roomTarget = 280;

    if (fan1On < 0) fan1On = 0;
    if (fan1On > 1) fan1On = 1;
    if (fan1Level < 0) fan1Level = 0;
    if (fan1Level > 5) fan1Level = 5;
    if (fan1Area < -30) fan1Area = -30;
    if (fan1Area > 30) fan1Area = 30;

    if (fan2On < 0) fan2On = 0;
    if (fan2On > 1) fan2On = 1;
    if (fan2Level < 0) fan2Level = 0;
    if (fan2Level > 5) fan2Level = 5;
    if (fan2Area < -30) fan2Area = -30;
    if (fan2Area > 30) fan2Area = 30;

    if (frostActive < 0) frostActive = 0;
    if (frostActive > 1) frostActive = 1;
    if (frostTemp < 40) frostTemp = 40;
    if (frostTemp > 100) frostTemp = 100;

    if (bakeTarget < 130) bakeTarget = 130;
    if (bakeTarget > 340) bakeTarget = 340;

    if (tempOffset < -40) tempOffset = -40;
    if (tempOffset > 40) tempOffset = 40;

    // Mettre à jour immédiatement le modèle local car le poêle recopie value -> prev
    // et n'émettra pas de POST_CONTROLS pour les valeurs imposées (§13.2)
    model_.controls["onOff"] = onOff;
    model_.controls["mode"] = mode;
    model_.controls["targetStage"] = targetStage;
    model_.controls["stage"] = targetStage;
    model_.controls["roomTarget"] = roomTarget;
    model_.controls["room"] = roomTarget;
    model_.controls["convectionFan1Active"] = fan1On;
    model_.controls["convectionFan1Level"] = fan1Level;
    model_.controls["convectionFan1Area"] = fan1Area;
    model_.controls["convectionFan2Active"] = fan2On;
    model_.controls["convectionFan2Level"] = fan2Level;
    model_.controls["convectionFan2Area"] = fan2Area;
    model_.controls["frostProtectionActive"] = frostActive;
    model_.controls["frostProtectionTemp"] = frostTemp;
    model_.controls["bakeTarget"] = bakeTarget;
    model_.controls["ecoMode"] = ecoMode;
    model_.controls["roomTempOffset"] = tempOffset;

    if (model_.controls_pos.size() < 5) model_.controls_pos.resize(5, 0);
    model_.controls_pos[0] = (long)model_.revision;
    model_.controls_pos[1] = onOff;
    model_.controls_pos[2] = mode;
    model_.controls_pos[3] = targetStage;
    model_.controls_pos[4] = roomTarget;

    bool sendExtended = (model_.controls_pos.size() >= 29 || hasMultiAirCmd || hasScheduleCmd || hasFrostCmd || hasBakeCmd || hasTempOffsetCmd || hasEcoCmd);
    // INDUO 2.26/2.27: the stove stores the k-th pair of GET_CONTROLS=1 in its record k, and its table has no
    // bakeTarget record (V1 record p = DOMO control p for p < 5, p + 1 after, see v1ToDomoCtrlIndex): the extended
    // frame below, in DOMO order, would shift every value from record 5 on (heating times, frost, offset...). Only
    // the five-field frame, validated on real 2.26 and 2.27 stoves, is sent there.
    if (model_.version_profile == DETECT_V1) sendExtended = false;
    if (sendExtended) {
      bool sendOffset = (model_.controls_pos.size() >= 32 || hasTempOffsetCmd);
      bool sendFrost = (model_.controls_pos.size() >= 31 || hasFrostCmd || sendOffset);
      size_t reqSize = sendOffset ? 32 : (sendFrost ? 31 : 29);
      if (model_.controls_pos.size() < reqSize) model_.controls_pos.resize(reqSize, 0);
      model_.controls_pos[5] = bakeTarget;
      model_.controls_pos[6] = ecoMode;
      model_.controls_pos[23] = fan1On;
      model_.controls_pos[24] = fan1Level;
      model_.controls_pos[25] = fan1Area;
      model_.controls_pos[26] = fan2On;
      model_.controls_pos[27] = fan2Level;
      model_.controls_pos[28] = fan2Area;
      if (sendFrost) {
        model_.controls_pos[29] = frostActive;
        model_.controls_pos[30] = frostTemp;
      }
      if (sendOffset) {
        model_.controls_pos[31] = tempOffset;
      }

      std::string b = "GET_CONTROLS=1; ";
      b += "revision=" + std::to_string(model_.revision) + "; ";
      b += "onOff=" + std::to_string(onOff) + "; ";
      b += "mode=" + std::to_string(mode) + "; ";
      b += "targetStage=" + std::to_string(targetStage) + "; ";
      b += "roomTarget=" + std::to_string(roomTarget) + "; ";

      b += "bakeTarget=" + std::to_string(bakeTarget) + "; ";
      b += "ecoMode=" + std::to_string(ecoMode) + "; ";

      for (int i = 7; i <= 20; i++) {
        long ht = (model_.controls_pos.size() > (size_t)i) ? model_.controls_pos[i] : 0;
        b += ctrlName(i) + "=" + std::to_string(ht) + "; ";
      }
      long htActive = (model_.controls_pos.size() > 21) ? model_.controls_pos[21] : 0;
      b += "heatingTimesActive=" + std::to_string(htActive) + "; ";
      long sbTemp = (model_.controls_pos.size() > 22 && model_.controls_pos[22] > 0) ? model_.controls_pos[22] : 160;
      b += "setBackTemp=" + std::to_string(sbTemp) + "; ";

      b += "convectionFan1Active=" + std::to_string(fan1On) + "; ";
      b += "convectionFan1Level=" + std::to_string(fan1Level) + "; ";
      b += "convectionFan1Area=" + std::to_string(fan1Area) + "; ";
      b += "convectionFan2Active=" + std::to_string(fan2On) + "; ";
      b += "convectionFan2Level=" + std::to_string(fan2Level) + "; ";
      b += "convectionFan2Area=" + std::to_string(fan2Area) + "; ";

      if (sendFrost) {
        b += "frostProtectionActive=" + std::to_string(frostActive) + "; ";
        b += "frostProtectionTemp=" + std::to_string(frostTemp) + "; ";
      }

      if (sendOffset) {
        b += "roomTempOffset=" + std::to_string(tempOffset) + "; ";
      }

      send(b);
    } else {
      // Émission dans l'ORDRE POSITIONNEL STRICT requis par le poêle (§13 / FUN_80010b54) :
      // 0: revision, 1: onOff, 2: mode, 3: targetStage, 4: roomTarget
      char b[160];
      snprintf(b, sizeof b,
               "GET_CONTROLS=1; revision=%ld; onOff=%ld; mode=%ld; targetStage=%ld; roomTarget=%ld; ",
               (long)model_.revision, onOff, mode, targetStage, roomTarget);
      send(b);
    }
    // A GET_CONTROLS frame replaces the registered names: on an INDUO-family stove register them again at the next poll.
    if (induoDialect()) v1_controls_registered_ = false;
    // Immediately chain revision request and flush to force the stove
    // to return updated telemetry within ~1.2s instead of waiting for the periodic loop
    sendRevision();
    transferCompleted();
    transferCompleted();
  }
  void setRssi(int r) { rssi_ = r; }
  void sendRevision() {
    // §7.4 : le poêle tokenise GET_REVISION/revision/frequency ensemble -> une trame
    char b[80];
    snprintf(b, sizeof b, "GET_REVISION=%d; revision=%ld; frequency=%d; ",
             rssi_, model_.revision, 60);
    send(b);
  }
  void sendTable(const char* head, const std::vector<std::string>& names, int flag) {
    // Format compatible poêle : paires nom=0; séparées par "; ".
    // Le poêle découpe avec strtok(..., "=; "). Ne pas ajouter de '\n' car '\n'
    // n'est pas dans le jeu de séparateurs et provoquerait un crash atoi(NULL).
    std::string f = head; f += "="; f += std::to_string(flag); f += "; ";
    for (auto& n : names) { f += n; f += "=0; "; }
    send(f);
  }
  // §7.3 : liste de réseaux ; SSID en hexa si DT=3 (§5.3). rssi dans -99..-1.
  void sendNetworks(const std::vector<std::pair<std::string,int>>& nets) {
    std::string f = "GET_NETWORKS=1;\n";
    for (auto& n : nets) {
      std::string s = (dt() == 3) ? hexEncode(n.first) : n.first;
      char line[128];
      snprintf(line, sizeof line, "%s=%d\n", s.c_str(), n.second);
      f += line;
    }
    send(f);
  }
  void transferCompleted() { send("TRANSFER_COMPLETED"); }

  const StoveModel& model() const { return model_; }
  void setRevision(long r) { model_.revision = r; }

  static const uint32_t SILENCE_MS = 40;       // choix d'implémentation (§4.3 : silence, durée non prouvée)
  static const uint32_t VERSION_RETRY_MS = 1000;
  // Délai entre trames vers le poêle, réglable à chaud (UI, /api/txgap), borné à [TX_GAP_MIN_MS, TX_GAP_MAX_MS].
  // Default 150 ms: seen working on real INDUO 2.26/2.27 stoves (issue #4) and on a DOMO 2.29 run for days at 100 ms.
  uint32_t txGapMs() const { return tx_gap_ms_; }
  void setTxGapMs(uint32_t ms) { tx_gap_ms_ = ms < TX_GAP_MIN_MS ? TX_GAP_MIN_MS : (ms > TX_GAP_MAX_MS ? TX_GAP_MAX_MS : ms); }
  static const uint32_t TX_GAP_MIN_MS = 50;
  static const uint32_t TX_GAP_MAX_MS = 600;
  static const uint32_t TX_GAP_MS = 150;  // default silence between frames
  static const uint32_t SYN_REPORT_MS = 1000;   // résumé des 0x16 reçus, au plus 1 ligne/s
  uint32_t syn_count_ = 0, syn_reported_ = 0, first_syn_ms_ = 0, last_syn_ms_ = 0;
  uint32_t last_syn_reply_ms_ = 0, last_syn_report_ms_ = 0;
  bool syn_replied_ = false;

private:
  // Les trames sont mises en file et émises UNE par UNE, espacées de TX_GAP_MS,
  // pour que le poêle voie un silence entre chacune (§4.3). Émettre plusieurs
  // trames d'affilée les collerait -> le poêle n'en traiterait que la première.
  void send(const std::string& s) { txq_.push_back(s); }
  void emitOne() {
    if (txq_.empty()) return;
    std::string s = txq_.front();
    // Les trames sont déjà formatées avec leur terminateur propre ('; ' ou '\n' pour status/networks)
    if (dbg_) dbg_("tx", s);
    tx_((const uint8_t*)s.data(), s.size());
    model_.frames_out++;
    txq_.pop_front();
    last_tx_ms_ = now_();
  }

  void dispatch(const std::string& buf) {
    model_.frames_in++;
    if (dbg_) dbg_("rx", buf);
    // toute trame de commande connue clôt un dump positionnel en cours ; seules
    // les continuations "=val" (isContinuation) le prolongent (traité plus bas).
    if (!isContinuation(buf)) pending_pos_ = nullptr;
    std::string clean = trim(buf);
    bool looksLikeProbe = (clean == "3" || clean == "0" || buf.find('\x16') != std::string::npos ||
                           (buf.find('\x02') != std::string::npos && buf.find('0') != std::string::npos));
    // Only accept the ACK for the stage we actually just probed with: a stray/late reply for a stage we have
    // since moved past (retry timeout) is ignored rather than locking onto a family we did not just ask about.
    if (buf.find("GET_WIFI_VERSION_FINISHED") != std::string::npos) {
      if (detect_stage_ == DETECT_V1) {
        model_.generation = 2; model_.version_ack = true;
        model_.version_profile = DETECT_V1; last_good_stage_ = DETECT_V1; post_ack_probe_streak_ = 0;
        pushStatus();
      }
      return;
    }
    if (buf.find("GET_CDCDEVICE_VERSION_FINISHED") != std::string::npos) {
      if (detect_stage_ == DETECT_V3 || detect_stage_ == DETECT_V28) {
        model_.generation = 1; model_.version_ack = true;               // 2.28 reuses the DOMO/V3 protocol, see stageProfile
        model_.version_profile = detect_stage_; last_good_stage_ = detect_stage_; post_ack_probe_streak_ = 0;
        // INDUO II 2.28 only (not DOMO/V3): request status right away, like V1. Read 2026-09-29 on a real RIKA
        // SONO (issue #4, darkranger555): the version ack alone did not unlock anything -- GET_SENSORS/
        // GET_REVISION/TRANSFER_COMPLETED all went unanswered for ~50s, then the stove started firing its
        // "nothing decoded" silence reply. On the INDUO 2.27 the decoder only starts processing commands once
        // the ID/token status exchange succeeds (decision routine, fn 0x8001d930); the DOMO/V3 flow in the .ino
        // only requests status AFTER sensor+control registration finishes, which never happens if the 2.28
        // needs that unlock first -- a deadlock. Left untouched for a real DOMO (detect_stage_==DETECT_V3),
        // since that flow is the one already proven in production.
        if (detect_stage_ == DETECT_V28) {
          // TEMPORARY 2.28 diagnostic (issue #4): ask for the stove's status in both dialects before pushing ours,
          // like the official key asks first (FINDINGS run #59). The stove only answers the request that matches
          // its current DT byte (0: POST_FIRENET_STATUS, else POST_CDCDEVICE_STATUS, 0x8001b738..0x8001b770);
          // with DT=0 sent, our FIRENET status and ping were ignored on a real SONO, so DT may be rewritten after
          // the probe (display command @22, 0x80027516). A bare request triggers no ID/token check on the stove.
          send("POST_FIRENET_STATUS");
          send("POST_CDCDEVICE_STATUS");
          pushStatus();
        }
      }
      return;
    }
    if (buf.find("GET_CDCDEVICE_VERSION_UNFINISHED") != std::string::npos) return;
    if (!model_.version_ack && looksLikeProbe) {
      txq_.clear();
      sendVersion();
      last_tx_ms_ = 0;
      return;
    }
    // V1 stove session reset. On the INDUO 2.27 the `02 30 03` reply comes from the silence responder
    // (fn 0x8004be38 in the disassembly), which only runs in states 2/4/9 of the offline-update machine:
    // after ~99 loop passes without any received byte it sends 02, 30, 03 once (three one-byte transfers)
    // and discards whatever it receives meanwhile. So seeing it means the stove is in that update state.
    if (clean == "0" || (buf.find('\x02') != std::string::npos && buf.find('0') != std::string::npos)) {
      model_.version_ack = false;
      model_.version_profile = -1;
      post_ack_probe_streak_ = 0;
      // Re-detection: start at the family that just worked (fast reacquire) rather than the full cycle,
      // falling back to DOMO/V3 first only on the very first handshake of the session.
      detect_stage_ = last_good_stage_ >= 0 ? last_good_stage_ : DETECT_V3;
      stage_start_ms_ = now_();
      txq_.clear();
      sendVersion();
      last_tx_ms_ = 0;
      return;
    }
    // Post-handshake probe recovery (issue #4): the stove kept "acknowledging"
    // then falling straight back into \x16 <digit> probing, and nothing ever
    // reset version_ack, so the link stayed stuck polling forever with all
    // sensors reading 0. Re-arm the handshake after a short streak instead.
    if (model_.version_ack && looksLikeProbe) {
      if (++post_ack_probe_streak_ >= POST_ACK_PROBE_RESET_THRESHOLD) {
        model_.version_ack = false;
        model_.version_profile = -1;
        post_ack_probe_streak_ = 0;
        detect_stage_ = last_good_stage_ >= 0 ? last_good_stage_ : DETECT_V3;
        stage_start_ms_ = now_();
        txq_.clear();
        sendVersion();
        last_tx_ms_ = 0;
      }
      return;
    }
    post_ack_probe_streak_ = 0;   // any real frame below => link is healthy again
    if (buf.find("GET_NETWORKS_FINISHED") != std::string::npos) return;
    const char* wantStatus = induoDialect() ? "POST_FIRENET_STATUS"
                                              : "POST_CDCDEVICE_STATUS";
    if (buf.find(wantStatus) != std::string::npos ||
        buf.find("POST_FIRENET_STATUS") != std::string::npos ||
        buf.find("POST_CDCDEVICE_STATUS") != std::string::npos) {
      pending_pos_ = nullptr; parseStatus(buf); return;
    }
    if (buf.find("POST_CONTROLS") != std::string::npos) {
      // V1: named records (all after the registration, then only the changed ones): update the DOMO-indexed vector in place.
      if (induoDialect() &&
          parseV1Named(afterHeader(buf), ctrlIndexByName, model_.controls_pos, model_.controls) > 0) return;
      std::vector<long> tmpPos;
      parseBody(afterHeader(buf), &model_.controls, tmpPos);
      if (tmpPos.size() >= 5 && tmpPos.size() >= model_.controls_pos.size()) {
        model_.controls_pos = tmpPos;
        for (size_t i = 0; i < model_.controls_pos.size(); i++) {
          std::string nm = ctrlName(i);
          if (!nm.empty()) model_.controls[nm] = model_.controls_pos[i];
        }
      } else {
        long on = 0, md = 2, st = 70, rm = 200;
        auto itO = model_.controls.find("onOff"); if (itO != model_.controls.end()) on = itO->second;
        auto itM = model_.controls.find("mode"); if (itM != model_.controls.end()) md = itM->second;
        auto itS = model_.controls.find("targetStage"); if (itS != model_.controls.end()) st = itS->second;
        auto itR = model_.controls.find("roomTarget"); if (itR != model_.controls.end()) rm = itR->second;
        if (model_.controls_pos.size() < 5) model_.controls_pos = { (long)model_.revision, on, md, st, rm };
        else {
          model_.controls_pos[0] = (long)model_.revision;
          model_.controls_pos[1] = on;
          model_.controls_pos[2] = md;
          model_.controls_pos[3] = st;
          model_.controls_pos[4] = rm;
        }
        for (size_t i = 5; i < model_.controls_pos.size(); i++) {
          auto it = model_.controls.find(ctrlName(i));
          if (it != model_.controls.end()) model_.controls_pos[i] = it->second;
        }
      }
      return;
    }
    if (buf.find("POST_SENSORS")  != std::string::npos) {
      if (induoDialect() && v1_sensors_registered_) {
        // V1: named records, only the changed ones are sent: update the DOMO-indexed vector in place.
        pending_pos_ = nullptr;
        parseV1Named(afterHeader(buf), sensIndexByName, model_.sensors_pos, model_.sensors); postSensors(); return;
      }
      model_.sensors_pos.clear(); pending_pos_ = &model_.sensors_pos;
      parseBody(afterHeader(buf), &model_.sensors, model_.sensors_pos); postSensors(); return; }
    // Trame de continuation d'un dump positionnel (§8.5) : le poêle peut étaler
    // les valeurs "=val" sur plusieurs trames séparées par un silence. On les
    // accumule tant qu'aucune commande connue n'arrive.
    if (pending_pos_ && isContinuation(buf)) { parseBody(buf, nullptr, *pending_pos_); return; }
    pending_pos_ = nullptr;
  }
  // corps d'une trame POST_* : tout ce qui suit le premier ';' (retire l'en-tête).
  static std::string afterHeader(const std::string& buf) {
    size_t p = buf.find(';');
    return (p == std::string::npos) ? std::string() : buf.substr(p + 1);
  }
  // une continuation ne contient aucun mot-clé de commande mais au moins un '='.
  static bool isContinuation(const std::string& buf) {
    if (buf.find('=') == std::string::npos) return false;
    return buf.find("POST_") == std::string::npos &&
           buf.find("GET_")  == std::string::npos &&
           buf.find("TRANSFER") == std::string::npos;
  }
  void parseStatus(const std::string& buf) {
    auto v = parseStatusFrame(buf);
    for (int i = 0; i < NUM_FIELDS && i < (int)v.size(); i++)
      model_.status[CDC_FIELDS[i].name] = v[i];
    auto it = model_.status.find("ssid");      // décodage hexa (§5.3)
    if (it != model_.status.end() && dt() == 3) it->second = hexDecode(it->second);
  }
  // Parse un corps "name=val; name=val; ..." (§7.5). Les paires nommées vont dans
  // `store` (s'il est fourni) ; TOUTES les valeurs sont aussi ajoutées à `pos`
  // dans l'ordre d'apparition, car le dump complet (§8.5) émet des "=val" SANS
  // nom : la position est alors la seule clé disponible (§14).
  void parseBody(const std::string& body, std::map<std::string,long>* store,
                 std::vector<long>& pos) {
    size_t i = 0;
    while (i < body.size()) {
      size_t sc = body.find(';', i); if (sc == std::string::npos) sc = body.size();
      std::string item = body.substr(i, sc - i);
      size_t eq = item.find('=');
      if (eq != std::string::npos && !trim(item).empty()) {
        std::string name = trim(item.substr(0, eq));
        std::string val  = trim(item.substr(eq + 1));
        long v = strtol(val.c_str(), nullptr, 10);
        pos.push_back(v);
        if (store) {
          if (!name.empty()) {
            (*store)[name] = v;
          } else if (store == &model_.controls) {
            (*store)[ctrlName((int)pos.size() - 1, model_.generation)] = v;
          } else if (store == &model_.sensors) {
            (*store)[sensName((int)pos.size() - 1, model_.generation)] = v;
          }
        }
      }
      i = sc + 1;
    }
  }
  // POST_SENSORS / POST_CONTROLS of a V1 stove after registration: "name=value; " pairs of the changed records (all of
  // them after the registration frame). The name gives the record, so the position does not depend on the frame content.
  // `pos` is indexed like the DOMO tables (sensIndexByName / ctrlIndexByName), the stove's V1 record shifted as in
  // v1ToDomoIndex / v1ToDomoCtrlIndex. Returns how many pairs were stored.
  int parseV1Named(const std::string& body, int (*indexByName)(const std::string&),
                   std::vector<long>& pos, std::map<std::string,long>& store) {
    int n = 0;
    size_t i = 0;
    while (i < body.size()) {
      size_t sc = body.find(';', i); if (sc == std::string::npos) sc = body.size();
      std::string item = body.substr(i, sc - i);
      size_t eq = item.find('=');
      if (eq != std::string::npos && !trim(item).empty()) {
        std::string name = trim(item.substr(0, eq));
        long v = strtol(trim(item.substr(eq + 1)).c_str(), nullptr, 10);
        int idx = indexByName(name);
        if (idx >= 0 && idx < 128) {
          if ((int)pos.size() <= idx) pos.resize(idx + 1, 0);
          pos[idx] = v;
          store[name] = v;
          n++;
        }
      }
      i = sc + 1;
    }
    return n;
  }
  void postSensors() { /* hook : le firmware relit model().sensors après un cycle */ }
  static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \r\n\t\x02\x03");
    size_t b = s.find_last_not_of(" \r\n\t\x02\x03");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
  }

  TxFn tx_; NowFn now_;
  bool v1_sensors_registered_ = false;      // names of GET_SENSORS sent since the last version handshake
  bool v1_controls_registered_ = false;     // names of GET_CONTROLS sent since the last version handshake or command
  StoveModel model_;
  std::string rx_;
  uint32_t tx_gap_ms_ = TX_GAP_MS;
  bool silence_pending_ = false;
  uint32_t last_version_ms_ = 0;
  bool version_sent_ = false;
  // Version-frame auto-detection state (see stageProfile / DETECT_* / maybeAdvanceStage above).
  int detect_stage_ = DETECT_V3;   // probed first: zero change for existing DOMO/main users on their first try
  uint32_t stage_start_ms_ = 0;    // when the current stage started, for the STAGE_TIMEOUT_MS advance
  int last_good_stage_ = -1;       // last stage that got acked this session: reused first on a reset
  int rssi_ = -55;
  uint32_t dropped_ = 0;
  std::deque<std::string> txq_;
  uint32_t last_tx_ms_ = 0;
  DbgFn dbg_ = nullptr;
  std::vector<long>* pending_pos_ = nullptr;   // cible d'accumulation positionnelle en cours
  // Some stoves (confirmed on an INDUO 2.26, issue #4) never actually stop
  // sending the \x16 <digit> reset probe after acknowledging the version —
  // the link never truly stabilizes, so every poll (GET_SENSORS/GET_REVISION/
  // TRANSFER_COMPLETED) just gets more probe noise back instead of real
  // POST_* frames. Track a streak of post-ack probe bytes and re-arm the
  // handshake instead of polling a dead link forever.
  int post_ack_probe_streak_ = 0;
  static const int POST_ACK_PROBE_RESET_THRESHOLD = 6;
  std::string ssid_;
  std::string pass_;
  std::string ip_;
  std::string mac_;
};

} // namespace firenet
