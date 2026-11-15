#pragma once
#include <WiFi.h>
#include <ETH.h>

namespace firenet {

// Explicit opt-in: these pins are connected to other peripherals on other S3 boards.
inline bool ethernetEnabled = false;

inline bool networkConnected() {
  return ethernetEnabled ? ETH.hasIP() : WiFi.status() == WL_CONNECTED;
}

inline IPAddress networkIP() {
  if (ethernetEnabled && ETH.hasIP()) return ETH.localIP();
  if (WiFi.getMode() & WIFI_AP) return WiFi.softAPIP();
  return ethernetEnabled ? IPAddress() : WiFi.localIP();
}

inline const char* networkMode() {
  if (ethernetEnabled && ETH.hasIP()) return "ETH";
  if (WiFi.getMode() & WIFI_AP) return "AP";
  return ethernetEnabled ? "OFF" : "STA";
}

inline bool validEthernetBoard(const String& board) {
  return board == "none" || board == "waveshare_s3_eth";
}

inline bool beginEthernet() {
  // Waveshare ESP32-S3-ETH: CS=14, INT=10, RST=9, SCLK=13, MISO=12, MOSI=11.
  // Use the ESP-IDF SPI driver and lwIP, sharing the HTTP/MQTT/OTA network stack.
  return ETH.begin(ETH_PHY_W5500, 1, 14, 10, 9, SPI2_HOST, 13, 12, 11);
}

}  // namespace firenet
