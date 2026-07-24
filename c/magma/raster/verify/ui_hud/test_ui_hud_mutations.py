#!/usr/bin/env python3
"""Adversarial mutation regressions for fullscreen inside-block overlay gates.

Honest C frames for overlay_inside_stone / overlay_inside_grass are evaluated
under the fullscreen exact gate (thr=ceil(noise_max); noise_max==0 => thr 0).
Honest may be PASS (exact) or RESIDUAL (nonzero hard_px) — residual is not a
mutation leak; the ROI gate exits nonzero for residual. Capture FAIL fails
this suite. Each mutation below must NOT pass (verdict != PASS):

  erase90      - set 90% of painted C pixels to composition gray
  blank_to_one - blank frame + single correct pixel
  plus1_ch     - +1 on a single channel of one exact-match stable pixel
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
    STABLE_AB_THR,
    evaluate_state,
    load_ppm,
    load_rgb,
    painted_mask,
)

IDS = sorted(FULLSCREEN_REPLACE)
MUTATION_NAMES = (
    "erase90",
    "blank_to_one",
    "plus1_ch",
    "shift_x2",
    "shift_x4",
    "shift_y2",
    "shift_y4",
    "recolor",
    "extra_pixels",
)


def mutate(c0, name, ja, jb=None):
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
        ys, xs = np.where(painted & (dmax == 0))
        if len(ys) == 0:
            ys, xs = np.where(painted & (dmax <= 1))
        if len(ys) == 0:
            ys, xs = np.where(painted)
        assert len(ys) > 0, "no painted pixel for blank_to_one"
        # Mid-index so it is not an edge quirk.
        i = len(ys) // 2
        c[ys[i], xs[i]] = c0[ys[i], xs[i]]
        return c
    if name == "plus1_ch":
        # Single-channel +1 on one A/B-stable exact C==J pixel. With noise_max=0
        # the gate thr is 0, so this must produce hard_px>=1 and not PASS.
        c = c0.copy()
        if jb is None:
            raise ValueError("plus1_ch requires jb")
        ab = np.abs(ja.astype(np.int16) - jb.astype(np.int16)).mean(axis=2)
        dmax = np.abs(c0.astype(np.int16) - ja.astype(np.int16)).max(axis=2)
        stable_exact = (ab <= STABLE_AB_THR) & (dmax == 0)
        ys, xs = np.where(stable_exact)
        if len(ys) == 0:
            ys, xs = np.where(dmax == 0)
        if len(ys) == 0:
            painted = painted_mask(c0)
            ys, xs = np.where(painted)
        assert len(ys) > 0, "no pixel for plus1_ch"
        i = len(ys) // 2
        y, x = int(ys[i]), int(xs[i])
        for ch in range(3):
            if c[y, x, ch] < 255:
                c[y, x, ch] = c[y, x, ch] + 1
                break
        else:
            raise AssertionError("plus1_ch: pixel fully saturated")
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

        # Honest: capture must be OK. Exact PASS is ideal; RESIDUAL is honest
        # residual (reported) not a mutation leak. FAIL breaks the suite.
        r0 = evaluate_state(sid, ja, jb, c0)
        if r0["verdict"] == "FAIL":
            n_fail += 1
            print("%-22s %-14s %10.3f %8s %10s  %s" % (
                sid, "honest",
                r0["c_vs_j"] if r0["c_vs_j"] == r0["c_vs_j"] else -1.0,
                str(r0.get("hard_px")),
                r0["verdict"],
                "NEED_CAPTURE_OK"))
            print("  reason=%s noise=%.4f" % (r0.get("reason"), r0.get("noise")))
            continue

        exact = (r0["verdict"] == "PASS" and r0.get("hard_px") == 0)
        expect_h = "PASS" if exact else "RESIDUAL_OK"
        print("%-22s %-14s %10.3f %8s %10s  %s thr=%s maxch=%s" % (
            sid, "honest",
            r0["c_vs_j"] if r0["c_vs_j"] == r0["c_vs_j"] else -1.0,
            str(r0.get("hard_px")),
            r0["verdict"],
            expect_h,
            r0.get("hard_thr"),
            r0.get("max_diff")))
        if not exact:
            print("  honest residual: hard_px=%s bbox=%s noise_max=%s" % (
                r0.get("hard_px"), r0.get("residual_bbox"), r0.get("noise_max")))
            for loc in (r0.get("residual_locs") or [])[:6]:
                print("    @(%d,%d) maxch=%d C=%s J=%s" % (
                    loc["x"], loc["y"], loc["maxch"], loc["c"], loc["j"]))

        for mut in MUTATION_NAMES:
            cm = mutate(c0, mut, ja, jb)
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
