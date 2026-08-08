#!/usr/bin/env python3
"""Classify sparse cell-dump diffs: fluid / mushroom / other.

Inputs: two sorted "x,y,z,state" files (blaze reference, magma wrapper).
Emits a short text census + optional per-cell class lines on stderr if --verbose.

PB codes (blaze/core/populate.h PopBlock):
  fluid: WATER=2 LAVA=11 FLOWING_LAVA=12 FLOWING_WATER=13
  mushroom: BROWN=42 RED=43 BROWN_SHROOM_BLOCK=79 RED_SHROOM_BLOCK=80
"""
from __future__ import annotations

import argparse
import sys
from collections import Counter

FLUID = {2, 11, 12, 13}
MUSHROOM = {42, 43, 79, 80}


def load(path: str) -> dict[tuple[int, int, int], int]:
    m: dict[tuple[int, int, int], int] = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            xs, ys, zs, ss = line.split(",")
            m[(int(xs), int(ys), int(zs))] = int(ss)
    return m


def classify(a: int, b: int) -> str:
    """Classify a differing cell by either side's state (prefer fluid > mush > other)."""
    if a in FLUID or b in FLUID:
        return "fluid"
    if a in MUSHROOM or b in MUSHROOM:
        return "mushroom"
    return "other"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("blaze", help="blaze sparse dump")
    ap.add_argument("magma", help="magma sparse dump")
    ap.add_argument("--label", default="", help="seed / tag for the summary line")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    blaze = load(args.blaze)
    magma = load(args.magma)
    keys = set(blaze) | set(magma)

    only_b = only_m = both_diff = 0
    cls = Counter()
    state_pairs: Counter[tuple[int, int]] = Counter()

    for k in keys:
        ba = blaze.get(k, 0)
        ma = magma.get(k, 0)
        if ba == ma:
            continue
        if ba and not ma:
            only_b += 1
        elif ma and not ba:
            only_m += 1
        else:
            both_diff += 1
        c = classify(ba, ma)
        cls[c] += 1
        state_pairs[(ba, ma)] += 1
        if args.verbose:
            x, y, z = k
            print(f"{c}\t{x},{y},{z}\t{ba}->{ma}", file=sys.stderr)

    n_diff = only_b + only_m + both_diff
    tag = args.label or "diff"
    print(f"SEED_SUMMARY {tag}")
    print(f"  blaze_cells={len(blaze)} magma_cells={len(magma)}")
    print(f"  diff_cells={n_diff}  only_blaze={only_b} only_magma={only_m} both_side={both_diff}")
    print(
        f"  class fluid={cls['fluid']} mushroom={cls['mushroom']} other={cls['other']}"
    )
    # top state-pair transitions for the report
    top = state_pairs.most_common(12)
    if top:
        print("  top_pairs (blaze->magma : count):")
        for (ba, ma), n in top:
            print(f"    {ba}->{ma} : {n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
