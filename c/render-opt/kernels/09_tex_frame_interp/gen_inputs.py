#!/usr/bin/env python3
"""Deterministic inputs for 09_tex_frame_interp.
Each record: blend factor d0 in [0,1] (plain decimal), then two random ARGB ints.
Output format (whitespace separated, one record per line): d0 j1 k1
"""
import random


def main():
    rnd = random.Random(909)
    n = 40000
    out = []
    for _ in range(n):
        d0 = rnd.random()  # in [0,1), matches the (0,1] blend range of the game
        # signed 32-bit ARGB ints; include fully-opaque and varied colors
        j1 = rnd.randint(-2147483648, 2147483647)
        k1 = rnd.randint(-2147483648, 2147483647)
        out.append(f"{d0:.17g} {j1} {k1}")
    print("\n".join(out))


if __name__ == "__main__":
    main()
