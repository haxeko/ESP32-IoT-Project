#!/bin/bash
# Flash one board's factory partition (run on the host, esptool from ESP-IDF v6.0.2 / esptool v5).
#   flash_factory.sh PORT PARTITION_BIN [OFFSET]
# OFFSET is the "fctry" offset of the project's partitions.csv: 0x3E0000 (4 MB boards, default),
# 0x1F0000 for matter-thread-plug-wroom-2m (2 MB). Flash it before the manufacturing firmware.
set -euo pipefail
PORT=$1 BIN=$2 OFFSET=${3:-0x3E0000}
[ "$(wc -c < "$BIN" | tr -d ' ')" = 24576 ] || { echo "$BIN is not a 0x6000-byte factory partition"; exit 1; }
esptool --chip esp32h2 -p "$PORT" write-flash "$OFFSET" "$BIN"
# Read back and compare
READBACK=$(mktemp)
esptool --chip esp32h2 -p "$PORT" read-flash "$OFFSET" 0x6000 "$READBACK"
cmp "$BIN" "$READBACK" && echo "fctry OK at $OFFSET"
rm -f "$READBACK"
