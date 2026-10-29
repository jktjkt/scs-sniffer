"""Unit ISO485 v1.1 schematic (pdftocairo SVG) with the SCS tap modifications drawn on top."""
import sys

src, dst = sys.argv[1], sys.argv[2]
base = open(src).read()

o = []
add = o.append
G, R, B, M = "#0a8f3c", "#e00000", "#1565c0", "#b0008a"  # keep/info, remove, add, replace


def wire(*pts, c=G, w=0.8):
    add(f'<polyline fill="none" stroke="{c}" stroke-width="{w}" stroke-linejoin="round" '
        f'points="{" ".join(f"{x},{y}" for x, y in pts)}"/>')


def dot(x, y, c=G): add(f'<circle cx="{x}" cy="{y}" r="1.3" fill="{c}"/>')


def text(x, y, s, size=5.0, c="#111", a="start", bold=False):
    fw = ' font-weight="bold"' if bold else ''
    add(f'<text x="{x}" y="{y}" font-size="{size}" fill="{c}" text-anchor="{a}"{fw}>{s}</text>')


def cover(x, y, w, h, s, c, size=4.6):
    """White patch over an original value, with the new value on it."""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#fffdf6" stroke="{c}" stroke-width="0.5"/>')
    text(x + 1, y + h - 1.4, s, size, c, bold=True)


def redx(x, y, s=4.0):
    add(f'<path d="M{x-s},{y-s} L{x+s},{y+s} M{x+s},{y-s} L{x-s},{y+s}" stroke="{R}" stroke-width="1.3"/>')


def tag(x, y, n, c):
    add(f'<circle cx="{x}" cy="{y}" r="3.2" fill="{c}"/>')
    text(x, y + 1.7, str(n), 4.4, "#fff", "middle", True)


def rlabel(x, y, n, s, box_xy, c):
    """Replacement label at (x, y) with a dashed leader to an outline around the original part."""
    bx1, by1, bx2, by2 = box_xy
    w = len(s) * 2.6 + 11
    lx = min(max((bx1 + bx2) / 2, x), x + w)          # leader end on the label
    ly = y + 1.8 if (by1 + by2) / 2 > y else y - 5.2   # bottom or top edge of the label
    py = by1 if ly < by1 else by2
    add(f'<line x1="{lx}" y1="{ly}" x2="{(bx1 + bx2) / 2}" y2="{py}" stroke="{c}" stroke-width="0.5" stroke-dasharray="1.5 1"/>')
    add(f'<rect x="{x}" y="{y-5.2}" width="{w}" height="7" rx="1" fill="#fffdf6" stroke="{c}" stroke-width="0.5"/>')
    tag(x + 3.6, y - 1.7, n, c)
    text(x + 8.2, y, s, 4.4, c, bold=True)


def res_h(x1, x2, y, ref, val, c):
    m = (x1 + x2) / 2
    wire((x1, y), (m - 8, y), c=c); add(f'<rect x="{m-8}" y="{y-2.5}" width="16" height="5" fill="#fff" stroke="{c}" stroke-width="0.8"/>')
    wire((m + 8, y), (x2, y), c=c)
    text(m, y - 4, ref, 4.4, c, "middle", True); text(m, y + 8, val, 4.4, c, "middle")


def shunt(x, y1, y2, kind, ref, val, c):
    """Vertical part from y1 down to y2: kind r (resistor), c (cap), d (TVS/TSS box)."""
    m = (y1 + y2) / 2
    if kind == "r":
        wire((x, y1), (x, m - 8), c=c); add(f'<rect x="{x-2.5}" y="{m-8}" width="5" height="16" fill="#fff" stroke="{c}" stroke-width="0.8"/>'); wire((x, m + 8), (x, y2), c=c)
    elif kind == "c":
        wire((x, y1), (x, m - 1.5), c=c); wire((x - 4.5, m - 1.5), (x + 4.5, m - 1.5), c=c, w=1.3)
        wire((x - 4.5, m + 1.5), (x + 4.5, m + 1.5), c=c, w=1.3); wire((x, m + 1.5), (x, y2), c=c)
    else:
        wire((x, y1), (x, m - 6), c=c); add(f'<rect x="{x-3.5}" y="{m-6}" width="7" height="12" fill="#fff" stroke="{c}" stroke-width="0.8"/>'); wire((x, m + 6), (x, y2), c=c)
    dx = 7 if kind == "c" else 4
    text(x + dx, m - 1, ref, 4.2, c, bold=True); text(x + dx, m + 4.5, val, 4.2, c)


