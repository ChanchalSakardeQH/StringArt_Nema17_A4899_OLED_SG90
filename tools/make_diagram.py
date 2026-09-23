#!/usr/bin/env python3
"""
Builds the String Art CNC wiring diagram as an SVG.

Generated rather than hand-drawn so every pin row, wire and junction lands on
the same grid -- a schematic that is a pixel out reads as sloppy, and by hand
that is exactly what happens after the third revision.

Pin assignments are taken from StringArt_Nema17_GUI.ino; if they change there,
change PINS below and re-run.
"""

import pathlib
import sys

# Two board variants. The GPIOs are identical -- every pin this firmware uses
# is brought out on both -- so only the labelling and the OLED's status differ.
BOARD = sys.argv[1] if len(sys.argv) > 1 else "38"
IS30 = (BOARD == "30")

BOARD_NAME = "ESP32 DevKit V1" if IS30 else "ESP32 WROOM-32"
BOARD_SUB  = "30-pin dev board" if IS30 else "38-pin dev board"
OUT_NAME   = "wiring_diagram_30pin" if IS30 else "wiring_diagram"

W, H = 1680, 1296

C = {
    "bg": "#ffffff",
    "ink": "#1c1a17",
    "dim": "#6b6459",
    "line": "#c9c2b6",
    "board": "#f6f3ee",
    "boardEdge": "#8d8578",
    "esp": "#eef2f7",
    "espEdge": "#4a6fa5",
    "v12": "#c0392b",
    "v5": "#e08a1e",
    "v33": "#8e44ad",
    "gnd": "#2b2b2b",
    "sig": "#1f6fb2",
    "coil": "#2e8b57",
    "warn": "#b8541c",
    "note": "#fbf8f2",
}

# GPIO -> (label, what it goes to)
PINS = {
    "step": 12, "dir": 14, "feeder": 18, "drill_servo": 19,
    "drill_motor": 25, "enable": 26, "limit": 27, "sda": 21, "scl": 22,
}

out = []
def add(s): out.append(s)

def esc(t):
    return (t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))

def text(x, y, t, size=13, fill=None, anchor="start", weight="normal",
         family="mono", style=""):
    f = ("'IBM Plex Mono','SF Mono',Menlo,Consolas,monospace" if family == "mono"
         else "'Inter','Helvetica Neue',Arial,sans-serif")
    add(f'<text x="{x}" y="{y}" font-family="{f}" font-size="{size}" '
        f'fill="{fill or C["ink"]}" text-anchor="{anchor}" '
        f'font-weight="{weight}" {style}>{esc(t)}</text>')

def box(x, y, w, h, fill, edge, r=8, sw=1.6, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" '
        f'fill="{fill}" stroke="{edge}" stroke-width="{sw}"{d}/>')

def wire(pts, color, width=2.2, dash=None):
    d = "M " + " L ".join(f"{px} {py}" for px, py in pts)
    da = f' stroke-dasharray="{dash}"' if dash else ""
    add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" '
        f'stroke-linecap="round" stroke-linejoin="round"{da}/>')

def dot(x, y, color, r=4):
    add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')

def pad(x, y, color=None):
    add(f'<rect x="{x-4}" y="{y-4}" width="8" height="8" rx="1.5" '
        f'fill="{color or C["ink"]}"/>')

# ---------------------------------------------------------------- canvas
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
    f'viewBox="0 0 {W} {H}" font-kerning="normal">')
add(f'<rect width="{W}" height="{H}" fill="{C["bg"]}"/>')

# ---------------------------------------------------------------- title
text(48, 56, "String Art CNC — Wiring Diagram", 30, C["ink"],
     weight="700", family="sans")
text(48, 82,
     ("ESP32 DevKit V1 (30-pin)  ·  A4988 + NEMA17  ·  2 × SG90  ·  drill motor  ·  limit switch  ·  0.96\" SSD1306 dashboard"
      if IS30 else
      "ESP32 WROOM-32 (38-pin)  ·  A4988 + NEMA17  ·  2 × SG90  ·  drill motor  ·  limit switch  ·  optional SSD1306"),
     14, C["dim"], family="sans")
