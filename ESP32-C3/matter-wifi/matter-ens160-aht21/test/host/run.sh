#!/bin/sh
# Host tests of the sensor drivers and the sensor task: no ESP32 or ESP-IDF needed, only a C++17 compiler (g++, or
# clang++ via CXX). The ESP-IDF / FreeRTOS API is stubbed in stubs/; time, the I2C bus, the AHT21 and the ENS160 are
# simulated (sim.h). Run: test/host/run.sh   (a test binary run with -v prints its log)
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
MAIN=../../main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
# Warnings as in the ESP-IDF build of the main component
FLAGS="-std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Werror -I stubs -I . -I $MAIN -I $MAIN/drivers"
DRIVERS="$MAIN/drivers/aht21.cpp $MAIN/drivers/ens160.cpp sim.cpp"

$CXX $FLAGS -o "$OUT/sensor_math" test_sensor_math.cpp sim.cpp
$CXX $FLAGS -o "$OUT/aht21" test_aht21.cpp $DRIVERS
$CXX $FLAGS -o "$OUT/ens160" test_ens160.cpp $DRIVERS
$CXX $FLAGS -o "$OUT/sensors" test_sensors.cpp $MAIN/sensors.cpp $DRIVERS
$CXX $FLAGS -DCONFIG_SENSOR_TEMPERATURE_OFFSET_TENTHS=-20 -DCONFIG_ENS160_REPORT_WHILE_WARMING_UP=1 \
    -o "$OUT/sensors_offset" test_sensors.cpp $MAIN/sensors.cpp $DRIVERS
$CXX $FLAGS -DNO_AHT21=1 -o "$OUT/sensors_no_aht21" test_sensors.cpp $MAIN/sensors.cpp $DRIVERS

status=0
for t in sensor_math aht21 ens160 sensors sensors_offset sensors_no_aht21; do
    "$OUT/$t" "$@" || status=1
done
[ $status -eq 0 ] && echo "All host tests passed" || echo "Host tests FAILED"
exit $status
