#!/bin/sh
# Host tests of the firmware logic: no ESP32 or ESP-IDF needed, only a C++17 compiler (g++, or clang++ via CXX).
# The ESP-IDF / FreeRTOS API is stubbed in stubs/, time and the LD2410 radar are simulated (sim.h, radar_sim.h).
# Run: test/host/run.sh   (a test binary run with -v prints its log)
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
MAIN=../../main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
# Warnings as in the ESP-IDF build of the main component
FLAGS="-std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare -Werror -I stubs -I . -I $MAIN -I $MAIN/drivers"
RADAR="$MAIN/drivers/ld2410.cpp radar_sim.cpp sim.cpp"

$CXX $FLAGS -o "$OUT/ld2410" test_ld2410.cpp radar_sim.cpp sim.cpp
$CXX $FLAGS -o "$OUT/settings" test_settings.cpp $MAIN/radar_settings.cpp $RADAR
$CXX $FLAGS -DCONFIG_LD2410_LEARN_RADAR_AUTO=0 -o "$OUT/learn_esp32" test_learn.cpp $MAIN/quiet_learn.cpp \
    $MAIN/radar_settings.cpp $RADAR
$CXX $FLAGS -DCONFIG_LD2410_LEARN_RADAR_AUTO=1 -o "$OUT/learn_radar" test_learn.cpp $MAIN/quiet_learn.cpp \
    $MAIN/radar_settings.cpp $RADAR
$CXX $FLAGS -o "$OUT/worker" test_worker.cpp $MAIN/quiet_learn.cpp $MAIN/radar_settings.cpp $RADAR
$CXX $FLAGS -o "$OUT/presence" test_presence.cpp radar_sim.cpp sim.cpp
$CXX $FLAGS -DCONFIG_PRESENCE_MAX_DISTANCE_CM=200 -o "$OUT/presence_distance" test_presence.cpp radar_sim.cpp sim.cpp
$CXX $FLAGS -o "$OUT/button" test_button.cpp sim.cpp
$CXX $FLAGS -o "$OUT/led" test_led.cpp sim.cpp
$CXX $FLAGS -DCONFIG_STATUS_LED_ACTIVE_HIGH=1 -o "$OUT/led_active_high" test_led.cpp sim.cpp
$CXX $FLAGS -DCONFIG_STATUS_LED_TYPE_WS2812=1 -DCONFIG_STATUS_LED_BRIGHTNESS=20 -o "$OUT/led_ws2812" test_led_ws2812.cpp \
    sim.cpp
$CXX $FLAGS -o "$OUT/identity" test_identity.cpp $MAIN/device_name.cpp sim.cpp
$CXX $FLAGS -o "$OUT/diag_report" test_diag_report.cpp sim.cpp
$CXX $FLAGS -o "$OUT/net_watchdog" test_net_watchdog.cpp sim.cpp
$CXX $FLAGS -o "$OUT/c3_temperature" test_c3_temperature.cpp sim.cpp

status=0
for t in ld2410 settings learn_esp32 learn_radar worker presence presence_distance button led led_active_high led_ws2812 \
    identity diag_report net_watchdog c3_temperature; do
    "$OUT/$t" || status=1
done
[ $status -eq 0 ] && echo "All host tests passed" || echo "Host tests FAILED"
exit $status
