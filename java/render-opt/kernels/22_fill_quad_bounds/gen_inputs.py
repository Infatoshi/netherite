#!/usr/bin/env python3
"""Deterministic seeded inputs for fill_quad_bounds (MC 1.11.2 BlockModelRenderer.fillQuadBounds).

Each record (one line) = 30 tokens:
  - 28 hex ints = a BakedQuad's vertexData (4 verts x 7 ints). Lanes 0,1,2 of each vertex are the
    XYZ position floats (raw bits); lanes 3-6 (color/u/v/light) are arbitrary - the method never
    reads them, so they are filled with random ints to prove that.
  - 1 int  = face enum ordinal 0-5 (D-U-N-S-W-E)
  - 1 int  = isFullCube boolean (0/1)

Positions are realistic quads in the [0,1] unit cube. Several regimes are emitted so the bounds
flags exercise both branches: full-cube faces (coords exactly on 0/1 planes, flag0 likely true),
inset/shrunk quads, and quads flush to a single face plane. No NaN/Inf/-0.0 in position lanes so
Java Math.min/max matches plain C comparison bit-for-bit.
"""
import random
import struct
import sys

N = 20000


def fbits(x):
    return "%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


def ibits(x):
    return "%08x" % (x & 0xFFFFFFFF)


def main():
    rnd = random.Random(2026)
    out = []
    for _ in range(N):
        face = rnd.randrange(6)
        fullcube = rnd.randrange(2)
        regime = rnd.randrange(4)

        # Build 4 vertex positions in the unit cube under one of several regimes.
        verts = []
        if regime == 0:
            # full-cube-ish face: coords snap to {0.0, 1.0} planes
            for _ in range(4):
                verts.append([float(rnd.randrange(2)) for _ in range(3)])
        elif regime == 1:
            # inset quad: coords in a shrunk sub-box
            lo = rnd.uniform(0.0, 0.3)
            hi = rnd.uniform(0.7, 1.0)
            for _ in range(4):
                verts.append([rnd.uniform(lo, hi) for _ in range(3)])
        elif regime == 2:
            # quad flush to one axis plane (constant on one axis -> f==f3 etc.)
            axis = rnd.randrange(3)
            plane = float(rnd.randrange(2))
            for _ in range(4):
                v = [rnd.uniform(0.0, 1.0) for _ in range(3)]
                v[axis] = plane
                verts.append(v)
        else:
            # arbitrary quad anywhere in the cube
            for _ in range(4):
                verts.append([rnd.uniform(0.0, 1.0) for _ in range(3)])

        toks = []
        for v in verts:
            toks.append(fbits(v[0]))
            toks.append(fbits(v[1]))
            toks.append(fbits(v[2]))
            for _ in range(4):  # lanes 3-6: arbitrary (unread)
                toks.append(ibits(rnd.getrandbits(32)))
        toks.append(str(face))
        toks.append(str(fullcube))
        out.append(" ".join(toks))
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
