#!/usr/bin/env python3
"""Decode SCS door-entry frames from pasted hex, see docs/protocol.md.

Reads stdin or the files given. Accepts ESPHome logs from scs_esphome.h (an optional
leading Unix timestamp, `frame A8 ... A3` lines) or bare hex, spaced or not.
Every frame is decoded on its own line, including the repeated copies. All other lines (comments,
other log lines, raw dumps, the `[I]` lines describing a frame) are passed through unchanged,
flagged with `|` and with their leading timestamp decoded.

  python3 scs_decode.py ../examples/0011-*
  wl-paste | python3 scs_decode.py --name 21=garage --name 22='G entrance'
"""

import argparse
import datetime
import fileinput
import re
import sys

START, END, ACK = 0xA8, 0xA3, 0xA5

TIMESTAMP = re.compile(r"^\s*(\d{9,}\.\d+)\s")
HEX_RUN = re.compile(r"(?<![0-9A-Za-z])[0-9A-Fa-f]{2}(?:\s+[0-9A-Fa-f]{2})+(?![0-9A-Za-z])")
HEX_BLOB = re.compile(r"^\s*([0-9A-Fa-f]{2})+\s*$")
ESPHOME_ECHO = re.compile(r"\[I\]\[scs:\d+\]")


def frames(data):
    """Split a byte list into (kind, bytes): 'frame', 'ack', 'bad' or 'stray'.

    A bad frame is reported up to the next A8 inside it, where decoding resumes;
    consecutive stray bytes are reported together."""
    i = 0
    while i < len(data):
        if data[i] == ACK:
            yield "ack", [ACK]
            i += 1
            continue
        if data[i] != START:
            j = i
            while j < len(data) and data[j] not in (START, ACK):
                j += 1
            yield "stray", data[i:j]
            i = j
            continue
        size = 11 if i + 1 < len(data) and data[i + 1] >> 4 in (0xD, 0xE) else 7
        f = data[i:i + size]
        chk = 0
        for p in f[1:-2]:
            chk ^= p
        if len(f) == size and f[-1] == END and f[-2] == chk:
            yield "frame", f
            i += size
            continue
        j = i + 1
        while j < i + len(f) and data[j] != START:
            j += 1
        yield "bad", data[i:j]
        i = j


def endpoint(p1, p2, names):
    if p2 >> 4 == 0x6 and p2 != 0x6F:
        # 12-bit flat number: low 8 bits in P1, high bits in the low nibble of P2
        return ("flat", f"flat {(p2 & 0x0F) << 8 | p1}")
    if p2 == 0x6F and p1 >> 4 == 0xA:
        n = f"{p1 - 0x80:02X}"
        return ("panel", f"panel {n}" + (f" ({names[n]})" if n in names else ""))
    return (None, f"{p1:02X} {p2:02X}")


MESSAGES = {
    # (P0, endpoint kind, P3 or None for any): text
    (0xB0, "panel", 0x88): "call set-up",
    (0x91, "flat", None): "ring",
    (0xB2, "panel", 0x88): "ringing",
    (0xB3, "flat", 0x88): "handset picked up",
    (0xB4, "panel", 0x88): "pick-up acknowledged",
    (0xB6, "flat", 0x98): "door key pressed",
    (0xB6, "flat", 0x88): "door key released",
    (0x96, "panel", 0xA4): "door lock open",
    (0x96, "panel", 0xA0): "door lock release",
    (0xB5, "flat", 0x88): "call ended: hung up",
    (0xB5, "panel", 0x88): "call ended: not answered",
    (0xBD, "flat", 0xA4): "left key pressed",
    (0xBD, "flat", 0xA0): "left key released",
}


def describe(f, names):
    if len(f) != 7:
        return "unknown (extended frame)"
    p0, p1, p2, p3 = f[1:5]
    kind, who = endpoint(p1, p2, names)
    text = MESSAGES.get((p0, kind, p3)) or MESSAGES.get((p0, kind, None))
    return f"{text}, {who}" if text else "unknown"


class Call:
    """Follows one call from B0/ring to B5 and reports it when it ends."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.active = False
        self.start = self.panel = self.flat = self.answered_at = None
        self.answered = False

    def elapsed(self, t):
        return "?" if t is None or self.start is None else f"{t - self.start:.1f} s"

    def feed(self, t, f, names):
        if len(f) != 7:
            return None
        p0, p1, p2, p3 = f[1:5]
        kind, who = endpoint(p1, p2, names)
        if p0 in (0xB0, 0x91) and not self.active:
            self.active, self.start = True, t
        if p0 in (0xB0, 0xB2) and kind == "panel":
            self.panel = who
        elif p0 == 0x91 and kind == "flat":
            self.flat = who
        elif p0 == 0xB3 and kind == "flat" and self.active and not self.answered:
            self.answered, self.answered_at = True, t
        elif p0 == 0xB5 and self.active:
            caller = self.panel or "unknown panel"
            if kind == "panel":
                caller = who
            out = f"call {caller} -> {self.flat or 'unknown flat'}: "
            if self.answered:
                out += f"answered after {self.elapsed(self.answered_at)}, hung up after {self.elapsed(t)}"
            else:
                out += f"not answered, ended after {self.elapsed(t)}"
            self.reset()
            return out
        return None




def stamp(t):
    if t is None:
        return " " * 14
    return datetime.datetime.fromtimestamp(t).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3] + "  "


def parse_lines(lines):
    """Yield ("frames", timestamp or None, bytes) for lines carrying SCS frames,
    ("pass", timestamp or None, rest of line) for everything else."""
    for line in lines:
        line = line.rstrip("\n")
        m = TIMESTAMP.match(line)
        t = float(m.group(1)) if m else None
        rest = line[m.end():] if m else line
        if not rest.lstrip().startswith("#") and not ESPHOME_ECHO.search(rest):
            if HEX_BLOB.match(rest):
                data = [int(h, 16) for h in re.findall(r"[0-9A-Fa-f]{2}", rest)]
            else:
                data = [int(h, 16) for h in " ".join(HEX_RUN.findall(rest)).split()]
            if data and (HEX_BLOB.match(rest) or START in data):
                yield "frames", t, data
                continue
        yield "pass", t, rest


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="input files (default: stdin)")
    ap.add_argument("--name", action="append", default=[], metavar="NN=TEXT",
                    help="label for panel NN, e.g. 21=garage (repeatable)")
    args = ap.parse_args()
    names = dict(n.split("=", 1) for n in args.name)

    call = Call()
    for what, t, data in parse_lines(fileinput.input(args.files)):
        if what == "pass":
            print(f"{stamp(t)}| {data}" if data.strip() or t else data)
            continue
        for kind, f in frames(data):
            hexs = " ".join(f"{b:02X}" for b in f)
            if kind != "frame":
                print(f"{stamp(t)}{hexs}  " + {"ack": "acknowledgement", "stray": "stray bytes",
                                               "bad": "bad frame (length, end byte or checksum)"}[kind])
                continue
            print(f"{stamp(t)}{hexs}  {describe(f, names)}")
            summary = call.feed(t, f, names)
            if summary:
                print(f"{stamp(t)}=> {summary}")

if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        sys.exit(0)
