// Self-update on the ESP32: looks at the latest GitHub release, and installs it when the user asks.
//
// Trust chain (see firenet_update_verify.h): public key in the firmware -> SHA256SUMS.minisig -> SHA256SUMS ->
// SHA-256 of open-firenet-ota.bin. Nothing is activated before the downloaded image has the signed hash, and nothing
// is installed unless the stove is switched off. Everything runs in its own task; the public methods return at once.
#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <functional>
#include <string>
#include "firenet_update_engine.h"
#include "firenet_update_glue.h"

extern const uint8_t firenet_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t firenet_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace firenet {

class Updater {
public:
  enum class State : uint8_t { Idle, Checking, Available, Downloading, Verifying, Installing, Error, Restarting };

  struct Status {
    const char* state = "idle";
    std::string current, latest, error;
    int percent = 0;
    bool stoveBlocks = false;
    bool autoCheck = false;
    int checkHours = 0;
    long lastCheckAgoS = -1;   // seconds since the last check that reached GitHub, -1 if none
  };

  // stoveState: the stove state name as in /api/state ("off", "standby", ...). beforeRestart: called just before the
  // restart that follows a successful installation.
  Updater(const char* currentVersion, std::function<std::string()> stoveState, std::function<void()> beforeRestart)
      : current_(currentVersion), stoveState_(stoveState), beforeRestart_(beforeRestart) {
    mx_ = xSemaphoreCreateMutex();
  }

  // Called before a download and after a failed one: the caller frees memory the download needs, then takes it back.
  void setMemoryHooks(std::function<void()> release, std::function<void()> restore) { release_ = release; restore_ = restore; }

  void begin() {
    Preferences p; p.begin("firenet", true);
    autoCheck_ = p.getBool("upd_auto", false);
    checkHours_ = p.getUChar("upd_hours", 24);
    p.end();
    if (checkHours_ != 12 && checkHours_ != 24 && checkHours_ != 168) checkHours_ = 24;
    nextAutoMs_ = millis() + FIRST_AUTO_DELAY_MS;
  }

  void setAuto(bool on, int hours) {
    if (hours != 12 && hours != 24 && hours != 168) hours = checkHours_;
    lock(); autoCheck_ = on; checkHours_ = hours; unlock();
    Preferences p; p.begin("firenet", false);
    p.putBool("upd_auto", on); p.putUChar("upd_hours", (uint8_t)hours);
    p.end();
    nextAutoMs_ = millis() + (on ? FIRST_AUTO_DELAY_MS : 0);
  }

  Status status() {
    Status s;
    lock();
    static const char* N[] = {"idle", "checking", "available", "downloading", "verifying", "installing", "error", "restarting"};
    s.state = N[(int)state_]; s.current = current_; s.latest = latest_; s.error = error_; s.percent = percent_;
    s.autoCheck = autoCheck_; s.checkHours = autoCheck_ ? checkHours_ : 0;
    s.lastCheckAgoS = lastOkMs_ ? (long)((millis() - lastOkMs_) / 1000) : -1;
    unlock();
    s.stoveBlocks = !upd::stoveAllowsUpdate(stoveState_());
    return s;
  }

  // Both return false when a task is already running, or (install) when nothing newer was found or the stove is on.
  bool requestCheck() { return start(Job::Check); }
  bool requestInstall() {
    lock(); bool ok = state_ == State::Available && !hash_.empty(); unlock();
    if (!ok || !upd::stoveAllowsUpdate(stoveState_())) return false;
    return start(Job::Install);
  }

  // Call from loop(): the periodic check, when the user switched it on. It only detects, it never installs.
  void poll() {
    lock(); bool due = autoCheck_ && state_ != State::Checking && state_ != State::Downloading && state_ != State::Verifying
                       && state_ != State::Installing && state_ != State::Restarting;
    uint8_t hours = checkHours_; unlock();
    if (!due || WiFi.status() != WL_CONNECTED || (int32_t)(millis() - nextAutoMs_) < 0) return;
    nextAutoMs_ = millis() + (uint32_t)hours * 3600000UL;   // one attempt per interval, success or not
    start(Job::Check);
  }

private:
  enum class Job { Check, Install };
  static constexpr uint32_t FIRST_AUTO_DELAY_MS = 10UL * 60UL * 1000UL;
  static constexpr uint32_t HTTP_TIMEOUT_MS = 8000;
  static constexpr const char* BASE = "https://github.com/openfirenet/open-firenet/releases/latest/download/";
  static constexpr const char* IMAGE_NAME = "open-firenet-ota.bin";
  // Public key of the release signature (same as the installer's OFFICIAL_PUBLIC_KEY and minisign.pub).
  static constexpr const char* PUBKEY = "RWQhzS5dR0kodMCMiFMESBDRDhxMyRaA4yNbIcAMD2B9kQS21I+FABT4";

  void lock() { xSemaphoreTake(mx_, portMAX_DELAY); }
  void unlock() { xSemaphoreGive(mx_); }
  void set(State s, const std::string& err = std::string(), int pct = 0) {
    lock(); state_ = s; error_ = err; percent_ = pct; unlock();
  }

  bool start(Job j) {
    lock();
    bool busy = running_ || state_ == State::Restarting;
    if (!busy) running_ = true;
    unlock();
    if (busy) return false;
    job_ = j;
    if (xTaskCreate(&Updater::entry, "fn_update", 16384, this, 1, nullptr) != pdPASS) {
      lock(); running_ = false; unlock();
      set(State::Error, "no memory");
      return false;
    }
    return true;
  }
  static void entry(void* self) {
    Updater* u = static_cast<Updater*>(self);
    if (u->job_ == Job::Check) u->doCheck(); else u->doInstall();
    u->lock(); u->running_ = false; u->unlock();
    vTaskDelete(nullptr);
  }

  void tlsSetup(NetworkClientSecure& c) {
    c.setCACertBundle(firenet_crt_bundle_start, (size_t)(firenet_crt_bundle_end - firenet_crt_bundle_start));
    c.setTimeout(HTTP_TIMEOUT_MS / 1000);
  }

  // Small text file (the signature files). Empty on any failure; `why` says which.
  std::string fetchText(const char* name, std::string& why) {
    NetworkClientSecure c; tlsSetup(c);
    HTTPClient h;
    h.setConnectTimeout(HTTP_TIMEOUT_MS); h.setTimeout(HTTP_TIMEOUT_MS);
    h.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    std::string url = std::string(BASE) + name;
    if (!h.begin(c, url.c_str())) { why = "begin"; return std::string(); }
    int code = h.GET();
    if (code != HTTP_CODE_OK) { why = code > 0 ? "http " + std::to_string(code) : std::string("net ") + HTTPClient::errorToString(code).c_str(); h.end(); return std::string(); }
    int len = h.getSize();
    if (len <= 0 || len > 4096) { why = "size"; h.end(); return std::string(); }
    String body = h.getString();
    h.end();
    return std::string(body.c_str(), body.length());
  }

  void doCheck() {
    if (release_) release_();                                // the TLS connection needs the memory of the screen
    checkNow();
    if (restore_) restore_();
  }

  void checkNow() {
    set(State::Checking);
    std::string why;
    std::string sums = fetchText("SHA256SUMS", why);
    std::string sig = sums.empty() ? std::string() : fetchText("SHA256SUMS.minisig", why);
    if (sums.empty() || sig.empty()) { set(State::Error, "download: " + why); return; }
    lock(); lastOkMs_ = millis(); if (!lastOkMs_) lastOkMs_ = 1; unlock();
    std::string comment;
    if (!upd::verifyRelease(sums, sig, PUBKEY, hashFn(), edFn(), comment)) { set(State::Error, "signature"); return; }
    upd::Version v = upd::versionFromComment(comment);
    if (!v.ok) { set(State::Error, "version"); return; }
    std::string h = upd::hashFor(sums, IMAGE_NAME);
    if (h.empty()) { set(State::Error, "no image in release"); return; }
    std::string vs = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    lock(); latest_ = vs; unlock();
    if (!upd::isNewer(v, upd::parseVersion(current_))) { lock(); hash_.clear(); unlock(); set(State::Idle); return; }
    lock(); hash_ = h; unlock();
    set(State::Available);
  }

  void doInstall() {
    std::string hash; lock(); hash = hash_; unlock();
    if (hash.empty() || !upd::stoveAllowsUpdate(stoveState_())) { set(State::Error, "not allowed"); return; }
    set(State::Downloading, std::string(), 0);
    if (release_) release_();
    bool installed = false;
    downloadAndInstall(hash, installed);
    if (!installed && restore_) restore_();
  }

  void downloadAndInstall(const std::string& hash, bool& installed) {
    NetworkClientSecure c; tlsSetup(c);
    HTTPClient h;
    h.setConnectTimeout(HTTP_TIMEOUT_MS); h.setTimeout(HTTP_TIMEOUT_MS);
    h.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    std::string url = std::string(BASE) + IMAGE_NAME;
    if (!h.begin(c, url.c_str())) { set(State::Error, "begin"); return; }
    int code = h.GET();
    if (code != HTTP_CODE_OK) { h.end(); set(State::Error, "http " + std::to_string(code)); return; }
    int total = h.getSize();
    if (total <= 0 || (size_t)total > (size_t)ESP.getFreeSketchSpace()) { h.end(); set(State::Error, "size"); return; }
    if (!Update.begin((size_t)total)) { h.end(); set(State::Error, "no update slot"); return; }

    mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts(&sha, 0);
    NetworkClient* s = h.getStreamPtr();
    uint8_t buf[1024]; int got = 0; uint32_t lastData = millis();
    while (got < total) {
      // The stove must stay off for the whole download: switching it on cancels the installation.
      if (!upd::stoveAllowsUpdate(stoveState_())) { Update.abort(); h.end(); mbedtls_sha256_free(&sha); set(State::Error, "stove switched on"); return; }
      int n = s->available();
      if (n <= 0) {
        if (!s->connected() || millis() - lastData > HTTP_TIMEOUT_MS) { Update.abort(); h.end(); mbedtls_sha256_free(&sha); set(State::Error, "download interrupted"); return; }
        delay(5); continue;
      }
      n = s->readBytes(buf, std::min<int>(n, sizeof buf));
      if (n <= 0) continue;
      lastData = millis();
      if (Update.write(buf, n) != (size_t)n) { Update.abort(); h.end(); mbedtls_sha256_free(&sha); set(State::Error, "write"); return; }
      mbedtls_sha256_update(&sha, buf, n);
      got += n;
      set(State::Downloading, std::string(), (int)((int64_t)got * 100 / total));
      delay(1);   // let the idle task run: a download that never waits starved it and the watchdog restarted the bridge
    }
    h.end();
    set(State::Verifying, std::string(), 100);
    uint8_t d[32]; mbedtls_sha256_finish(&sha, d); mbedtls_sha256_free(&sha);
    char hex[65]; for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
    if (hash != hex) { Update.abort(); set(State::Error, "hash mismatch"); return; }   // nothing was activated
    set(State::Installing, std::string(), 100);
    if (!Update.end(true)) { set(State::Error, "end"); return; }   // activates the new image
    installed = true;
    set(State::Restarting, std::string(), 100);
    if (beforeRestart_) beforeRestart_();
    delay(500);
    ESP.restart();
  }

  static upd::Blake2b512Fn hashFn() {
    return [](const uint8_t* m, size_t n, uint8_t* out) { crypto_generichash(out, 64, m, n, nullptr, 0); };
  }
  static upd::Ed25519Fn edFn() {
    return [](const uint8_t* m, size_t n, const uint8_t* s, const uint8_t* k) { return crypto_sign_verify_detached(s, m, n, k) == 0; };
  }

  std::string current_, latest_, error_, hash_;
  std::function<std::string()> stoveState_;
  std::function<void()> beforeRestart_, release_, restore_;
  SemaphoreHandle_t mx_;
  State state_ = State::Idle;
  Job job_ = Job::Check;
  bool running_ = false, autoCheck_ = false;
  uint8_t checkHours_ = 24;
  int percent_ = 0;
  uint32_t lastOkMs_ = 0, nextAutoMs_ = 0;
};

}  // namespace firenet
