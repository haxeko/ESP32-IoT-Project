#!/bin/sh
# Host tests of the fan state machine and the IR decoder: no ESP32 or ESP-IDF needed, only a C compiler
# (gcc, or clang via CC). Run: test/host/run.sh
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
MAIN=../../main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FLAGS="-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I . -I $MAIN"

$CC $FLAGS -o "$OUT/fan_logic" test_fan_logic.c "$MAIN/fan_logic.c" "$MAIN/fan_record.c"
$CC $FLAGS -o "$OUT/ir" test_ir.c "$MAIN/ir_decode.c" "$MAIN/ir_table.c" "$MAIN/fan_logic.c"

status=0
for t in fan_logic ir; do
    "$OUT/$t" || status=1
done
[ $status -eq 0 ] && echo "All host tests passed" || echo "Host tests FAILED"
exit $status
