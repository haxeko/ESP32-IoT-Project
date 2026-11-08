#!/bin/sh
# Host tests of the door contact state machine (main/contact_fsm.cpp), the factory reset by battery insertions
# (main/power_cycle.cpp) and the reported battery values (main/battery_report.cpp, main/battery_gauge.cpp):
# no ESP32 or ESP-IDF needed, only a C++17 compiler (g++, or clang++ via CXX).
# Run: test/host/run.sh
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
MAIN=../../main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

$CXX -std=gnu++17 -Wall -Wextra -Werror -I "$MAIN" -o "$OUT/contact_fsm" test_contact_fsm.cpp "$MAIN/contact_fsm.cpp"
"$OUT/contact_fsm"
$CXX -std=gnu++17 -Wall -Wextra -Werror -I "$MAIN" -o "$OUT/power_cycle" test_power_cycle.cpp "$MAIN/power_cycle.cpp"
"$OUT/power_cycle"
$CXX -std=gnu++17 -Wall -Wextra -Werror -I "$MAIN" -o "$OUT/battery_report" test_battery_report.cpp \
    "$MAIN/battery_report.cpp"
"$OUT/battery_report"
$CXX -std=gnu++17 -Wall -Wextra -Werror -I "$MAIN" -o "$OUT/battery_gauge" test_battery_gauge.cpp \
    "$MAIN/battery_gauge.cpp"
"$OUT/battery_gauge"