def gnd(x, y, c=G):
    wire((x, y), (x, y + 3), c=c)
    for i, w in enumerate((4.5, 3, 1.5)):
        wire((x - w, y + 3 + i * 1.6), (x + w, y + 3 + i * 1.6), c=c)


add('<g id="scs-onboard-v2" font-family="Helvetica, Arial, sans-serif">')

# ---------------- marks on the original sheet ----------------
redx(327.6, 117.5); tag(318, 117.5, 1, R)                                   # R10
redx(327.6, 82); tag(318, 82, 2, R)                                        # R8
rlabel(500, 176, 3, "F2 → R101 33k MMA0204", (507, 153.5, 535, 161), M)
redx(521, 157.4, 3); redx(521, 99.3, 3); redx(445, 123.4, 3); redx(500.7, 112, 3.2); redx(500.7, 142.5, 3.2)  # original parts: DNP
rlabel(505, 70, 4, "F1 → R103 33k MMA0204", (507, 95.5, 535, 103), M)
rlabel(395, 70, 5, "R6 → 0 Ω (SHIELD = GND)", (430, 120, 460, 127), M)               # R6
redx(521, 122, 3.2); redx(521, 134, 3.2); tag(578, 127, 6, R)               # GDT1/2
rlabel(450, 80, 8, "TSS1 → C102 + R104", (496.5, 101, 505, 124), M)          # TSS1
rlabel(420, 176, 7, "TSS3 → R102 + C101", (496.5, 128, 505, 156), M)       # TSS3
text(697, 94.6, "→ ESP32 RX", 4.4, G, bold=True); tag(728, 93, 11, G)
text(697, 101.8, "high or open", 4.4, R, bold=True); tag(728, 100.2, 10, R)
redx(195, 142.5, 3.5); tag(186, 142.5, 9, R)  # R13
wire((646, 142.9), (646, 150.4), c=B, w=1.4); dot(646, 142.9, B); dot(646, 150.4, B)
wire((646, 157.4), (646, 164.4), c=B, w=1.4); dot(646, 157.4, B); dot(646, 164.4, B)  # G-NC link
tag(616, 147.7, 12, B)                                # R13

# ---------------- bottom: bus side ----------------
add('<rect x="40" y="310" width="395" height="240" rx="3" fill="#f7fbff" stroke="#1565c0" stroke-width="1"/>')
text(47, 321, "Modified bus side", 6.2, B, bold=True)
add('<circle cx="62" cy="345" r="2" fill="#fff" stroke="#111" stroke-width="0.8"/>'); text(62, 339, "SCS BUS +", 5, "#111", "middle", True)
add('<circle cx="62" cy="530" r="2" fill="#fff" stroke="#111" stroke-width="0.8"/>'); text(62, 541, "SCS BUS −", 5, "#111", "middle", True)
wire((64, 345), (90, 345), (90, 450), c="#111"); dot(90, 370, "#111")
wire((90, 370), (112, 370), c="#111"); wire((90, 450), (112, 450), c="#111")
text(96, 366, "JP1-3 A", 4.2, "#111"); text(96, 446, "JP1-4 B", 4.2, "#111")
text(66, 405, "A–B linked", 4.2, B, bold=True); text(66, 410.5, "on PCB (12)", 4.2, B, bold=True)
# A
res_h(112, 170, 370, "R101 33k", "F2 position", M)
wire((170, 370), (330, 370), c=G); dot(200, 370); dot(240, 370); dot(285, 370); dot(330, 370)
shunt(200, 370, 420, "r", "R102", "5.62k", M)
shunt(240, 370, 420, "c", "C101", "2.2n C0G/X7R", M)
shunt(285, 370, 420, "d", "DZ2", "5V", G)
wire((200, 420), (305, 420), c=G); dot(240, 420); dot(285, 420)
text(198, 433, "R102, C101 stacked on TSS3 pads", 4.2, M)
wire((330, 370), (395, 370), c=G); text(398, 371.6, "U1-12 (A)", 4.6, G, bold=True)
# B
res_h(112, 170, 450, "R103 33k", "F1 position", M)
wire((170, 450), (330, 450), c=G); dot(200, 450); dot(240, 450); dot(285, 450); dot(330, 450)
shunt(200, 450, 500, "r", "R104", "5.49k", M)
shunt(240, 450, 500, "c", "C102", "10µ X5R/X7R", M)
shunt(285, 450, 500, "d", "DZ1", "5V", G)
wire((200, 500), (305, 500), c=G); dot(240, 500); dot(285, 500)
text(198, 513, "C102, R104 stacked on TSS1 pads", 4.2, M)
wire((330, 450), (395, 450), c=G); text(398, 451.6, "U1-13 (B)", 4.6, G, bold=True)
shunt(330, 370, 450, "d", "TSS2", "A–B", G)
add(f'<path d="M305,420 V447 A3,3 0 0 1 305,453 V530" fill="none" stroke="{G}" stroke-width="0.8"/>')
dot(305, 500); wire((64, 530), (150, 530), c="#111"); res_h(150, 208, 530, "R6 0 Ω", "", M); wire((208, 530), (305, 530), c=G); dot(305, 530); gnd(305, 530)
text(80, 526, "JP1-1 G = SHIELD", 4.2, "#111", bold=True); text(235, 526, "GND", 4.6, G, bold=True)
text(47, 548, "A = 0.1455·Vbus (τ 11 µs), B = 0.1426·Vbus (τ 47 ms). At 27 V: idle A−B +0.08 V, 3 V dip −0.33 V. Bus load 1.4 mA, max 1.8 mA.", 4.2, "#333")

