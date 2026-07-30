#!/usr/bin/env python3
"""Non-vacuous mutation regressions for ui_entities hard owned-pixel gate.

Protocol:
  1) Honest C must be capture-OK-ish: RESIDUAL or CAPTURE_BLOCKED (never PASS
     until renderer closes; FAIL fails the suite).
  2) Real-golden Java_A control:
       - zero A/B on owned  => must PASS
       - nonzero A/B on owned => must be CAPTURE_BLOCKED (never PASS)
  3) Synthetic zero-noise control (A=B=C painted subject) must PASS.
  4) Corruptions of the zero-noise synthetic must NOT PASS, with paint.
  5) Explicit hole / extras classes must trip on a zero-noise base:
       hole_magma_38070, hole_dragon_6571122, hole_slime_fringe,
       hole_fireball_thin, extra_black_outside, xp_no_orb_blocked.

Mutations on synthetic base:
  erase90, blank, plus1_ch, missing_dark, missing_transparent,
  shift_x2, shift_y2, recolor,
  extra_black / extra_midgray / extra_midchroma / extra_bright / extra_vivid
"""
from __future__ import print_function

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_ui_entities_oracle import (  # noqa: E402
    DEFAULT_STATES,
    H,
    MIN_OWNED,
    W,
    crop,
    evaluate_state,
    family,
    load_ppm,
    load_rgb,
    owned_mask,
    roi_rect,
    subject_seg,
)

MUTATION_NAMES = (
    "erase90",
    "blank",
    "plus1_ch",
    "missing_dark",
    "missing_transparent",
    "shift_x2",
    "shift_y2",
    "recolor",
    "extra_black",
    "extra_midgray",
    "extra_midchroma",
    "extra_bright",
    "extra_vivid",
)

EXTRA_COLORS = {
    "extra_black": (0, 0, 0),
    "extra_midgray": (128, 128, 128),
    "extra_midchroma": (160, 80, 120),
    "extra_bright": (240, 240, 240),
    "extra_vivid": (255, 0, 255),
}

# Empirical hole colors the prior color-threshold masks missed.
HOLE_MAGMA = (38, 7, 0)
HOLE_DRAGON = (65, 71, 122)
HOLE_SLIME_FRINGE = (90, 140, 85)   # greenish gel fringe
HOLE_FIREBALL_ORANGE = (235, 170, 24)


def pad_color(ja):
    """Sample a sky/pad color from the top-left corner for blanking."""
    patch = ja[4:12, 4:12].astype(np.int32)
    return tuple(int(x) for x in patch.mean(axis=(0, 1)).round())


def make_synthetic_zero_noise(sid, seed_ja):
    """Build A=B=C frames with a visible family subject and zero A/B noise.

    Pad/sky from seed_ja corners; paint a compact subject blob at ROI center
    so ownership is parity-capable and control PASSes.
    """
    fam = family(sid)
    base = seed_ja.astype(np.int16, copy=True)
    # Flatten to pad-ish gray so residual background is stable, keep sky top.
    pad = np.array(pad_color(seed_ja), dtype=np.int16)
    # Keep full frame seed for realism, then paint subject over ROI center.
    rect = roi_rect(sid)
    x0, y0, x1, y1 = rect
    cx = (x0 + x1) // 2
    cy = (y0 + y1) // 2

    # Family-colored subject blob (large enough for MIN_OWNED after dilate).
    def paint_blob(img, color, rx=18, ry=18):
        yy0 = max(0, cy - ry)
        yy1 = min(H, cy + ry)
        xx0 = max(0, cx - rx)
        xx1 = min(W, cx + rx)
        img[yy0:yy1, xx0:xx1] = np.array(color, dtype=np.int16)
        return (yy1 - yy0) * (xx1 - xx0)

    if fam == "slime":
        paint_blob(base, (80, 180, 70), 22, 22)
        # greenish fringe ring (hole class)
        base[cy - 24:cy - 20, cx - 10:cx + 10] = np.array(
            HOLE_SLIME_FRINGE, dtype=np.int16)
    elif fam == "magma":
        paint_blob(base, (20, 8, 4), 20, 20)
        base[cy - 6:cy + 6, cx - 6:cx + 6] = np.array(HOLE_MAGMA, dtype=np.int16)
        base[cy - 10:cy - 4, cx - 8:cx + 8] = np.array(
            (180, 60, 10), dtype=np.int16)
    elif fam == "dragon":
        paint_blob(base, (20, 18, 30), 24, 14)
        base[cy - 4:cy + 4, cx - 4:cx + 4] = np.array(
            HOLE_DRAGON, dtype=np.int16)
        # ray accent
        base[cy - 30:cy - 10, cx - 2:cx + 2] = np.array(
            (220, 100, 220), dtype=np.int16)
    elif fam == "dig":
        # Sparse gray crumbs (A=B so no re-roll; subject via near_black + crumbs)
        paint_blob(base, (90, 90, 90), 8, 8)
        base[cy - 12:cy - 8, cx - 12:cx - 8] = np.array(
            (0, 0, 0), dtype=np.int16)
        base[cy + 4:cy + 10, cx + 4:cx + 12] = np.array(
            (70, 50, 34), dtype=np.int16)
    elif fam == "fireball":
        paint_blob(base, (0, 0, 0), 6, 6)
        base[cy - 10:cy + 10, cx - 10:cx + 10] = np.array(
            HOLE_FIREBALL_ORANGE, dtype=np.int16)
        base[cy - 3:cy + 3, cx - 3:cx + 3] = np.array(
            (0, 0, 0), dtype=np.int16)
    elif fam == "xp_orb":
        paint_blob(base, (120, 200, 40), 10, 10)
        base[cy - 4:cy + 4, cx - 4:cx + 4] = np.array(
            (200, 220, 30), dtype=np.int16)
    else:
        paint_blob(base, (200, 50, 50), 16, 16)

    # A == B == C
    return base.copy(), base.copy(), base.copy()


