#!/usr/bin/env python3
"""Guarded 1-LSB rounding tier for the ui_hud oracle ROI gate.

Three verdicts on hard exact-bar rows (HAND_HARD + FULLSCREEN_REPLACE):
  PASS      - zero differing owned pixels
  PASS-LSB  - A/B noise 0 AND every differing owned pixel differs by at most 1
              (8-bit) in every channel (px>1==0, per-channel max delta <= 1)
              AND nz <= 2% of the row's owned ROI
  RESIDUAL/FAIL - anything else (hard_px / cluster reporting unchanged)

Not a mean PASS-FLOOR. Core HUD / death chrome / soft CAPTURE_OK keep their
existing contracts (do not absorb durability +1 extras as PASS-LSB).

Mutation guard (non-vacuous), on a real exact row's candidate ROI:
  uniform +1 everywhere FAIL via the count cap
  a single pixel +2 FAIL via px>1
  a 3x3 +12 recolor FAIL via hard/cluster (HARD_THR=10)
  live hand_eat_mid / hand_block_shield residual pinned RESIDUAL (not PASS-LSB)
"""
from __future__ import print_function

import argparse
import os
import sys

import numpy as np

HARD_THR = 10.0
LSB_FRAC = 0.02

# Pinned honest residual (gamer 2026-08-22, same C candidate as OPEN_DIVERGENCES
# handscene numbers). Guard fails if a row flips to PASS / PASS-LSB or if
# px>1 / hard_px / maxch move off this measurement.
REAL_PIN = {
    "hand_eat_mid": {
        "verdict": "RESIDUAL",
        "hard_px": 73440,
        "px_gt_1": 21526,
        "maxch": 215,
        "n_owned": 89242,
    },
    "hand_block_shield": {
        "verdict": "RESIDUAL",
        "hard_px": 28564,
        "px_gt_1": 6925,
        "maxch": 61,
        "n_owned": 29351,
    },
}

GUARD_SID = "overlay_inside_stone"
# Interior of overlay ROI (2,2)-(852,478); avoid HUD chrome at the bottom.
INJECT_Y, INJECT_X = 10, 10


def _rgb(a):
    return np.asarray(a, dtype=np.int16)


def classify_owned(ja, jb, jm, owned, noise_max=1e-6):
    """Return a JSON-safe stats dict. `ok` is True for PASS and PASS-LSB."""
    ja = _rgb(ja)
    jb = _rgb(jb)
    jm = _rgb(jm)
    if owned is None:
        owned = np.ones(ja.shape[:2], dtype=bool)
    else:
        owned = np.asarray(owned, dtype=bool)
    n_roi = int(owned.sum())
    ab = np.abs(ja - jb)
    ch = np.abs(jm - ja)
    if n_roi > 0:
        noise = float(ab[owned].mean())
        d = ch.mean(axis=2)
        maxch_px = ch.max(axis=2)
        maxch = int(maxch_px[owned].max()) if n_roi else 0
        nz = int(((maxch_px > 0) & owned).sum())
        px_gt_1 = int(((maxch_px > 1) & owned).sum())
        hard_px = int(((d >= HARD_THR) & owned).sum())
        diff = float(d[owned].mean())
    else:
        noise = float(ab.mean()) if ab.size else float("nan")
        maxch = int(ch.max()) if ch.size else 0
        nz = 0
        px_gt_1 = 0
        hard_px = 0
        diff = float("nan")
    lsb_cap = LSB_FRAC * float(n_roi)
    noise_fail = bool(noise == noise and noise > noise_max)

    if noise_fail:
        verdict = "FAIL"
        ok = False
        reason = "ab_noise"
    elif nz == 0:
        verdict = "PASS"
        ok = True
        reason = "exact"
    elif (
        noise == 0.0
        and px_gt_1 == 0
        and maxch <= 1
        and nz <= lsb_cap
    ):
        verdict = "PASS-LSB"
        ok = True
        reason = "lsb"
    else:
        verdict = "RESIDUAL"
        ok = False
        reason = "residual"

    return {
        "ok": ok,
        "verdict": verdict,
        "reason": reason,
        "noise": noise,
        "diff": diff,
        "hard_px": hard_px,
        "nz": nz,
        "px_gt_1": px_gt_1,
        "maxch": maxch,
        "n_roi": n_roi,
        "lsb_cap": lsb_cap,
        "lsb_frac": LSB_FRAC,
        "noise_fail": noise_fail,
    }


