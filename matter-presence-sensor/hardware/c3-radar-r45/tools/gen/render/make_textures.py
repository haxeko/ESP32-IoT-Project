import subprocess, cairosvg, numpy as np, pcbnew, io
from PIL import Image, ImageDraw
PCB = "/home/claude/c3_radar_r45/c3_radar_r45.kicad_pcb"
K = 60.0  # px per mm
RB = 22.5
def layer(name):
    svg = f"/tmp/tex/{name}.svg"
    subprocess.run(["kicad-cli", "pcb", "export", "svg", "-o", svg, "--layers", name, "--black-and-white",
                    "--page-size-mode", "2", "--exclude-drawing-sheet", "--drill-shape-opt", "0", PCB], check=True, capture_output=True)
    head = open(svg).read(600)
    import re
    w = float(re.search(r'width="([\d.]+)mm"', head).group(1)); h = float(re.search(r'height="([\d.]+)mm"', head).group(1))
    png = cairosvg.svg2png(url=svg, output_width=int(round(w * K)), output_height=int(round(h * K)), background_color="white")
    a = np.asarray(Image.open(io.BytesIO(png)).convert("L"))
    return a < 128, (w, h)
edge, size = layer("Edge.Cuts")
ys, xs = np.nonzero(edge)
cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
r_px = ((xs.max() - xs.min()) + (ys.max() - ys.min())) / 4
print("svg size mm", size, "circle center px", cx, cy, "radius px", r_px, "-> px/mm", r_px / RB)
N = int(2 * RB * K)
def crop(m):  # crop to the 30x30 mm square around the circle, return NxN
    x0, y0 = int(round(cx - RB * K)), int(round(cy - RB * K))
    out = np.zeros((N, N), bool)
    sub = m[max(0, y0):y0 + N, max(0, x0):x0 + N]
    out[max(0, -y0):max(0, -y0) + sub.shape[0], max(0, -x0):max(0, -x0) + sub.shape[1]] = sub
    return out
yy, xx = np.mgrid[0:N, 0:N]
disc = ((xx + 0.5 - N / 2) ** 2 + (yy + 0.5 - N / 2) ** 2) <= (RB * K) ** 2
b = pcbnew.LoadBoard(PCB)
holes = []
for t in b.GetTracks():
    if t.GetClass() == "PCB_VIA":
        p = t.GetPosition(); holes.append((pcbnew.ToMM(p.x) - 100, 100 - pcbnew.ToMM(p.y), pcbnew.ToMM(t.GetDrillValue()) / 2))
for pd in b.GetPads():
    if pd.GetDrillSize().x > 0:
        p = pd.GetPosition(); holes.append((pcbnew.ToMM(p.x) - 100, 100 - pcbnew.ToMM(p.y), pcbnew.ToMM(pd.GetDrillSize().x) / 2))
MASK = np.array([22, 84, 48]); MASK_CU = np.array([46, 128, 72]); GOLD = np.array([222, 180, 72])
SILK = np.array([244, 244, 240]); FR4 = np.array([196, 186, 140])
for side, pre in (("top", "F"), ("bottom", "B")):
    cu, opening, silk = crop(layer(f"{pre}.Cu")[0]), crop(layer(f"{pre}.Mask")[0]), crop(layer(f"{pre}.SilkS")[0])
    img = np.zeros((N, N, 4), np.uint8)
    rgb = np.where(cu[..., None], MASK_CU, MASK)
    rgb = np.where((opening & cu)[..., None], GOLD, rgb)
    rgb = np.where((opening & ~cu)[..., None], FR4, rgb)
    rgb = np.where((silk & ~opening)[..., None], SILK, rgb)
    img[..., :3] = rgb; img[..., 3] = np.where(disc, 255, 0)
    im = Image.fromarray(img, "RGBA"); d = ImageDraw.Draw(im)
    for (u, v, r) in holes:
        px, py = N / 2 + u * K, N / 2 - v * K
        R = r * K
        d.ellipse([px - R, py - R, px + R, py + R], fill=(28, 26, 22, 255))
    im.save(f"/home/claude/render45/{side}.png")
    print(side, "cu%", round(cu[disc].mean() * 100, 1), "opening%", round(opening[disc].mean() * 100, 1), "silk%", round(silk[disc].mean() * 100, 1))
print("holes", len(holes))
