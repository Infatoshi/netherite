#!/usr/bin/env python3
"""Deterministic inputs for 07_mipmap_blend_gamma.
Each record: 4 ARGB ints + a hasTransparency flag (0/1).
Output format (one record per line): c0 c1 c2 c3 ht

Mix of cases to exercise both branches and edges:
  - opaque + non-opaque + fully-transparent (alpha 0) pixels,
  - the transparency branch (ht=1) where alpha-0 pixels are skipped and the
    all-skipped -> pow(0,..)=0 -> i2<96 -> 0 edge can fire,
  - the non-transparency branch (ht=0).
"""
import random


def rand_argb(rnd):
    # 50% fully opaque, 25% fully transparent, 25% random alpha
    r = rnd.random()
    if r < 0.5:
        a = 255
    elif r < 0.75:
        a = 0
    else:
        a = rnd.randint(0, 255)
    rr = rnd.randint(0, 255)
    g = rnd.randint(0, 255)
    b = rnd.randint(0, 255)
    val = (a << 24) | (rr << 16) | (g << 8) | b
    # to signed 32-bit
    if val >= 0x80000000:
        val -= 0x100000000
    return val


def main():
    rnd = random.Random(707)
    n = 40000
    out = []
    for _ in range(n):
        c0 = rand_argb(rnd)
        c1 = rand_argb(rnd)
        c2 = rand_argb(rnd)
        c3 = rand_argb(rnd)
        ht = rnd.randint(0, 1)
        out.append(f"{c0} {c1} {c2} {c3} {ht}")
    print("\n".join(out))


if __name__ == "__main__":
    main()
