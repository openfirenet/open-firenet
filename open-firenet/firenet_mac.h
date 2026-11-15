#pragma once
#include <Arduino.h>
#include "esp_mac.h"

// MAC address of the chip's Wi-Fi station, read from its factory value: WiFi.macAddress() gives zeros while the station
// interface is not started (setup mode), and the installer recognises a bridge by this address.
static inline String stationMac() {
  uint8_t m[6] = {0};
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char buf[18];
  snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(buf);
}
