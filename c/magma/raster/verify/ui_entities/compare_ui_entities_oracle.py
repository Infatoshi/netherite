#!/usr/bin/env python3
"""Hard full-ROI gate: ui_entities Java goldens vs C frame_capture.

Product parity is max-channel exactness across the complete family ROI and
requires deterministic Java A/B zero across that ROI. Nonzero A/B is never
PASS.

The complete ROI is intentional. Color-derived subject segmentation repeatedly
proved unsound: it either omitted dim/dark/extra entity pixels or swallowed
grass/end-stone scene pixels. The interactive raster product must reproduce the
same composed pixels, so scenery inside the pinned family ROI is part of the
contract rather than an exempt background.

A/B + verdict policy:
  - Measure noise_mean / noise_max (max channel) on the full ROI.
  - If ANY ROI A/B pixel differs (noise_max > 0 or ab_nz > 0):
      CAPTURE_BLOCKED — never PASS, even for C == Java_A or mid-envelope C.
  - xp_orb remains CAPTURE_BLOCKED until the Java golden visibly contains an
    orb; matching the empty gray pad is not entity parity.
  - Zero A/B + capable capture: hard_thr = 0; PASS only if hard_px == 0.
  - Zero A/B + hard residual: RESIDUAL (nonzero exit; no parity claim).
  - Missing files / candidate fail: FAIL.

Deleted: owned_within_per_pixel_ab PASS (mid-envelope is not product parity).
"""
from __future__ import print_function

import argparse
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image

W, H = 854, 480

# Family A/B noise ceilings on owned (mean abs RGB). Excess => capture FAIL
# (broken pin), distinct from CAPTURE_BLOCKED (nonzero but under ceiling).
NOISE_MEAN_MAX = {
    "default": 3.0,
    "slime": 6.0,
    "magma": 10.0,
    "dragon": 2.0,
    "dig": 12.0,       # particles re-roll; still not a pass budget
    "fireball": 4.0,
    "xp_orb": 2.0,
}
# Min owned pixels for a parity-capable mask (non-vacuous subject).
MIN_OWNED = 8
RESIDUAL_LOC_SAMPLES = 12

DEFAULT_STATES = [
    "slime_size1", "slime_size2", "slime_size4", "slime_squish",
    "magma_size1", "magma_size2", "magma_size4", "magma_squish",
    "dragon_death_50", "dragon_death_100", "dragon_death_190",
    "dig_stone", "dig_grass",
    "fireball_small", "fireball_dragon", "xp_orb",
]


