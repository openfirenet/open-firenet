#!/usr/bin/env bash
# Compile et exécute les tests hôte du cœur protocole + le pont d'intégration.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(dirname "$HERE")"
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/firenet_protocol_test.cpp" -o /tmp/firenet_core_test && /tmp/firenet_core_test
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/link_test.cpp"             -o /tmp/firenet_link_test && /tmp/firenet_link_test
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/host_bridge.cpp"           -o /tmp/host_bridge
python3 "$HERE/test_stove_sim.py"
