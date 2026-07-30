#!/usr/bin/env python3
"""Deterministic input stream for 01_sin_cos_table: one float angle per line.
Values are rounded to float precision and printed with enough digits to round-trip
identically under both Java Float.parseFloat and C strtof. Covers small angles,
multiples of pi, negatives, and a wide range (incl. large angles that stress the
float->int index cast)."""
import math
import random
import struct

def as_float(x):
    return struct.unpack("f", struct.pack("f", x))[0]

def emit(x, out):
    out.append("%.9g" % as_float(x))

def main():
    out = []
    edges = [0.0, -0.0, 1.0, -1.0,
             math.pi, -math.pi, math.pi / 2, -math.pi / 2,
             2 * math.pi, 4 * math.pi, 0.5, 0.25, 100.0, -100.0,
             1e4, -1e4, 1e5, 1e6]
    for e in edges:
        emit(e, out)

    rnd = random.Random(1)
    # dense sweep over [-2pi, 2pi]
    for _ in range(40000):
        emit(rnd.uniform(-2 * math.pi, 2 * math.pi), out)
    # mid range angles
    for _ in range(40000):
        emit(rnd.uniform(-1000.0, 1000.0), out)
    # wide range that pushes value*10430.378 toward int limits but stays in range
    for _ in range(20000):
        emit(rnd.uniform(-2.0e5, 2.0e5), out)

    print("\n".join(out))

if __name__ == "__main__":
    main()
