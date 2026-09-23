#!/usr/bin/env python3
"""
String Art CNC — perfboard carrier board.

The layout is defined as data, checked, and only then drawn. That order is the
point: a wiring drawing made by hand looks equally convincing whether or not
the nets actually connect, and the mistakes only show up with a soldering iron
in your hand.

What gets checked before anything is drawn:
  · every hole used is on the board
  · no two components claim the same hole
  · no wire ends in a hole that is not a component pin or a bus rail
  · every net is a single connected group -- nothing left floating
  · every ESP32 GPIO the firmware drives appears exactly once

Board: 24 x 18 holes on 2.54 mm pitch = 61 x 46 mm, so a stock 70 x 50 mm
perfboard takes it with room to drill mounting holes.

The board carries signals and 5 V only. The A4988 expansion board and the drill
MOSFET module each take 12 V on their own terminals, so no motor current passes
through here at all -- which is what makes a hand-built board a reasonable idea.
"""

import pathlib
import sys

COLS, ROWS = 24, 18
PITCH = 2.54

# --------------------------------------------------------------------------
# ESP32 DevKit V1, 30-pin. Row 1 is the USB end.
# Left strip = the 3V3 side, right strip = the VIN side.
# --------------------------------------------------------------------------
ESP_L_COL, ESP_R_COL = 2, 12
ESP_LEFT = ["VIN", "GND", "D13", "D12", "D14", "D27", "D26", "D25",
            "D33", "D32", "D35", "D34", "VN", "VP", "EN"]
ESP_RIGHT = ["3V3", "GND", "D15", "D2", "D4", "RX2", "TX2", "D5",
             "D18", "D19", "D21", "RX0", "TX0", "D22", "D23"]

# --------------------------------------------------------------------------
# Components: name -> (col, first_row, [pin labels], body label, width in cols)
# --------------------------------------------------------------------------
COMPONENTS = {
    "ESP-L": (ESP_L_COL, 1, ESP_LEFT, "ESP32 DevKit V1 · 1×15 female", 1),
    "ESP-R": (ESP_R_COL, 1, ESP_RIGHT, "", 1),
    "J1": (15, 1, ["STEP", "DIR", "EN", "GND"], "A4988 board", 1),
    "J2": (15, 6, ["GND", "+5V", "SIG"], "Feeder SG90", 1),
    "J3": (15, 10, ["GND", "+5V", "SIG"], "Drill SG90", 1),
    "J4": (15, 14, ["SIG", "GND"], "Drill MOSFET", 1),
    "J5": (19, 1, ["GND", "3V3", "SDA", "SCL"], "0.96\" OLED", 1),
    "J6": (19, 6, ["LIM", "GND"], "Limit switch", 1),
    "J7": (21, 9, ["+5V", "", "GND"], "5 V in (screw)", 1),
    "C1": (21, 13, ["+", "", "\u2212"], "470 µF 10 V", 1),
}
# Pins given as "" are body-only holes (the middle of a 5.08 mm part).

# --------------------------------------------------------------------------
# Bus rails: bare tinned wire soldered along a whole row.
# --------------------------------------------------------------------------
RAILS = {
    "+3V3": (16, 12, 20, "#8e44ad"),
    "+5V":  (17, 1, 24, "#e08a1e"),
    "GND":  (18, 1, 24, "#2b2b2b"),
}

