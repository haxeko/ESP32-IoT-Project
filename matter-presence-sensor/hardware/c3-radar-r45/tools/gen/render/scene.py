import math, pcbnew

PCB = "/home/claude/c3_radar_r45/c3_radar_r45.kicad_pcb"
R = 22.5
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
T = 1.6                   # board thickness (top face y=0, bottom face y=-T)
XZ = "matrix <1,0,0, 0,0,1, 0,1,0, 0,0,0>"   # map image/text XY plane onto the XZ plane (y_img -> +Z)

b = pcbnew.LoadBoard(PCB)
uv = lambda p: (pcbnew.ToMM(p.x) - 100.0, 100.0 - pcbnew.ToMM(p.y))
fps = {f.GetReference(): f for f in b.GetFootprints()}
pads = {r: {p.GetNumber(): uv(p.GetPosition()) for p in f.Pads()} for r, f in fps.items()}

out = []
P = out.append


def box(x0, y0, z0, x1, y1, z1, tex):
    return f"box{{<{x0:.4f},{y0:.4f},{z0:.4f}>,<{x1:.4f},{y1:.4f},{z1:.4f}> texture{{{tex}}}}}"


def rbox(cx, cy, cz, hx, hy, hz, tex, e=0.15):
    return (f"superellipsoid{{<{e},{e}> scale <{hx:.4f},{hy:.4f},{hz:.4f}> "
            f"translate <{cx:.4f},{cy:.4f},{cz:.4f}> texture{{{tex}}}}}")


def text(s, x, y, z, size, tex, center=True, rot=0):
    w = 0.62 * size * len(s)
    dx = -w / 2 if center else 0
    return (f"text{{ttf \"{FONT}\" \"{s}\" 0.04, 0 scale {size} translate <{dx:.3f},0,0> rotate z*{rot} {XZ} "
            f"translate <{x:.4f},{y:.4f},{z:.4f}> texture{{{tex}}}}}")


