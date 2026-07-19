#!/usr/bin/env python3
"""Deterministic seeded inputs for facebakery_make_quad (FaceBakery.makeBakedQuad -> int[28]).

Each record (one line) = 23 tokens. Floats are raw 32-bit hex; ints are decimal but always 0..5 so
the C candidate's uniform base-16 parse agrees:
  fx fy fz  tx ty tz        posFrom / posTo in model space [0,16] (getPositionsDiv16 divides by 16)
  facing(0-5)               EnumFacing of the face
  uvQuarter(0-3)            blockFaceUV.rotation/90
  uvs[4]                    model UV in [0,16]
  minU maxU minV maxV       sprite atlas UV bounds in [0,1]
  partPresent(0/1)          0 -> partRotation==null (applyFacing runs); 1 -> rotatePart runs, no applyFacing
  axis(0/1/2) angle ox oy oz rescale(0/1)   BlockPartRotation

Faces are realistic unit-cube boxes (full block + insets), all 6 facings, uvlock-style rotations,
and both the no-rotation path (exercises getFacingFromVertexData + applyFacing + fillNormal) and the
part-rotated path (0/22.5/45/90 deg, rescale on/off).
"""
import random
import struct
import sys

N = 20000
ANGLES = [0.0, 22.5, -22.5, 45.0, -45.0, 90.0, -90.0]


def fb(x):
    return "%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


def main():
    rnd = random.Random(31031)
    out = []
    for _ in range(N):
        facing = rnd.randrange(6)
        regime = rnd.randrange(3)
        if regime == 0:
            # full block
            fx, fy, fz, tx, ty, tz = 0.0, 0.0, 0.0, 16.0, 16.0, 16.0
        elif regime == 1:
            # inset box
            fx = rnd.uniform(0, 6); fy = rnd.uniform(0, 6); fz = rnd.uniform(0, 6)
            tx = rnd.uniform(10, 16); ty = rnd.uniform(10, 16); tz = rnd.uniform(10, 16)
        else:
            lo = lambda: rnd.uniform(0, 16)
            xs = sorted([lo(), lo()]); ys = sorted([lo(), lo()]); zs = sorted([lo(), lo()])
            fx, tx = xs; fy, ty = ys; fz, tz = zs

        uvQuarter = rnd.randrange(4)
        uvs = [rnd.uniform(0.0, 16.0) for _ in range(4)]
        u0 = rnd.uniform(0.0, 0.9); u1 = rnd.uniform(u0, 1.0)
        v0 = rnd.uniform(0.0, 0.9); v1 = rnd.uniform(v0, 1.0)

        if rnd.random() < 0.6:
            partPresent = 0; axis = 0; angle = 0.0; origin = [8.0, 8.0, 8.0]; rescale = 0
        else:
            partPresent = 1; axis = rnd.randrange(3); angle = rnd.choice(ANGLES)
            origin = [8.0, 8.0, 8.0] if rnd.random() < 0.6 else [rnd.uniform(0, 16) for _ in range(3)]
            rescale = rnd.randrange(2)

        toks = [fb(fx), fb(fy), fb(fz), fb(tx), fb(ty), fb(tz), str(facing), str(uvQuarter)]
        toks += [fb(u) for u in uvs]
        toks += [fb(u0), fb(u1), fb(v0), fb(v1)]
        toks += [str(partPresent), str(axis), fb(angle), fb(origin[0]), fb(origin[1]), fb(origin[2]), str(rescale)]
        out.append(" ".join(toks))
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