add(f'<line x1="48" y1="100" x2="{W-48}" y2="100" stroke="{C["line"]}" stroke-width="1.5"/>')

# ---------------------------------------------------------------- ESP32
EX, EY, EW, EH = 620, 150, 300, 560
box(EX, EY, EW, EH, C["esp"], C["espEdge"], r=10, sw=2)
text(EX + EW/2, EY + 34, BOARD_NAME, 17, C["ink"], "middle", "700", "sans")
text(EX + EW/2, EY + 54, BOARD_SUB, 12, C["dim"], "middle", family="sans")
add(f'<line x1="{EX+20}" y1="{EY+70}" x2="{EX+EW-20}" y2="{EY+70}" '
    f'stroke="{C["line"]}" stroke-width="1.2"/>')

# left rail pins: name, y, colour
left_pins = [
    ("VIN  (5V in)", 120, C["v5"]),
    ("GND",          158, C["gnd"]),
    ("3V3",          196, C["v33"]),
    ("GPIO21  SDA",  262, C["sig"]),
    ("GPIO22  SCL",  300, C["sig"]),
    ("GPIO27  LIMIT",380, C["sig"]),
]
right_pins = [
    ("STEP   GPIO12", 120, C["sig"]),
    ("DIR    GPIO14", 158, C["sig"]),
    ("EN     GPIO26", 196, C["sig"]),
    ("FEEDER GPIO18", 272, C["sig"]),
    ("DRILL  GPIO19", 310, C["sig"]),
    ("MOTOR  GPIO25", 386, C["sig"]),
    ("GND",           470, C["gnd"]),
]

LP, RP = {}, {}
for name, dy, col in left_pins:
    y = EY + dy
    pad(EX, y, col)
    text(EX + 14, y + 4, name, 12.5, C["ink"])
    LP[name.split()[0] if "GPIO" not in name else name.split()[0]] = (EX, y)
    LP[name] = (EX, y)
for name, dy, col in right_pins:
    y = EY + dy
    pad(EX + EW, y, col)
    text(EX + EW - 14, y + 4, name, 12.5, C["ink"], "end")
    RP[name] = (EX + EW, y)

text(EX + EW/2, EY + EH - 16, "USB only powers the ESP32 — the motor needs its own 12 V",
     11, C["warn"], "middle", family="sans")

# ---------------------------------------------------------------- 12V PSU
box(60, 150, 230, 96, C["board"], C["boardEdge"])
text(175, 180, "12 V  DC SUPPLY", 15, C["ink"], "middle", "700", "sans")
text(175, 202, "2 A minimum", 12, C["dim"], "middle", family="sans")
pad(290, 196, C["v12"]); text(276, 200, "+12V", 11.5, C["v12"], "end")
pad(290, 224, C["gnd"]); text(276, 228, "GND", 11.5, C["gnd"], "end")

# ---------------------------------------------------------------- buck
box(60, 292, 230, 128, C["board"], C["boardEdge"])
text(175, 320, "BUCK  12 V → 5 V", 15, C["ink"], "middle", "700", "sans")
text(175, 339, "≥ 3 A  ·  servos + ESP32", 12, C["dim"], "middle", family="sans")
pad(60, 370, C["v12"]);  text(74, 374, "IN+", 11.5, C["v12"])
pad(60, 398, C["gnd"]);  text(74, 402, "IN−", 11.5, C["gnd"])
pad(290, 370, C["v5"]);  text(276, 374, "OUT+ 5V", 11.5, C["v5"], "end")
pad(290, 398, C["gnd"]); text(276, 402, "OUT−", 11.5, C["gnd"], "end")

# 12V -> buck in
wire([(290, 196), (330, 196), (330, 268), (40, 268), (40, 370), (60, 370)], C["v12"])
wire([(290, 224), (312, 224), (312, 282), (26, 282), (26, 398), (60, 398)], C["gnd"])

