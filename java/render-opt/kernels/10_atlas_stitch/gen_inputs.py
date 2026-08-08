#!/usr/bin/env python3
"""Deterministic sprite sequence for 10_atlas_stitch.
Line 1: "maxWidth maxHeight count". Next `count` lines: "width height name".

Sizes are powers-of-two-ish and small mixed dims so the bin-packer exercises every path:
 - square sprites (no rotation, exact-fit + subslot recursion),
 - non-square sprites (trigger Holder rotation init + allocateSlot rotate retries),
 - duplicate dimensions (subslot reuse + name tiebreak in the comparator),
 - a spread of sizes that forces expansion in BOTH width and height.
maxWidth/maxHeight are generous so every sprite fits (an unfittable sprite makes the golden throw).
Names are zero-padded, equal-length, unique (so strcmp == String.compareTo and the sort order is
uniquely determined on both sides)."""
import random


def main():
    rnd = random.Random(1010)
    # MC tile dims are powers of two; mix square and non-square.
    dims = [4, 8, 8, 16, 16, 16, 32, 32, 64, 128]
    sprites = []
    n = 600
    for i in range(n):
        w = rnd.choice(dims)
        h = rnd.choice(dims)
        sprites.append((w, h, f"spr_{i:04d}"))

    # generous atlas bound so everything fits
    maxW = maxH = 1 << 16

    lines = [f"{maxW} {maxH} {len(sprites)}"]
    for w, h, name in sprites:
        lines.append(f"{w} {h} {name}")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