def chip(ref, L, W, H, body, term, tl):
    p1, p2 = pads[ref]["1"], pads[ref]["2"]
    cx, cz = (p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2
    ang = math.degrees(math.atan2(p2[1] - p1[1], p2[0] - p1[0]))
    y0 = 0.0 if not fps[ref].IsFlipped() else -T - H
    parts = [box(-L / 2 + tl, 0, -W / 2, L / 2 - tl, H, W / 2, body),
             box(-L / 2, 0, -W / 2 - 0.01, -L / 2 + tl, H + 0.01, W / 2 + 0.01, term),
             box(L / 2 - tl, 0, -W / 2 - 0.01, L / 2, H + 0.01, W / 2 + 0.01, term)]
    return f"union{{ {' '.join(parts)} rotate y*{-ang:.3f} translate <{cx:.4f},{y0:.4f},{cz:.4f}> }}"


def axis_frame(ref, a_pads, b_pads):
    A = [pads[ref][n] for n in a_pads]; B = [pads[ref][n] for n in b_pads]
    ax = (sum(p[0] for p in A) / len(A), sum(p[1] for p in A) / len(A))
    bx = (sum(p[0] for p in B) / len(B), sum(p[1] for p in B) / len(B))
    c = uv(fps[ref].GetPosition())
    return c, math.degrees(math.atan2(bx[1] - ax[1], bx[0] - ax[0]))


def local(ref, n, c, ang):
    p = pads[ref][n]; a = math.radians(ang)
    dx, dz = p[0] - c[0], p[1] - c[1]
    return dx * math.cos(a) + dz * math.sin(a), -dx * math.sin(a) + dz * math.cos(a)


# ------------------------------------------------------------------ materials
P("""#version 3.7;
global_settings { assumed_gamma 1.0 max_trace_level 8 }
#declare Tsilver = texture{ pigment{ rgb <0.80,0.80,0.82> } finish{ ambient 0.04 diffuse 0.55 metallic 0.7 specular 0.7 roughness 0.015 reflection 0.18 brilliance 2 } }
#declare Tcan    = texture{ pigment{ rgb <0.74,0.75,0.77> } finish{ ambient 0.05 diffuse 0.65 metallic 0.5 specular 0.45 roughness 0.04 reflection 0.08 } normal{ granite 0.03 scale 0.4 } }
#declare Tgold   = texture{ pigment{ rgb <0.88,0.68,0.26> } finish{ ambient 0.05 diffuse 0.6 metallic 0.8 specular 0.7 roughness 0.02 reflection 0.15 } }
#declare Tblack  = texture{ pigment{ rgb <0.045,0.045,0.05> } finish{ ambient 0.03 diffuse 0.65 specular 0.35 roughness 0.03 } }
#declare Tdark   = texture{ pigment{ rgb <0.02,0.02,0.02> } finish{ ambient 0 diffuse 0.3 } }
#declare Tcer    = texture{ pigment{ rgb <0.70,0.56,0.36> } finish{ ambient 0.05 diffuse 0.75 specular 0.15 roughness 0.06 } }
#declare Tptc    = texture{ pigment{ rgb <0.28,0.36,0.24> } finish{ ambient 0.05 diffuse 0.7 specular 0.2 roughness 0.05 } }
#declare Tmod    = texture{ pigment{ rgb <0.07,0.07,0.09> } finish{ ambient 0.04 diffuse 0.6 specular 0.5 roughness 0.02 } }
#declare Tradar  = texture{ pigment{ rgb <0.06,0.10,0.30> } finish{ ambient 0.04 diffuse 0.65 specular 0.55 roughness 0.02 } }
#declare Twhite  = texture{ pigment{ rgb <0.93,0.93,0.90> } finish{ ambient 0.08 diffuse 0.8 } }
#declare Tengr   = texture{ pigment{ rgb <0.42,0.43,0.45> } finish{ ambient 0.04 diffuse 0.7 specular 0.2 } }
#declare Tfoam   = texture{ pigment{ rgb <0.86,0.86,0.83> } finish{ ambient 0.06 diffuse 0.85 } }
#declare Tfr4    = texture{ pigment{ rgb <0.62,0.66,0.42> } finish{ ambient 0.05 diffuse 0.7 specular 0.1 } }
#declare Tsolder = texture{ pigment{ rgb <0.78,0.78,0.80> } finish{ ambient 0.04 diffuse 0.5 metallic 0.6 specular 0.8 roughness 0.01 reflection 0.2 } }
#ifndef (VIEW) #declare VIEW = 1; #end
""")

# ------------------------------------------------------------------ board
skin = "finish{ ambient 0.05 diffuse 0.72 specular 0.35 roughness 0.012 reflection 0.03 }"
P(f"cylinder{{<0,{-T},0>,<0,0,0>,{R} texture{{Tfr4}}}}")
P(f"cylinder{{<0,-0.002,0>,<0,0.014,0>,{R} texture{{ pigment{{ image_map{{ png \"top.png\" interpolate 2 }} {XZ} scale <{2*R},1,{2*R}> translate <{-R},0,{-R}> }} {skin} }}}}")
P(f"cylinder{{<0,{-T - 0.014},0>,<0,{-T + 0.002},0>,{R} texture{{ pigment{{ image_map{{ png \"bottom.png\" interpolate 2 }} {XZ} scale <{2*R},1,{2*R}> translate <{-R},0,{-R}> }} {skin} }}}}")

# ------------------------------------------------------------------ passives
sizes = {"R_0402_1005Metric": (1.0, 0.5, 0.35, 0.22), "R_0603_1608Metric": (1.6, 0.8, 0.45, 0.3),
         "C_0603_1608Metric": (1.6, 0.8, 0.8, 0.3), "C_0805_2012Metric": (2.0, 1.25, 0.95, 0.4),
         "Fuse_0805_2012Metric": (2.0, 1.25, 0.55, 0.4)}
for ref, f in fps.items():
    name = f.GetFPID().GetLibItemName().wx_str()
    if name in sizes:
        L, W, H, tl = sizes[name]
        body = "Tblack" if ref.startswith("R") else ("Tptc" if ref.startswith("F") else "Tcer")
        P(chip(ref, L, W, H, body, "Tsilver", tl))

# D1 SOD-123F with cathode band at pad 1
p1, p2 = pads["D1"]["1"], pads["D1"]["2"]
c = ((p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2); a = math.degrees(math.atan2(p2[1] - p1[1], p2[0] - p1[0]))
P(f"union{{ {rbox(0, 0.5, 0, 1.35, 0.5, 0.85, 'Tblack', 0.2)} {box(-1.05, 0.98, -0.8, -0.65, 1.01, 0.8, 'Twhite')} "
  f"{box(-1.9, 0, -0.55, -1.3, 0.12, 0.55, 'Tsilver')} {box(1.3, 0, -0.55, 1.9, 0.12, 0.55, 'Tsilver')} "
  f"scale <1,-1,1> translate <0,{-T},0> rotate y*{-a:.3f} translate <{c[0]:.4f},0,{c[1]:.4f}> }}")

# U2 SOT-23-5 (bottom)
c, a = axis_frame("U2", ["1", "2", "3"], ["4", "5"])
leads = []
for n in ("1", "2", "3", "4", "5"):
    lx, lz = local("U2", n, c, a)
    s = 1 if lx > 0 else -1
    leads.append(box(min(s * 0.8, s * 1.4), 0.0, lz - 0.2, max(s * 0.8, s * 1.4), 0.15, lz + 0.2, "Tsilver"))
lx1, lz1 = local("U2", "1", c, a)
P(f"union{{ {rbox(0, 0.55, 0, 0.8, 0.45, 1.45, 'Tblack', 0.15)} {' '.join(leads)} "
  f"cylinder{{<{0.45 * (1 if lx1 > 0 else -1)},0.99,{lz1 * 0.75:.3f}>,<{0.45 * (1 if lx1 > 0 else -1)},1.01,{lz1 * 0.75:.3f}>,0.15 texture{{Twhite}}}} "
  f"scale <1,-1,1> translate <0,{-T},0> rotate y*{-a:.3f} translate <{c[0]:.4f},0,{c[1]:.4f}> }}")

# SW1 Omron B3U-1000P (bottom)
c, a = axis_frame("SW1", ["1"], ["2"])
P(f"union{{ {rbox(0, 0.5, 0, 1.5, 0.5, 1.25, 'Tblack', 0.1)} {box(-1.5, 1.0, -1.25, 1.5, 1.12, 1.25, 'Tsilver')} "
  f"cylinder{{<0,1.12,0>,<0,1.55,0>,0.72 texture{{Tblack}}}} "
  f"{box(-2.1, 0, -0.5, -1.45, 0.15, 0.5, 'Tsilver')} {box(1.45, 0, -0.5, 2.1, 0.15, 0.5, 'Tsilver')} "
  f"scale <1,-1,1> translate <0,{-T},0> rotate y*{-a:.3f} translate <{c[0]:.4f},0,{c[1]:.4f}> }}")

# J1 USB-C (bottom side, mouth facing -v)
jc = uv(fps["J1"].GetPosition())
front = jc[1] - 3.4525
P(f"union{{ {rbox(jc[0], -T - 1.58, jc[1], 4.47, 1.58, 3.45, 'Tsilver', 0.25)}"
  f" {rbox(jc[0], -T - 1.58, front + 0.4, 4.12, 1.25, 0.45, 'Tdark', 0.35)}"
  f" {box(jc[0] - 3.3, -T - 1.88, front - 0.02, jc[0] + 3.3, -T - 1.28, front + 0.9, 'Tblack')} }}")

# U1 ESP32-C3-WROOM-02 (top), module v = 0.5 .. 20.5, antenna end at the round edge
mb = 0.5
m = [box(-9, 0.05, mb, 9, 0.85, mb + 20, "Tmod"), rbox(0, 0.85 + 1.175, mb + 7.0, 8.15, 1.175, 6.55, "Tcan", 0.06)]
for n, (pu, pv) in pads["U1"].items():
    if n.isdigit() and int(n) <= 18:
        sg = 1 if pu > 0 else -1
        m.append(box(sg * 8.55, 0.05, pv - 0.42, sg * 9.03, 0.86, pv + 0.42, "Tgold"))
xs = [-6.6 + 1.2 * k for k in range(12)]
for k, x in enumerate(xs):
    m.append(box(x - 0.18, 0.85, mb + 14.6, x + 0.18, 0.88, mb + 19.1, "Tgold"))
    if k < len(xs) - 1:
        zz = mb + 19.1 if k % 2 == 0 else mb + 14.6
        top = zz > mb + 15
        m.append(box(x - 0.18, 0.85, zz - 0.36 if top else zz, x + 1.38, 0.88, zz if top else zz + 0.36, "Tgold"))
m.append(text("ESP32-C3-WROOM-02", 0, 3.2, mb + 9.6, 1.25, "Tengr"))
m.append(text("ESP32-C3-N4  2.4GHz", 0, 3.2, mb + 7.4, 0.9, "Tengr"))
m.append(text("ESPRESSIF", 0, 3.2, mb + 4.6, 1.1, "Tengr"))
P("union{ " + " ".join(m) + " }")

# RD1 radar: header insulator on the PCB (y 0..2.5), radar PCB y 2.5..3.5, body below the pin row
yp = pads["RD1"]["3"][1]
r = [box(-11, 2.5, yp - 14.535, 11, 3.5, yp + 1.465, "Tradar"), box(-6.35, 0.0, yp - 1.25, 6.35, 2.5, yp + 1.25, "Tblack")]
for ux in [-5.08, -2.54, 0, 2.54, 5.08]:
    r.append(box(ux - 0.32, -T - 1.1, yp - 0.32, ux + 0.32, 3.8, yp + 0.32, "Tgold"))
    r.append(f"cone{{<{ux},3.5,{yp}>,0.75,<{ux},3.9,{yp}>,0.34 texture{{Tsolder}}}}")
for col in (-4.6, 4.6):
    r.append(box(col - 0.12, 3.5, yp - 13.3, col + 0.12, 3.53, yp - 3.6, "Tgold"))
    for k in range(4):
        z0 = yp - 12.9 + k * 2.55
        r.append(box(col - 1.5, 3.5, z0, col + 1.5, 3.535, z0 + 1.75, "Tgold"))
r.append(text("HLK-LD2410C", 0, 3.5, yp - 2.75, 1.15, "Twhite"))
for k, lab in enumerate(["TX", "RX", "OUT", "GND", "VCC"]):
    r.append(text(lab, -5.08 + 2.54 * k, 3.5, yp - 1.55, 0.62, "Twhite"))
P("union{ " + " ".join(r) + " }")
for ux in [-5.08, -2.54, 0, 2.54, 5.08]:
    P(f"cone{{<{ux},{-T},{yp}>,0.8,<{ux},{-T - 0.75},{yp}>,0.36 texture{{Tsolder}}}}")

# ------------------------------------------------------------------ lights, camera, background
P("""
sky_sphere{ pigment{ gradient y color_map{ [0 rgb <0.80,0.83,0.88>] [0.5 rgb <0.95,0.95,0.96>] [1 rgb <0.86,0.89,0.94>] } scale 2 translate -1 } }
#if (VIEW = 3)
  camera{ location <44,-66,-56> look_at <0,-2.4,-1.0> angle 38 right x*image_width/image_height }
  light_source{ <30,-60,-40> color rgb 1.0 area_light <16,0,0>,<0,0,16>,5,5 adaptive 1 jitter }
  light_source{ <-40,-20,30> color rgb 0.35 shadowless }
  light_source{ <0,40,0> color rgb 0.15 shadowless }
#else
  #if (VIEW = 2)
    camera{ location <0,150,-0.001> look_at <0,0,0> sky <0,0,1> angle 25 right x*image_width/image_height }
  #else
    camera{ location <-46,64,-56> look_at <0,0.8,-0.8> angle 38 right x*image_width/image_height }
  #end
  light_source{ <-30,60,-40> color rgb 1.0 area_light <16,0,0>,<0,0,16>,5,5 adaptive 1 jitter }
  light_source{ <40,25,30> color rgb 0.35 shadowless }
  light_source{ <0,-40,0> color rgb 0.12 shadowless }
  plane{ y, -12 texture{ pigment{ rgb <0.94,0.94,0.95> } finish{ ambient 0.15 diffuse 0.75 } } }
#end
""")
open("/home/claude/render45/scene.pov", "w").write("\n".join(out))
print("scene written:", len(out), "blocks; radar pin row v =", yp)
