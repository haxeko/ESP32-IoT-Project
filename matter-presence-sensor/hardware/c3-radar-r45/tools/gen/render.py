import subprocess, sys, cairosvg, os
PCB = "/home/claude/c3_radar_r45/c3_radar_r45.kicad_pcb"
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/render"
os.makedirs(out, exist_ok=True)
views = {
 "front": ("Edge.Cuts,F.Cu,F.SilkS,F.Fab,F.CrtYd,F.Mask", False),
 "back":  ("Edge.Cuts,B.Cu,B.SilkS,B.Fab,B.CrtYd,B.Mask", True),
}
for name,(layers,mirror) in views.items():
    svg = f"{out}/{name}.svg"
    cmd = ["kicad-cli","pcb","export","svg","-o",svg,"--layers",layers,"--page-size-mode","2","--exclude-drawing-sheet"]
    if mirror: cmd.append("--mirror")
    cmd.append(PCB)
    subprocess.run(cmd, check=True, capture_output=True)
    cairosvg.svg2png(url=svg, write_to=f"{out}/{name}.png", output_width=1100, background_color="white")
    print("wrote", f"{out}/{name}.png")
