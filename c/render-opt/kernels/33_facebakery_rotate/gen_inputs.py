#!/usr/bin/env python3
"""Deterministic seeded inputs for facebakery_rotate (MC 1.11.2 FaceBakery rotatePart + rotateVertex).

Each record (one line) = 25 tokens, all floats as raw 32-bit hex, ints in decimal (0..3 so
decimal == hex, since the C candidate parses every token base-16):
  vx vy vz             vertex position (model space, [0,16])
  axis                 0=X 1=Y 2=Z 3=none(partRotation==null)
  angle                rotation angle in DEGREES (hex bits); realistic {0,22.5,-22.5,45,-45,90,-90}
  ox oy oz             rotation origin (model space, typically 8 = block centre)
  rescale              0/1
  m[16]                javax.vecmath row-major 4x4 matrix fed to rotateVertex's transform

Matrices: identity (X0_Y0 short-circuit) + plausible affine rotations built with the same
LWJGL-style rotate formula, plus a few with non-unit w-row to exercise the (|w-1|>1e-5) scale branch.
"""
import math
import random
import struct
import sys

N = 20000
ANGLES = [0.0, 22.5, -22.5, 45.0, -45.0, 90.0, -90.0]


def fb(x):
    return "%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


def identity():
    return [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def rot_matrix(rnd):
    # build a row-major rotation-ish matrix about a random axis/angle (float32 throughout)
    ang = rnd.choice([0, 90, 180, 270, 30, 45]) * math.pi / 180.0
    ax = rnd.choice([(1, 0, 0), (0, 1, 0), (0, 0, 1)])
    c = math.cos(ang)
    s = math.sin(ang)
    t = 1.0 - c
    x, y, z = ax
    m = [
        t * x * x + c,     t * x * y - s * z, t * x * z + s * y, rnd.uniform(-0.5, 0.5),
        t * x * y + s * z, t * y * y + c,     t * y * z - s * x, rnd.uniform(-0.5, 0.5),
        t * x * z - s * y, t * y * z + s * x, t * z * z + c,     rnd.uniform(-0.5, 0.5),
        0, 0, 0, 1.0,
    ]
    return m


def main():
    rnd = random.Random(33033)
    out = []
    for _ in range(N):
        pos = [rnd.uniform(0.0, 16.0) for _ in range(3)]
        regime = rnd.randrange(4)
        if regime == 0:
            axis = 3  # no part rotation
            angle = 0.0
            rescale = 0
        else:
            axis = rnd.randrange(3)
            angle = rnd.choice(ANGLES)
            rescale = rnd.randrange(2)
        origin = [8.0, 8.0, 8.0] if rnd.random() < 0.6 else [rnd.uniform(0, 16) for _ in range(3)]

        r = rnd.random()
        if r < 0.4:
            m = identity()
        elif r < 0.9:
            m = rot_matrix(rnd)
        else:
            m = rot_matrix(rnd)
            m[15] = rnd.uniform(1.2, 2.0)  # w != 1 -> exercise the scale branch

        toks = [fb(pos[0]), fb(pos[1]), fb(pos[2]), str(axis), fb(angle),
                fb(origin[0]), fb(origin[1]), fb(origin[2]), str(rescale)]
        toks += [fb(v) for v in m]
        out.append(" ".join(toks))
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