def maybe_pass_lsb(verdict, noise_max, px_gt_1, maxch, nz, n_owned):
    """Upgrade RESIDUAL to PASS-LSB when the rounding-tier conditions hold."""
    if verdict != "RESIDUAL":
        return verdict
    if n_owned is None or n_owned <= 0:
        return verdict
    if noise_max is None or not (noise_max == 0 or noise_max == 0.0):
        return verdict
    if px_gt_1 is None or maxch is None or nz is None:
        return verdict
    if not (px_gt_1 == 0 and maxch <= 1 and 0 < nz <= LSB_FRAC * float(n_owned)):
        return verdict
    return "PASS-LSB"


def inject_uniform_lsb(img):
    """Systematic 1-LSB move on every channel of every pixel (clip-safe)."""
    a = _rgb(img).copy()
    bump = np.where(a < 255, 1, -1)
    return a + bump


def inject_single_plus2(img, y=INJECT_Y, x=INJECT_X):
    """One pixel, +2 (or -2 if clip) on every channel so mean |d| = 2."""
    a = _rgb(img).copy()
    pix = a[y, x]
    if np.all(pix <= 253):
        a[y, x] = pix + 2
    else:
        a[y, x] = np.maximum(pix - 2, 0)
    return a


def inject_patch_plus12(img, y=INJECT_Y, x=INJECT_X, size=3, delta=12):
    """3x3 recolor by +12 (or -12 if clip) on every channel; mean |d| = 12."""
    a = _rgb(img).copy()
    patch = a[y:y + size, x:x + size]
    if np.all(patch <= 255 - delta):
        a[y:y + size, x:x + size] = patch + delta
    else:
        a[y:y + size, x:x + size] = np.clip(patch - delta, 0, 255)
    return a


def _case_row(name, st, expect_verdict, extra_ok, note):
    if isinstance(expect_verdict, (tuple, list, set)):
        verdict_ok = st.get("verdict") in expect_verdict
        expect_out = "|".join(expect_verdict)
    else:
        verdict_ok = st.get("verdict") == expect_verdict
        expect_out = expect_verdict
    caught = verdict_ok and extra_ok
    return {
        "name": name,
        "verdict": st.get("verdict"),
        "expect": expect_out,
        "nz": int(st.get("nz") if st.get("nz") is not None else
                  st.get("hard_px") or 0),
        "px_gt_1": int(st.get("px_gt_1") or 0),
        "maxch": int(st.get("maxch") if st.get("maxch") is not None else
                     (st.get("max_diff") or 0)),
        "hard_px": int(st.get("hard_px") or 0),
        "n_roi": int(st.get("n_roi") if st.get("n_roi") is not None else
                     st.get("n_owned") or st.get("n_stable") or 0),
        "ok": bool(caught),
        "note": note,
    }


def _chroma(rgb):
    return int(rgb.max()) - int(rgb.min())


def _is_grass(rgb):
    r, g, b = int(rgb[0]), int(rgb[1]), int(rgb[2])
    return g >= r + 10 and g >= b + 10 and g > 40


def _is_sky(rgb):
    r, g, b = int(rgb[0]), int(rgb[1]), int(rgb[2])
    return b >= r + 20 and b >= g and b > 80


def _is_gray(rgb, thr=16):
    return _chroma(rgb) <= thr


def bucket_pair(c_rgb, j_rgb):
    """wall / painted / border_selbox / grass / sky for one C,J pair."""
    if _is_grass(j_rgb) or _is_grass(c_rgb):
        return "grass"
    if _is_sky(j_rgb) or _is_sky(c_rgb):
        return "sky"
    if _is_gray(c_rgb) and _is_gray(j_rgb):
        lo = min(int(np.mean(c_rgb)), int(np.mean(j_rgb)))
        hi = max(int(np.mean(c_rgb)), int(np.mean(j_rgb)))
        # RenderGlobal.drawSelectionBox: black 0.4 alpha over stone (gray darken).
        if lo <= 40 and hi >= 50:
            return "border_selbox"
        return "wall"
    return "painted"


