#!/usr/bin/env bash
# Regenerate the whole board from tools/design.py + tools/layout.py (KiCad 9.0, Python 3, gcc).
#   schematic -> ERC -> netlist check -> PCB -> routing -> DRC -> fab files
set -euo pipefail
cd "$(dirname "$0")"
HW=../hardware
P=c3_ws2812_3ch
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

python3 build_sch.py
kicad-cli sch erc --severity-all --exit-code-violations -o "$TMP/erc.rpt" "$HW/$P.kicad_sch"
kicad-cli sch export netlist --format kicadsexpr -o "$TMP/sch.net" "$HW/$P.kicad_sch" >/dev/null
python3 check_netlist.py "$TMP/sch.net"

python3 build_pcb.py
python3 autoroute.py "$HW/$P.kicad_pcb" 8
kicad-cli pcb drc --schematic-parity --severity-all --exit-code-violations -o "$TMP/drc.rpt" "$HW/$P.kicad_pcb"

python3 export_fab.py
