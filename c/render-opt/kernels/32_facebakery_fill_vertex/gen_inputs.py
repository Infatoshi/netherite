#!/usr/bin/env python3
"""Deterministic seeded inputs for facebakery_fill_vertex (FaceBakery.fillVertexData/storeVertexData).

Each record (one line) = 24 tokens. Floats are raw 32-bit hex; ints are decimal but always 0..5 so
the C candidate's uniform base-16 parse agrees (base16 == base10 for 0..5):
  vertexIndex(0-3) facing(0-5) shade(0/1)
  bounds[6]                position bounds float[6] (EnumFacing-indexed, ~[0,1] unit cube)
  uvQuarter(0-3)           blockFaceUV.rotation/90
  uvs[4]                   blockFaceUV.uvs, model UV in [0,16]
  minU maxU minV maxV      sprite atlas UV bounds in [0,1]
  axis(0/1/2/3) angle ox oy oz rescale   BlockPartRotation (3=none)

Covers all 6 facings x 4 vertices, uvlock-style rotations, and both partRotation present/absent
with realistic 0/22.5/45/90 deg angles to exercise rotatePart's rescale branches.
"""
import random
import struct
import sys

N = 20000
ANGLES = [0.0, 22.5, -22.5, 45.0, -45.0, 90.0, -90.0]


def fb(x):
    return "%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


def main():
    rnd = random.Random(32032)
    out = []
    for _ in range(N):
        vertexIndex = rnd.randrange(4)
        facing = rnd.randrange(6)
        shade = rnd.randrange(2)
        # bounds: realistic div-16 positions in [0,1], occasionally inset
        if rnd.random() < 0.5:
            bounds = [float(rnd.randrange(2)) for _ in range(6)]
        else:
            bounds = [rnd.uniform(0.0, 1.0) for _ in range(6)]
        uvQuarter = rnd.randrange(4)
        uvs = [rnd.uniform(0.0, 16.0) for _ in range(4)]
        # sprite atlas bounds: minU<maxU, minV<maxV in [0,1]
        u0 = rnd.uniform(0.0, 0.9); u1 = rnd.uniform(u0, 1.0)
        v0 = rnd.uniform(0.0, 0.9); v1 = rnd.uniform(v0, 1.0)

        if rnd.random() < 0.5:
            axis = 3; angle = 0.0; rescale = 0
            origin = [8.0, 8.0, 8.0]
        else:
            axis = rnd.randrange(3); angle = rnd.choice(ANGLES); rescale = rnd.randrange(2)
            origin = [0.5, 0.5, 0.5] if rnd.random() < 0.6 else [rnd.uniform(0, 1) for _ in range(3)]

        toks = [str(vertexIndex), str(facing), str(shade)]
        toks += [fb(b) for b in bounds]
        toks.append(str(uvQuarter))
        toks += [fb(u) for u in uvs]
        toks += [fb(u0), fb(u1), fb(v0), fb(v1)]
        toks += [str(axis), fb(angle), fb(origin[0]), fb(origin[1]), fb(origin[2]), str(rescale)]
        out.append(" ".join(toks))
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
