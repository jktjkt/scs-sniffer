"""Top-side board photo with the SCS tap modifications drawn on top."""
import sys

photo, dst = sys.argv[1], sys.argv[2]  # photo: path relative to the output SVG
W, H = 2016, 1512
o = []
add = o.append
COL = {"red": "#ff3b30", "grn": "#34c759", "mag": "#ff4fd8", "blu": "#4da3ff", "cyan": "#5ac8fa", "wht": "#ffffff"}


def box(x1, y1, x2, y2, c, dash=False):
    d = ' stroke-dasharray="10 6"' if dash else ""
    add(f'<rect x="{x1}" y="{y1}" width="{x2-x1}" height="{y2-y1}" rx="6" fill="none" stroke="{COL[c]}" stroke-width="4"{d}/>')


def cross(x1, y1, x2, y2):
    add(f'<path d="M{x1},{y1} L{x2},{y2} M{x2},{y1} L{x1},{y2}" stroke="{COL["red"]}" stroke-width="5"/>')


def leader(px, py, ax, ay, c):
    add(f'<line x1="{px}" y1="{py}" x2="{ax}" y2="{ay}" stroke="{COL[c]}" stroke-width="3"/>')
    add(f'<circle cx="{px}" cy="{py}" r="6" fill="{COL[c]}"/>')


def label(px, py, lx, ly, lines, c, w=None, anchor_x=None):
    fs = 25
    w = w or max(len(s) for s in lines) * fs * 0.55 + 24
    h = len(lines) * (fs + 6) + 14
    ay = ly + h if py is not None and py > ly + h else ly
    ax = anchor_x if anchor_x is not None else min(max(px or 0, lx + 20), lx + w - 20)
    if px is not None:
        leader(px, py, ax, ay, c)
    add(f'<rect x="{lx}" y="{ly}" width="{w}" height="{h}" rx="8" fill="rgba(0,0,0,0.80)" stroke="{COL[c]}" stroke-width="3"/>')
    for i, s in enumerate(lines):
        add(f'<text x="{lx+12}" y="{ly + 10 + (i+1)*(fs+6) - 8}" font-size="{fs}" font-weight="{"bold" if i == 0 else "normal"}" '
            f'fill="{COL[c] if i == 0 else "#fff"}">{s}</text>')


add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Helvetica, Arial, sans-serif">')
add(f'<image href="{photo}" x="0" y="0" width="{W}" height="{H}"/>')
add('<text x="24" y="50" font-size="38" font-weight="bold" fill="#fff">M5Stack Unit ISO485 as SCS bus tap: board modifications</text>')
legend = [("red", "remove"), ("mag", "replace"), ("grn", "keep"), ("cyan", "wiring")]
x = 24
for c, s in legend:
    add(f'<rect x="{x}" y="66" width="22" height="22" rx="4" fill="{COL[c]}"/>')
    add(f'<text x="{x+30}" y="85" font-size="25" fill="#fff">{s}</text>')
    x += 60 + len(s) * 14

# ---------------- bus side ----------------
box(1400, 358, 1542, 418, "mag")                     # F1
box(1413, 888, 1572, 952, "mag")                     # F2
box(1243, 372, 1302, 488, "mag")                     # R6
box(1483, 423, 1622, 632, "red"); cross(1495, 435, 1610, 620)   # GDT1
box(1483, 658, 1622, 862, "red"); cross(1495, 670, 1610, 850)   # GDT2
box(1388, 418, 1487, 632, "mag")                     # TSS1
box(1393, 658, 1487, 872, "mag")                     # TSS3
box(1133, 822, 1160, 866, "red"); cross(1137, 826, 1156, 862)   # R8
box(1129, 891, 1164, 946, "red"); cross(1133, 895, 1160, 942)   # R10
box(1638, 383, 2010, 975, "cyan")                    # terminal

label(1471, 358, 1128, 112, ["F1 (B) → R103 33 k 0.1 %", "Vishay MMA02040D3302BB300", "MiniMELF, 2 kV 1.2/50 µs"], "mag", w=465)
label(1272, 372, 700, 112, ["R6 → 0 Ω, 0805", "SHIELD = GND, G = SCS BUS −"], "mag", w=420)
label(1437, 470, 1605, 112, ["TSS1 (B) → C102 + R104", "C102: 10 µF X5R/X7R 1206", "R104: 5.49 k 0.1 % 1206, on top"], "mag", w=410)
label(1552, 527, 1640, 230, ["GDT1, GDT2: remove"], "red", w=368)
leader(1552, 760, 1700, 275, "red")
label(1440, 840, 1555, 990, ["TSS3 (A) → R102 + C101", "R102: 5.62 k 0.1 % 1206", "C101: 2.2 nF C0G/X7R 1206, on top"], "mag", w=440)
label(1493, 952, 1540, 1122, ["F2 (A) → R101 33.2 k 0.1 %", "Vishay SMM02040D3322BB300", "MiniMELF, 2 kV 1.2/50 µs"], "mag", w=470)
label(1146, 918, 690, 1250, ["R10: remove"], "red", w=360)
add(f'<polyline points="1160,844 1215,844 1215,1250" fill="none" stroke="{COL["red"]}" stroke-width="3"/>')
add(f'<circle cx="1160" cy="844" r="6" fill="{COL["red"]}"/>')
label(None, None, 1170, 1250, ["R8: remove", "R104 on the TSS1 pads replaces it"], "red", w=470)
box(633, 870, 657, 924, "red"); cross(637, 874, 653, 920)
label(645, 924, 620, 1310, ["R13: remove", "if kept, MCU must drive IO1 high"], "red", w=430)

# terminal wiring
add(f'<rect x="1700" y="430" width="300" height="258" rx="8" fill="rgba(0,0,0,0.80)" stroke="{COL["cyan"]}" stroke-width="3"/>')
for i, s in enumerate(["JP1 (lid: G ☒ A B)", "A → SCS BUS +", "G → SCS BUS −", "B: linked to A", "(bottom side)"]):
    add(f'<text x="1712" y="{466 + i*48}" font-size="27" font-weight="{"bold" if i == 0 else "normal"}" '
        f'fill="{COL["cyan"] if i == 0 else "#fff"}">{s}</text>')

# ---------------- host side ----------------
add('<circle cx="868" cy="748" r="16" fill="none" stroke="#fff" stroke-width="3"/>')
add('<text x="842" y="806" font-size="22" fill="#fff" font-weight="bold">pin 1</text>')
for p, y in ((12, 912), (13, 868)):
    add(f'<rect x="1004" y="{y-14}" width="40" height="28" rx="4" fill="rgba(0,0,0,0.8)"/>')
    add(f'<text x="1024" y="{y+8}" font-size="22" font-weight="bold" fill="#fff" text-anchor="middle">{p}</text>')

add("</svg>")
open(dst, "w").write("\n".join(o))
