#!/usr/bin/env python3
"""Guarded 1-LSB rounding tier for the inventory-preview ROI gate.

Three verdicts (this gate only; not a mean PASS-FLOOR):
  PASS      - every pixel equal (diff==0 everywhere)
  PASS-LSB  - A/B noise 0 AND every differing pixel differs by at most 1
              (8-bit) in every channel (px>1==0, per-channel max delta <= 1)
              AND nz <= 2% of the ROI pixel count
  FAIL      - anything else (hard_px / cluster path unchanged at the caller)

Mutation guard (non-vacuous): uniform +1 on an exact ROI must FAIL via the
count cap; a single pixel +2 must FAIL via px>1; a 3x3 +12 recolor must FAIL
via hard_px; the real pose1/pose2 residual must be PASS-LSB, not exact PASS.
"""
from __future__ import print_function

import numpy as np

HARD_THR = 10.0
LSB_FRAC = 0.02
# Pinned honest residual under pin_preview_anim (lane/preview). Guard 4 fails
# if the live ROI is exact or if nz moves off this measurement.
REAL_NZ = {"pose1": 62, "pose2": 140}


def _rgb(a):
    return np.asarray(a, dtype=np.int16)


def mean_abs(a, b):
    d = np.abs(_rgb(a) - _rgb(b))
    return float(d.mean())


def per_px(a, b):
    return np.abs(_rgb(a) - _rgb(b)).mean(axis=2)


def channel_delta(a, b):
    return np.abs(_rgb(a) - _rgb(b))


def classify_preview(ja, jb, jm, noise_max=1e-6):
    """Return a JSON-safe stats dict. `ok` is True for PASS and PASS-LSB."""
    ja = _rgb(ja)
    jb = _rgb(jb)
    jm = _rgb(jm)
    noise = mean_abs(ja, jb)
    diff = mean_abs(ja, jm)
    ch = channel_delta(ja, jm)
    d = ch.mean(axis=2)
    n_roi = int(d.size)
    maxch = int(ch.max()) if ch.size else 0
    hard_px = int((d >= HARD_THR).sum())
    nz = int((d > 0).sum())
    px_gt_1 = int((ch.max(axis=2) > 1).sum())
    px_gt_1_mean = int((d > 1).sum())
    lsb_cap = LSB_FRAC * n_roi
    noise_fail = bool(noise > noise_max)

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
        verdict = "FAIL"
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
        "px_gt_1_mean": px_gt_1_mean,
        "maxch": maxch,
        "n_roi": n_roi,
        "lsb_cap": lsb_cap,
        "lsb_frac": LSB_FRAC,
        "noise_fail": noise_fail,
    }


def pose_record(st, pose_name):
    return {
        "pose": pose_name,
        "noise": st["noise"],
        "magma_vs_j": st["diff"],
        "hard_px": st["hard_px"],
        "nz": st["nz"],
        "px_gt_1": st["px_gt_1"],
        "maxch": st["maxch"],
        "n_roi": st["n_roi"],
        "verdict": st["verdict"],
        "pass": bool(st["ok"]),
    }


def inject_uniform_lsb(img):
    """Systematic 1-LSB move on every channel of every pixel (clip-safe)."""
    a = _rgb(img).copy()
    bump = np.where(a < 255, 1, -1)
    return a + bump


def inject_single_plus2(img):
    """One pixel, +2 (or -2 if clip) on every channel so mean |d| = 2."""
    a = _rgb(img).copy()
    pix = a[0, 0]
    if np.all(pix <= 253):
        a[0, 0] = pix + 2
    else:
        a[0, 0] = np.maximum(pix - 2, 0)
    return a


def inject_patch_plus12(img, size=3, delta=12):
    """3x3 recolor by +12 (or -12 if clip) on every channel; mean |d| = 12."""
    a = _rgb(img).copy()
    patch = a[:size, :size]
    if np.all(patch <= 255 - delta):
        a[:size, :size] = patch + delta
    else:
        a[:size, :size] = np.clip(patch - delta, 0, 255)
    return a


def _case_row(name, st, expect_verdict, extra_ok, note):
    caught = (st["verdict"] == expect_verdict) and extra_ok
    return {
        "name": name,
        "verdict": st["verdict"],
        "expect": expect_verdict,
        "nz": st["nz"],
        "px_gt_1": st["px_gt_1"],
        "maxch": st["maxch"],
        "hard_px": st["hard_px"],
        "n_roi": st["n_roi"],
        "ok": caught,
        "note": note,
    }