def classify_hand_tails(goldens, cframes, ids=None):
    """Bucket owned px with max-channel delta > 1. Returns list of dicts."""
    from compare_ui_hud_oracle import (  # noqa: E402
        crop,
        evaluate_hand_exact,
        hand_owned_mask,
        load_ppm,
        load_rgb,
        roi_rect,
    )
    if ids is None:
        ids = ("hand_bow_pull20", "hand_eat_mid", "hand_block_shield")
    out = []
    bucket_names = ("wall", "painted", "border_selbox", "grass", "sky")
    print("-- hand >1-delta tails --")
    print("%-20s %8s %8s %8s %6s %8s  %s" % (
        "id", "n_owned", "nz", "px>1", "maxch", "lsb_cap", "PASS-LSB?"))
    for sid in ids:
        ja_p = os.path.join(goldens, "%s_a.png" % sid)
        jb_p = os.path.join(goldens, "%s_b.png" % sid)
        c_p = os.path.join(cframes, "c_%s.ppm" % sid)
        if not (os.path.isfile(ja_p) and os.path.isfile(jb_p)
                and os.path.isfile(c_p)):
            print("%-20s  MISSING frames" % sid)
            continue
        ja_full = load_rgb(ja_p)
        jb_full = load_rgb(jb_p)
        c_full = load_ppm(c_p)
        row = evaluate_hand_exact(sid, ja_full, jb_full, c_full)
        rect = roi_rect(sid)
        ja = crop(ja_full, rect)
        c = crop(c_full, rect)
        h = min(ja.shape[0], c.shape[0])
        w = min(ja.shape[1], c.shape[1])
        ja, c = ja[:h, :w], c[:h, :w]
        owned = hand_owned_mask(ja, c)
        ch = np.abs(c.astype(np.int16) - ja.astype(np.int16))
        maxch_px = ch.max(axis=2)
        nz_m = owned & (maxch_px > 0)
        gt1 = owned & (maxch_px > 1)
        n_owned = int(owned.sum())
        nz = int(nz_m.sum())
        n_gt1 = int(gt1.sum())
        maxch = int(maxch_px[owned].max()) if n_owned else 0
        lsb_cap = LSB_FRAC * float(n_owned)
        can_lsb = (
            row.get("noise_max", 1) == 0
            and n_gt1 == 0
            and maxch <= 1
            and 0 < nz <= lsb_cap
        )
        buckets = {k: 0 for k in bucket_names}
        samples = {k: [] for k in bucket_names}
        ys, xs = np.where(gt1)
        for y, x in zip(ys, xs):
            b = bucket_pair(c[y, x], ja[y, x])
            buckets[b] += 1
            if len(samples[b]) < 4:
                samples[b].append({
                    "x": int(x + rect[0]),
                    "y": int(y + rect[1]),
                    "maxch": int(maxch_px[y, x]),
                    "c": [int(v) for v in c[y, x]],
                    "j": [int(v) for v in ja[y, x]],
                })
        eq1 = {k: 0 for k in bucket_names}
        ys1, xs1 = np.where(owned & (maxch_px == 1))
        for y, x in zip(ys1, xs1):
            eq1[bucket_pair(c[y, x], ja[y, x])] += 1
        qualify = "yes" if can_lsb else "no"
        why = []
        if n_gt1 > 0:
            why.append("px>1=%d" % n_gt1)
        if nz > lsb_cap:
            why.append("nz=%d>cap=%.1f" % (nz, lsb_cap))
        if maxch > 1:
            why.append("maxch=%d" % maxch)
        print("%-20s %8d %8d %8d %6d %8.1f  %s %s" % (
            sid, n_owned, nz, n_gt1, maxch, lsb_cap, qualify,
            ("(%s)" % ", ".join(why)) if why else ""))
        print("  gt1 buckets  wall=%d painted=%d selbox=%d grass=%d sky=%d" % (
            buckets["wall"], buckets["painted"], buckets["border_selbox"],
            buckets["grass"], buckets["sky"]))
        print("  eq1 buckets  wall=%d painted=%d selbox=%d grass=%d sky=%d" % (
            eq1["wall"], eq1["painted"], eq1["border_selbox"],
            eq1["grass"], eq1["sky"]))
        for k in bucket_names:
            for loc in samples[k]:
                print("    %s @(%d,%d) maxch=%d C=%s J=%s" % (
                    k, loc["x"], loc["y"], loc["maxch"], loc["c"], loc["j"]))
        rec = {
            "id": sid,
            "n_owned": n_owned,
            "nz": nz,
            "px_gt_1": n_gt1,
            "maxch": maxch,
            "lsb_cap": lsb_cap,
            "can_pass_lsb": bool(can_lsb),
            "why_not": why,
            "gt1_buckets": buckets,
            "eq1_buckets": eq1,
            "gt1_samples": samples,
            "verdict": row.get("verdict"),
            "c_vs_j": row.get("c_vs_j"),
            "hard_px": row.get("hard_px"),
        }
        out.append(rec)
    return out