# --------------------------------------------------------------------------
# Wires: (from, to, net).  A hole is "COMP.PIN" or "(col,row)" on a rail.
# --------------------------------------------------------------------------
WIRES = [
    ("ESP-L.VIN",  (2, 17),  "+5V"),
    ("ESP-L.GND",  (2, 18),  "GND"),
    ("ESP-R.GND",  (11, 18), "GND"),
    ("ESP-R.3V3",  (12, 16), "+3V3"),

    ("ESP-L.D12",  "J1.STEP", "STEP"),
    ("ESP-L.D14",  "J1.DIR",  "DIR"),
    ("ESP-L.D26",  "J1.EN",   "EN"),
    ("J1.GND",     (14, 18),  "GND"),

    ("ESP-R.D18",  "J2.SIG",  "SERVO_FEED"),
    ("J2.GND",     (16, 18),  "GND"),
    ("J2.+5V",     (16, 17),  "+5V"),

    ("ESP-R.D19",  "J3.SIG",  "SERVO_DRILL"),
    ("J3.GND",     (17, 18),  "GND"),
    ("J3.+5V",     (17, 17),  "+5V"),

    ("ESP-L.D25",  "J4.SIG",  "DRILL_SIG"),
    ("J4.GND",     (18, 18),  "GND"),

    ("ESP-R.D21",  "J5.SDA",  "SDA"),
    ("ESP-R.D22",  "J5.SCL",  "SCL"),
    ("J5.3V3",     (19, 16),  "+3V3"),
    ("J5.GND",     (20, 18),  "GND"),

    ("ESP-L.D27",  "J6.LIM",  "LIMIT"),
    ("J6.GND",     (21, 18),  "GND"),

    ("J7.+5V",     (23, 17),  "+5V"),
    ("J7.GND",     (23, 18),  "GND"),

    ("C1.+",       (22, 17),  "+5V"),
    ("C1.\u2212",  (22, 18),  "GND"),
]

# GPIOs the firmware drives, to be cross-checked against the layout.
FIRMWARE_PINS = {"D12": "STEP", "D14": "DIR", "D26": "EN", "D27": "LIMIT",
                 "D18": "SERVO_FEED", "D19": "SERVO_DRILL", "D25": "DRILL_SIG",
                 "D21": "SDA", "D22": "SCL"}

# ==========================================================================
#  Resolve and verify
# ==========================================================================
problems = []

pin_hole = {}          # "COMP.PIN" -> (col,row)
hole_owner = {}        # (col,row) -> "COMP.PIN"

for name, (col, row0, pins, _label, _w) in COMPONENTS.items():
    for i, pin in enumerate(pins):
        h = (col, row0 + i)
        if not (1 <= h[0] <= COLS and 1 <= h[1] <= ROWS):
            problems.append(f"{name} pin {pin or i} is off the board at {h}")
            continue
        if h in hole_owner:
            problems.append(f"{name}.{pin} collides with {hole_owner[h]} at {h}")
        hole_owner[h] = f"{name}.{pin}" if pin else f"{name}.(body)"
        if pin:
            pin_hole[f"{name}.{pin}"] = h

def rail_hole(h):
    for net, (row, c0, c1, _col) in RAILS.items():
        if h[1] == row and c0 <= h[0] <= c1:
            return net
    return None

def resolve(end):
    if isinstance(end, tuple):
        return end
    if end not in pin_hole:
        problems.append(f"wire references unknown pin {end}")
        return None
    return pin_hole[end]

# rails must not sit on top of a component
for net, (row, c0, c1, _c) in RAILS.items():
    for c in range(c0, c1 + 1):
        if (c, row) in hole_owner:
            problems.append(f"rail {net} crosses {hole_owner[(c, row)]} at {(c, row)}")

# build nets
nets = {}
rail_ends = {}
for a, b, net in WIRES:
    ha, hb = resolve(a), resolve(b)
    if ha is None or hb is None:
        continue
    for h, raw in ((ha, a), (hb, b)):
        if isinstance(raw, tuple):
            r = rail_hole(h)
            if r is None:
                problems.append(f"wire ends at {h}, which is neither a pin nor a rail")
            elif r != net:
                problems.append(f"wire for {net} lands on the {r} rail at {h}")
            else:
                rail_ends.setdefault(h, []).append(net)
    nets.setdefault(net, set()).update([a if isinstance(a, str) else f"rail{ha}",
                                        b if isinstance(b, str) else f"rail{hb}"])

