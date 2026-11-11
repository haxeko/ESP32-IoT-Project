#!/bin/sh
# Rebuilds the board from design.py: placement, Freerouting, stitching vias, DRC. KiCad 7 python (pcbnew) and
# Freerouting 2.1.0 (FREEROUTING=path/to/freerouting-2.1.0.jar). The schematic is build_sch.py.
set -e
cd "$(dirname "$0")"
FR=${FREEROUTING:-/tmp/claude-0/fr/freerouting-2.1.0.jar}
WORK=${H2C_WORK:-/tmp/h2c_route}
python3 make_symlib.py
python3 build_pcb.py
python3 preroute.py
python3 export_dsn.py
rm -f "$WORK/board.ses"
timeout 900 java -Djava.awt.headless=true -jar "$FR" -de "$WORK/board.dsn" -do "$WORK/board.ses" -mp 30 \
    --gui.enabled=false > "$WORK/fr.log" 2>&1
grep -o '"incomplete_count": [0-9]*' "$WORK/fr.log" | tail -1
python3 import_ses.py
python3 stitch.py
python3 drc.py fill
