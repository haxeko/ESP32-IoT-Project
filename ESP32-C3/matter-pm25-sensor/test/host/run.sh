#!/bin/sh
# Host tests of the drivers and the local UI: no ESP32 or ESP-IDF needed, only
# a C/C++ compiler (gcc/g++, or clang via CC/CXX). Run: test/host/run.sh
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
CXX=${CXX:-g++}
MAIN=../../main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FLAGS="-std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Werror -I stubs -I . -I $MAIN -I $MAIN/drivers"

$CC -c -o "$OUT/fonts.o" -I "$MAIN/ui" "$MAIN/ui/fonts.c"
$CXX $FLAGS -DPERIOD=300 -o "$OUT/pm_duty" test_pm_sensor.cpp sim.cpp
$CXX $FLAGS -DPERIOD=0 -o "$OUT/pm_continuous" test_pm_sensor.cpp sim.cpp
$CXX $FLAGS -o "$OUT/button" test_button.cpp sim.cpp
$CXX $FLAGS -o "$OUT/scd4x" test_scd4x.cpp sim.cpp
$CXX $FLAGS -o "$OUT/local_ui" test_local_ui.cpp "$MAIN/drivers/sh1106.cpp" sim.cpp "$OUT/fonts.o"
$CXX $FLAGS -DCONFIG_OLED_CONTROLLER_SSD1306=1 -o "$OUT/local_ui_ssd1306" test_local_ui.cpp \
    "$MAIN/drivers/sh1106.cpp" sim.cpp "$OUT/fonts.o"
$CXX $FLAGS -DCONFIG_DISPLAY_BUTTON_GPIO=-1 -o "$OUT/local_ui_no_button" test_local_ui.cpp \
    "$MAIN/drivers/sh1106.cpp" sim.cpp "$OUT/fonts.o"
$CXX $FLAGS -DCONFIG_CO2_SENSOR_NONE=1 -o "$OUT/local_ui_no_co2" test_local_ui.cpp \
    "$MAIN/drivers/sh1106.cpp" sim.cpp "$OUT/fonts.o"

status=0
for t in pm_duty pm_continuous button scd4x local_ui local_ui_ssd1306 local_ui_no_button local_ui_no_co2; do
    "$OUT/$t" || status=1
done
[ $status -eq 0 ] && echo "All host tests passed" || echo "Host tests FAILED"
exit $status
