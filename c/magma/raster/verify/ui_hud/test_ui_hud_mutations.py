#!/usr/bin/env python3
"""Adversarial mutation regressions for fullscreen inside-block overlay gates.

Honest C frames for overlay_inside_stone / overlay_inside_grass must PASS the
fullscreen hard_px gate. Each mutation below must NOT pass:

  erase90      - set 90% of painted C pixels to composition gray
  blank_to_one - blank frame + single correct pixel
  shift_x2/x4  - global horizontal shift 2 / 4 px
  shift_y2/y4  - global vertical shift 2 / 4 px
  recolor      - add +20 per channel
  extra_pixels - 50 sparse bright wrong pixels

Portal / fire / underwater stay soft (not exercised here).
"""
from __future__ import print_function

import argparse
import os
import sys

import numpy as np

# Same directory import
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_ui_hud_oracle import (  # noqa: E402
    FULLSCREEN_REPLACE,
    GRAY,
    evaluate_state,
    load_ppm,
    load_rgb,
    painted_mask,
)

IDS = sorted(FULLSCREEN_REPLACE)
MUTATION_NAMES = (
    "erase90",
    "blank_to_one",
    "shift_x2",
    "shift_x4",
    "shift_y2",
    "shift_y4",
    "recolor",
    "extra_pixels",
)


def mutate(c0, name, ja):
    """Return a mutated C frame (int16 HxWx3)."""
    c0 = c0.astype(np.int16, copy=False)
    h, w = c0.shape[:2]
    if name == "erase90":
        c = c0.copy()
        painted = painted_mask(c)
        ys, xs = np.where(painted)
        n = len(ys)
        if n == 0:
            return c
        kill = np.random.RandomState(0).choice(n, size=int(0.9 * n), replace=False)
        c[ys[kill], xs[kill]] = GRAY
        return c
    if name == "blank_to_one":
        c = np.full_like(c0, GRAY)
        painted = painted_mask(c0)
        dmax = np.abs(c0.astype(np.int16) - ja.astype(np.int16)).max(axis=2)
        ys, xs = np.where(painted & (dmax <= 1))
        if len(ys) == 0:
            ys, xs = np.where(painted)
        assert len(ys) > 0, "no painted pixel for blank_to_one"
        # Mid-index so it is not an edge quirk.
        i = len(ys) // 2
        c[ys[i], xs[i]] = c0[ys[i], xs[i]]
        return c
    if name == "shift_x2":
        c = np.full_like(c0, GRAY)
        c[:, 2:] = c0[:, :-2]
        return c
    if name == "shift_x4":
        c = np.full_like(c0, GRAY)
        c[:, 4:] = c0[:, :-4]
        return c
    if name == "shift_y2":
        c = np.full_like(c0, GRAY)
        c[2:, :] = c0[:-2, :]
        return c
    if name == "shift_y4":
        c = np.full_like(c0, GRAY)
        c[4:, :] = c0[:-4, :]
        return c
    if name == "recolor":
        return np.clip(c0.astype(np.int16) + 20, 0, 255).astype(np.int16)
    if name == "extra_pixels":
        c = c0.copy()
        rng = np.random.RandomState(1)
        for _ in range(50):
            y = int(rng.randint(0, h))
            x = int(rng.randint(0, w))
            c[y, x] = [255, 0, 0]
        return c
    raise ValueError("unknown mutation: " + name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--cframes", required=True)
    args = ap.parse_args()

    n_fail = 0
    print("ui_hud mutation regressions (fullscreen inside-block):")
    print("%-22s %-14s %10s %8s %10s  %s" % (
        "state", "mutation", "C-vs-J", "hard_px", "verdict", "expect"))

    for sid in IDS:
        ja_p = os.path.join(args.goldens, "%s_a.png" % sid)
        jb_p = os.path.join(args.goldens, "%s_b.png" % sid)
        c_p = os.path.join(args.cframes, "c_%s.ppm" % sid)
        if not (os.path.isfile(ja_p) and os.path.isfile(jb_p)
                and os.path.isfile(c_p)):
            print("%-22s %-14s  MISSING assets" % (sid, "-"))
            n_fail += 1
            continue

        ja = load_rgb(ja_p)
        jb = load_rgb(jb_p)
        c0 = load_ppm(c_p)

        # Honest must PASS (gate not vacuously rejecting everything).
        r0 = evaluate_state(sid, ja, jb, c0)
        ok_honest = r0["verdict"] == "PASS" and (r0.get("hard_px") == 0)
        print("%-22s %-14s %10.3f %8s %10s  %s" % (
            sid, "honest",
            r0["c_vs_j"] if r0["c_vs_j"] == r0["c_vs_j"] else -1.0,
            str(r0.get("hard_px")),
            r0["verdict"],
            "PASS" if ok_honest else "NEED_PASS"))
        if not ok_honest:
            n_fail += 1
            print("  reason=%s noise=%.4f max_diff=%s stable_frac=%s" % (
                r0.get("reason"), r0.get("noise"),
                r0.get("max_diff"), r0.get("stable_frac")))

        for mut in MUTATION_NAMES:
            cm = mutate(c0, mut, ja)
            r = evaluate_state(sid, ja, jb, cm)
            # Mutation must not claim parity.
            rejected = r["verdict"] != "PASS"
            expect = "REJECT"
            status = "ok" if rejected else "LEAK"
            if not rejected:
                n_fail += 1
            print("%-22s %-14s %10.3f %8s %10s  %s %s" % (
                sid, mut,
                r["c_vs_j"] if r["c_vs_j"] == r["c_vs_j"] else -1.0,
                str(r.get("hard_px")),
                r["verdict"],
                expect, status))

    if n_fail:
        print("ui_hud mutations: FAIL (%d)" % n_fail)
        return 1
    print("ui_hud mutations: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
