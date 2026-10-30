#!/usr/bin/env python3
"""Fabrication outputs: Gerbers + drill (zip), BOM, pick-and-place, schematic PDF, images."""
import csv
import os
import shutil
import subprocess
import sys
import zipfile
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
HW = os.path.join(ROOT, "hardware")
FAB = os.path.join(ROOT, "fab")
IMG = os.path.join(ROOT, "docs", "images")
P = design.PROJECT
PCB = os.path.join(HW, P + ".kicad_pcb")
SCH = os.path.join(HW, P + ".kicad_sch")
THT = {"J1", "J2", "J3", "J4", "F1", "F2", "F3", "SW1", "SW2"}


def run(*args):
    subprocess.run(list(args), check=True, capture_output=True)


def gerbers():
    gdir = os.path.join(FAB, "gerbers")
    if os.path.isdir(gdir):
        shutil.rmtree(gdir)
    os.makedirs(gdir)
    run("kicad-cli", "pcb", "export", "gerbers", "--output", gdir + "/", "--subtract-soldermask",
        "--layers", "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts", PCB)
    run("kicad-cli", "pcb", "export", "drill", "--output", gdir + "/", "--format", "excellon",
        "--excellon-units", "mm", "--excellon-separate-th", "--generate-map", "--map-format", "gerberx2", PCB)
    zpath = os.path.join(FAB, P + "_gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)
    return zpath


def bom():
    groups = OrderedDict()
    for p in design.PARTS:
        if not p.get("in_bom", True) or p["dnp"]:
            continue
        key = (p["value"], p["footprint"], p["mpn"], p["lcsc"])
        groups.setdefault(key, []).append(p["ref"])
    path = os.path.join(FAB, P + "-BOM.csv")
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "Quantity", "MPN / note", "LCSC Part #", "Mount"])
        for (value, fp, mpn, lcsc), refs in groups.items():
            refs = sorted(refs, key=lambda r: (r.rstrip("0123456789"), int("0" + r[len(r.rstrip("0123456789")):])))
            mount = "THT" if set(refs) & THT else "SMD"
            w.writerow([value, ",".join(refs), fp.split(":", 1)[1], len(refs), mpn, lcsc, mount])
    return path


def cpl():
    tmp = os.path.join(FAB, "_pos.csv")
    run("kicad-cli", "pcb", "export", "pos", "--output", tmp, "--format", "csv", "--units", "mm",
        "--side", "both", "--exclude-dnp", "--use-drill-file-origin", PCB)
    path = os.path.join(FAB, P + "-pos.csv")
    with open(tmp, encoding="utf-8") as fi, open(path, "w", newline="", encoding="utf-8") as fo:
        w = csv.writer(fo)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in csv.DictReader(fi):
            w.writerow([row["Ref"], row["PosX"] + "mm", row["PosY"] + "mm",
                        "Top" if row["Side"] == "top" else "Bottom", row["Rot"]])
    os.remove(tmp)
    return path


def pdfs_and_images():
    os.makedirs(IMG, exist_ok=True)
    sch_pdf = os.path.join(FAB, P + "_schematic.pdf")
    run("kicad-cli", "sch", "export", "pdf", "--output", sch_pdf, SCH)
    run("pdftoppm", "-r", "110", "-png", "-singlefile", sch_pdf, os.path.join(IMG, "schematic"))
    renders = {
        "render_top": ["--side", "top"],
        "render_bottom": ["--side", "bottom"],
        "render_iso": ["--side", "top", "--perspective", "--rotate", "-38,0,-28", "--zoom", "0.95"],
    }
    for name, extra in renders.items():
        run("kicad-cli", "pcb", "render", "--quality", "high", "--width", "1800", "--height", "1500",
            "--background", "opaque", "--floor", *extra, "-o", os.path.join(IMG, name + ".png"), PCB)
    snap = os.path.join(HERE, "snap.py")
    run(sys.executable, snap, PCB, os.path.join(IMG, "copper_top.png"), "F.Cu,F.Silkscreen,Edge.Cuts", "14")
    run(sys.executable, snap, PCB, os.path.join(IMG, "copper_bottom.png"), "B.Cu,B.Silkscreen,Edge.Cuts", "14")
    return sch_pdf


def main():
    os.makedirs(FAB, exist_ok=True)
    for f in (gerbers(), bom(), cpl(), pdfs_and_images()):
        print("wrote", os.path.relpath(f, ROOT))


if __name__ == "__main__":
    main()
