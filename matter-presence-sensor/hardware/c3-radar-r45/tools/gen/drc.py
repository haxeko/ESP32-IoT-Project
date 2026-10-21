import pcbnew, sys, re, collections
PCB = "/home/claude/c3_radar_r45/c3_radar_r45.kicad_pcb"
b = pcbnew.LoadBoard(PCB)
if len(sys.argv)>1 and sys.argv[1]=="fill":
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
rep = "/tmp/drc.rpt"
pcbnew.WriteDRCReport(b, rep, pcbnew.EDA_UNITS_MILLIMETRES, True)
txt = open(rep).read()
items = re.findall(r'\[(\w+)\]: ([^\n]*)\n([^\[]*)', txt)
cnt = collections.Counter(i[0] for i in items)
print(cnt)
show = sys.argv[2:] if len(sys.argv)>2 else None
for code,msg,body in items:
    if code=="unconnected_items" and not show: continue
    if show and code not in show: continue
    print(code, "|", msg.strip(), "|", " ".join(body.split())[:230])
