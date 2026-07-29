#!/usr/bin/env python3
"""Adversarial mutation regressions for fullscreen hard_px + hand exact gates.

Honest C frames for overlay_inside_stone / overlay_inside_grass /
overlay_underwater / overlay_portal_050 are evaluated under the fullscreen
exact gate (hard_thr always 0; PASS only if noise_max==0 AND hard_px==0).
Honest may be PASS (bit-exact), RESIDUAL (C residual with bit-exact A/B), or
CAPTURE_BLOCKED (A/B maxch residual > 0 — no C may PASS). Capture FAIL fails
this suite. Each mutation below must NOT pass (verdict != PASS):

  erase90      - set 90% of painted C pixels to composition gray
  blank_to_one - blank frame + single correct pixel
  plus1_ch     - +1 on a single channel of one exact-match stable pixel
  shift_x2/x4  - global horizontal shift 2 / 4 px
  shift_y2/y4  - global vertical shift 2 / 4 px
  recolor      - add +20 per channel
  extra_pixels - 50 sparse bright wrong pixels
  extra_black / extra_midgray / extra_midchroma / extra_bright
               - 50 sparse extras at black, mid-gray, mid-chroma, bright

Hand viewmodels (hand_bow_pull20 / hand_eat_mid / hand_block_shield) use
Java∪C subject ownership, hard_thr=0, A/B exact for PASS. Synthetic control
must PASS; mutations must reject: missing Java-only silhouette, C-extra,
+1 channel, shift, recolor.

Explicit non-vacuous controls (always run):
  synth_zero_noise_pass - bit-exact A=B=C synthetic can PASS
  portal_real_ab_blocked - real portal A/B: C in {Java_a, Java_b, mid, Ja+1}
                           must never PASS (CAPTURE_BLOCKED)
  portal_java_a_plus1_blocked - C=Java_a+1 blocked on real portal goldens
  hand_synth_exact_pass + hand mutations from that control

Fire stays soft (not exercised here). Underwater is hard full-ROI honest residual (~4.97/ch) — mutations must not claim PASS.
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
    H,
    HAND_HARD,
    STABLE_AB_THR,
    W,
    evaluate_fullscreen_replace,
    evaluate_hand_exact,
    evaluate_state,
    load_ppm,
    load_rgb,
    painted_mask,
    roi_rect,
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
    "extra_black",
    "extra_midgray",
    "extra_midchroma",
    "extra_bright",
)

PORTAL_SID = "overlay_portal_050"
# Real portal A/B residual floor (measured: 865 full-frame maxch=1 pixels).
PORTAL_MIN_AB_MAXCH_GE1 = 100

HAND_SID = "hand_block_shield"
HAND_MUTATIONS = (
    "missing_java_silhouette",
    "c_extra",
    "plus1_ch",
    "shift_x2",
    "recolor",
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
        # Single-channel +1 on one A/B-stable exact C==J pixel. Prefer a
        # non-gray subject pixel so hand Java∪C ownership (backdrop-relative)
        # still scores the change; gray +1 under thr stays unowned and would
        # vacuous-PASS. With hard_thr=0 this must produce hard_px>=1 and not
        # PASS (or CAPTURE_BLOCKED when A/B itself has residual maxch).
        c = c0.copy()
        if jb is None:
            raise ValueError("plus1_ch requires jb")
        ab = np.abs(ja.astype(np.int16) - jb.astype(np.int16)).mean(axis=2)
        dmax = np.abs(c0.astype(np.int16) - ja.astype(np.int16)).max(axis=2)
        stable_exact = (ab <= STABLE_AB_THR) & (dmax == 0)
        painted = painted_mask(c0)
        ys, xs = np.where(stable_exact & painted)
        if len(ys) == 0:
            ys, xs = np.where(stable_exact)
        if len(ys) == 0:
            ys, xs = np.where(dmax == 0)
        if len(ys) == 0:
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
    if name in ("extra_black", "extra_midgray", "extra_midchroma", "extra_bright"):
        colors = {
            "extra_black": [0, 0, 0],
            "extra_midgray": [128, 128, 128],
            "extra_midchroma": [64, 160, 200],
            "extra_bright": [255, 255, 255],
        }
        c = c0.copy()
        rng = np.random.RandomState(2 + hash(name) % 97)
        col = colors[name]
        for _ in range(50):
            y = int(rng.randint(0, h))
            x = int(rng.randint(0, w))
            c[y, x] = col
        return c
    if name == "missing_java_silhouette":
        # Wipe C subject where Java still has subject paint -> Java-only hole.
        c = c0.copy()
        j_paint = painted_mask(ja)
        c[j_paint] = GRAY
        return c
    if name == "c_extra":
        # C-only bright pixels outside the Java subject but near it so hand
        # ROI crops still include them (full-frame random often lands outside
        # the lower-band hand ROI and would vacuous-PASS).
        c = c0.copy()
        j_paint = painted_mask(ja)
        ys_j, xs_j = np.where(j_paint)
        placed = 0
        if len(ys_j) > 0:
            y_lo = max(0, int(ys_j.min()) - 10)
            y_hi = min(h, int(ys_j.max()) + 11)
            x_lo = max(0, int(xs_j.min()) - 10)
            x_hi = min(w, int(xs_j.max()) + 11)
            rng = np.random.RandomState(3)
            tries = 0
            while placed < 40 and tries < 8000:
                tries += 1
                y = int(rng.randint(y_lo, max(y_lo + 1, y_hi)))
                x = int(rng.randint(x_lo, max(x_lo + 1, x_hi)))
                if j_paint[y, x]:
                    continue
                c[y, x] = [255, 0, 0]
                placed += 1
        if placed < 10:
            # Force a block adjacent to subject (or top-left if none).
            if len(ys_j) > 0:
                y0 = max(0, int(ys_j.min()) - 8)
                x0 = int(xs_j.min())
                c[y0:y0 + 6, x0:x0 + 10] = [255, 0, 0]
            else:
                c[2:12, 2:12] = [255, 0, 0]
        return c
    raise ValueError("unknown mutation: " + name)


def _synth_zero_noise_pass():
    """Bit-exact synthetic A=B=C must PASS (proves gate is non-vacuous)."""
    ja = np.full((H, W, 3), GRAY, dtype=np.int16)
    # Distinct non-gray portal-like patch so painted_mask is non-empty.
    ja[40:200, 80:400, 0] = 120
    ja[40:200, 80:400, 1] = 40
    ja[40:200, 80:400, 2] = 160
    jb = ja.copy()
    c = ja.copy()
    r = evaluate_fullscreen_replace(PORTAL_SID, ja, jb, c)
    ok = (
        r["verdict"] == "PASS"
        and r.get("hard_px") == 0
        and r.get("noise_max") == 0.0
        and r.get("hard_thr") == 0
    )
    if not ok:
        print("CONTROL FAIL: synth_zero_noise_pass verdict=%s hard_px=%s "
              "noise_max=%s thr=%s reason=%s" % (
                  r.get("verdict"), r.get("hard_px"), r.get("noise_max"),
                  r.get("hard_thr"), r.get("reason")))
        return 1
    print("control ok: %-28s -> PASS  hard_px=0 noise_max=0 thr=0" % (
        "synth_zero_noise_pass",))
    return 0


def _hand_synth_frames():
    """Bit-exact synthetic A=B=C hand viewmodel on isolation GRAY.

    Places a non-gray shield-like patch inside the hand_block_shield ROI so
    Java∪C subject ownership is non-empty and exact match can PASS.
    """
    ja = np.full((H, W, 3), GRAY, dtype=np.int16)
    x0, y0, x1, y1 = roi_rect(HAND_SID)
    # Inset patch so ROI border stays GRAY (backdrop) and subject is clear.
    ja[y0 + 20:y1 - 20, x0 + 30:x1 - 30, 0] = 160
    ja[y0 + 20:y1 - 20, x0 + 30:x1 - 30, 1] = 120
    ja[y0 + 20:y1 - 20, x0 + 30:x1 - 30, 2] = 70
    # Distinct "wood rim" edge for silhouette sensitivity.
    ja[y0 + 18:y0 + 22, x0 + 28:x1 - 28] = (200, 170, 90)
    jb = ja.copy()
    c = ja.copy()
    return ja, jb, c


def _hand_synth_exact_controls():
    """Hand exact gate: synthetic PASS + required mutation rejects."""
    ja, jb, c0 = _hand_synth_frames()
    n_err = 0

    r = evaluate_hand_exact(HAND_SID, ja, jb, c0)
    ok = (
        r["verdict"] == "PASS"
        and r.get("hard_px") == 0
        and r.get("noise_max") == 0.0
        and r.get("hard_thr") == 0
        and (r.get("n_owned") or 0) > 0
    )
    if not ok:
        print("CONTROL FAIL: hand_synth_exact_pass verdict=%s hard_px=%s "
              "noise_max=%s thr=%s owned=%s reason=%s" % (
                  r.get("verdict"), r.get("hard_px"), r.get("noise_max"),
                  r.get("hard_thr"), r.get("n_owned"), r.get("reason")))
        n_err += 1
    else:
        print("control ok: %-28s -> PASS  hard_px=0 noise_max=0 thr=0 "
              "owned=%s" % ("hand_synth_exact_pass", r.get("n_owned")))

    # Also exercise evaluate_state routing for every HAND_HARD id on the same
    # synthetic geometry (ROI differs for eat; rebuild patch per id).
    for sid in sorted(HAND_HARD):
        ja_s = np.full((H, W, 3), GRAY, dtype=np.int16)
        x0, y0, x1, y1 = roi_rect(sid)
        ja_s[y0 + 16:y1 - 16, x0 + 24:x1 - 24] = (150, 110, 60)
        jb_s = ja_s.copy()
        c_s = ja_s.copy()
        rr = evaluate_state(sid, ja_s, jb_s, c_s)
        if rr["verdict"] != "PASS" or rr.get("hard_px") != 0:
            print("CONTROL FAIL: hand_synth_%s verdict=%s hard_px=%s" % (
                sid, rr.get("verdict"), rr.get("hard_px")))
            n_err += 1
        else:
            print("control ok: %-28s -> PASS  hard_px=0 owned=%s" % (
                "hand_synth_" + sid, rr.get("n_owned")))

        for mut in HAND_MUTATIONS:
            cm = mutate(c_s, mut, ja_s, jb_s)
            rm = evaluate_state(sid, ja_s, jb_s, cm)
            rejected = rm["verdict"] != "PASS"
            if not rejected:
                print("CONTROL FAIL: hand mut %s on %s still PASS "
                      "(hard_px=%s reason=%s)" % (
                          mut, sid, rm.get("hard_px"), rm.get("reason")))
                n_err += 1
            else:
                print("control ok: %-28s -> %s  hard_px=%s (mut %s)" % (
                    sid + "/" + mut, rm["verdict"], rm.get("hard_px"), mut))

    return n_err


def _portal_real_ab_controls(goldens):
    """Real portal A/B has residual maxch=1: no C may PASS."""
    ja_p = os.path.join(goldens, "%s_a.png" % PORTAL_SID)
    jb_p = os.path.join(goldens, "%s_b.png" % PORTAL_SID)
    if not (os.path.isfile(ja_p) and os.path.isfile(jb_p)):
        print("CONTROL FAIL: missing portal goldens for A/B controls")
        return 1

    ja = load_rgb(ja_p)
    jb = load_rgb(jb_p)
    ab_max = np.abs(ja.astype(np.int16) - jb.astype(np.int16)).max(axis=2)
    n_ab = int((ab_max >= 1).sum())
    if n_ab < PORTAL_MIN_AB_MAXCH_GE1:
        print("CONTROL FAIL: portal A/B maxch>=1 count %d < floor %d "
              "(control would be vacuous)" % (n_ab, PORTAL_MIN_AB_MAXCH_GE1))
        return 1
    print("control info: portal full-frame ab_maxch_ge1=%d (max=%d)" % (
        n_ab, int(ab_max.max())))

    n_err = 0
    controls = [
        ("portal_C_Java_a", ja),
        ("portal_C_Java_b", jb),
        ("portal_C_midpoint",
         ((ja.astype(np.int16) + jb.astype(np.int16)) // 2).astype(np.int16)),
        ("portal_C_Java_a_plus1",
         np.clip(ja.astype(np.int16) + 1, 0, 255).astype(np.int16)),
    ]
    for label, c in controls:
        r = evaluate_fullscreen_replace(PORTAL_SID, ja, jb, c)
        blocked = r["verdict"] != "PASS"
        # Stronger: must be CAPTURE_BLOCKED (A/B residual), not a false PASS.
        want_blocked = r["verdict"] == "CAPTURE_BLOCKED" and r.get("noise_max", 0) > 0
        if not blocked or not want_blocked:
            print("CONTROL FAIL: %s verdict=%s (want CAPTURE_BLOCKED) "
                  "hard_px=%s thr=%s noise_max=%s" % (
                      label, r.get("verdict"), r.get("hard_px"),
                      r.get("hard_thr"), r.get("noise_max")))
            n_err += 1
        else:
            print("control ok: %-28s -> %s  hard_px=%s thr=%s noise_max=%s "
                  "ab_ge1=%s" % (
                      label, r["verdict"], r.get("hard_px"), r.get("hard_thr"),
                      r.get("noise_max"), r.get("n_ab_maxch_ge1")))
    return n_err


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--cframes", required=True)
    args = ap.parse_args()

    n_fail = 0
    print("ui_hud mutation regressions (fullscreen hard_px + hand exact):")
    print("%-22s %-14s %10s %8s %10s  %s" % (
        "state", "mutation", "C-vs-J", "hard_px", "verdict", "expect"))

    # Non-vacuous controls first (do not depend on C frames).
    n_fail += _synth_zero_noise_pass()
    n_fail += _portal_real_ab_controls(args.goldens)
    n_fail += _hand_synth_exact_controls()

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

        # Honest: capture must not FAIL. PASS / RESIDUAL / CAPTURE_BLOCKED OK.
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

        exact = (r0["verdict"] == "PASS" and r0.get("hard_px") == 0
                 and (r0.get("noise_max") or 0) == 0)
        if r0["verdict"] == "CAPTURE_BLOCKED":
            expect_h = "CAPTURE_BLOCKED_OK"
        elif exact:
            expect_h = "PASS"
        else:
            expect_h = "RESIDUAL_OK"
        print("%-22s %-14s %10.3f %8s %10s  %s thr=%s maxch=%s nmax=%s" % (
            sid, "honest",
            r0["c_vs_j"] if r0["c_vs_j"] == r0["c_vs_j"] else -1.0,
            str(r0.get("hard_px")),
            r0["verdict"],
            expect_h,
            r0.get("hard_thr"),
            r0.get("max_diff"),
            r0.get("noise_max")))
        if not exact:
            print("  honest residual/block: hard_px=%s bbox=%s noise_max=%s "
                  "ab_ge1=%s" % (
                      r0.get("hard_px"), r0.get("residual_bbox"),
                      r0.get("noise_max"), r0.get("n_ab_maxch_ge1")))
            for loc in (r0.get("residual_locs") or [])[:6]:
                print("    @(%d,%d) maxch=%d C=%s J=%s" % (
                    loc["x"], loc["y"], loc["maxch"], loc["c"], loc["j"]))

        # Portal product must never honest-PASS under real A/B residual.
        if sid == PORTAL_SID and r0["verdict"] == "PASS":
            n_fail += 1
            print("  CONTROL FAIL: real portal honest must not PASS "
                  "(noise_max=%s)" % r0.get("noise_max"))

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
