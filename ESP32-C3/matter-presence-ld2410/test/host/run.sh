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
FILTER="-DCONFIG_PRESENCE_ENERGY_FILTER=1 -DCONFIG_PRESENCE_ENERGY_FILTER_STRONG=15 -DCONFIG_PRESENCE_ENERGY_FILTER_FRAMES=3"
$CXX $FLAGS $FILTER -o "$OUT/energy_filter" test_energy_filter.cpp radar_sim.cpp sim.cpp
$CXX $FLAGS $FILTER -o "$OUT/worker_filter" test_worker_filter.cpp $MAIN/presence.cpp $MAIN/quiet_learn.cpp \
    $MAIN/radar_settings.cpp $RADAR
ENGINE="-DCONFIG_PRESENCE_ESP_ENGINE=1 -DCONFIG_PRESENCE_ENGINE_TRIGGER_REPORTS=6 -DCONFIG_PRESENCE_ENGINE_TRIGGER_WINDOW=8 \
    -DCONFIG_PRESENCE_ENGINE_FAST_REPORTS=3 -DCONFIG_PRESENCE_ENGINE_FAST_MARGIN=20 \
    -DCONFIG_PRESENCE_ENGINE_HOLD_REPORTS=3 -DCONFIG_PRESENCE_ENGINE_HOLD_WINDOW=10 -DCONFIG_PRESENCE_ENGINE_DATA_TIMEOUT_MS=2000 \
    -DCONFIG_PRESENCE_ENGINE_HOLD_DROP_PERCENT=40 -DCONFIG_PRESENCE_ENGINE_HOLD_DROP_MAX=10 \
    -DCONFIG_PRESENCE_ENGINE_LEARN_FLOOR_PERCENT=75 -DCONFIG_PRESENCE_ENGINE_LEARN_MIN_MARGIN=8 \
    -DCONFIG_LD2410_LEARN_DURATION_SEC=120 -DCONFIG_PRESENCE_ENGINE_STATUS_LOG_SEC=300 \
    -DCONFIG_PRESENCE_ENGINE_TRIGGER_MARGIN=0 -DCONFIG_PRESENCE_ENGINE_TRIGGER_NOISE_MARGIN=5"
$CXX $FLAGS $ENGINE -O1 -o "$OUT/presence_engine" test_presence_engine.cpp $MAIN/presence_engine.cpp sim.cpp
$CXX $FLAGS $ENGINE -o "$OUT/esp_presence" test_esp_presence.cpp $MAIN/presence_engine.cpp $MAIN/radar_settings.cpp $RADAR
$CXX $FLAGS $ENGINE -o "$OUT/worker_engine" test_worker_engine.cpp $MAIN/quiet_learn.cpp $MAIN/radar_settings.cpp $RADAR

status=0
for t in ld2410 settings learn_esp32 learn_radar worker presence presence_distance button led led_active_high led_ws2812 \
    identity diag_report net_watchdog c3_temperature energy_filter \
    worker_filter presence_engine esp_presence worker_engine; do
    "$OUT/$t" || status=1
done
[ $status -eq 0 ] && echo "All host tests passed" || echo "Host tests FAILED"
exit $status
