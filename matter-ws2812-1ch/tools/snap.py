#!/usr/bin/env python3
"""Render selected PCB layers to a cropped PNG (for quick visual checks).

usage: snap.py <board.kicad_pcb> <out.png> <layers> [px_per_mm]
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(__file__))
import layout  # noqa: E402

from PIL import Image  # noqa: E402


def main():
    board, out, layers = sys.argv[1:4]
    ppm = float(sys.argv[4]) if len(sys.argv) > 4 else 20.0
    with tempfile.TemporaryDirectory() as td:
        pdf = os.path.join(td, "b.pdf")
        subprocess.run(["kicad-cli", "pcb", "export", "pdf", "--mode-single", "-l", layers,
                        "--drill-shape-opt", "2", "-o", pdf, board], check=True, capture_output=True)
        dpi = ppm * 25.4
        subprocess.run(["pdftoppm", "-r", str(dpi), "-png", "-singlefile", pdf, os.path.join(td, "p")], check=True)
        im = Image.open(os.path.join(td, "p.png"))
        m = 2.0
        x0, y0 = layout.ORIGIN
        box = [int((x0 - m) * ppm), int((y0 - m) * ppm), int((x0 + layout.W + m) * ppm), int((y0 + layout.H + m) * ppm)]
        im.crop(box).save(out)
    print(out)


if __name__ == "__main__":
    main()