# ---------------- host side ----------------
add('<rect x="445" y="310" width="365" height="80" rx="3" fill="#f7fff9" stroke="#0a8f3c" stroke-width="1"/>')
text(452, 321, "Host side (Grove)", 6.2, G, bold=True)
rows = [
    ("IO2  UART_Tx", "module output (RO via Q2) → ESP32 RX / RMT input, 34 µs pulses", G),
    ("", "enable the MCU pull-up on that pin (on-board pull-up R11 is only 100k)", G),
    ("IO1  UART_Rx", "keep at logic high (or leave floating)", R),
    ("5V / GND", "host supply; M1 powers the bus side", "#111"),
    ("IO1 low", "tap stops receiving; SCS bus sees ≤0.1 mA, no effect", R),
]
y = 333
for a, b, c in rows:
    text(452, y, a, 4.8, c, bold=True); text(500, y, b, 4.6, "#222"); y += 13

add('<rect x="445" y="398" width="365" height="152" rx="3" fill="#fff6f6" stroke="#e00000" stroke-width="1"/>')
text(452, 409, "Modifications", 6.2, R, bold=True)
changes = [
    (1, R, "R10: remove."),
    (2, R, "R8: remove. R104 on the TSS1 pads replaces it (bottom of divider B)."),
    (3, M, "F2 → R101 33k 0.1 %: Vishay MMA02040D3302BB300 MiniMELF (2 kV 1.2/50 µs)."),
    (4, M, "F1 → R103 33k 0.1 %: the same part. A 0.1 % 1206 works too, with a lower surge rating."),
    (5, M, "R6 → 0 Ω. SHIELD = iso GND; terminal G = SCS BUS −."),
    (6, R, "GDT1, GDT2: remove. Once fired, a GDT across the 27 V bus supply would keep conducting."),
    (7, M, "TSS3 → R102 5.62k 0.1 % 1206 (TNPW12065K62BEEA), C101 2.2 nF C0G/X7R 1206 stacked on it."),
    (8, M, "TSS1 → C102 10 µF ≥16 V X5R/X7R 1206, R104 5.49k 0.1 % 1206 (ERA-8AEB5491V) stacked on it."),
    (9, R, "R13 (100k, Q1 gate to GND): remove. If kept, IO1 must be driven high: open, it floats at ~1.65 V."),
    (10, R, "Grove IO1 (UART_Rx): keep at logic high or leave floating."),
    (11, G, "Grove IO2 (UART_Tx) → ESP32 RX, MCU pull-up enabled."),
    (12, B, "Link A and B: short wire between the JP1 A and B pads (bottom side)."),
]
y = 418
for n, c, s in changes:
    tag(456, y - 1.6, n, c); text(463, y, s, 4.6, "#222"); y += 11.0

add("</g>")
open(dst, "w").write(base.replace("</svg>", "\n".join(o) + "\n</svg>"))
