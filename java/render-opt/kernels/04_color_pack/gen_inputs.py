#!/usr/bin/env python3
"""Deterministic input stream for 04_color_pack: "r g b colorA colorB" per line.
r/g/b are color components (mostly 0..255 but also out-of-range to probe wrap),
colorA/colorB are full 32-bit packed ARGB ints."""
import random

INT_MIN = -(2**31)
INT_MAX = 2**31 - 1

def wrap32(v):
    return ((v - INT_MIN) % (2**32)) + INT_MIN

def main():
    out = []
    # edge cases for rgb components and packed colors
    edges = [0, 1, 127, 128, 255, 256, -1, -256, 65535, 16777215, INT_MIN, INT_MAX]
    for r in (0, 255, 128, 256, -1):
        for g in (0, 255, 128):
            for b in (0, 255, 1):
                ca = 0xFF000000 - (1 << 32) if False else wrap32(0xFFABCDEF)
                out.append(f"{r} {g} {b} {ca} {wrap32(0x80FF8040)}")
    # explicit packed-color edge pairs (full alpha, zero, mid)
    packed = [0x00000000, 0xFFFFFFFF, 0xFF000000, 0x00FFFFFF,
              0xFF7F3F1F, 0x12345678, 0xDEADBEEF, 0x00808080]
    for ca in packed:
        for cb in packed:
            out.append(f"200 100 50 {wrap32(ca)} {wrap32(cb)}")

    rnd = random.Random(404)
    for _ in range(90000):
        r = rnd.randint(0, 255)
        g = rnd.randint(0, 255)
        b = rnd.randint(0, 255)
        ca = rnd.randint(INT_MIN, INT_MAX)
        cb = rnd.randint(INT_MIN, INT_MAX)
        out.append(f"{r} {g} {b} {ca} {cb}")
    # some out-of-range components to exercise rgb shift/wrap
    for _ in range(10000):
        r = rnd.randint(-1000, 100000)
        g = rnd.randint(-1000, 100000)
        b = rnd.randint(-1000, 100000)
        ca = rnd.randint(INT_MIN, INT_MAX)
        cb = rnd.randint(INT_MIN, INT_MAX)
        out.append(f"{r} {g} {b} {ca} {cb}")

    print("\n".join(out))

if __name__ == "__main__":
    main()
