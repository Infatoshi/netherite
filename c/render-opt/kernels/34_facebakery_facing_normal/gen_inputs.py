#!/usr/bin/env python3
"""Deterministic inputs for 34_facebakery_facing_normal (FaceBakery getFacingFromVertexData+applyFacing).

Each line = 29 tokens: 28 hex ints (BakedQuad vertexData, 4 verts x 7 ints) + targetFacing(0-5).

CRITICAL (per the normal-argmax robustness): the facing is chosen by argmax of 6 axis dot products
against the quad's normal. Random int[28] risks a near-tie that flips the chosen facing between the
golden's inlined Vector3f and any other impl. So we emit ONLY structured axis-aligned planar quads:
4 coplanar rectangle corners on one cube face (coords in [0,1], div16-realistic). That guarantees one
dot ~ +-1 and the rest ~0 -> the chosen facing is unambiguous regardless of last-bit normal noise.

Lanes 0,1,2 of each vertex = XYZ position (raw float bits). Lanes 3-6 (color/u/v/light) are random:
the method reads only positions for the normal; applyFacing copies lanes 4,5 (u,v) on an
epsilonEquals position match, so random distinct UVs prove that copy path. targetFacing is random
0-5 (covers target==chosen and target!=chosen for full applyFacing reorder coverage).
"""
import random
import struct
import sys

N = 30000


def fbits(x):
    return "%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


def ibits(x):
    return "%08x" % (x & 0xFFFFFFFF)


def face_corners(rnd, axis, pv, lo, hi):
    """4 rectangle corners on the plane (free-axis order CCW): (lo,lo),(hi,lo),(hi,hi),(lo,hi)."""
    pairs = [(lo, lo), (hi, lo), (hi, hi), (lo, hi)]
    free = [a for a in range(3) if a != axis]
    corners = []
    for (u, v) in pairs:
        c = [0.0, 0.0, 0.0]
        c[axis] = pv
        c[free[0]] = u
        c[free[1]] = v
        corners.append(c)
    return corners


def main():
    rnd = random.Random(34034)
    out = []
    for _ in range(N):
        axis = rnd.randrange(3)
        pv = rnd.choice([0.0, 1.0, rnd.uniform(0.0, 1.0)])
        lo = rnd.uniform(0.0, 0.4)
        hi = rnd.uniform(0.6, 1.0)
        corners = face_corners(rnd, axis, pv, lo, hi)
        # optionally reverse winding to flip the normal sign (still unambiguous facing)
        if rnd.random() < 0.5:
            corners = corners[::-1]

        toks = []
        for c in corners:
            toks.append(fbits(c[0]))
            toks.append(fbits(c[1]))
            toks.append(fbits(c[2]))
            for _ in range(4):  # lanes 3-6 arbitrary (3=color,4=u,5=v,6=light)
                toks.append(ibits(rnd.getrandbits(32)))
        toks.append(str(rnd.randrange(6)))  # targetFacing
        out.append(" ".join(toks))

    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
