#!/bin/sh
# Host tests of the LD2410 parser, the presence decisions and the Wi-Fi setup
# portal helpers: no ESP32 or ESP-IDF needed, only a C compiler (gcc, or clang
# via CC). Run: test/host/run.sh
set -e
cd "$(dirname "$0")"
CC=${CC:-gcc}
MAIN=../../main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FLAGS="-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I . -I $MAIN -I $MAIN/drivers"

$CC $FLAGS -o "$OUT/ld2410_parser" test_ld2410_parser.c "$MAIN/drivers/ld2410_parser.c"
$CC $FLAGS -o "$OUT/presence" test_presence.c "$MAIN/presence.c"
$CC $FLAGS -o "$OUT/captive_dns" test_captive_dns.c "$MAIN/captive_dns.c"
$CC $FLAGS -o "$OUT/portal_form" test_portal_form.c "$MAIN/portal_form.c"

status=0
for t in ld2410_parser presence captive_dns portal_form; do
    "$OUT/$t" || status=1
done
[ $status -eq 0 ] && echo "All host tests passed" || echo "Host tests FAILED"
exit $status