# ---------------------------------------------------------------- OLED
box(60, 452, 230, 134, C["board"], C["boardEdge"],
    sw=(2 if IS30 else 1.6), dash=(None if IS30 else "6 4"))
text(175, 480, ("0.96\" OLED DASHBOARD" if IS30 else "SSD1306  128×64"),
     (14 if IS30 else 15), C["ink"], "middle", "700", "sans")
text(175, 499, ("SSD1306 · 128×64 · I²C 0x3C" if IS30 else "optional · I²C 0x3C"),
     12, C["dim"], "middle", family="sans")
OLED = {}
for i, (lbl, col) in enumerate([("VCC 3V3", C["v33"]), ("GND", C["gnd"]),
                                ("SDA", C["sig"]), ("SCL", C["sig"])]):
    y = 520 + i*19
    pad(290, y, col)
    text(276, y + 4, lbl, 11.5, col, "end")
    OLED[lbl] = y

# ---------------------------------------------------------------- limit switch
box(60, 616, 230, 112, C["board"], C["boardEdge"])
text(175, 646, "LIMIT SWITCH", 15, C["ink"], "middle", "700", "sans")
text(175, 666, "mechanical · NO contact", 12, C["dim"], "middle", family="sans")
text(175, 690, "use  C  and  NO", 12, C["warn"], "middle", "700")
text(175, 710, "internal pull-up — no resistor", 11, C["dim"], "middle", family="sans")
pad(290, 676, C["sig"])
pad(290, 700, C["gnd"])

# ---------------------------------------------------------------- A4988
AX, AY, AW, AH = 1010, 150, 250, 306
box(AX, AY, AW, AH, C["board"], C["boardEdge"], sw=2)
text(AX + AW/2, AY + 30, "A4988", 17, C["ink"], "middle", "700", "sans")
text(AX + AW/2, AY + 50, "stepper driver + heatsink", 12, C["dim"], "middle", family="sans")
add(f'<line x1="{AX+18}" y1="{AY+64}" x2="{AX+AW-18}" y2="{AY+64}" '
    f'stroke="{C["line"]}" stroke-width="1.2"/>')

a_left = [("STEP", 90, C["sig"]), ("DIR", 118, C["sig"]), ("ENABLE", 146, C["sig"]),
          ("VDD 3V3", 186, C["v33"]), ("GND", 214, C["gnd"]),
          ("VMOT", 250, C["v12"])]
for lbl, dy, col in a_left:
    y = AY + dy
    pad(AX, y, col)
    text(AX + 13, y + 4, lbl, 12, C["ink"])
pad(AX, AY + 278, C["gnd"]); text(AX + 13, AY + 282, "GND", 12, C["ink"])

for i, lbl in enumerate(["1B", "1A", "2A", "2B"]):
    y = AY + 96 + i*30
    pad(AX + AW, y, C["coil"])
    text(AX + AW - 13, y + 4, lbl, 12, C["ink"], "end")

text(AX + AW/2, AY + AH - 12, "SLEEP — RESET linked", 11, C["dim"], "middle", family="sans")

# MS jumpers
box(AX, AY + AH + 14, AW, 78, C["note"], C["line"], r=6, sw=1.2)
text(AX + 12, AY + AH + 36, "MS1  MS2  MS3", 12, C["ink"], weight="700")
text(AX + 12, AY + AH + 54, "H     H     L   =  1/8  step", 11.5, C["ink"])
text(AX + 12, AY + AH + 72, "H     H     H   =  1/16 step", 11.5, C["dim"])

# ---------------------------------------------------------------- NEMA17
NX, NY = 1400, 206
box(NX, NY, 230, 186, C["board"], C["boardEdge"], sw=2)
text(NX + 115, NY + 32, "NEMA 17", 17, C["ink"], "middle", "700", "sans")
text(NX + 115, NY + 52, "bipolar · 4 wire", 12, C["dim"], "middle", family="sans")
for i, lbl in enumerate(["A coil", "A coil", "B coil", "B coil"]):
    y = NY + 72 + i*30
    pad(NX, y, C["coil"])
    text(NX + 14, y + 4, lbl, 11.5, C["dim"])
