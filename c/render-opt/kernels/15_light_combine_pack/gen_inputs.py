#!/usr/bin/env python3
"""Deterministic input stream for 15_light_combine_pack.
Each line: "skyLight blockLight override" (signed int32, decimal).
Covers the real domain (0..15 light levels), the override branch (override > blockLight and not),
plus full-range int32 randoms to stress the left-shift wrap / sign handling."""
import random

INT_MIN = -(2**31)
INT_MAX = 2**31 - 1


def main():
    out = []

    # full real domain: light levels 0..15 x 0..15 x override 0..15
    for sky in range(16):
        for block in range(16):
            for override in range(16):
                out.append(f"{sky} {block} {override}")

    # explicit override-branch edges
    out += ["15 0 15", "0 15 0", "5 3 10", "5 10 3", "0 0 0", "15 15 15"]

    rnd = random.Random(1500)
    # randoms within a slightly wider plausible range
    for _ in range(20000):
        out.append(f"{rnd.randint(0, 31)} {rnd.randint(0, 31)} {rnd.randint(0, 31)}")
    # full-range int32 stress (wrap / sign on the shifts)
    for _ in range(20000):
        out.append(f"{rnd.randint(INT_MIN, INT_MAX)} {rnd.randint(INT_MIN, INT_MAX)} {rnd.randint(INT_MIN, INT_MAX)}")

    print("\n".join(out))


if __name__ == "__main__":
    main()
