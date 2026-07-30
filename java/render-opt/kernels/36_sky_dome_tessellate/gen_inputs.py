#!/usr/bin/env python3
"""Deterministic inputs for 36_sky_dome_tessellate (MC 1.11.2 RenderGlobal.renderSky).

renderSky is called in exactly two configurations by the game:
  generateSky():  posY = 16.0F,  reverseX = false
  generateSky2(): posY = -16.0F, reverseX = true
Both real configs are emitted first. Extra random posY values (both reverseX) follow to exercise
the float cast of an arbitrary plane height; the grid geometry itself is fixed.

Each line = 2 tokens: posY (hex IEEE-754 float-bits) + reverseX (0/1).
"""
import random
import struct
import sys


def fb(f):
    return "%x" % struct.unpack("<I", struct.pack("<f", f))[0]


def main():
    rnd = random.Random(36036)
    out = []
    # the two real game configs
    out.append(f"{fb(16.0)} 0")
    out.append(f"{fb(-16.0)} 1")
    # extra coverage: arbitrary plane heights, both reverseX
    for _ in range(200):
        posY = rnd.uniform(-512.0, 512.0)
        rx = rnd.randrange(2)
        out.append(f"{fb(posY)} {rx}")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