# two wires must not share a rail hole
for h, ns in rail_ends.items():
    if len(ns) > 1:
        problems.append(f"{len(ns)} wires share rail hole {h}")

# every signal net must have exactly two ends; power nets reach the rail
for net, members in nets.items():
    if net in RAILS:
        continue
    if len(members) != 2:
        problems.append(f"net {net} has {len(members)} ends, expected 2: {sorted(members)}")

# every component pin must actually go somewhere. Dropping a ground wire is
# the easiest mistake to make and the hardest to see on a finished board, so
# an unconnected pin is an error rather than something to notice later.
wired_pins = set()
for a, b, _net in WIRES:
    for e in (a, b):
        if isinstance(e, str):
            wired_pins.add(e)
for name, (col, row0, pins, _label, _w) in COMPONENTS.items():
    for pin in pins:
        if not pin:
            continue
        ref = f"{name}.{pin}"
        if ref in wired_pins:
            continue
        if rail_hole(pin_hole[ref]):
            continue                      # sits directly on a bus rail
        if name.startswith("ESP") and pin not in FIRMWARE_PINS \
           and pin not in ("VIN", "GND", "3V3"):
            continue                      # spare ESP32 pins are meant to be free
        problems.append(f"{ref} is not connected to anything")

# every firmware GPIO appears once, on the right net
for gpio, net in FIRMWARE_PINS.items():
    hits = [w for w in WIRES if isinstance(w[0], str) and w[0].endswith("." + gpio)]
    if len(hits) != 1:
        problems.append(f"GPIO {gpio} appears {len(hits)} times, expected once")
    elif hits[0][2] != net:
        problems.append(f"GPIO {gpio} is wired to {hits[0][2]}, firmware uses it for {net}")

if problems:
    print("LAYOUT FAILED:")
    for p in problems:
        print("  ·", p)
    sys.exit(1)
print(f"layout ok — {len(hole_owner)} holes used, {len(WIRES)} wires, "
      f"{len(nets)} nets, {len(FIRMWARE_PINS)} GPIOs matched to the firmware")

# ==========================================================================
#  Draw
# ==========================================================================
NETCOL = {"+5V": "#e08a1e", "GND": "#2b2b2b", "+3V3": "#8e44ad",
          "STEP": "#1f6fb2", "DIR": "#1f6fb2", "EN": "#1f6fb2",
          "LIMIT": "#1f6fb2", "SDA": "#0f8a7a", "SCL": "#0f8a7a",
          "SERVO_FEED": "#c0392b", "SERVO_DRILL": "#c0392b",
          "DRILL_SIG": "#b8541c"}

M = 112                      # margin
S = 34                       # px per hole
BW, BH = COLS * S, ROWS * S
W, H = BW + M * 2 + 430, BH + M + 330

def X(c): return M + (c - 0.5) * S
def Y(r): return M + (r - 0.5) * S

o = []
def A(s): o.append(s)

