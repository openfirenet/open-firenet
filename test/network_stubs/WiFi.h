#pragma once
#include <string>
using String = std::string;
using IPAddress = unsigned;
constexpr int WL_CONNECTED = 3;
constexpr int WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2;
struct WiFiClass {
  int mode = WIFI_OFF, state = 0;
  int getMode() const { return mode; }
  int status() const { return state; }
  IPAddress localIP() const { return 1; }
  IPAddress softAPIP() const { return 2; }
};
inline WiFiClass WiFi;