def run_lsb_guard(pose1_ja, pose1_jb, pose1_jm, pose2_ja, pose2_jb, pose2_jm,
                  noise_max=1e-6):
    """Prove the tier still rejects real bugs. Failure fails the gate run.

    Mutations 1-3 start from a bit-exact clone of Java A so each tooth is
    isolated (uniform +1 must not be FAIL only because leftover residual
    becomes +2). Case 4 is the live magma residual.
    """
    ja = _rgb(pose1_ja)
    cases = []

    # 1) systematic +1 on every pixel. Count cap must catch it (100% > 2%).
    #    maxch stays 1 and px>1 stays 0, otherwise the cap is untested.
    mut = inject_uniform_lsb(ja)
    st = classify_preview(ja, ja, mut, noise_max)
    extra = (
        st["verdict"] == "FAIL"
        and st["nz"] == st["n_roi"]
        and st["px_gt_1"] == 0
        and st["maxch"] <= 1
        and st["nz"] > st["lsb_cap"]
    )
    cases.append(_case_row(
        "uniform_plus1", st, "FAIL", extra,
        "FAIL (count cap: 100% > 2%; maxch<=1 px>1==0)",
    ))

    # 2) single pixel off by +2. nz=1 is under the 2% cap; px>1 must fire.
    mut = inject_single_plus2(ja)
    st = classify_preview(ja, ja, mut, noise_max)
    extra = (
        st["verdict"] == "FAIL"
        and st["px_gt_1"] > 0
        and st["maxch"] > 1
        and st["nz"] >= 1
    )
    cases.append(_case_row(
        "single_plus2", st, "FAIL", extra,
        "FAIL (px>1 > 0 / maxch > 1)",
    ))

    # 3) 3x3 patch recolored by +12. hard_px / cluster path (HARD_THR=10).
    mut = inject_patch_plus12(ja)
    st = classify_preview(ja, ja, mut, noise_max)
    extra = (
        st["verdict"] == "FAIL"
        and st["hard_px"] > 0
        and st["maxch"] > 1
    )
    cases.append(_case_row(
        "patch_plus12", st, "FAIL", extra,
        "FAIL (hard_px / cluster path)",
    ))

    # 4) live residual must be PASS-LSB, never exact PASS.
    st1 = classify_preview(pose1_ja, pose1_jb, pose1_jm, noise_max)
    extra1 = (
        st1["verdict"] == "PASS-LSB"
        and st1["nz"] == REAL_NZ["pose1"]
        and st1["px_gt_1"] == 0
        and st1["maxch"] <= 1
    )
    cases.append(_case_row(
        "real_pose1", st1, "PASS-LSB", extra1,
        "PASS-LSB not PASS (exact); nz=%d pin" % REAL_NZ["pose1"],
    ))

    st2 = classify_preview(pose2_ja, pose2_jb, pose2_jm, noise_max)
    extra2 = (
        st2["verdict"] == "PASS-LSB"
        and st2["nz"] == REAL_NZ["pose2"]
        and st2["px_gt_1"] == 0
        and st2["maxch"] <= 1
    )
    cases.append(_case_row(
        "real_pose2", st2, "PASS-LSB", extra2,
        "PASS-LSB not PASS (exact); nz=%d pin" % REAL_NZ["pose2"],
    ))

    print("-- preview LSB mutation guard --")
    print(
        "%-16s %-8s %7s %6s %6s %6s  %s" % (
            "case", "verdict", "nz", "px>1", "maxch", "hard", "result",
        )
    )
    n_fail = 0
    for c in cases:
        result = "OK" if c["ok"] else "MISS"
        if not c["ok"]:
            n_fail += 1
        print(
            "%-16s %-8s %7d %6d %6d %6d  %s  %s" % (
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
            "preview LSB guard: PASS "
            "(uniform+1 count-cap FAIL, +2 px>1 FAIL, 3x3+12 hard FAIL, "
            "real pose1/pose2 PASS-LSB)"
        )
    else:
        print("preview LSB guard: FAIL (%d)" % n_fail)

    report = {
        "pass": passed,
        "lsb_frac": LSB_FRAC,
        "real_nz_pin": dict(REAL_NZ),
        "cases": [
            {k: v for k, v in c.items()} for c in cases
        ],
    }
    return passed, report
