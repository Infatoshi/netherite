#!/usr/bin/env python3
"""Deterministic input stream for 03_pow2_log2: one signed 32-bit int per line.
Covers edge cases (0, 1, all powers of two, off-by-one around powers, negatives,
INT_MIN/INT_MAX) plus a wide seeded random spread across the full int32 range."""
import random

INT_MIN = -(2**31)
INT_MAX = 2**31 - 1

def emit(vals, out):
    for v in vals:
        # wrap into signed 32-bit
        v = ((v - INT_MIN) % (2**32)) + INT_MIN
        out.append(str(v))

def main():
    out = []
    edge = [0, 1, 2, 3, INT_MIN, INT_MAX, -1, -2]
    # all powers of two and their neighbours
    for p in range(0, 31):
        v = 1 << p
        edge += [v - 1, v, v + 1]
    for p in range(0, 32):
        edge += [-(1 << p)]
    emit(edge, out)

    rnd = random.Random(101)
    for _ in range(80000):
        out.append(str(rnd.randint(INT_MIN, INT_MAX)))
    # extra clustering in the small positive range (typical real usage)
    for _ in range(20000):
        out.append(str(rnd.randint(0, 1 << 20)))

    print("\n".join(out))

if __name__ == "__main__":
    main()