# coil wires
for i in range(4):
    ay = AY + 96 + i*30
    ny = NY + 72 + i*30
    mid = 1300 + i*14
    wire([(AX + AW, ay), (mid, ay), (mid, ny), (NX, ny)], C["coil"], 2)

# ---------------------------------------------------------------- servos
def servo(y, title, sub, gpio_key):
    box(1010, y, 250, 86, C["board"], C["boardEdge"])
    text(1135, y + 28, title, 15, C["ink"], "middle", "700", "sans")
    text(1135, y + 48, sub, 12, C["dim"], "middle", family="sans")
    pad(1010, y + 26, C["sig"]);  text(1024, y + 30, "SIG", 11.5, C["sig"])
    pad(1010, y + 48, C["v5"]);   text(1024, y + 52, "5V", 11.5, C["v5"])
    pad(1010, y + 70, C["gnd"]);  text(1024, y + 74, "GND", 11.5, C["gnd"])
    return (1010, y + 26), (1010, y + 48), (1010, y + 70)

fs_sig, fs_5v, fs_gnd = servo(560, "SG90  —  FEEDER", "thread tube", "feeder")
ds_sig, ds_5v, ds_gnd = servo(668, "SG90  —  DRILL LIFT", "raises / lowers", "drill_servo")

# ---------------------------------------------------------------- drill motor stage
box(1010, 776, 250, 128, C["board"], C["boardEdge"])
text(1135, 806, "MOSFET MODULE", 15, C["ink"], "middle", "700", "sans")
text(1135, 826, "logic-level · drill motor", 12, C["dim"], "middle", family="sans")
text(1135, 886, "flyback diode across motor", 11, C["warn"], "middle", family="sans")
pad(1010, 848, C["sig"]); text(1024, 852, "SIG", 11.5, C["sig"])
pad(1010, 868, C["gnd"]); text(1024, 872, "GND", 11.5, C["gnd"])
pad(1260, 840, C["v12"]); text(1246, 844, "V+", 11.5, C["v12"], "end")
pad(1260, 864, C["gnd"]); text(1246, 868, "OUT", 11.5, C["gnd"], "end")

box(1400, 796, 230, 96, C["board"], C["boardEdge"])
text(1515, 832, "DRILL MOTOR", 15, C["ink"], "middle", "700", "sans")
text(1515, 854, "12 V DC", 12, C["dim"], "middle", family="sans")
pad(1400, 840, C["v12"]); pad(1400, 864, C["gnd"])
wire([(1260, 840), (1400, 840)], C["v12"], 2)
wire([(1260, 864), (1400, 864)], C["gnd"], 2)

# ---------------------------------------------------------------- signal wires
# STEP / DIR / EN
for (pin_label, a_dy) in [("STEP   GPIO12", 90), ("DIR    GPIO14", 118),
                          ("EN     GPIO26", 146)]:
    x0, y0 = RP[pin_label]
    y1 = AY + a_dy
    midx = 950 + (a_dy - 90) // 2
    wire([(x0, y0), (midx, y0), (midx, y1), (AX, y1)], C["sig"])

# servos
sx, sy = RP["FEEDER GPIO18"]
wire([(sx, sy), (975, sy), (975, fs_sig[1]), (1010, fs_sig[1])], C["sig"])
sx, sy = RP["DRILL  GPIO19"]
wire([(sx, sy), (962, sy), (962, ds_sig[1]), (1010, ds_sig[1])], C["sig"])
sx, sy = RP["MOTOR  GPIO25"]
wire([(sx, sy), (949, sy), (949, 848), (1010, 848)], C["sig"])

# limit switch
lx, ly = LP["GPIO27  LIMIT"]
wire([(290, 676), (400, 676), (400, ly), (lx, ly)], C["sig"])

