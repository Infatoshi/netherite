#!/usr/bin/env python3
"""Deterministic inputs for 39_font_glyph_quad (MC 1.11.2 FontRenderer.renderDefaultChar).

Each line = 5 tokens: ch (0-255 decimal) + charWidth (decimal int) + posX (hex float-bits) +
posY (hex float-bits) + italic (0/1).

Coverage: all 256 char codes (drives i=ch%16*8, j=ch/16*8 across the 128px sheet), realistic
charWidth values (default.png widths run ~2..9; the renderer also tolerates 0), italic on/off (the
k=1 shear), and arbitrary posX/posY. The full code range x a width sample exercises every glyph cell.
"""
import random
import struct
import sys


def fb(f):
    return "%x" % struct.unpack("<I", struct.pack("<f", f))[0]


def main():
    rnd = random.Random(39039)
    out = []
    for ch in range(256):
        for _ in range(8):
            cw = rnd.randrange(0, 17)
            posX = rnd.uniform(-2048.0, 2048.0)
            posY = rnd.uniform(-2048.0, 2048.0)
            it = rnd.randrange(2)
            out.append(f"{ch} {cw} {fb(posX)} {fb(posY)} {it}")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
