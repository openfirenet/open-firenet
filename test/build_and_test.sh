#!/usr/bin/env bash
# Compile et exécute les tests hôte du cœur protocole + le pont d'intégration.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(dirname "$HERE")"
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/firenet_protocol_test.cpp" -o /tmp/firenet_core_test && /tmp/firenet_core_test
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/link_test.cpp"             -o /tmp/firenet_link_test && /tmp/firenet_link_test
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/api_test.cpp"              -o /tmp/firenet_api_test && /tmp/firenet_api_test
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/mqtt_test.cpp"             -o /tmp/firenet_mqtt_test && /tmp/firenet_mqtt_test
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/host_bridge.cpp"           -o /tmp/host_bridge
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/web_guard_test.cpp"       -o /tmp/firenet_web_guard_test && /tmp/firenet_web_guard_test
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/update_verify_test.cpp"    -o /tmp/firenet_update_verify_test && /tmp/firenet_update_verify_test
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/display_config_test.cpp"    -o /tmp/firenet_display_config_test && /tmp/firenet_display_config_test
g++ -std=c++17 -Wall -I"$ROOT/open-firenet" -I"$ROOT" "$HERE/crash_test.cpp"          -o /tmp/firenet_crash_test && /tmp/firenet_crash_test
python3 "$HERE/test_stove_sim.py"
python3 "$HERE/openapi_test.py"
node "$HERE/i18n_test.mjs"
node "$HERE/diag_file_test.mjs"