A(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
A(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')

def T(x, y, t, size=13, fill="#1c1a17", anchor="start", weight="normal", mono=True):
    fam = ("'IBM Plex Mono',Menlo,Consolas,monospace" if mono
           else "'Inter','Helvetica Neue',Arial,sans-serif")
    t = t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    A(f'<text x="{x}" y="{y}" font-family="{fam}" font-size="{size}" fill="{fill}" '
      f'text-anchor="{anchor}" font-weight="{weight}">{t}</text>')

T(M, 46, "String Art CNC — Perfboard Layout", 30, "#1c1a17", weight="700", mono=False)
T(M, 64, f"{COLS} × {ROWS} holes on 2.54 mm pitch  ·  61 × 46 mm  ·  fits a stock 70 × 50 mm board  ·  viewed from the COMPONENT side",
  14, "#6b6459", mono=False)

# board
A(f'<rect x="{M}" y="{M}" width="{BW}" height="{BH}" rx="6" fill="#e7dfc9" stroke="#b3a887" stroke-width="2"/>')

# holes + coordinates
for c in range(1, COLS + 1):
    T(X(c), M - 20, chr(64 + c), 11, "#8a8172", "middle")
for r in range(1, ROWS + 1):
    T(M - 12, Y(r) + 4, str(r), 11, "#8a8172", "end")
for c in range(1, COLS + 1):
    for r in range(1, ROWS + 1):
        A(f'<circle cx="{X(c)}" cy="{Y(r)}" r="3.4" fill="#ffffff" stroke="#b3a887" stroke-width="1"/>')

# rails
for net, (row, c0, c1, col) in RAILS.items():
    A(f'<line x1="{X(c0)}" y1="{Y(row)}" x2="{X(c1)}" y2="{Y(row)}" stroke="{col}" '
      f'stroke-width="7" stroke-linecap="round" opacity="0.9"/>')
    T(X(c0) - 16, Y(row) + 5, net, 13, col, "end", "700")

# component bodies
for name, (col, row0, pins, label, _w) in COMPONENTS.items():
    n = len(pins)
    x, y = X(col) - S * 0.42, Y(row0) - S * 0.42
    w, h = S * 0.84, S * (n - 1) + S * 0.84
    A(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="5" fill="#fdf9ef" '
      f'stroke="#4a6fa5" stroke-width="2"/>')
    for i, pin in enumerate(pins):
        A(f'<circle cx="{X(col)}" cy="{Y(row0+i)}" r="4.6" fill="#4a6fa5"/>')
        if pin:
            T(X(col) - 12, Y(row0 + i) + 4, pin, 11, "#1c1a17", "end")
    if label:
        T(X(col), y - 14, label, 12, "#1c1a17", "middle", "700", mono=False)

T(X(ESP_L_COL) + (X(ESP_R_COL) - X(ESP_L_COL)) / 2, Y(ROWS) + 6,
  "USB end  ↑  row 1", 12, "#b8541c", "middle", "700", mono=False)

# wires
for a, b, net in WIRES:
    ha = pin_hole[a] if isinstance(a, str) else a
    hb = pin_hole[b] if isinstance(b, str) else b
    col = NETCOL.get(net, "#1f6fb2")
    A(f'<line x1="{X(ha[0])}" y1="{Y(ha[1])}" x2="{X(hb[0])}" y2="{Y(hb[1])}" '
      f'stroke="{col}" stroke-width="2.6" opacity="0.85"/>')
    A(f'<circle cx="{X(ha[0])}" cy="{Y(ha[1])}" r="3" fill="{col}"/>')
    A(f'<circle cx="{X(hb[0])}" cy="{Y(hb[1])}" r="3" fill="{col}"/>')

# ---- side panel -------------------------------------------------------
PX = M + BW + 40
T(PX, M + 6, "WIRE LIST", 15, "#1c1a17", weight="700", mono=False)
T(PX, M + 26, "Insulated wire, component side unless noted.", 11.5, "#6b6459", mono=False)
yy = M + 52
def hole_name(e):
    if isinstance(e, tuple):
        return f"{chr(64+e[0])}{e[1]}"
    c, r = pin_hole[e]
    return f"{chr(64+c)}{r}"

for a, b, net in WIRES:
    col = NETCOL.get(net, "#1f6fb2")
    A(f'<circle cx="{PX+6}" cy="{yy-4}" r="4" fill="{col}"/>')
    lab_a = a if isinstance(a, str) else "rail"
    lab_b = b if isinstance(b, str) else "rail"
    T(PX + 18, yy, f"{hole_name(a):>4} → {hole_name(b):<4}  {net}", 12)
    T(PX + 250, yy, f"{lab_a} → {lab_b}", 10.5, "#8a8172")
    yy += 19

yy += 14
T(PX, yy, "BUS RAILS — bare tinned wire", 13, "#1c1a17", weight="700", mono=False)
yy += 20
for net, (row, c0, c1, col) in RAILS.items():
    A(f'<circle cx="{PX+6}" cy="{yy-4}" r="4" fill="{col}"/>')
    T(PX + 18, yy, f"row {row}: {chr(64+c0)}{row} → {chr(64+c1)}{row}   {net}", 12)
    yy += 19

# ---- notes ------------------------------------------------------------
NY = M + BH + 44
A(f'<rect x="{M}" y="{NY}" width="{BW+380}" height="238" rx="8" fill="#fbf8f2" stroke="#c9c2b6"/>')
T(M + 20, NY + 26, "BUILD NOTES", 13, "#1c1a17", weight="700", mono=False)
notes = [
    "Row-spacing does not matter here. Solder the two 1×15 female strips wherever YOUR DevKit sits —",
    "     if it is 22.86 mm rather than 25.4 mm, put the right-hand strip on column K instead of L and every",
    "     wire to it shifts one hole. Nothing else changes. Fit the strips before any wire.",
    "Wires from the left strip (column B) cross under the ESP32. Run them on the solder side; the module",
    "     stands clear on its headers, so there is nothing to short against.",
    "Only 5 V and signals are on this board. The A4988 expansion board and the drill MOSFET module each",
    "     take 12 V on their own terminals — no motor current passes through here.",
    "C1 (470 µF) sits across the 5 V rail beside the servo headers. Two SG90s starting together is exactly",
    "     what browns out an ESP32, and the cap is the cheapest fix. Watch the polarity: the stripe goes to GND.",
    "Servo headers are GND / +5V / SIG top to bottom — brown, red, orange. Check against your leads.",
    "The limit switch goes to the C and NO terminals. NC reads as permanently triggered.",
]
for i, n in enumerate(notes):
    T(M + 20, NY + 52 + i * 17, ("· " + n) if not n.startswith("     ") else n,
      12, "#1c1a17", mono=False)

T(W - M, H - 18, "StringArt_Nema17_GUI  ·  pin assignments verified against StringArt_Nema17_GUI.ino",
  11, "#6b6459", "end", mono=False)
A("</svg>")

root = pathlib.Path("/home/claude/work/out/pkg/StringArt_Nema17_GUI")
(root / "perfboard_layout.svg").write_text("\n".join(o), encoding="utf-8")
print("wrote perfboard_layout.svg")

# ---- build sheet -------------------------------------------------------
md = ["# Perfboard build sheet", "",
      f"{COLS} × {ROWS} holes on 2.54 mm pitch — 61 × 46 mm. A stock 70 × 50 mm",
      "perfboard takes it with room for mounting holes.", "",
      "Columns are lettered A–X left to right, rows numbered 1–18 top to bottom.",
      "Row 1 is the USB end of the ESP32.", "",
      "## Parts", "",
      "| Ref | Part | Holes |", "|---|---|---|"]
for name, (col, row0, pins, label, _w) in COMPONENTS.items():
    md.append(f"| {name} | {label or '(same module)'} | "
              f"{chr(64+col)}{row0} – {chr(64+col)}{row0+len(pins)-1} |")
md += ["", "## Bus rails", "",
       "Bare tinned wire soldered along the row.", "",
       "| Net | Run |", "|---|---|"]
for net, (row, c0, c1, _c) in RAILS.items():
    md.append(f"| {net} | {chr(64+c0)}{row} → {chr(64+c1)}{row} |")
md += ["", "## Wires", "",
       "| From | To | Net | |", "|---|---|---|---|"]
for a, b, net in WIRES:
    md.append(f"| {hole_name(a)} | {hole_name(b)} | {net} | "
              f"{a if isinstance(a,str) else 'rail'} → {b if isinstance(b,str) else 'rail'} |")
(root / "PERFBOARD_BUILD.md").write_text("\n".join(md) + "\n", encoding="utf-8")
print("wrote PERFBOARD_BUILD.md")
