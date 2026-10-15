#!/bin/sh
# Host tests of the LED bar driver, the color conversions and the step motor planner: no ESP32 or ESP-IDF needed, only a C compiler
# (gcc, or clang via CC). Run: test/host/run.sh
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
MAIN=../../main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FLAGS="-std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror -I stubs -I . -I $MAIN"
LED="-DCONFIG_LED_COUNT=30 -DCONFIG_LED_GPIO=4 -DCONFIG_LED_FIXED_BRIGHTNESS_PERCENT=30"

$CC $FLAGS $LED -o "$OUT/led_bar" test_led_bar.c
$CC $FLAGS $LED -DCONFIG_LED_FILL_REVERSE=1 -o "$OUT/led_bar_reverse" test_led_bar.c
$CC $FLAGS $LED -DSIM_STRIP_INIT_FAILS -o "$OUT/led_bar_init_fails" test_led_bar.c
$CC $FLAGS -o "$OUT/color_convert" test_color_convert.c "$MAIN/color_convert.c" -lm
$CC $FLAGS -o "$OUT/motion" test_motion.c "$MAIN/motion.c" -lm

status=0
for t in led_bar led_bar_reverse led_bar_init_fails color_convert motion; do
    "$OUT/$t" || status=1
done
[ $status -eq 0 ] && echo "All host tests passed" || echo "Host tests FAILED"
exit $status
