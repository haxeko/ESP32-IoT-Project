#!/bin/sh
# Host tests of the LED bar driver, the color conversions, the step motor planner and the step motor driver (simulated
# timer, GPIO, TMC2209, lamp and end stop): no ESP32 or ESP-IDF needed, only a C compiler (gcc, or clang via CC).
# Run: test/host/run.sh
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
# Step motor driver: Kconfig defaults (in test_stepper.c) plus one build per option that changes its code paths
MOTOR="-DCONFIG_MOTOR_ENABLED=1 -DCONFIG_LED_GPIO=4 -DCONFIG_APP_BUTTON_GPIO=9 -DCONFIG_FREERTOS_HZ=100"
STEPPER="test_stepper.c $MAIN/motion.c"
$CC $FLAGS $MOTOR -o "$OUT/stepper" $STEPPER
$CC $FLAGS $MOTOR -DCONFIG_MOTOR_DIR_INVERT=1 -DCONFIG_MOTOR_IDLE_OFF_MS=2000 -o "$OUT/stepper_invert_idle_off" $STEPPER
$CC $FLAGS $MOTOR -DCONFIG_MOTOR_ENDSTOP_NC=0 -o "$OUT/stepper_no_contact" $STEPPER
$CC $FLAGS $MOTOR -DCONFIG_MOTOR_EN_GPIO=-1 -o "$OUT/stepper_no_en" $STEPPER
$CC $FLAGS $MOTOR -DCONFIG_MOTOR_LEAD_UM=2000 -DCONFIG_MOTOR_MICROSTEPS=32 -DCONFIG_MOTOR_IDLE_OFF_MS=2000 \
    -o "$OUT/stepper_fine" $STEPPER

status=0
for t in led_bar led_bar_reverse led_bar_init_fails color_convert motion stepper stepper_invert_idle_off \
    stepper_no_contact stepper_no_en stepper_fine; do
    "$OUT/$t" || status=1
done
[ $status -eq 0 ] && echo "All host tests passed" || echo "Host tests FAILED"
exit $status
