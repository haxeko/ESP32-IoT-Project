#!/bin/sh
# Host test of the button gesture state machine (main/button_fsm.cpp): no ESP32 or ESP-IDF needed,
# only a C++17 compiler (g++, or clang++ via CXX). Run: test/host/run.sh
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
MAIN=../../main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

$CXX -std=gnu++17 -Wall -Wextra -Werror -I "$MAIN" -o "$OUT/button_fsm" test_button_fsm.cpp "$MAIN/button_fsm.cpp"
"$OUT/button_fsm"
