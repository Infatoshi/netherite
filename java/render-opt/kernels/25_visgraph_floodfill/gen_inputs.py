#!/usr/bin/env python3
"""Deterministic seeded inputs for visgraph_floodfill (MC 1.11.2 VisGraph.computeVisibility).

Each record (one line) = 65 tokens:
  - token 0      = count of set (opaque) bits (== popcount of the bitset, derived below)
  - tokens 1..64 = 64 hex words (16 hex digits each) of the 4096-bit opaque set; bit i of cell index
    i lives in word[i>>6] at position (i&63). Cell index layout (matching VisGraph) is
    index = x<<0 | y<<8 | z<<4, but for generating random patterns the index meaning is irrelevant -
    we just pick random cells in [0,4096).

Densities are spread so all three branches of computeVisibility() are exercised and the flood-fill
explores real connectivity:
  - very sparse  (< 256 opaque)   -> triggers the `4096 - empty < 256` all-visible branch
  - light/medium (~5-40%)         -> rich multi-region connectivity through the flood fill
  - ~50%                          -> the hard middle
  - dense        (~80-99%)        -> narrow channels, many tiny pockets
  - full         (4096 opaque)    -> triggers the `empty == 0` all-hidden branch
"""
import random
import sys

N = 4000
TOTAL = 4096


def emit(opaque_set, out):
    words = [0] * 64
    for idx in opaque_set:
        words[idx >> 6] |= (1 << (idx & 63))
    toks = [str(len(opaque_set))]
    toks.extend("%016x" % wd for wd in words)
    out.append(" ".join(toks))


def main():
    rnd = random.Random(31337)
    out = []
    for r in range(N):
        regime = r % 5
        if regime == 0:
            # very sparse: 0..255 set bits -> all-visible branch
            k = rnd.randrange(0, 256)
            s = set(rnd.sample(range(TOTAL), k))
        elif regime == 1:
            # light/medium density
            p = rnd.uniform(0.05, 0.40)
            s = {i for i in range(TOTAL) if rnd.random() < p}
            if 4096 - len(s) < 256:  # keep it out of the all-visible branch
                s = set(list(s)[: 4096 - 256])
        elif regime == 2:
            # ~50%
            s = {i for i in range(TOTAL) if rnd.random() < 0.5}
        elif regime == 3:
            # dense: 80..99% (but never full)
            p = rnd.uniform(0.80, 0.99)
            s = {i for i in range(TOTAL) if rnd.random() < p}
            if len(s) >= TOTAL:
                s.discard(rnd.randrange(TOTAL))
        else:
            # full (empty == 0 branch)
            s = set(range(TOTAL))
        emit(s, out)
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
