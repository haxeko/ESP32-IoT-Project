# Manufacturing outputs into ../../fab (and layer pictures into ../../docs/images):
#   gerbers/ + h2_contact_gerbers.zip   Gerber X2 (Protel extensions) + Excellon drill + drill map + job file
#   h2_contact-BOM.csv                   grouped BOM with part notes (all parts, J2 included)
#   h2_contact-pos.csv                   KiCad placement file (mm, both sides)
#   h2_contact-BOM-JLCPCB.csv            JLCPCB columns: Comment, Designator, Footprint, LCSC Part # (empty: pick parts)
#   h2_contact-CPL-JLCPCB.csv            JLCPCB columns: Designator, Mid X, Mid Y, Layer, Rotation
#   h2_contact_schematic.pdf
# J2 (screw terminal, through-hole) is hand soldered: it is in the BOM but not in the JLCPCB BOM/CPL.
import csv
import glob
import os
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import FAB_DIR, HERE, IMG_DIR, PARTS, PROJ_DIR, PROJECT, WORK

PCB = f"{PROJ_DIR}/{PROJECT}.kicad_pcb"
SCH = f"{PROJ_DIR}/{PROJECT}.kicad_sch"
GERB = os.path.join(FAB_DIR, "gerbers")
HAND_SOLDERED = {"J2"}
LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"


def run(*args):
    subprocess.run(args, check=True, capture_output=True)


shutil.rmtree(GERB, ignore_errors=True)
os.makedirs(GERB)
os.makedirs(IMG_DIR, exist_ok=True)
run("kicad-cli", "pcb", "export", "gerbers", "--layers", LAYERS, "--subtract-soldermask", "-o", GERB + "/", PCB)
run("kicad-cli", "pcb", "export", "drill", "--format", "excellon", "--drill-origin", "absolute", "-u", "mm",
    "--generate-map", "--map-format", "gerberx2", "-o", GERB + "/", PCB)
zpath = os.path.join(FAB_DIR, f"{PROJECT}_gerbers.zip")
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(glob.glob(GERB + "/*")):
        z.write(f, os.path.basename(f))

pos = os.path.join(FAB_DIR, f"{PROJECT}-pos.csv")
run("kicad-cli", "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both", "-o", pos, PCB)
rows = list(csv.DictReader(open(pos)))

# grouped BOM (same value + footprint + note), from design.py
groups = {}
for p in PARTS:
    if p["ref"].startswith("TP"):
        continue
    key = (p["value"], p["fp"].split(":")[1], p["part"])
    groups.setdefault(key, []).append(p["ref"])
order = lambda r: (r.rstrip("0123456789"), int(r[len(r.rstrip("0123456789")):]))
with open(os.path.join(FAB_DIR, f"{PROJECT}-BOM.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["References", "Qty", "Value", "Footprint", "Side", "Part / note"])
    for (val, fp, note), refs in sorted(groups.items(), key=lambda kv: order(sorted(kv[1], key=order)[0])):
        refs = sorted(refs, key=order)
        w.writerow([" ".join(refs), len(refs), val, fp, "top", note + (" (hand soldered)" if set(refs) & HAND_SOLDERED and "hand" not in note else "")])
    w.writerow(["H1 H2", 2, "M2 mounting hole", "MountingHole_2.2mm_M2", "-", "NPTH, no part"])
    w.writerow(["TP1 TP2 TP3 TP4", 4, "test pad", "TestPoint_Pad_D1.5mm", "bottom", "bare copper pads 3V3 GND TX0 RX0, no part"])

placed = [r for r in rows if r["Ref"] not in HAND_SOLDERED]
with open(os.path.join(FAB_DIR, f"{PROJECT}-CPL-JLCPCB.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for r in sorted(placed, key=lambda r: order(r["Ref"])):
        w.writerow([r["Ref"], f'{float(r["PosX"]):.4f}mm', f'{float(r["PosY"]):.4f}mm', r["Side"].capitalize(),
                    f'{float(r["Rot"]):.1f}'])
refs_placed = {r["Ref"] for r in placed}
with open(os.path.join(FAB_DIR, f"{PROJECT}-BOM-JLCPCB.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
    jlc = {}   # one line per value + footprint
    for (val, fp, note), refs in groups.items():
        jlc.setdefault((val, fp), []).extend(r for r in refs if r in refs_placed)
    for (val, fp), refs in sorted(jlc.items(), key=lambda kv: order(sorted(kv[1], key=order)[0]) if kv[1] else ("~", 0)):
        if refs:
            w.writerow([val, ",".join(sorted(refs, key=order)), fp, ""])

run("kicad-cli", "sch", "export", "pdf", "-o", os.path.join(FAB_DIR, f"{PROJECT}_schematic.pdf"), SCH)

# layer pictures (needs node + Playwright Chromium; skipped without them)
os.makedirs(WORK, exist_ok=True)
pics = {"top": ("F.Cu,F.SilkS,F.Fab,Edge.Cuts", []), "copper_top": ("F.Cu,Edge.Cuts", []),
        "copper_bottom": ("B.Cu,B.SilkS,Edge.Cuts", ["--mirror"])}
for name, (layers, extra) in pics.items():
    svg = f"{WORK}/{name}.svg"
    run("kicad-cli", "pcb", "export", "svg", "--layers", layers, "--exclude-drawing-sheet", "--page-size-mode", "2",
        *extra, "-o", svg, PCB)
    try:
        run("node", os.path.join(HERE, "svg2png.js"), svg, os.path.join(IMG_DIR, f"{name}.png"), "1")
    except (OSError, subprocess.CalledProcessError) as e:
        print("no picture for", name, e)

print("fab outputs:", sorted(os.listdir(FAB_DIR)))
print("gerbers:", sorted(os.listdir(GERB)))
