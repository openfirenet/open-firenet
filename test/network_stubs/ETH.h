#pragma once
#include "WiFi.h"
#include <array>
constexpr int ETH_PHY_W5500 = 5, SPI2_HOST = 1;
struct ETHClass {
  bool addressed = false;
  std::array<int, 9> arguments{};
  bool hasIP() const { return addressed; }
  IPAddress localIP() const { return 3; }
  bool begin(int phy, int addr, int cs, int irq, int rst, int host, int sck, int miso, int mosi) {
    arguments = {phy, addr, cs, irq, rst, host, sck, miso, mosi};
    return true;
  }
};
inline ETHClass ETH;