def mutate(base, name, ja, jb, sid):
    """Mutate a perfect (zero-noise) base frame. Returns (mut, n_paint)."""
    c = base.astype(np.int16, copy=True)
    h, w = c.shape[:2]
    fam = family(sid)
    rect = roi_rect(sid)
    owned, _info = owned_mask(ja, jb, base, fam, rect)
    full_owned = np.zeros((h, w), dtype=bool)
    x0, y0, x1, y1 = rect
    oh, ow = owned.shape
    full_owned[y0:y0 + oh, x0:x0 + ow] = owned
    pad = np.array(pad_color(ja), dtype=np.int16)
    rng = np.random.RandomState(0)

    if name == "erase90":
        ys, xs = np.where(full_owned)
        n = len(ys)
        if n == 0:
            return c, 0
        kill_n = max(1, int(0.9 * n))
        kill = rng.choice(n, size=kill_n, replace=False)
        before = c[ys[kill], xs[kill]].copy()
        c[ys[kill], xs[kill]] = pad
        n_paint = int(np.any(before != pad, axis=1).sum())
        return c, n_paint

    if name == "blank":
        before = c.copy()
        c[:, :] = pad
        n_paint = int(np.any(before != c, axis=2).sum())
        return c, n_paint

    if name == "plus1_ch":
        ra = crop(ja, rect)
        rb = crop(jb, rect)
        ab_max = np.abs(ra.astype(np.int16) - rb.astype(np.int16)).max(axis=2)
        dmax = np.abs(base[y0:y0 + oh, x0:x0 + ow].astype(np.int16)
                      - ra.astype(np.int16)).max(axis=2)
        zero_ab_exact = owned & (ab_max == 0) & (dmax == 0)
        ys, xs = np.where(zero_ab_exact)
        if len(ys) == 0:
            ys, xs = np.where(owned & (ab_max == 0))
        if len(ys) == 0:
            ys, xs = np.where(owned)
        assert len(ys) > 0, "no pixel for plus1_ch on %s" % sid
        i = len(ys) // 2
        yy, xx = int(ys[i] + y0), int(xs[i] + x0)
        for ch in range(3):
            if c[yy, xx, ch] < 255:
                c[yy, xx, ch] = c[yy, xx, ch] + 1
                return c, 1
        c[yy, xx, 0] = max(0, int(c[yy, xx, 0]) - 1)
        return c, 1

    if name == "missing_dark":
        sub = c[full_owned]
        if sub.size == 0:
            return c, 0
        lum = sub.astype(np.int16).sum(axis=1)
        dark_local = lum < 120
        if not dark_local.any():
            thr = int(np.percentile(lum, 20))
            dark_local = lum <= thr
        ys, xs = np.where(full_owned)
        pick = dark_local
        if not pick.any():
            return c, 0
        before = c[ys[pick], xs[pick]].copy()
        c[ys[pick], xs[pick]] = pad
        n_paint = int(np.any(before != pad, axis=1).sum())
        return c, n_paint

    if name == "missing_transparent":
        sub = c[full_owned].astype(np.int16)
        if sub.size == 0:
            return c, 0
        chroma = sub.max(axis=1) - sub.min(axis=1)
        soft = chroma <= 25
        if soft.sum() < max(1, int(0.05 * len(soft))):
            lum = sub.sum(axis=1)
            thr = int(np.percentile(lum, 80))
            soft = lum >= thr
        ys, xs = np.where(full_owned)
        pick = soft
        if not pick.any():
            return c, 0
        before = c[ys[pick], xs[pick]].copy()
        c[ys[pick], xs[pick]] = pad
        n_paint = int(np.any(before != pad, axis=1).sum())
        return c, n_paint

    if name == "shift_x2":
        out = np.full_like(c, pad.reshape(1, 1, 3))
        out[:, 2:] = c[:, :-2]
        n_paint = int(np.any(out != c, axis=2).sum())
        return out, n_paint

    if name == "shift_y2":
        out = np.full_like(c, pad.reshape(1, 1, 3))
        out[2:, :] = c[:-2, :]
        n_paint = int(np.any(out != c, axis=2).sum())
        return out, n_paint

    if name == "recolor":
        out = np.clip(c.astype(np.int16) + 20, 0, 255).astype(np.int16)
        n_paint = (
            int(np.any(out[full_owned] != c[full_owned], axis=1).sum())
            if full_owned.any() else 0
        )
        return out, n_paint

    if name in EXTRA_COLORS:
        color = np.array(EXTRA_COLORS[name], dtype=np.int16)
        n_paint = 0
        ys, xs = np.where(full_owned)
        if len(ys):
            idx = rng.choice(len(ys), size=min(20, len(ys)), replace=False)
            for i in idx:
                y, x = int(ys[i]), int(xs[i])
                if not np.array_equal(c[y, x], color):
                    c[y, x] = color
                    n_paint += 1
        rx0, ry0, rx1, ry1 = rect
        for _ in range(20):
            y = int(rng.randint(ry0, max(ry0 + 1, ry1)))
            x = int(rng.randint(rx0, max(rx0 + 1, rx1)))
            if not np.array_equal(c[y, x], color):
                c[y, x] = color
                n_paint += 1
        return c, n_paint

    raise ValueError("unknown mutation: " + name)


