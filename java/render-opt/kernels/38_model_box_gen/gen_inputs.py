#!/usr/bin/env python3
"""Deterministic inputs for 38_model_box_gen (MC 1.11.2 ModelBox ctor + TexturedQuad).

Each line = 12 tokens:
  texU texV  x(fhex) y(fhex) z(fhex)  dx dy dz  delta(fhex)  mirror(0/1)  texW(fhex) texH(fhex)

Coverage mirrors how entity models call `new ModelBox(...)`: integer texture offsets, float corner
origin, integer box extents (dx,dy,dz), a small `delta` inflate, mirror on/off, and the model's
texture sheet size (commonly 64x64, also 32/128). Degenerate extents (a 0-thickness axis) are
included to drive the Vec3d.normalize() `d0 < 1e-4 -> ZERO` branch in the per-quad normal.
"""
import random
import struct
import sys


def fb(f):
    return "%x" % struct.unpack("<I", struct.pack("<f", f))[0]


def main():
    rnd = random.Random(38038)
    out = []
    sheets = [16.0, 32.0, 64.0, 128.0]

    def emit(texU, texV, x, y, z, dx, dy, dz, delta, mirror, tw, th):
        out.append(f"{texU} {texV} {fb(x)} {fb(y)} {fb(z)} {dx} {dy} {dz} "
                   f"{fb(delta)} {mirror} {fb(tw)} {fb(th)}")

    # explicit edge cases
    emit(0, 0, 0.0, 0.0, 0.0, 8, 8, 8, 0.0, 0, 64.0, 64.0)          # unit-ish cube, no delta, no mirror
    emit(0, 0, 0.0, 0.0, 0.0, 8, 8, 8, 0.0, 1, 64.0, 64.0)          # mirrored
    emit(16, 16, -4.0, 12.0, -2.0, 4, 12, 4, 0.25, 0, 64.0, 32.0)   # typical limb
    emit(0, 0, 0.0, 0.0, 0.0, 0, 8, 8, 0.0, 0, 64.0, 64.0)          # dx=0 -> flat box (ZERO normal branch)
    emit(0, 0, 0.0, 0.0, 0.0, 8, 0, 8, 0.0, 1, 64.0, 64.0)          # dy=0, mirrored
    emit(0, 0, 0.0, 0.0, 0.0, 8, 8, 0, 0.0, 0, 64.0, 64.0)          # dz=0

    for _ in range(8000):
        texU = rnd.randrange(0, 96)
        texV = rnd.randrange(0, 96)
        x = rnd.uniform(-16.0, 16.0)
        y = rnd.uniform(-16.0, 16.0)
        z = rnd.uniform(-16.0, 16.0)
        dx = rnd.randrange(0, 17)
        dy = rnd.randrange(0, 17)
        dz = rnd.randrange(0, 17)
        delta = rnd.choice([0.0, rnd.uniform(0.0, 0.75)])
        mirror = rnd.randrange(2)
        sheet = rnd.choice(sheets)
        emit(texU, texV, x, y, z, dx, dy, dz, delta, mirror, sheet, sheet)

    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
