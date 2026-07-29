#!/usr/bin/env python3
"""Deterministic input stream for 27_translucent_sort.
Line 1: "n camXbits camYbits camZbits" (n quads; camera as hex IEEE-754 float-bits).
Then n lines, each 28 hex int32 = the quad's raw BLOCK-format vertex ints.

Only the 3 position floats of each vertex (ints +0/+1/+2 within each 7-int vertex) affect the
sort; the other lanes are filled with arbitrary bits. Coordinates are kept moderate so every
quad distance is a finite, non-negative normal float (no inf/NaN) -> plain ordering holds and the
stable tiebreak is the only special case. A block of quads is given IDENTICAL positions to force
exact-tie distances and actually exercise the stable (ascending-index) tiebreak."""
import random
import struct


def fbits(f):
    return format(struct.unpack("<I", struct.pack("<f", f))[0], "x")


def main():
    rnd = random.Random(2727)
    n = 4000
    cam = [rnd.uniform(-64, 64) for _ in range(3)]

    quads = []
    for q in range(n):
        # ~25% of quads: clone an earlier quad's centroid to create exact-tie distances
        if q > 0 and rnd.random() < 0.25:
            verts = quads[rnd.randint(0, q - 1)][0]
        else:
            cx = rnd.uniform(-256, 256)
            cy = rnd.uniform(-256, 256)
            cz = rnd.uniform(-256, 256)
            verts = []
            for _ in range(4):
                verts.append((round(cx + rnd.uniform(-2, 2), 3),
                              round(cy + rnd.uniform(-2, 2), 3),
                              round(cz + rnd.uniform(-2, 2), 3)))
        # build the 28-int row: position floats at +0/+1/+2 of each vertex, junk elsewhere
        row = []
        for v in range(4):
            px, py, pz = verts[v]
            row.append(fbits(px))
            row.append(fbits(py))
            row.append(fbits(pz))
            row.append(format(rnd.getrandbits(32), "x"))  # color
            row.append(fbits(rnd.uniform(0, 1)))          # u
            row.append(fbits(rnd.uniform(0, 1)))          # v
            row.append(format(rnd.getrandbits(32), "x"))  # lightmap
        quads.append((verts, row))

    out = [str(n) + " " + " ".join(fbits(c) for c in cam)]
    for _, row in quads:
        out.append(" ".join(row))
    print("\n".join(out))


if __name__ == "__main__":
    main()