def min_paint_for(name):
    if name == "plus1_ch":
        return 1
    if name in EXTRA_COLORS:
        return 5
    if name in ("erase90", "blank", "recolor", "shift_x2", "shift_y2"):
        return 8
    if name in ("missing_dark", "missing_transparent"):
        return 1
    return 1


def run_hole_counterexamples(seed_ja):
    """Assert every empirical hole / extras class trips (nonzero rejection).

    Uses synthetic zero-noise bases so the only failure mode is mask/policy.
    Returns list of (name, ok, detail).
    """
    results = []

    def check(name, ja, jb, c, expect_not_pass=True, expect_verdict=None):
        r = evaluate_state(name if name in DEFAULT_STATES else "magma_size1",
                           ja, jb, c)
        # For custom sids use the family via a real state id below.
        ok = True
        detail = "verdict=%s reason=%s owned=%s hard=%s" % (
            r["verdict"], r["reason"], r.get("n_owned"), r.get("hard_px"))
        if expect_verdict is not None:
            ok = r["verdict"] == expect_verdict
        elif expect_not_pass:
            ok = r["verdict"] != "PASS"
        return ok, detail, r

    # --- hole_magma_38070: subject must own [38,7,0]; erase it => not PASS ---
    ja, jb, perfect = make_synthetic_zero_noise("magma_size2", seed_ja)
    r0 = evaluate_state("magma_size2", ja, jb, perfect)
    if r0["verdict"] != "PASS":
        results.append(("hole_magma_38070_control", False,
                        "synthetic magma must PASS first: " + r0["reason"]))
    else:
        # Confirm mask owns the hole color on Java
        rect = roi_rect("magma_size2")
        ra = crop(ja, rect)
        m = subject_seg(ra, "magma")
        r, g, b = ra[:, :, 0], ra[:, :, 1], ra[:, :, 2]
        hit = (r == HOLE_MAGMA[0]) & (g == HOLE_MAGMA[1]) & (b == HOLE_MAGMA[2])
        owns = bool(hit.any() and bool(np.all(m[hit])))
        if not owns:
            results.append(("hole_magma_38070_owned", False,
                            "mask misses [38,7,0] hit=%d owned=%d" % (
                                int(hit.sum()),
                                int((hit & m).sum()) if hit.any() else 0)))
        else:
            # Remove hole pixels -> must reject
            cm = perfect.copy()
            x0, y0, _, _ = rect
            ys, xs = np.where(hit)
            pad = np.array(pad_color(seed_ja), dtype=np.int16)
            for yi, xi in zip(ys, xs):
                cm[y0 + yi, x0 + xi] = pad
            ok, detail, _ = check("magma_size2", ja, jb, cm)
            # re-eval with correct sid
            rr = evaluate_state("magma_size2", ja, jb, cm)
            ok = rr["verdict"] != "PASS"
            results.append(("hole_magma_38070", ok,
                            "verdict=%s hard=%s" % (rr["verdict"], rr["hard_px"])))

    # --- hole_dragon_6571122 ---
    ja, jb, perfect = make_synthetic_zero_noise("dragon_death_50", seed_ja)
    r0 = evaluate_state("dragon_death_50", ja, jb, perfect)
    if r0["verdict"] != "PASS":
        results.append(("hole_dragon_6571122_control", False,
                        "synthetic dragon must PASS: " + r0["reason"]))
    else:
        rect = roi_rect("dragon_death_50")
        ra = crop(ja, rect)
        m = subject_seg(ra, "dragon")
        r, g, b = ra[:, :, 0], ra[:, :, 1], ra[:, :, 2]
        hit = (r == HOLE_DRAGON[0]) & (g == HOLE_DRAGON[1]) & (b == HOLE_DRAGON[2])
        owns = bool(hit.any() and bool(np.all(m[hit])))
        if not owns:
            results.append(("hole_dragon_6571122_owned", False,
                            "mask misses [65,71,122] hit=%d owned=%d" % (
                                int(hit.sum()),
                                int(m[hit].sum()) if hit.any() else 0)))
        else:
            cm = perfect.copy()
            x0, y0, _, _ = rect
            pad = np.array(pad_color(seed_ja), dtype=np.int16)
            ys, xs = np.where(hit)
            for yi, xi in zip(ys, xs):
                cm[y0 + yi, x0 + xi] = pad
            # Also erase a chunk of dark body so residual is forced
            owned, _ = owned_mask(ja, jb, perfect, "dragon", rect)
            ys2, xs2 = np.where(owned)
            for i in range(min(40, len(ys2))):
                cm[y0 + ys2[i], x0 + xs2[i]] = pad
            rr = evaluate_state("dragon_death_50", ja, jb, cm)
            ok = rr["verdict"] != "PASS"
            results.append(("hole_dragon_6571122", ok,
                            "verdict=%s hard=%s owns=%s" % (
                                rr["verdict"], rr["hard_px"], owns)))

    # --- hole_slime_fringe ---
    ja, jb, perfect = make_synthetic_zero_noise("slime_size2", seed_ja)
    r0 = evaluate_state("slime_size2", ja, jb, perfect)
    if r0["verdict"] != "PASS":
        results.append(("hole_slime_fringe_control", False,
                        "synthetic slime must PASS: " + r0["reason"]))
    else:
        rect = roi_rect("slime_size2")
        ra = crop(ja, rect)
        m = subject_seg(ra, "slime")
        r, g, b = ra[:, :, 0], ra[:, :, 1], ra[:, :, 2]
        hit = (
            (r == HOLE_SLIME_FRINGE[0])
            & (g == HOLE_SLIME_FRINGE[1])
            & (b == HOLE_SLIME_FRINGE[2])
        )
        owns = bool(hit.any() and bool(np.all(m[hit])))
        cm = perfect.copy()
        x0, y0, _, _ = rect
        pad = np.array(pad_color(seed_ja), dtype=np.int16)
        # Erase fringe + 50% green body
        owned, _ = owned_mask(ja, jb, perfect, "slime", rect)
        ys, xs = np.where(owned)
        for i in range(0, len(ys), 2):
            cm[y0 + ys[i], x0 + xs[i]] = pad
        rr = evaluate_state("slime_size2", ja, jb, cm)
        ok = rr["verdict"] != "PASS" and owns
        results.append(("hole_slime_fringe", ok,
                        "verdict=%s owns_fringe=%s hard=%s hit=%d" % (
                            rr["verdict"], owns, rr["hard_px"],
                            int(hit.sum()))))

    # --- hole_fireball_thin: orange+core owned; erase => reject ---
    ja, jb, perfect = make_synthetic_zero_noise("fireball_small", seed_ja)
    r0 = evaluate_state("fireball_small", ja, jb, perfect)
    if r0["verdict"] != "PASS" or r0["n_owned"] < MIN_OWNED:
        results.append(("hole_fireball_thin_control", False,
                        "synthetic fireball must PASS with owned>=%d: v=%s n=%s" % (
                            MIN_OWNED, r0["verdict"], r0["n_owned"])))
    else:
        rect = roi_rect("fireball_small")
        ra = crop(ja, rect)
        m = subject_seg(ra, "fireball")
        r, g, b = ra[:, :, 0], ra[:, :, 1], ra[:, :, 2]
        hit = (
            (r == HOLE_FIREBALL_ORANGE[0])
            & (g == HOLE_FIREBALL_ORANGE[1])
            & (b == HOLE_FIREBALL_ORANGE[2])
        )
        owns = bool(hit.any() and (hit & m).any())
        cm = perfect.copy()
        x0, y0, _, _ = rect
        pad = np.array(pad_color(seed_ja), dtype=np.int16)
        owned, _ = owned_mask(ja, jb, perfect, "fireball", rect)
        ys, xs = np.where(owned)
        for yi, xi in zip(ys, xs):
            cm[y0 + yi, x0 + xi] = pad
        rr = evaluate_state("fireball_small", ja, jb, cm)
        ok = rr["verdict"] != "PASS" and owns
        results.append(("hole_fireball_thin", ok,
                        "verdict=%s owns_orange=%s n_owned0=%s" % (
                            rr["verdict"], owns, r0["n_owned"])))

    # --- extra_black_outside: black outside prior body still owned via C ---
    ja, jb, perfect = make_synthetic_zero_noise("magma_size1", seed_ja)
    r0 = evaluate_state("magma_size1", ja, jb, perfect)
    rect = roi_rect("magma_size1")
    x0, y0, x1, y1 = rect
    owned, _ = owned_mask(ja, jb, perfect, "magma", rect)
    # Find a pad pixel inside ROI outside owned
    ra = crop(ja, rect)
    outside = ~owned
    ys, xs = np.where(outside)
    cm = perfect.copy()
    n_paint = 0
    for i in range(min(30, len(ys))):
        # pick corners of ROI (away from center blob)
        yi, xi = int(ys[i]), int(xs[i])
        # prefer near ROI border
        if yi < 5 or xi < 5 or yi > owned.shape[0] - 6 or xi > owned.shape[1] - 6:
            cm[y0 + yi, x0 + xi] = np.array((0, 0, 0), dtype=np.int16)
            n_paint += 1
    if n_paint < 5:
        # force paint at ROI corners
        for dy, dx in ((2, 2), (2, 4), (4, 2), (3, 5), (5, 3),
                       (owned.shape[0] - 3, 2), (2, owned.shape[1] - 3)):
            cm[y0 + dy, x0 + dx] = np.array((0, 0, 0), dtype=np.int16)
            n_paint += 1
    rr = evaluate_state("magma_size1", ja, jb, cm)
    # C black extras must enter owned via subject_seg(C) and hard_px > 0
    ok = rr["verdict"] != "PASS" and rr.get("hard_px", 0) > 0
    results.append(("extra_black_outside", ok,
                    "verdict=%s hard=%s paint=%d owned0=%s owned1=%s" % (
                        rr["verdict"], rr.get("hard_px"), n_paint,
                        r0.get("n_owned"), rr.get("n_owned"))))

    # --- xp_no_orb_blocked: pad-only ROI => CAPTURE_BLOCKED, never PASS ---
    ja_xp = seed_ja.astype(np.int16, copy=True)
    # Force ROI to pure pad gray (no orb)
    rect = roi_rect("xp_orb")
    x0, y0, x1, y1 = rect
    ja_xp[y0:y1, x0:x1] = np.array((120, 120, 120), dtype=np.int16)
    jb_xp = ja_xp.copy()
    c_xp = ja_xp.copy()
    rr = evaluate_state("xp_orb", ja_xp, jb_xp, c_xp)
    ok = (
        rr["verdict"] == "CAPTURE_BLOCKED"
        and not rr.get("parity_capable", True)
        and str(rr.get("reason", "")).startswith("no_parity_capable")
    )
    results.append(("xp_no_orb_blocked", ok,
                    "verdict=%s reason=%s owned=%s" % (
                        rr["verdict"], rr["reason"], rr.get("n_owned"))))

    # --- nonzero A/B control blocked (synthetic A≠B) ---
    ja, jb, perfect = make_synthetic_zero_noise("slime_size1", seed_ja)
    jb_nz = jb.copy()
    rect = roi_rect("slime_size1")
    owned, _ = owned_mask(ja, jb, perfect, "slime", rect)
    x0, y0, _, _ = rect
    ys, xs = np.where(owned)
    assert len(ys) > 0
    for i in range(min(20, len(ys))):
        jb_nz[y0 + ys[i], x0 + xs[i], 1] = min(
            255, int(jb_nz[y0 + ys[i], x0 + xs[i], 1]) + 30)
    # C == A (mid-envelope would have been the old false PASS)
    rr = evaluate_state("slime_size1", ja, jb_nz, ja.copy())
    ok = rr["verdict"] == "CAPTURE_BLOCKED" and rr["reason"] == "nonzero_ab_on_owned"
    results.append(("nonzero_ab_control_blocked", ok,
                    "verdict=%s reason=%s ab_nz=%s" % (
                        rr["verdict"], rr["reason"], rr.get("ab_nz"))))

    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--c-frames", required=True,
                    help="dir of honest C <id>.ppm frames")
    ap.add_argument("--states", nargs="*", default=None)
    args = ap.parse_args()

    states = args.states or list(DEFAULT_STATES)
    n_fail = 0

    print("ui_entities mutation regressions (owned hard_px / CAPTURE_BLOCKED):")
    print("%-18s %-22s %8s %8s %16s  %s" % (
        "state", "case", "hard_px", "n_paint", "verdict", "note"))

    # Seed frame for synthetics
    seed_path = os.path.join(args.goldens, "slime_size1_a.png")
    if not os.path.isfile(seed_path):
        print("MISSING seed golden %s" % seed_path)
        return 1
    seed_ja = load_rgb(seed_path)

    # --- global hole / policy counterexamples ---
    print("-- hole / policy counterexamples --")
    for name, ok, detail in run_hole_counterexamples(seed_ja):
        status = "ok" if ok else "LEAK"
        if not ok:
            n_fail += 1
        print("%-18s %-22s %8s %8s %16s  %s %s" % (
            "-", name, "-", "-", "n/a", status, detail))

    for sid in states:
        ja_p = os.path.join(args.goldens, "%s_a.png" % sid)
        jb_p = os.path.join(args.goldens, "%s_b.png" % sid)
        c_p = os.path.join(args.c_frames, "%s.ppm" % sid)
        if not (os.path.isfile(ja_p) and os.path.isfile(jb_p)
                and os.path.isfile(c_p)):
            print("%-18s %-22s  MISSING assets" % (sid, "-"))
            n_fail += 1
            continue

        ja = load_rgb(ja_p)
        jb = load_rgb(jb_p)
        c0 = load_ppm(c_p)

        # --- honest candidate: residual or blocked, never PASS ---
        r0 = evaluate_state(sid, ja, jb, c0)
        if r0["verdict"] == "FAIL":
            n_fail += 1
            print("%-18s %-22s %8s %8s %16s  %s" % (
                sid, "honest", str(r0.get("hard_px")), "-", r0["verdict"],
                "NEED_CAPTURE_OK " + str(r0.get("reason"))))
            continue
        if r0["verdict"] == "PASS":
            n_fail += 1
            print("%-18s %-22s %8s %8s %16s  %s" % (
                sid, "honest", str(r0.get("hard_px")), "-", r0["verdict"],
                "EXPECTED_RESIDUAL_OR_BLOCKED"))
        else:
            print("%-18s %-22s %8s %8s %16s  %s hard_px=%s owned=%s ab_nz=%s" % (
                sid, "honest", str(r0.get("hard_px")), "-", r0["verdict"],
                "HONEST_OK", r0.get("hard_px"), r0.get("n_owned"),
                r0.get("ab_nz")))

        # --- real-golden Java_A control ---
        r_ctrl = evaluate_state(sid, ja, jb, ja.copy())
        if r_ctrl.get("ab_blocked") or (
                r_ctrl.get("noise_max", 0) and r_ctrl["noise_max"] > 0):
            # Nonzero A/B: must be CAPTURE_BLOCKED, never PASS
            if r_ctrl["verdict"] != "CAPTURE_BLOCKED":
                n_fail += 1
                print("%-18s %-22s %8s %8s %16s  %s reason=%s" % (
                    sid, "control_real_nz_ab", str(r_ctrl.get("hard_px")),
                    "-", r_ctrl["verdict"], "MUST_BLOCK",
                    r_ctrl.get("reason")))
            else:
                print("%-18s %-22s %8s %8s %16s  %s ab_nz=%s" % (
                    sid, "control_real_nz_ab", str(r_ctrl.get("hard_px")),
                    "-", r_ctrl["verdict"], "BLOCKED_OK",
                    r_ctrl.get("ab_nz")))
        elif not r_ctrl.get("parity_capable", True):
            if r_ctrl["verdict"] != "CAPTURE_BLOCKED":
                n_fail += 1
                print("%-18s %-22s %8s %8s %16s  %s" % (
                    sid, "control_real_empty", str(r_ctrl.get("hard_px")),
                    "-", r_ctrl["verdict"], "MUST_BLOCK_EMPTY"))
            else:
                print("%-18s %-22s %8s %8s %16s  %s" % (
                    sid, "control_real_empty", str(r_ctrl.get("hard_px")),
                    "-", r_ctrl["verdict"], "BLOCKED_OK"))
        else:
            # Zero A/B + capable: Java_A copy must PASS
            if r_ctrl["verdict"] != "PASS":
                n_fail += 1
                print("%-18s %-22s %8s %8s %16s  %s reason=%s" % (
                    sid, "control_real_zero_ab", str(r_ctrl.get("hard_px")),
                    "-", r_ctrl["verdict"], "MUST_PASS",
                    r_ctrl.get("reason")))
            else:
                print("%-18s %-22s %8s %8s %16s  %s" % (
                    sid, "control_real_zero_ab", str(r_ctrl.get("hard_px")),
                    "-", r_ctrl["verdict"], "PASS_OK"))

        # --- synthetic zero-noise control must PASS ---
        sja, sjb, sperfect = make_synthetic_zero_noise(sid, seed_ja)
        r_syn = evaluate_state(sid, sja, sjb, sperfect)
        if r_syn["verdict"] != "PASS":
            n_fail += 1
            print("%-18s %-22s %8s %8s %16s  %s reason=%s owned=%s" % (
                sid, "control_synth_zero", str(r_syn.get("hard_px")),
                "-", r_syn["verdict"], "SYNTH_MUST_PASS",
                r_syn.get("reason"), r_syn.get("n_owned")))
            # Still run mutations to surface more leaks
        else:
            print("%-18s %-22s %8s %8s %16s  %s" % (
                sid, "control_synth_zero", str(r_syn.get("hard_px")),
                "-", r_syn["verdict"], "PASS_OK"))

        for mut in MUTATION_NAMES:
            cm, n_paint = mutate(sperfect, mut, sja, sjb, sid)
            r = evaluate_state(sid, sja, sjb, cm)
            min_p = min_paint_for(mut)
            rejected = r["verdict"] != "PASS"
            paint_ok = n_paint >= min_p
            residual_ok = True
            if mut in ("erase90", "blank", "recolor") and r.get("hard_px", 0) == 0:
                # blank may CAPTURE_BLOCKED (empty owned) which is also reject
                if r["verdict"] == "PASS":
                    residual_ok = False
                elif r["verdict"] == "CAPTURE_BLOCKED":
                    residual_ok = True
                else:
                    residual_ok = r.get("hard_px", 0) > 0 or r["verdict"] != "PASS"
            ok = rejected and paint_ok and residual_ok
            status = "ok" if ok else "LEAK"
            if not ok:
                n_fail += 1
            print("%-18s %-22s %8s %8d %16s  %s reject=%s paint_ok=%s" % (
                sid, mut,
                str(r.get("hard_px")),
                n_paint,
                r["verdict"],
                status,
                rejected,
                paint_ok,
            ))

    if n_fail:
        print("ui_entities mutations: FAIL (%d)" % n_fail)
        return 1
    print("ui_entities mutations: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
