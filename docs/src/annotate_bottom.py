"""Bottom-side board photo, rotated to landscape (Grove left, terminal right), with the SCS tap wiring drawn on top."""
import sys

photo, dst = sys.argv[1], sys.argv[2]  # photo: portrait 1512 x 2016, path relative to the output SVG
W, H = 2016, 1512
o = []
add = o.append
COL = {"red": "#ff3b30", "grn": "#34c759", "blu": "#4da3ff", "cyan": "#5ac8fa"}


def tag(px, py, text, c, side):
    """One-line label level with the pad; side='right' puts it right of the pad, 'left' left of it."""
    fs = 26
    w = len(text) * fs * 0.58 + 28
    h = 44
    if side == "right":
        lx = px + 90
        add(f'<line x1="{px}" y1="{py}" x2="{lx}" y2="{py}" stroke="{COL[c]}" stroke-width="3"/>')
    else:
        lx = px - 90 - w
        add(f'<line x1="{px}" y1="{py}" x2="{lx + w}" y2="{py}" stroke="{COL[c]}" stroke-width="3"/>')
    add(f'<circle cx="{px}" cy="{py}" r="7" fill="{COL[c]}"/>')
    add(f'<rect x="{lx}" y="{py - h/2}" width="{w}" height="{h}" rx="7" fill="rgba(0,0,0,0.82)" stroke="{COL[c]}" stroke-width="3"/>')
    add(f'<text x="{lx + 12}" y="{py + 9}" font-size="{fs}" fill="#fff">{text}</text>')


add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Helvetica, Arial, sans-serif">')
# rotated 90° clockwise into landscape
add(f'<image href="{photo}" x="0" y="0" width="{H}" height="{W}" transform="translate({W},0) rotate(90)"/>')
add('<rect x="10" y="12" width="1010" height="56" rx="8" fill="rgba(0,0,0,0.82)"/>')
add('<text x="24" y="52" font-size="34" font-weight="bold" fill="#fff">Unit ISO485 as SCS bus tap: bottom side</text>')

# terminal pads (x 1597): G 444, NC 559, A 690, B 816; A-B wire link
add(f'<line x1="1597" y1="690" x2="1597" y2="816" stroke="{COL["blu"]}" stroke-width="16" stroke-linecap="round"/>')
add(f'<line x1="1597" y1="444" x2="1597" y2="559" stroke="{COL["blu"]}" stroke-width="16" stroke-linecap="round"/>')
tag(1597, 559, "NC: wire link to G (no connection)", "blu", "left")
tag(1592, 444, "G → SCS BUS −", "cyan", "left")
tag(1597, 690, "A → SCS BUS +", "cyan", "left")
tag(1597, 816, "B: wire link to A", "blu", "left")

# Grove pads (x 417): T↓R 534, R↑T 595, 5 660, G 724
tag(417, 534, "ESP32 RMT RX, pull-up on", "grn", "right")
tag(417, 595, "high or floating", "red", "right")
tag(417, 660, "5 → 5 V", "cyan", "right")
tag(417, 724, "G → GND", "cyan", "right")


notes = ["UART_Rx (R↑T)",
         "Open at the MCU (R13 removed) or high: RS-485 driver off, receiver on, SCS data on UART_Tx.",
         "Low: the tap stops receiving; the driver pushes at most 0.1 mA into the SCS bus (a few mV).",
         "With R13 still fitted, an open UART_Rx floats at ~1.65 V and the tap is unreliable: drive it high."]
add('<rect x="200" y="1030" width="1616" height="190" rx="10" fill="rgba(0,0,0,0.82)" stroke="#ff3b30" stroke-width="3"/>')
for i, t in enumerate(notes):
    add(f'<text x="224" y="{1078 + i*42}" font-size="28" font-weight="{"bold" if i == 0 else "normal"}" fill="{"#ff3b30" if i == 0 else "#fff"}">{t}</text>')
add("</svg>")
open(dst, "w").write("\n".join(o))