def _row_from_eval(r, n_roi_fallback=0):
    maxch = r.get("maxch")
    if maxch is None:
        md = r.get("max_diff")
        maxch = int(md) if md == md and md is not None else 0
    n_roi = r.get("n_owned")
    if n_roi is None:
        n_roi = r.get("n_stable")
    if n_roi is None:
        n_roi = n_roi_fallback
    return {
        "verdict": r.get("verdict"),
        "nz": int(r.get("hard_px") or 0),
        "px_gt_1": int(r.get("px_gt_1") or 0),
        "maxch": int(maxch),
        "hard_px": int(r.get("hard_px") or 0),
        "n_roi": int(n_roi or 0),
        "n_owned": r.get("n_owned"),
        "n_stable": r.get("n_stable"),
        "max_diff": r.get("max_diff"),
        "reason": r.get("reason"),
    }


def run_lsb_guard(goldens, cframes):
    """Prove the tier still rejects real bugs. Failure fails the gate run.

    Mutations 1-3 start from the live overlay_inside_stone candidate (bit-exact
    vs Java_a) so each tooth is isolated. Pins 4-5 are the live hand residuals.
    """
    from compare_ui_hud_oracle import (  # noqa: E402
        evaluate_fullscreen_replace,
        evaluate_hand_exact,
        load_ppm,
        load_rgb,
    )

    ja_p = os.path.join(goldens, "%s_a.png" % GUARD_SID)
    jb_p = os.path.join(goldens, "%s_b.png" % GUARD_SID)
    c_p = os.path.join(cframes, "c_%s.ppm" % GUARD_SID)
    if not (os.path.isfile(ja_p) and os.path.isfile(jb_p) and os.path.isfile(c_p)):
        print("ui_hud LSB guard: FAIL (missing %s frames)" % GUARD_SID)
        return False, {"pass": False, "cases": []}

    ja = load_rgb(ja_p)
    jb = load_rgb(jb_p)
    c0 = load_ppm(c_p)

    cases = []

    # Live exact row must stay PASS (mutations start from a real candidate).
    r0 = evaluate_fullscreen_replace(GUARD_SID, ja, jb, c0)
    extra0 = (
        r0["verdict"] == "PASS"
        and int(r0.get("hard_px") or 0) == 0
        and (r0.get("noise_max") or 0) == 0
    )
    cases.append(_case_row(
        "real_%s" % GUARD_SID, _row_from_eval(r0), "PASS", extra0,
        "live exact row PASS (mutations isolated from residual)",
    ))

    # 1) systematic +1 on every pixel. Count cap must catch it (100% > 2%).
    mut = inject_uniform_lsb(c0)
    r = evaluate_fullscreen_replace(GUARD_SID, ja, jb, mut)
    st = _row_from_eval(r)
    n_roi = st["n_roi"]
    lsb_cap = LSB_FRAC * float(n_roi) if n_roi else 0.0
    extra = (
        r["verdict"] not in ("PASS", "PASS-LSB")
        and st["px_gt_1"] == 0
        and st["maxch"] <= 1
        and st["nz"] > lsb_cap
        and st["nz"] >= max(1, int(0.5 * n_roi))
    )
    cases.append(_case_row(
        "uniform_plus1", st, ("RESIDUAL", "FAIL"), extra,
        "FAIL/RESIDUAL (count cap: ~100% > 2%; maxch<=1 px>1==0)",
    ))

    # 2) single pixel off by +2. nz=1 is under the 2% cap; px>1 must fire.
    mut = inject_single_plus2(c0)
    r = evaluate_fullscreen_replace(GUARD_SID, ja, jb, mut)
    st = _row_from_eval(r)
    extra = (
        r["verdict"] not in ("PASS", "PASS-LSB")
        and st["px_gt_1"] > 0
        and st["maxch"] > 1
        and st["nz"] >= 1
    )
    cases.append(_case_row(
        "single_plus2", st, ("RESIDUAL", "FAIL"), extra,
        "FAIL/RESIDUAL (px>1 > 0 / maxch > 1)",
    ))

    # 3) 3x3 patch recolored by +12. hard_px / cluster path (HARD_THR=10).
    mut = inject_patch_plus12(c0)
    r = evaluate_fullscreen_replace(GUARD_SID, ja, jb, mut)
    st = _row_from_eval(r)
    extra = (
        r["verdict"] not in ("PASS", "PASS-LSB")
        and st["hard_px"] > 0
        and st["maxch"] >= int(HARD_THR)
    )
    cases.append(_case_row(
        "patch_plus12", st, ("RESIDUAL", "FAIL"), extra,
        "FAIL/RESIDUAL (hard_px / cluster path, maxch>=10)",
    ))

    # 4-5) live hand residuals pinned RESIDUAL, not PASS-LSB.
    for sid, pin in REAL_PIN.items():
        ja_h = load_rgb(os.path.join(goldens, "%s_a.png" % sid))
        jb_h = load_rgb(os.path.join(goldens, "%s_b.png" % sid))
        c_h = load_ppm(os.path.join(cframes, "c_%s.ppm" % sid))
        rh = evaluate_hand_exact(sid, ja_h, jb_h, c_h)
        st = _row_from_eval(rh)
        extra = (
            rh["verdict"] == pin["verdict"]
            and st["hard_px"] == pin["hard_px"]
            and st["px_gt_1"] == pin["px_gt_1"]
            and st["maxch"] == pin["maxch"]
            and int(rh.get("n_owned") or 0) == pin["n_owned"]
        )
        cases.append(_case_row(
            "real_%s" % sid, st, pin["verdict"], extra,
            "RESIDUAL not PASS-LSB; px>1=%d hard_px=%d maxch=%d pin" % (
                pin["px_gt_1"], pin["hard_px"], pin["maxch"]),
        ))

    print("-- ui_hud LSB mutation guard --")
    print(
        "%-28s %-10s %7s %6s %6s %6s  %s" % (
            "case", "verdict", "nz", "px>1", "maxch", "hard", "result",
        )
    )
    n_fail = 0
    for c in cases:
        result = "OK" if c["ok"] else "MISS"
        if not c["ok"]:
            n_fail += 1
        print(
            "%-28s %-10s %7d %6d %6d %6d  %s  %s" % (
                c["name"], c["verdict"], c["nz"], c["px_gt_1"],
                c["maxch"], c["hard_px"], result, c["note"],
            )
        )
        if not c["ok"]:
            print(
                "  MUTATION SELF-TEST FAIL: %s expect %s got %s "
                "(nz=%d px>1=%d maxch=%d hard_px=%d)" % (
                    c["name"], c["expect"], c["verdict"],
                    c["nz"], c["px_gt_1"], c["maxch"], c["hard_px"],
                )
            )

    passed = n_fail == 0
    if passed:
        print(
            "ui_hud LSB guard: PASS "
            "(uniform+1 count-cap FAIL, +2 px>1 FAIL, 3x3+12 hard FAIL, "
            "live eat/shield RESIDUAL pin)"
        )
    else:
        print("ui_hud LSB guard: FAIL (%d)" % n_fail)

    tails = classify_hand_tails(goldens, cframes)

    report = {
        "pass": passed,
        "lsb_frac": LSB_FRAC,
        "real_pin": dict(REAL_PIN),
        "guard_sid": GUARD_SID,
        "cases": list(cases),
        "tails": tails,
    }
    return passed, report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--cframes", required=True)
    ap.add_argument("--classify-only", action="store_true")
    args = ap.parse_args()
    if args.classify_only:
        classify_hand_tails(args.goldens, args.cframes)
        return 0
    ok, _ = run_lsb_guard(args.goldens, args.cframes)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
