#!/usr/bin/env python3
"""Run the firmware's actual HTTP and serial provisioning handlers with host-side I/O doubles."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "open-firenet/open-firenet.ino").read_text()


def function(signature):
    start = source.index(signature)
    # These handlers end at an unindented brace, unlike nested blocks.
    end = source.index("\n}", start) + 2
    return source[start:end]


mocks = r'''
#include <cassert>
#include <map>
#include <string>
#include <cstdio>
struct String : std::string {
  using std::string::string;
  String(std::string s) : std::string(s) {}
  bool startsWith(const char* prefix) const { return rfind(prefix, 0) == 0; }
  String substring(size_t a, size_t b = std::string::npos) const { return substr(a, b == std::string::npos ? b : b-a); }
  int indexOf(char c) const { auto n = find(c); return n == npos ? -1 : (int)n; }
  size_t length() const { return size(); }
};
struct Print {
  template<class... T> void printf(const char*, T...) {}
  void println(const char*) {}
  void flush() {}
} DBG;
struct Prefs {
  std::map<std::string, std::string> values;
  void begin(const char*, bool) {}
  void end() {}
  void putString(const char* key, const String& value) { values[key] = value; }
} prefs;
struct Web {
  std::map<std::string, String> args;
  int status = 0;
  bool hasArg(const char* key) { return args.count(key); }
  String arg(const char* key) { return args[key]; }
  void send(int code, const char*, const char*) { status = code; }
} web;
String g_ethBoard = "waveshare_s3_eth";
constexpr int NOTE_WIFI_CHANGE = 1;
int restarts = 0;
void delay(int) {}
void restartBecause(int) { restarts++; }
'''
cases = r'''
int main() {
  prefs.values["eth_board"] = "waveshare_s3_eth";
  // Invalid HTTP provisioning must not change the connection selection.
  handleWifi();
  assert(web.status == 400 && restarts == 0);
  assert(prefs.values["eth_board"] == "waveshare_s3_eth");
  web.args = {{"ssid", "home"}, {"pass", "secret:with:colons"}};
  handleWifi();
  assert(web.status == 200 && restarts == 1);
  assert(prefs.values["eth_board"] == "none" && g_ethBoard == "none");
  assert(prefs.values["ssid"] == "home");
  assert(prefs.values["pass"] == "secret:with:colons");
  // An open Wi-Fi network is supported too.
  web.args = {{"ssid", "open"}};
  handleWifi();
  assert(prefs.values["pass"].empty());
  prefs.values["eth_board"] = g_ethBoard = "waveshare_s3_eth";
  assert(!applySetWifi("SETWIFI::invalid", DBG));
  assert(prefs.values["eth_board"] == "waveshare_s3_eth");
  assert(applySetWifi("SETWIFI:home:secret:with:colons", DBG));
  assert(prefs.values["eth_board"] == "none" && g_ethBoard == "none");
  assert(prefs.values["ssid"] == "home");
  assert(prefs.values["pass"] == "secret:with:colons");
  assert(applySetWifi("SETWIFI:open:", DBG));
  assert(prefs.values["pass"].empty());
  assert(!applySetWifi("other", DBG));
  puts("Wi-Fi provisioning tests passed");
}
'''
with tempfile.TemporaryDirectory(prefix="firenet-provisioning-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(mocks + function("static void handleWifi()") +
                                   function("static bool applySetWifi(") + cases)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(path / "test.cpp"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
