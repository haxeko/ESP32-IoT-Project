#!/bin/sh
# Host tests: the radar protocol layer (components/radar_proto), the probe's
# channel task against simulated radars (radar_probe, with the ESP-IDF stubs of
# stubs/) and the Python tools. No ESP32 or ESP-IDF needed, only a C++ compiler
# (g++, or clang++ via CXX) and Python 3 (numpy for the report tests).
# Run: test/host/run.sh
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
PROTO=../../components/radar_proto
PROBE=../../radar_probe/main
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FLAGS="-std=gnu++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I $PROTO/include"
PROTO_SRC="$PROTO/hlk_frame.cpp $PROTO/ld2410_proto.cpp $PROTO/ld2450_proto.cpp"

$CXX $FLAGS -o "$OUT/radar_proto" test_radar_proto.cpp $PROTO_SRC
$CXX $FLAGS -Wno-unused-parameter -Wno-missing-field-initializers -I stubs -I . -I $PROBE -o "$OUT/probe" \
    test_probe.cpp sim_probe.cpp $PROBE/probe_out.cpp $PROTO_SRC

status=0
"$OUT/radar_proto" || status=1
PROBE_TEST_RLOG="$OUT/probe.rlog" "$OUT/probe" || status=1
# The probe's records, as decode.py reads them: same format on both sides
python3 ../../tools/decode.py "$OUT/probe.rlog" -o "$OUT/probe_csv" > /dev/null || status=1
python3 - "$OUT/probe_csv/summary.json" <<'PY' || status=1
import json, sys
s = json.load(open(sys.argv[1]))
ok = (s["checksum_errors"] == 0 and s["bad_records"] == 0 and s["decode_errors"] == 0 and s["ld2410_reports"] > 50
      and s["replies"]["1"]["read_version"] == "firmware V2.44.24073110 (type 1)")
print("decode of the probe records:", "OK" if ok else "FAILED " + json.dumps(s)[:500])
sys.exit(0 if ok else 1)
PY
python3 -m unittest discover -s ../../tools/tests -t ../../tools -q || status=1
[ $status -eq 0 ] && echo "All host tests passed" || echo "Host tests FAILED"
exit $status
