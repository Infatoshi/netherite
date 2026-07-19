#!/usr/bin/env python3
"""Deterministic seeded inputs for the AABB-in-frustum test kernel.

Each record = 24 raw-float-bits hex ints (6 planes x 4 coeffs) then 6 raw-double-bits hex longs
= AABB (minX, minY, minZ, maxX, maxY, maxZ). Planes get random (normalized) normals + random
offset d; boxes get random center + half-extent over a range comparable to d, deliberately mixed
to produce inside / outside / straddling cases. Raw-bits emission keeps Java and C inputs identical.
"""
import math
import random
import struct
import sys

N = 40000


def fbits(x):
    return "%08x" % struct.unpack("<I", struct.pack("<f", x))[0]


def dbits(x):
    return "%016x" % struct.unpack("<Q", struct.pack("<d", x))[0]


def main():
    rnd = random.Random(770077)
    out = []
    for _ in range(N):
        toks = []
        for _p in range(6):
            # random normal direction (gaussian -> uniform on sphere), normalized
            nx, ny, nz = rnd.gauss(0, 1), rnd.gauss(0, 1), rnd.gauss(0, 1)
            mag = math.sqrt(nx * nx + ny * ny + nz * nz)
            if mag < 1e-9:
                nx, ny, nz, mag = 1.0, 0.0, 0.0, 1.0
            nx, ny, nz = nx / mag, ny / mag, nz / mag
            d = rnd.uniform(-60.0, 60.0)
            toks += [fbits(nx), fbits(ny), fbits(nz), fbits(d)]
        # box: center near origin, varying size; range chosen vs plane d for a mix of results
        cx = rnd.uniform(-120.0, 120.0)
        cy = rnd.uniform(-120.0, 120.0)
        cz = rnd.uniform(-120.0, 120.0)
        h = rnd.uniform(0.5, 40.0)
        minx, miny, minz = cx - h, cy - h, cz - h
        maxx, maxy, maxz = cx + h, cy + h, cz + h
        toks += [dbits(minx), dbits(miny), dbits(minz), dbits(maxx), dbits(maxy), dbits(maxz)]
        out.append(" ".join(toks))
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