# OLED
ox, oy = LP["GPIO21  SDA"]
wire([(290, OLED["SDA"]), (430, OLED["SDA"]), (430, oy), (ox, oy)], C["sig"], 2, dash=(None if IS30 else "5 4"))
ox, oy = LP["GPIO22  SCL"]
wire([(290, OLED["SCL"]), (446, OLED["SCL"]), (446, oy), (ox, oy)], C["sig"], 2, dash=(None if IS30 else "5 4"))
ox, oy = LP["3V3"]
wire([(290, OLED["VCC 3V3"]), (462, OLED["VCC 3V3"]), (462, oy), (ox, oy)], C["v33"], 2, dash=(None if IS30 else "5 4"))

# 3V3 to A4988 VDD
x3, y3 = LP["3V3"]
wire([(x3, y3), (520, y3), (520, 132), (AX - 40, 132), (AX - 40, AY + 186), (AX, AY + 186)], C["v33"])

# ---------------------------------------------------------------- power rails
R12, R5, RG = 980, 1008, 1036
add(f'<line x1="140" y1="{R12}" x2="{W-140}" y2="{R12}" stroke="{C["v12"]}" stroke-width="5" stroke-linecap="round"/>')
add(f'<line x1="140" y1="{R5}"  x2="{W-140}" y2="{R5}"  stroke="{C["v5"]}"  stroke-width="5" stroke-linecap="round"/>')
add(f'<line x1="140" y1="{RG}"  x2="{W-140}" y2="{RG}"  stroke="{C["gnd"]}" stroke-width="5" stroke-linecap="round"/>')
text(130, R12 + 5, "+12 V", 13, C["v12"], "end", "700")
text(W - 130, R12 + 5, "+12 V", 13, C["v12"], weight="700")
text(130, R5 + 5, "+5 V", 13, C["v5"], "end", "700")
text(W - 130, R5 + 5, "+5 V", 13, C["v5"], weight="700")
text(130, RG + 5, "GND", 13, C["gnd"], "end", "700")
text(W - 130, RG + 5, "GND", 13, C["gnd"], weight="700")
text(140, RG + 30, "Every ground joins this one rail — supply, buck, ESP32, driver, servos, motor stage.",
     12, C["dim"], family="sans")

def drop(x, y_from, rail, color):
    wire([(x, y_from), (x, rail)], color, 2)
    dot(x, rail, color)

# supply into the rails
drop(330, 196, R12, C["v12"])
wire([(290, 196), (330, 196)], C["v12"])
drop(312, 224, RG, C["gnd"])
drop(360, 370, R5, C["v5"]); wire([(290, 370), (360, 370)], C["v5"])
drop(344, 398, RG, C["gnd"]); wire([(290, 398), (344, 398)], C["gnd"])

# ESP32 power
vx, vy = LP["VIN  (5V in)"]
wire([(vx, vy), (560, vy), (560, R5)], C["v5"]); dot(560, R5, C["v5"])
gx, gy = LP["GND"]
wire([(gx, gy), (540, gy), (540, RG)], C["gnd"]); dot(540, RG, C["gnd"])
gx, gy = RP["GND"]
wire([(gx, gy), (990, gy), (990, RG)], C["gnd"]); dot(990, RG, C["gnd"])

# A4988 power
wire([(AX, AY + 214), (988, AY + 214), (988, 916), (1120, 916), (1120, RG)], C["gnd"])
dot(1120, RG, C["gnd"])
wire([(AX, AY + 250), (976, AY + 250), (976, 930), (1160, 930), (1160, R12)], C["v12"])
dot(1160, R12, C["v12"])
wire([(AX, AY + 278), (966, AY + 278), (966, 944), (1200, 944), (1200, RG)], C["gnd"])
dot(1200, RG, C["gnd"])

# 100uF across VMOT
add(f'<line x1="1160" y1="{R12}" x2="1160" y2="{RG}" stroke="{C["line"]}" stroke-width="1.4" stroke-dasharray="4 3"/>')
add(f'<rect x="1142" y="1005" width="36" height="7" rx="2" fill="{C["ink"]}"/>')
text(1186, 926, "100 µF  close to VMOT", 11, C["dim"], family="sans")

