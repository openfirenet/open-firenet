// Run with -Itest/network_stubs -Iopen-firenet: the real network helper, fake interfaces.
#include "firenet_network.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
  assert(firenet::validEthernetBoard("none"));
  assert(firenet::validEthernetBoard("waveshare_s3_eth"));
  assert(!firenet::validEthernetBoard("other"));
  assert(!firenet::validEthernetBoard(""));
  assert(!firenet::ethernetEnabled);
  WiFi.mode = WIFI_STA;
  WiFi.state = WL_CONNECTED;
  assert(firenet::networkConnected());
  assert(firenet::networkIP() == WiFi.localIP());
  assert(std::strcmp(firenet::networkMode(), "STA") == 0);
  WiFi.mode |= WIFI_AP;
  assert(firenet::networkIP() == WiFi.softAPIP());
  assert(std::strcmp(firenet::networkMode(), "AP") == 0);

  firenet::ethernetEnabled = true;
  // A connected Wi-Fi station must not masquerade as a wired connection.
  assert(!firenet::networkConnected());
  assert(firenet::networkIP() == WiFi.softAPIP());
  WiFi.mode = WIFI_OFF;
  assert(firenet::networkIP() == IPAddress());
  assert(std::strcmp(firenet::networkMode(), "OFF") == 0);
  assert(firenet::beginEthernet());
  const std::array<int, 9> pins = {ETH_PHY_W5500, 1, 14, 10, 9, SPI2_HOST, 13, 12, 11};
  assert(ETH.arguments == pins);

  // DHCP acquired, including while the setup hotspot is still being closed.
  for (int mode : {WIFI_OFF, WIFI_AP}) {
    WiFi.mode = mode;
    ETH.addressed = true;
    assert(firenet::networkConnected());
    assert(firenet::networkIP() == ETH.localIP());
    assert(std::strcmp(firenet::networkMode(), "ETH") == 0);
    ETH.addressed = false;
    assert(!firenet::networkConnected());
    assert(firenet::networkIP() == (mode == WIFI_AP ? WiFi.softAPIP() : IPAddress()));
  }
  std::cout << "network tests passed\n";
}
