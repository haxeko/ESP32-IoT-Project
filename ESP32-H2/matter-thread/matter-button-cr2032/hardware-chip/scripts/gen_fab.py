#!/usr/bin/env python3
"""Check (ERC, DRC with schematic parity) and export the manufacturing files with kicad-cli.

  fab/matter-button-cr2032-chip-gerber.zip
                                        Gerber of the 4 copper layers (Protel names: .gtl, .g1 = L2, .g2 = L3, .gbl;
                                        no X2/netlist attributes, as the JLCPCB KiCad guide) + Excellon drill (mm)
                                        + drill map
  fab/bom.csv                           grouped bill of materials (MPN, LCSC), not fitted parts marked DNP
  fab/bom-jlcpcb.csv                    JLCPCB assembly BOM: Comment, Designator, Footprint, LCSC Part #, MPN
  fab/cpl-jlcpcb.csv                    JLCPCB pick and place: Designator, Mid X, Mid Y, Layer, Rotation
                                        (both without the hand soldered part, no LCSC part number: BT1)
  docs/schematic.pdf                    KiCad schematic
  docs/assembly-top.svg, -bottom.svg    assembly drawings (fab layers: outlines, designators, values)
  docs/pcb-top.png, docs/pcb-bottom.png 3D renders (no component models: pads, mask, silk)
  docs/pcb-layout.png                   copper + silkscreen of both sides (optional, cairosvg + Pillow)

Fails when ERC or DRC reports anything (errors and warnings).
"""

import csv
import glob
import os
import re
import shutil
import subprocess
import sys
import zipfile

import design
from kicad_sexp import HW

PCB = os.path.join(HW, design.PROJECT + ".kicad_pcb")
SCH = os.path.join(HW, design.PROJECT + ".kicad_sch")
FAB = os.path.join(HW, "fab")
DOCS = os.path.join(HW, "docs")
BUILD = os.path.join(HW, "build")


def cli(*args):
    r = subprocess.run(["kicad-cli", *args], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"kicad-cli {' '.join(args)} failed:\n{r.stdout}\n{r.stderr}")
    return r.stdout


def check():
    os.makedirs(BUILD, exist_ok=True)
    erc = os.path.join(BUILD, "erc.rpt")
    cli("sch", "erc", "--severity-all", "-o", erc, SCH)
    n = re.search(r"ERC messages: (\d+)", open(erc).read())
    drc = os.path.join(BUILD, "drc.rpt")
    cli("pcb", "drc", "--severity-all", "--schematic-parity", "-o", drc, PCB)
    text = open(drc).read()
    counts = [int(x) for x in re.findall(r"Found (\d+) ", text)]
    print(f"ERC: {n.group(1)} messages; DRC: {counts} (violations, unconnected, footprint, parity)")
    if int(n.group(1)) or any(counts):
        raise SystemExit("ERC/DRC not clean, see build/erc.rpt and build/drc.rpt")