# servo power
for (p5, pg) in [(fs_5v, fs_gnd), (ds_5v, ds_gnd)]:
    wire([(p5[0], p5[1]), (940, p5[1]), (940, 902), (1300, 902), (1300, R5)], C["v5"], 2)
    wire([(pg[0], pg[1]), (928, pg[1]), (928, 890), (1340, 890), (1340, RG)], C["gnd"], 2)
dot(1300, R5, C["v5"]); dot(1340, RG, C["gnd"])

# limit switch ground
wire([(290, 700), (420, 700), (420, RG)], C["gnd"], 2); dot(420, RG, C["gnd"])
# oled ground
wire([(290, OLED["GND"]), (310, OLED["GND"]), (310, 760), (470, 760), (470, RG)], C["gnd"], 2, dash=(None if IS30 else "5 4"))
dot(470, RG, C["gnd"])

# mosfet power
wire([(1010, 868), (938, 868), (938, 958), (1430, 958), (1430, RG)], C["gnd"], 2)
dot(1430, RG, C["gnd"])
# +12 V feeds the motor stage's V+ from the rail, around the right of the motor
wire([(1260, 840), (1290, 840), (1290, 780), (1656, 780), (1656, R12)], C["v12"], 2)
dot(1656, R12, C["v12"])

# ---------------------------------------------------------------- notes
NY0 = 1082
box(48, NY0, 800, 176, C["note"], C["line"], r=8, sw=1.2)
text(68, NY0 + 26, "BEFORE POWERING UP", 13, C["ink"], weight="700", family="sans")
notes_l = [
    "Set the A4988 current limit first.  Vref ≈ Imax × 0.8  on 0.1 Ω boards.",
    "Fit the heatsink.  Without one it thermally shuts down within a minute.",
    "Measure the coil pairs before wiring:  a few ohms = one coil, open = different coils.",
    "Never unplug the motor while the driver is powered — it destroys A4988s.",
    "GPIO12 is a strapping pin: it must be LOW at boot. Do not pull STEP up.",
    "Check the gear ratio and pin count in Setup before the first move.",
]
for i, n in enumerate(notes_l):
    text(68, NY0 + 52 + i*20, "· " + n, 12, C["ink"], family="sans")

box(872, NY0, 760, 176, C["note"], C["line"], r=8, sw=1.2)
text(892, NY0 + 26, "WHY IT IS WIRED THIS WAY", 13, C["ink"], weight="700", family="sans")
notes_r = [
    "Servos run from the 5 V buck, never the ESP32's 3V3 — an SG90 stalling",
    "     browns out the board and reboots it mid-job.",
    "ENABLE is active LOW and driven at boot, so the motor is released until asked for.",
    "The limit switch uses C and NO with the internal pull-up:  open reads HIGH.",
    "     Wiring it to NC reads as permanently triggered.",
]
for i, n in enumerate(notes_r):
    text(892, NY0 + 52 + i*20, ("· " if not n.startswith("     ") else "") + n,
         12, C["ink"], family="sans")

# ---------------------------------------------------------------- legend
LGX = 48
text(LGX, 918, "LEGEND", 12, C["dim"], weight="700", family="sans")
for i, (lbl, col) in enumerate([("+12 V", C["v12"]), ("+5 V", C["v5"]),
                                ("3.3 V", C["v33"]), ("GND", C["gnd"]),
                                ("signal", C["sig"]), ("motor coil", C["coil"])]):
    x = LGX + i*108
    add(f'<line x1="{x}" y1="936" x2="{x+22}" y2="936" stroke="{col}" stroke-width="3.5" stroke-linecap="round"/>')
    text(x + 30, 940, lbl, 11.5, C["ink"], family="sans")

text(W - 48, H - 18, "StringArt_Nema17_GUI  ·  pin assignments as in StringArt_Nema17_GUI.ino",
     11, C["dim"], "end", family="sans")

add("</svg>")

p = pathlib.Path("/home/claude/work/out/pkg/StringArt_Nema17_GUI/%s.svg" % OUT_NAME)
p.write_text("\n".join(out), encoding="utf-8")
print("wrote", p, len("\n".join(out)), "bytes")
