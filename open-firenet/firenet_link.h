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
  using TxFn  = std::function<void(const uint8_t*, size_t)>;
  using NowFn = std::function<uint32_t()>;

  DongleLink(TxFn tx, NowFn now) : tx_(tx), now_(now) {}
  using DbgFn = std::function<void(const char*, const std::string&)>;
  void onDebug(DbgFn f) { dbg_ = f; }        // (sens, contenu) : "rx"/"tx"/"drop"
  uint32_t dropped() const { return dropped_; }

  // --- réception : appeler avec chaque octet reçu du poêle -------------------
  void onByte(uint8_t b) {
    if (b == 0x16) {
      // Octet de sonde SYN : émis par le poêle au boot (VA 0x80039f74).
      // On répond IMMÉDIATEMENT, sans attendre que la file TX soit vide.
      // Raison : si le dongle met > ~12s à répondre (init WiFi, etc.), le watchdog
      // poêle (*0x1ac4) expire et le poêle bascule en unlinked (*0x1ac8=0), état
      // depuis lequel il répond \x02 0 \x03 à TOUT sans jamais traiter la version.
      if (!model_.version_ack) {
        txq_.clear();          // abandonner toute trame en attente — la version prime
        sendVersion();
        last_tx_ms_ = 0;       // émettre sans délai dès le prochain poll()
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
    if (!txq_.empty() && (now_() - last_tx_ms_) >= TX_GAP_MS) emitOne();
  }
  bool txIdle() const { return txq_.empty(); }
  size_t txPending() const { return txq_.size(); }

  // --- émissions (rôle dongle) ----------------------------------------------
  // Sur cette branche test/v1-protocol, le profil est verrouillé sur V1 :
  //   "GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; BL=101; APP=111; REV=360; DT=1; "
  // Prouvé par décompilation du firmware officiel clé FireNet V2.26 (STM32 VA 0x08012304) :
  // le firmware officiel de la clé émet cette chaîne exacte et le poêle INDUO
  // (confirmé le 16/09 par Cyril) y répond immédiatement GET_WIFI_VERSION_FINISHED.
  // BL=101, APP=111 (version du .dat V2.26), REV=360, DT=1.
  struct VersionProfile { const char* prefix; int bl; int app; int rev; int dt; };
  static const VersionProfile& profileV1() { static const VersionProfile p =
      {"GET_WIFI_VERSION_GET_CDCDEVICE_VERSION=0; ", 101, 111, 360, 1}; return p; }
  static const VersionProfile& profileV3() { static const VersionProfile p =
      {"GET_CDCDEVICE3_VERSION=0; ", 999, 201, 12201, 3}; return p; }
  const VersionProfile& profile() const { return profile_ ? profileV1() : profileV3(); }
  int dt() const { return profile().dt; }      // effective DT of the active profile

  void sendVersion() {
    // test/v1-protocol : pas de fallback V1↔V3 — on reste sur V1 fixe jusqu'à l'ACK.
    // Format textuel officiel FireNet V1 (STM32 VA 0x08012304) :
    char b[96];
    snprintf(b, sizeof b, "%sBL=%d; APP=%d; REV=%d; DT=%d; ",
             profileV1().prefix, profileV1().bl, profileV1().app, profileV1().rev, profileV1().dt);
    send(b);
    profile_tries_++;
  }

  void setCredentials(const std::string& ssid, const std::string& pass,
                      const std::string& ip = "", const std::string& mac = "") {
    ssid_ = ssid; pass_ = pass; ip_ = ip; mac_ = mac;
  }

  void requestStatus() {
    if (model_.generation == 2) {
      pushStatus();
    } else {
      send("POST_CDCDEVICE_STATUS");
    }
  }

  // Firenet V1 status: EXACTLY 19 fields (0 to 18, ending with mac, no OTA fields).
  // bl=101, app=112, rev=360, spwf=0, symbol=4, initialised=1.
  void pushStatus(const std::string& ssidClear = "", const std::string& wpa2 = "",
                  const std::string& ip = "", const std::string& mac = "",
                  int rssi = -55, const std::string& id = "0000000",
                  const std::string& token = "00000000") {
    std::string s_ssid = ssidClear.empty() ? ssid_ : ssidClear;
    std::string s_pass = wpa2.empty() ? pass_ : wpa2;
    std::string s_ip   = ip.empty() ? ip_ : ip;
    std::string s_mac  = mac.empty() ? mac_ : mac;

    std::string ssid = (dt() == 3) ? hexEncode(s_ssid) : s_ssid;
    std::string f = (model_.generation == 2) ? "GET_FIRENET_STATUS=0;\n"
                                              : "GET_CDCDEVICE_STATUS=0;\n";
    char rssis[8], apps[8], bls[8], revs[8];
    snprintf(rssis, sizeof rssis, "%d", rssi);
    snprintf(apps, sizeof apps, "%d", profile().app);
    snprintf(bls, sizeof bls, "%d", profile().bl);
    snprintf(revs, sizeof revs, "%d", profile().rev);
    const char* vals[19] = {
      "0","1","0","0","1","4","0",          // monitoring,on_off,scan,init,initialised,symbol,error
      bls, apps, revs, "0", rssis,           // bl,app,rev,spwf,rssi
      id.c_str(), token.c_str(), (model_.generation == 2 ? "1" : "3"), // id,token,protocol
      ssid.c_str(), s_pass.c_str(),         // ssid,wpa2
      s_ip.c_str(), s_mac.c_str()};         // ip,mac
    for (int i = 0; i < 19; i++) { f += vals[i]; f += '\n'; }
    if (model_.generation != 2) {
      f += "0\n0\n0\n";
    }
    send(f);
  }

  // Lit les capteurs : en V1, requêtes par priorité 1 ou 2. En V3, déclare les sentinelles.
  void pollSensors(const std::vector<std::string>& names = {}) {
    if (model_.generation == 2) {
      send("GET_SENSORS=1; \n");
      transferCompleted();
    } else {
      sendTable("GET_SENSORS", names, 0);
      sendRevision();                       // déclenche la sélection (§8.3)
      transferCompleted();                  // -> POST_CONTROLS si en attente
      transferCompleted();                  // -> POST_SENSORS
    }
  }
  void pollPrio2Sensors() {
    if (model_.generation == 2) {
      send("GET_SENSORS=2; \n");
      transferCompleted();
    }
  }
  void pollControls(const std::vector<std::string>& names = {}) {
    if (model_.generation == 2) {
      send("GET_CONTROLS=0; \n");
      transferCompleted();
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

    // Mettre à jour avec les valeurs passées dans `full`
    for (const auto& kv : full) {
      if (kv.first == "onOff" || kv.first == "on") onOff = kv.second;
      else if (kv.first == "mode") mode = kv.second;
      else if (kv.first == "targetStage" || kv.first == "stage" || kv.first == "power") targetStage = kv.second;
      else if (kv.first == "roomTarget" || kv.first == "room" || kv.first == "temperature") roomTarget = kv.second;
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

    // Mettre à jour immédiatement le modèle local car le poêle recopie value -> prev
    // et n'émettra pas de POST_CONTROLS pour les valeurs imposées (§13.2)
    model_.controls["onOff"] = onOff;
    model_.controls["mode"] = mode;
    model_.controls["targetStage"] = targetStage;
    model_.controls["stage"] = targetStage;
    model_.controls["roomTarget"] = roomTarget;
    model_.controls["room"] = roomTarget;
    if (model_.controls_pos.size() >= 5) {
      model_.controls_pos[1] = onOff;
      model_.controls_pos[2] = mode;
      model_.controls_pos[3] = targetStage;
      model_.controls_pos[4] = roomTarget;
    }

    // Émission dans l'ORDRE POSITIONNEL STRICT requis par le poêle (§13 / FUN_80010b54) :
    // 0: revision, 1: onOff, 2: mode, 3: targetStage, 4: roomTarget
    char b[160];
    snprintf(b, sizeof b,
             "GET_CONTROLS=1; revision=%ld; onOff=%ld; mode=%ld; targetStage=%ld; roomTarget=%ld; ",
             (long)model_.revision, onOff, mode, targetStage, roomTarget);
    send(b);
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
  static const uint32_t TX_GAP_MS = 600;  // silence entre trames (garantit >100 ticks poêle)

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
  // buffers pour les entiers de version (snprintf-safe)
  const char* itoaBL()  { snprintf(bBL_,8,"%d",BL_VERSION);  return bBL_; }
  const char* itoaAPP() { snprintf(bAPP_,8,"%d",APP_VERSION);return bAPP_;}
  const char* itoaREV() { snprintf(bREV_,8,"%d",APP_REVISION);return bREV_;}
  char bBL_[8], bAPP_[8], bREV_[8];

  void dispatch(const std::string& buf) {
    model_.frames_in++;
    if (dbg_) dbg_("rx", buf);
    // toute trame de commande connue clôt un dump positionnel en cours ; seules
    // les continuations "=val" (isContinuation) le prolongent (traité plus bas).
    if (!isContinuation(buf)) pending_pos_ = nullptr;
    std::string clean = trim(buf);
    bool looksLikeProbe = (clean == "3" || clean == "0" || buf.find('\x16') != std::string::npos ||
                           (buf.find('\x02') != std::string::npos && buf.find('0') != std::string::npos));
    if (buf.find("GET_WIFI_VERSION_FINISHED") != std::string::npos) {
      model_.generation = 2; model_.version_ack = true;
      model_.version_profile = profile_; post_ack_probe_streak_ = 0;
      pushStatus();
      return;
    }
    if (buf.find("GET_CDCDEVICE_VERSION_FINISHED") != std::string::npos) {
      model_.generation = 1; model_.version_ack = true;
      model_.version_profile = profile_; post_ack_probe_streak_ = 0; return;
    }
    if (buf.find("GET_CDCDEVICE_VERSION_UNFINISHED") != std::string::npos) return;
    if (!model_.version_ack && looksLikeProbe) {
      txq_.clear();
      sendVersion();
      last_tx_ms_ = 0;
      return;
    }
    // Réinitialisation de session poêle V1 (VA 0x8003be74 émet \x02 0 \x03 quand *0x1ac8=0) :
    // Le poêle signale explicitement que la session est perdue ou expirée.
    if (clean == "0" || (buf.find('\x02') != std::string::npos && buf.find('0') != std::string::npos)) {
      model_.version_ack = false;
      model_.version_profile = -1;
      post_ack_probe_streak_ = 0;
      profile_ = 1;          // rester sur V1 — le poêle INDUO ne comprend pas V3
      profile_tries_ = 0;    // remettre le compteur à zéro pour ne pas déclencher de fallback
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
        profile_ = 1;          // rester sur V1 au re-arm
        profile_tries_ = 0;
        txq_.clear();
        sendVersion();
        last_tx_ms_ = 0;
      }
      return;
    }
    post_ack_probe_streak_ = 0;   // any real frame below => link is healthy again
    if (buf.find("GET_NETWORKS_FINISHED") != std::string::npos) return;
    const char* wantStatus = (model_.generation == 2) ? "POST_FIRENET_STATUS"
                                                       : "POST_CDCDEVICE_STATUS";
    if (buf.find(wantStatus) != std::string::npos ||
        buf.find("POST_FIRENET_STATUS") != std::string::npos ||
        buf.find("POST_CDCDEVICE_STATUS") != std::string::npos) {
      pending_pos_ = nullptr; parseStatus(buf); return;
    }
    if (buf.find("POST_CONTROLS") != std::string::npos) {
      std::vector<long> tmpPos;
      parseBody(afterHeader(buf), &model_.controls, tmpPos);
      if (tmpPos.size() >= 5) {
        model_.controls_pos = tmpPos;
      } else {
        long on = 0, md = 2, st = 70, rm = 200;
        auto itO = model_.controls.find("onOff"); if (itO != model_.controls.end()) on = itO->second;
        auto itM = model_.controls.find("mode"); if (itM != model_.controls.end()) md = itM->second;
        auto itS = model_.controls.find("targetStage"); if (itS != model_.controls.end()) st = itS->second;
        auto itR = model_.controls.find("roomTarget"); if (itR != model_.controls.end()) rm = itR->second;
        model_.controls_pos = { (long)model_.revision, on, md, st, rm };
      }
      return;
    }
    if (buf.find("POST_SENSORS")  != std::string::npos) {
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
            (*store)[ctrlName((int)pos.size() - 1)] = v;
          } else if (store == &model_.sensors) {
            (*store)[sensName((int)pos.size() - 1, model_.generation)] = v;
          }
        }
      }
      i = sc + 1;
    }
  }
  void postSensors() { /* hook : le firmware relit model().sensors après un cycle */ }
  static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \r\n\t\x02\x03");
    size_t b = s.find_last_not_of(" \r\n\t\x02\x03");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
  }

  TxFn tx_; NowFn now_;
  StoveModel model_;
  std::string rx_;
  bool silence_pending_ = false;
  uint32_t last_version_ms_ = 0;
  bool version_sent_ = false;
  // automatic version-frame fallback. profile_: 0 = V3, 1 = V1.
  // Start on V1 (older stoves); if the stove does not acknowledge, sendVersion()
  // switches to V3, and back again, until an ACK arrives.
  int profile_ = 1;
  int profile_tries_ = 0;
  static const int PROFILE_SWITCH_AFTER = 3;   // unacked attempts before switching
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