def gerbers():
    out = os.path.join(BUILD, "gerber")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    layers = "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"
    cli("pcb", "export", "gerbers", "--layers", layers, "--subtract-soldermask", "--no-x2", "--no-netlist",
        "-o", out + "/", PCB)
    cli("pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm", "--generate-map",
        "--map-format", "gerberx2", "-o", out + "/", PCB)
    os.makedirs(FAB, exist_ok=True)
    zpath = os.path.join(FAB, design.PROJECT + "-gerber.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(glob.glob(os.path.join(out, "*"))):
            info = zipfile.ZipInfo(os.path.basename(f), date_time=(2026, 1, 1, 0, 0, 0))  # reproducible zip
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(f, "rb") as fh:
                z.writestr(info, fh.read())
    print("gerber:", zpath, len(glob.glob(os.path.join(out, "*"))), "files")


def bom():
    path = os.path.join(FAB, "bom.csv")
    cli("sch", "export", "bom", "--fields", "${QUANTITY},Reference,Value,Footprint,MPN,LCSC,Description,${DNP}",
        "--labels", "Qty,Designator,Value,Footprint,MPN,LCSC,Description,DNP",
        "--group-by", "Value,Footprint,MPN,${DNP}", "--ref-range-delimiter", "", "-o", path, SCH)
    print("bom:", path)
    bom_jlcpcb()


# Package names for the JLCPCB BOM (their parts matching reads Comment + Footprint)
PACKAGE = [("R_0402", "0402"), ("C_0402", "0402"), ("L_0402", "0402"), ("C_0603", "0603"), ("C_1210", "1210"),
           ("L_1008", "1008"), ("SOT-23", "SOT-23"), ("WSON-10", "WSON-10 2.5x2.5mm"), ("QFN-32", "QFN-32 4x4mm"),
           ("Crystal_SMD_3225", "SMD3225-4P"), ("Crystal_SMD_3215", "SMD3215-2P"),
           ("XL-2121", "SMD 2.1x2.1mm RGB LED"), ("PTS810", "SMD 4.2x3.2mm tact switch"),
           ("Keystone_3034", "CR2032 holder SMD")]


# Fitted parts LCSC does not stock (no LCSC part number): left out of the JLCPCB BOM and CPL, soldered by hand
HAND_SOLDERED = [p["ref"] for p in design.PARTS if p["bom"] and not p.get("dnp") and not p["lcsc"]]


def bom_jlcpcb():
    """Parts JLCPCB assembles: no DNP, no test pads, no solder jumpers, no HAND_SOLDERED part."""
    raw = os.path.join(BUILD, "bom-raw.csv")
    cli("sch", "export", "bom", "--fields", "Value,Reference,Footprint,LCSC,MPN", "--labels",
        "Comment,Designator,Footprint,LCSC,MPN", "--group-by", "Value,Footprint,LCSC", "--ref-range-delimiter", "",
        "--exclude-dnp", "-o", raw, SCH)
    path = os.path.join(FAB, "bom-jlcpcb.csv")
    with open(raw) as f, open(path, "w", newline="") as g:
        w = csv.writer(g)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Manufacturer Part Number"])
        for row in csv.DictReader(f):
            if not row["LCSC"]:
                continue
            fp = next(name for key, name in PACKAGE if key in row["Footprint"])
            w.writerow([row["Comment"], row["Designator"], fp, row["LCSC"], row["MPN"]])
    print("bom jlcpcb:", path, "(hand soldered:", ", ".join(HAND_SOLDERED) + ")")


def cpl():
    raw = os.path.join(BUILD, "pos.csv")
    cli("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both", "--exclude-dnp", "-o", raw,
        PCB)
    path = os.path.join(FAB, "cpl-jlcpcb.csv")
    with open(raw) as f, open(path, "w", newline="") as g:
        w = csv.writer(g)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in csv.DictReader(f):
            if row["Ref"] in HAND_SOLDERED:
                continue
            w.writerow([row["Ref"], f"{float(row['PosX']):.4f}mm", f"{float(row['PosY']):.4f}mm",
                        row["Side"].capitalize(), f"{float(row['Rot']) % 360:g}"])
    print("cpl:", path)


def documents():
    os.makedirs(DOCS, exist_ok=True)
    cli("sch", "export", "pdf", "-o", os.path.join(DOCS, "schematic.pdf"), SCH)
    for side, layers, mirror in (("top", "F.Fab,Edge.Cuts", []), ("bottom", "B.Fab,Edge.Cuts", ["--mirror"])):
        cli("pcb", "export", "svg", "--mode-single", "--layers", layers, *mirror, "--page-size-mode", "2",
            "--exclude-drawing-sheet", "--sketch-pads-on-fab-layers", "--crossout-DNP-footprints-on-fab-layers",
            "--black-and-white", "-o", os.path.join(DOCS, f"assembly-{side}.svg"), PCB)
    for side in ("top", "bottom"):
        cli("pcb", "render", "--side", side, "--width", "1500", "--height", "1200", "--quality", "high",
            "--background", "opaque", "--zoom", "0.9", "-o", os.path.join(DOCS, f"pcb-{side}.png"), PCB)
    layout_png()
    print("docs:", DOCS)


def layout_png():
    """docs/pcb-layout.png: copper and silkscreen of both sides (bottom mirrored, as seen from below).
    Optional: needs cairosvg and Pillow (pip install cairosvg pillow)."""
    try:
        import cairosvg
        from PIL import Image
    except ImportError:
        print("pcb-layout.png skipped (pip install cairosvg pillow)")
        return
    pngs = []
    for side, layers, mirror in (("top", "F.Cu,F.SilkS,Edge.Cuts", []),
                                 ("bottom", "B.Cu,B.SilkS,Edge.Cuts", ["--mirror"])):
        svg = os.path.join(BUILD, f"layout-{side}.svg")
        cli("pcb", "export", "svg", "--mode-single", "--layers", layers, *mirror, "--page-size-mode", "2",
            "--exclude-drawing-sheet", "-o", svg, PCB)
        png = os.path.join(BUILD, f"layout-{side}.png")
        cairosvg.svg2png(url=svg, write_to=png, output_width=900, background_color="white")
        pngs.append(Image.open(png))
    a, b = pngs
    im = Image.new("RGB", (a.width + b.width + 40, max(a.height, b.height)), "white")
    im.paste(a, (0, 0))
    im.paste(b, (a.width + 40, 0))
    im.save(os.path.join(DOCS, "pcb-layout.png"), optimize=True)


if __name__ == "__main__":
    check()
    gerbers()
    bom()
    cpl()
    documents()
    sys.stdout.flush()