def roi_rect(state_id):
    """Exclusive (x0,y0,x1,y1) feature ROI. Tuned for driver camera/subject."""
    if state_id.startswith("dragon_death"):
        return (80, 40, W - 80, H - 80)
    if state_id.startswith("dig_"):
        return (W // 2 - 120, H // 2 - 80, W // 2 + 120, H // 2 + 100)
    if state_id.startswith("fireball"):
        # Small fireball sits above mid-frame (y≈111–144); include upper band.
        return (W // 2 - 100, H // 2 - 150, W // 2 + 100, H // 2 + 80)
    if state_id == "xp_orb":
        return (W // 2 - 60, H // 2 - 60, W // 2 + 60, H // 2 + 40)
    # Slime/magma: include upper sky so tall squish (Y scale) is inside the ROI.
    if state_id.startswith("slime") or state_id.startswith("magma"):
        return (W // 2 - 140, 40, W // 2 + 140, H - 40)
    return (W // 2 - 140, H // 3, W // 2 + 140, H - 60)


def family(state_id):
    if state_id.startswith("slime"):
        return "slime"
    if state_id.startswith("magma"):
        return "magma"
    if state_id.startswith("dragon"):
        return "dragon"
    if state_id.startswith("dig"):
        return "dig"
    if state_id.startswith("fireball"):
        return "fireball"
    if state_id == "xp_orb":
        return "xp_orb"
    return "default"


def load_rgb(path):
    im = Image.open(path).convert("RGB")
    a = np.asarray(im, dtype=np.int16)
    if a.shape[0] != H or a.shape[1] != W:
        out = np.zeros((H, W, 3), dtype=np.int16)
        h, w = a.shape[:2]
        ys, xs = min(H, h), min(W, w)
        out[:ys, :xs] = a[:ys, :xs]
        return out
    return a


def load_ppm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError("not P6: " + path)
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        wh = line.split()
        while len(wh) < 2:
            wh += f.readline().split()
        w, h = int(wh[0]), int(wh[1])
        maxv = int(f.readline().split()[0])
        assert maxv == 255
        raw = f.read(w * h * 3)
    a = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 3).astype(np.int16)
    if h != H or w != W:
        out = np.zeros((H, W, 3), dtype=np.int16)
        ys, xs = min(H, h), min(W, w)
        out[:ys, :xs] = a[:ys, :xs]
        return out
    return a


def crop(a, rect):
    x0, y0, x1, y1 = rect
    x0 = max(0, min(W, x0)); x1 = max(0, min(W, x1))
    y0 = max(0, min(H, y0)); y1 = max(0, min(H, y1))
    if a.ndim == 2:
        return a[y0:y1, x0:x1]
    return a[y0:y1, x0:x1]


def mean_abs(a, b):
    if a.size == 0 or b.size == 0:
        return float("nan")
    return float(np.abs(a.astype(np.int16) - b.astype(np.int16)).mean())


def _sky_mask(r, g, b):
    """Pale blue sky (capture profile, no clouds)."""
    return (
        (b > 200) & (g > 180) & (r > 140)
        & ((b.astype(np.int16) - r.astype(np.int16)) > 30)
    )


def _pad_mask(r, g, b):
    """Gray cobble pad: low-chroma midtones. Pure black/bright are not pad."""
    chroma = (
        np.maximum(np.maximum(r, g), b) - np.minimum(np.minimum(r, g), b)
    )
    return (
        (chroma <= 14)
        & (r >= 40) & (r <= 200)
        & (np.abs(r.astype(np.int16) - g.astype(np.int16)) <= 10)
        & (np.abs(g.astype(np.int16) - b.astype(np.int16)) <= 10)
    )


def _endstone_mask(r, g, b):
    chroma = (
        np.maximum(np.maximum(r, g), b) - np.minimum(np.minimum(r, g), b)
    )
    return (
        (r > 150) & (g > 140) & (b > 70) & (b < 190)
        & (g > b + 15) & (r > b + 15)
        & (chroma > 15) & (chroma < 100)
    )


def _dirt_mask(r, g, b):
    chroma = (
        np.maximum(np.maximum(r, g), b) - np.minimum(np.minimum(r, g), b)
    )
    return (
        (r > g + 8) & (r > b + 8)
        & (r > 55) & (r < 170) & (g < 130) & (b < 110)
        & (chroma > 12) & (chroma < 90) & (g > 30)
    )


def _grass_mask(r, g, b):
    return (
        (g > r + 18) & (g > b + 12) & (g > 55) & (r < 170) & (b < 150)
    )


def dilate_bool(mask, radius):
    """Chebyshev dilate without scipy."""
    if radius <= 0 or not mask.any():
        return mask.copy()
    h, w = mask.shape
    out = mask.copy()
    ys, xs = np.where(mask)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dy == 0 and dx == 0:
                continue
            yy = ys + dy
            xx = xs + dx
            keep = (yy >= 0) & (yy < h) & (xx >= 0) & (xx < w)
            out[yy[keep], xx[keep]] = True
    return out


def subject_seg(img, fam, ab_unstable=None):
    """Conservative subject mask for one frame (not C-only / not free warm ROI).

    Derivation:
      - Exclude world: sky, gray pad, endstone shelf, dirt, horizon grass.
      - Own family subject (body/gel/rays/fire/spray) with explicit hole
        closures: magma [38,7,0], dragon [65,71,122], slime greenish fringes,
        near-black extras, thin fireball core+fire, xp green-gold only.
      - Dig uses A/B re-roll spray (particles are pad-colored).
      - xp with no visible orb returns empty (CAPTURE_BLOCKED; no full-ROI).
    """
    r = img[:, :, 0].astype(np.int16)
    g = img[:, :, 1].astype(np.int16)
    b = img[:, :, 2].astype(np.int16)
    h = r.shape[0]
    sky = _sky_mask(r, g, b)
    pad = _pad_mask(r, g, b)
    endstone = _endstone_mask(r, g, b)
    dirt = _dirt_mask(r, g, b)
    grass = _grass_mask(r, g, b)
    near_black = (r + g + b < 45) & ~sky & ~pad
    ys = np.arange(h)[:, None]

    if fam == "slime":
        # Body + LayerSlimeGel: broad greenish incl. translucent fringes.
        # Horizon grass only (top strip); mid-frame green is subject.
        top_grass = grass & (ys < 40)
        bg = sky | pad | endstone | dirt | top_grass
        greenish = (
            (g > r + 8) & (g > b + 4) & (g > 50) & (r < 190) & ~pad & ~sky
        )
        # Soft gel / dark green edges that fail the bright-body cut.
        soft_gel = (
            (g >= r - 5) & (g > b) & (g > 40) & (g < 160)
            & (r < 150) & ~pad & ~sky & ~top_grass
        )
        subj = (greenish | soft_gel | near_black | (~bg & (g > r))) & ~sky
        return dilate_bool(subj, 1)

    if fam == "magma":
        # Dark charcoal + warm face plates. Close [38,7,0]: sum==45, r==38
        # (old thr r>40 and sum<45 both missed it).
        bg = sky | pad | endstone | dirt | grass
        warm = (r > g + 5) & (r > b + 5) & (r > 25) & ~sky
        dark = (r + g + b <= 55) & ~sky & ~pad
        dark_red = (
            (r >= g) & (r >= b) & (r + g + b < 120) & (r > 8) & ~sky & ~pad
        )
        subj = (warm | dark | dark_red | near_black | (~bg)) & ~sky
        return dilate_bool(subj, 1)

    if fam == "dragon":
        # Body dark purple/black incl. [65,71,122] (b=122 > old b<110 cut),
        # death rays, dissolve flash, pink accents. No bare ~bg (pale sky
        # gradients would swallow the ROI).
        dark = (r < 100) & (g < 100) & (b < 135) & ~sky
        purple = (
            (b >= r - 5) & (b > g) & (b > 55) & (b < 160)
            & (r < 110) & (g < 110) & ~sky
        )
        ray = (r > 160) & (b > 160) & (g < r - 20)
        dissolve = (r > 180) & (g > 180) & (b > 180) & ~sky
        pink = (r > 150) & (b > 120) & (g < r - 30) & ~sky
        subj = dark | purple | ray | dissolve | pink | near_black
        # Drop endstone / dirt that slipped into dark via shade.
        subj = subj & ~endstone & ~dirt & ~pad
        return dilate_bool(subj, 1)

    if fam == "dig":
        # ParticleDigging crumbs re-roll A/B; pad-colored so color-only fails.
        # Spray-anchor A/B instability + stone/dirt/grass flecks nearby.
        # near_black owns outside-mask black extras on C.
        gray = (
            (np.abs(r - g) <= 8) & (np.abs(g - b) <= 8)
            & (r >= 30) & (r <= 200)
        )
        dirt_c = (r > g + 5) & (r > b + 5) & (r > 40) & (r < 180) & (g < 140)
        grass_fleck = (
            (g > r + 8) & (g > b + 5) & (g > 40) & (g < 190) & (r < 170)
        )
        crumbs = (gray | dirt_c | grass_fleck) & ~sky
        if ab_unstable is not None and ab_unstable.any():
            spray = dilate_bool(ab_unstable, 8)
            return (
                ab_unstable
                | spray
                | (crumbs & dilate_bool(ab_unstable, 14))
                | near_black
            )
        return crumbs | near_black

    if fam == "fireball":
        # Fire orange/yellow + dark core; dilate for thin fireball_small.
        fire = (
            ((r > 120) & (r > g) & (r > b - 10) & (g > 30) & (b < 160))
            | ((r > 180) & (g > 80) & (b < 130))
            | ((r > 200) & (g > 150) & (b < 170))
        )
        core = (r + g + b < 80) & ~sky & ~pad
        subj = dilate_bool(fire | core | near_black, 2)
        return subj & ~sky & ~pad & ~grass & ~dirt

    if fam == "xp_orb":
        # experience_orb.png green-gold / yellow only. Empty => no orb in
        # golden (CAPTURE_BLOCKED). Never full-ROI pad PASS.
        green_gold = (
            (g > 90) & (g >= r - 15) & (g > b + 10)
            & (r > 40) & (b < 200) & ~sky & ~pad
        )
        yellow = (
            (r > 140) & (g > 140) & (b < r - 20) & (b < g - 20) & ~sky & ~pad
        )
        return green_gold | yellow

    # default: non-sky non-pad
    return ~sky & ~pad


def owned_mask(ja, jb, c, fam, rect):
    """Complete feature ROI, with an explicit xp-orb presence prerequisite."""
    ra, rb = crop(ja, rect), crop(jb, rect)
    ab_max = np.abs(ra.astype(np.int16) - rb.astype(np.int16)).max(axis=2)
    ab_unstable = ab_max > 0
    owned = np.ones(ra.shape[:2], dtype=bool)
    xp_visible = True
    if fam == "xp_orb":
        xp_visible = bool(subject_seg(ra, fam).any()
                          or subject_seg(rb, fam).any())
    n = int(owned.sum())
    parity_capable = n >= MIN_OWNED and xp_visible
    return owned, {
        "j_a": n,
        "j_b": n,
        "c": n,
        "fallback_full_roi": True,
        "parity_capable": parity_capable,
        "ab_unstable_n": int(ab_unstable.sum()),
        "xp_visible": xp_visible,
        "derivation": "complete_family_roi",
    }


def clusters(diff_mask, min_px=32):
    ys, xs = np.where(diff_mask)
    if len(ys) == 0:
        return []
    bins = {}
    for y, x in zip(ys.tolist(), xs.tolist()):
        key = (y // 16, x // 16)
        bins.setdefault(key, 0)
        bins[key] += 1
    out = []
    for (by, bx), n in sorted(bins.items(), key=lambda kv: -kv[1]):
        if n >= min_px:
            out.append({"by": by * 16, "bx": bx * 16, "px": n})
    return out[:12]


def evaluate_state(sid, ja_full, jb_full, c_full):
    """Hard owned-pixel evaluation. Used by gate + mutation suite."""
    fam = family(sid)
    rect = roi_rect(sid)
    noise_lim = NOISE_MEAN_MAX.get(fam, NOISE_MEAN_MAX["default"])

    ra = crop(ja_full, rect)
    rb = crop(jb_full, rect)
    rc = crop(c_full, rect)
    h = min(ra.shape[0], rb.shape[0], rc.shape[0])
    w = min(ra.shape[1], rb.shape[1], rc.shape[1])
    ra, rb, rc = ra[:h, :w], rb[:h, :w], rc[:h, :w]

    owned, mask_info = owned_mask(ja_full, jb_full, c_full, fam, rect)
    owned = owned[:h, :w]
    n_owned = int(owned.sum())
    parity_capable = bool(mask_info.get("parity_capable", n_owned >= MIN_OWNED))

    ab_ch = np.abs(ra.astype(np.int16) - rb.astype(np.int16)).astype(np.float64)
    ab_maxch_px = ab_ch.max(axis=2)

    if n_owned > 0:
        noise_mean = float(ab_ch[owned].mean())
        noise_max = float(ab_maxch_px[owned].max()) if owned.any() else 0.0
        ab_nz = int((ab_maxch_px[owned] > 0).sum())
    else:
        noise_mean = float("nan")
        noise_max = float("nan")
        ab_nz = 0

    # Product rule: any owned A/B difference blocks PASS forever.
    ab_blocked = (
        n_owned > 0
        and noise_max == noise_max
        and (noise_max > 0.0 or ab_nz > 0)
    )
    zero_ab = (
        n_owned > 0
        and noise_max == noise_max
        and noise_max == 0.0
        and ab_nz == 0
    )

    residual_locs = []
    residual_bbox = None
    hard_mask = np.zeros((h, w), dtype=bool)
    if n_owned > 0:
        diff_ch = np.abs(rc.astype(np.int16) - ra.astype(np.int16)).astype(
            np.float64)
        diff_mean_px = diff_ch.mean(axis=2)
        diff_maxch_px = diff_ch.max(axis=2)
        c_vs_j = float(diff_mean_px[owned].mean())
        max_diff = float(diff_maxch_px[owned].max())
        # Hard residual only meaningful under zero A/B (parity path).
        # Under nonzero A/B we still report envelope exceedance for diagnostics
        # but verdict is CAPTURE_BLOCKED regardless of C.
        if zero_ab:
            hard_mask = owned & (diff_maxch_px > 0)
        else:
            hard_mask = owned & (diff_maxch_px > ab_maxch_px)
        hard_px = int(hard_mask.sum())
        n_stable = int((owned & (ab_maxch_px == 0)).sum())
        stable_frac = float(n_stable) / float(n_owned) if n_owned else 0.0
        if hard_px > 0:
            ys, xs = np.where(hard_mask)
            x0, y0, _, _ = rect
            full_xs = xs + x0
            full_ys = ys + y0
            residual_bbox = [
                int(full_xs.min()), int(full_ys.min()),
                int(full_xs.max()), int(full_ys.max()),
            ]
            step = max(1, hard_px // RESIDUAL_LOC_SAMPLES)
            for i in range(0, hard_px, step):
                if len(residual_locs) >= RESIDUAL_LOC_SAMPLES:
                    break
                yi, xi = int(ys[i]), int(xs[i])
                residual_locs.append({
                    "x": int(full_xs[i]),
                    "y": int(full_ys[i]),
                    "maxch": int(diff_maxch_px[yi, xi]),
                    "ab_maxch": int(ab_maxch_px[yi, xi]),
                    "c": [int(rc[yi, xi, k]) for k in range(3)],
                    "j": [int(ra[yi, xi, k]) for k in range(3)],
                })
        cls = clusters(hard_mask, min_px=16)
    else:
        c_vs_j = float("nan")
        max_diff = float("nan")
        hard_px = 0
        n_stable = 0
        stable_frac = 0.0
        cls = []

    # Presence: Java A ROI vs sky corner (feature not empty sky).
    sky = ja_full[8:8 + h, 8:8 + w]
    if sky.shape != ra.shape:
        sky = ja_full[8:8 + ra.shape[0], 8:8 + ra.shape[1]]
    presence = mean_abs(ra, sky) if sky.size == ra.size else 0.0

    # Capture integrity: over-ceiling noise is FAIL (broken pin), not a PASS
    # loophole. Empty/non-capable ownership is CAPTURE_BLOCKED.
    noise_over = (
        noise_mean == noise_mean and noise_mean > noise_lim
    )

    if noise_over:
        verdict = "FAIL"
        reason = "capture_noise"
    elif not parity_capable:
        verdict = "CAPTURE_BLOCKED"
        if fam == "xp_orb" and n_owned == 0:
            reason = "no_parity_capable_ownership_xp_missing_orb"
        else:
            reason = "no_parity_capable_ownership"
    elif ab_blocked:
        verdict = "CAPTURE_BLOCKED"
        reason = "nonzero_ab_on_owned"
    elif hard_px == 0 and zero_ab:
        verdict = "PASS"
        reason = "owned_exact_zero_ab"
    else:
        verdict = "RESIDUAL"
        reason = "hard_residual"

    return {
        "id": sid,
        "family": fam,
        "noise": noise_mean,
        "noise_max": noise_max,
        "ab_nz": ab_nz,
        "c_vs_j": c_vs_j,
        "max_diff": max_diff,
        "hard_px": hard_px,
        "hard_thr": 0 if zero_ab else -1,
        "hard_thr_note": (
            "0 (A/B exact)" if zero_ab else "CAPTURE_BLOCKED_nonzero_ab"
        ),
        "n_owned": n_owned,
        "n_stable": n_stable,
        "stable_frac": stable_frac,
        "presence": presence,
        "verdict": verdict,
        "reason": reason,
        "roi": list(rect),
        "noise_limit": noise_lim,
        "mask_info": mask_info,
        "residual_bbox": residual_bbox,
        "residual_locs": residual_locs,
        "clusters": cls[:8],
        "parity_capable": parity_capable,
        "ab_blocked": ab_blocked,
        "rule": "complete_roi_zero_ab_hard",
    }


def list_states(goldens, only=None):
    man = os.path.join(goldens, "capture_manifest.json")
    if os.path.isfile(man):
        with open(man) as f:
            states = json.load(f).get("states", [])
    else:
        states = []
        for fn in sorted(os.listdir(goldens)):
            if fn.endswith("_a.png"):
                states.append(fn[:-6])
    if only:
        states = [s for s in states if s in only]
    if not states:
        states = list(DEFAULT_STATES)
    return states


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--candidate", default=None,
                    help="path to entity_oracle_candidate binary")
    ap.add_argument("--c-out", default="/tmp/magma_ui_entities_c")
    ap.add_argument("--c-frames", default=None,
                    help="dir of prebuilt <id>.ppm (skip candidate run)")
    ap.add_argument("--info", action="store_true",
                    help="report only; do not fail process on residual/blocked")
    ap.add_argument("--states", nargs="*", default=None)
    ap.add_argument("--json-out", default=None)
    args = ap.parse_args()

    states = list_states(args.goldens, args.states)
    c_dir = args.c_frames or args.c_out
    os.makedirs(c_dir, exist_ok=True)

    failed = 0
    residual = 0
    blocked = 0
    results = []

    print(
        "%-20s %8s %8s %7s %8s %8s %7s %16s  %s" % (
            "state", "noise", "nmax", "owned", "stable", "c_vs_j", "hard_px",
            "verdict", "reason"))

    for sid in states:
        ja_p = os.path.join(args.goldens, "%s_a.png" % sid)
        jb_p = os.path.join(args.goldens, "%s_b.png" % sid)
        meta = os.path.join(args.goldens, "meta", "%s.json" % sid)
        c_ppm = os.path.join(c_dir, "%s.ppm" % sid)

        if not (os.path.isfile(ja_p) and os.path.isfile(jb_p)):
            print("%-20s  MISSING golden" % sid)
            failed += 1
            results.append({"id": sid, "verdict": "FAIL", "reason": "missing_golden"})
            continue

        need_run = not os.path.isfile(c_ppm)
        if need_run:
            if not args.candidate:
                print("%-20s  MISSING c-frame and no --candidate" % sid)
                failed += 1
                results.append({"id": sid, "verdict": "FAIL",
                                "reason": "missing_c"})
                continue
            if not os.path.isfile(meta):
                print("%-20s  MISSING meta" % sid)
                failed += 1
                results.append({"id": sid, "verdict": "FAIL",
                                "reason": "missing_meta"})
                continue
            cmd = [args.candidate, "--state", sid, "--meta", meta,
                   "--ppm", c_ppm, "--w", str(W), "--h", str(H)]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not os.path.isfile(c_ppm):
                print("FAIL %s candidate rc=%d\n%s\n%s" % (
                    sid, r.returncode, r.stdout, r.stderr))
                failed += 1
                results.append({"id": sid, "verdict": "FAIL",
                                "reason": "candidate_fail"})
                continue

        ja = load_rgb(ja_p)
        jb = load_rgb(jb_p)
        c = load_ppm(c_ppm)
        row = evaluate_state(sid, ja, jb, c)
        results.append(row)

        v = row["verdict"]
        if v == "FAIL":
            failed += 1
        elif v == "RESIDUAL":
            residual += 1
        elif v == "CAPTURE_BLOCKED":
            blocked += 1

        print("%-20s %8.3f %8.1f %7d %8d %8.3f %7d %16s  %s" % (
            sid,
            row["noise"] if row["noise"] == row["noise"] else -1.0,
            row["noise_max"] if row["noise_max"] == row["noise_max"] else -1.0,
            row["n_owned"],
            row["n_stable"],
            row["c_vs_j"] if row["c_vs_j"] == row["c_vs_j"] else -1.0,
            row["hard_px"] if row["hard_px"] is not None else -1,
            v,
            row["reason"],
        ))
        if v not in ("PASS",) and row.get("residual_bbox"):
            print("  residual_bbox=%s thr=%s maxch=%s mask=%s" % (
                row["residual_bbox"], row["hard_thr"], row["max_diff"],
                row.get("mask_info")))
            for loc in (row.get("residual_locs") or [])[:4]:
                print("    @(%d,%d) maxch=%d C=%s J=%s" % (
                    loc["x"], loc["y"], loc["maxch"], loc["c"], loc["j"]))
        if v not in ("PASS",) and row.get("clusters"):
            print("  clusters: %s" % row["clusters"][:6])

    out_json = args.json_out or os.path.join(c_dir, "gate_report.json")
    report = {
        "results": results,
        "failed": failed,
        "residual": residual,
        "blocked": blocked,
        "total": len(states),
        "policy": (
            "owned=complete family ROI; PASS only if ROI A/B noise_max==0 "
            "AND hard_px==0; nonzero A/B => CAPTURE_BLOCKED; xp missing a "
            "visible orb => CAPTURE_BLOCKED; RESIDUAL/CAPTURE_BLOCKED/FAIL "
            "are nonzero exit"
        ),
    }
    with open(out_json, "w") as f:
        json.dump(report, f, indent=2)

    n_pass = sum(1 for r in results if r.get("verdict") == "PASS")
    print("report: %s  fail=%d residual=%d blocked=%d pass=%d / %d" % (
        out_json, failed, residual, blocked, n_pass, len(states)))
    print(
        "PASS = hard_px==0 on parity-capable owned with zero A/B. "
        "CAPTURE_BLOCKED = nonzero A/B or no capable ownership. "
        "RESIDUAL = zero A/B capture OK, C residual. "
        "FAIL = missing/over-ceiling noise."
    )

    if args.info:
        return 0
    # Product gate: any non-PASS is nonzero (no false parity).
    if failed or residual or blocked:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
