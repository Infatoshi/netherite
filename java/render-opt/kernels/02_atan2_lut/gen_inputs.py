#!/usr/bin/env python3
"""Deterministic input stream for 02_atan2_lut: "y x" (two doubles) per line.
Note atan2 signature is atan2(y, x). Doubles are printed via repr so Java parseDouble and
C strtod produce identical bits. Covers all sign quadrants, zeros, equal magnitudes, the
y>x vs y<=x branch, and a wide log-uniform magnitude range. NaN/inf are excluded (the NaN
path is handled but not exercised here)."""
import math
import random

def emit(y, x, out):
    out.append("%s %s" % (repr(float(y)), repr(float(x))))

def main():
    out = []
    specials = [0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0,
                1e-12, -1e-12, 1e12, -1e12, 3.0, 1.0000001]
    for y in specials:
        for x in specials:
            emit(y, x, out)
    # exact diagonal / axis cases
    for v in (1.0, 2.0, 5.0, 0.0):
        emit(v, v, out)
        emit(-v, v, out)
        emit(v, -v, out)
        emit(0.0, v, out)
        emit(v, 0.0, out)

    rnd = random.Random(2)
    def rmag():
        m = math.exp(rnd.uniform(math.log(1e-9), math.log(1e9)))
        return m if rnd.random() < 0.5 else -m

    for _ in range(70000):
        emit(rmag(), rmag(), out)
    # near-equal magnitudes to stress the swap branch and LUT index boundaries
    for _ in range(20000):
        x = rmag()
        y = x * rnd.uniform(0.95, 1.05)
        emit(y, x, out)

    print("\n".join(out))

if __name__ == "__main__":
    main()
