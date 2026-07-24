#!/usr/bin/env python3
"""Feature-specific ROI compare gate for ui_hud oracle goldens.

For each state ID:
  noise  = mean |Java_a - Java_b| over ROI
  c_vs_j = mean |C - Java_a| over ROI  (only where C painted non-gray)
  gate   = noise + MARGIN

Verdicts (capture integrity first; no false parity claims):
  FAIL        - missing files, capture noise over ceiling, C drew nothing on hard
  CAPTURE_OK  - soft state: A/B frozen + feature ROI present; no hard C parity claim
  PASS        - hard state: C painted pixels within noise+margin (parity claim)
  RESIDUAL    - hard state: capture OK but C-vs-J exceeds gate (nonzero exit)

Hard RESIDUAL returns nonzero. Soft states never claim pixel parity.
Gray C backdrop is composition isolation only; not a live-world equivalence claim.
Inside-block C uses real atlas particle UVs (not solid synthetic texels).
"""
from __future__ import print_function

import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

W, H = 854, 480
S = 2
CX = (W + S - 1) // S // 2  # 213
SH = (H + S - 1) // S       # 240
HB_X = (CX - 91) * S        # 244
HB_Y = (SH - 22) * S        # 436
J1 = (SH - 39) * S          # 402

# Capture noise ceiling (must match driver intent; no 40 loophole).
NOISE_MAX_DEFAULT = 2.0
NOISE_MAX = {
    "hud_hurt_flash_on": 3.0,
    "hud_hurt_flash_off": 3.0,
    "hand_bow_pull20": 3.0,
    "overlay_portal_050": 12.0,
    "overlay_fire": 35.0,
    "hud_death": 5.0,
    "overlay_inside_stone": 3.0,
    "overlay_inside_grass": 3.0,
    "overlay_underwater": 3.0,
}


def roi_rect(name):
    """Return (x0,y0,x1,y1) exclusive ROI for a state id / feature class."""
    if name in ("hud_armor_iron",):
        return (HB_X, J1 - 10 * S, HB_X + 10 * 8 * S, J1 + 9 * S)
    if name in ("hud_absorption_armor",):
        return (HB_X, J1 - 20 * S, HB_X + 10 * 8 * S, J1 + 9 * S)
    if name in ("hud_hurt_flash_on", "hud_hurt_flash_off"):
        return (HB_X, J1, HB_X + 10 * 8 * S, J1 + 9 * S)
    if name in ("hud_hunger_poison",):
        x1 = HB_X + 182 * S
        return (x1 - 10 * 8 * S - 9 * S, J1, x1, J1 + 9 * S)
    if name in ("hud_air_partial",):
        air_y = (SH - 49) * S
        x1 = (CX + 91) * S
        return (x1 - 10 * 8 * S - 9 * S, air_y, x1, air_y + 9 * S)
    if name in ("hud_xp_half",):
        xp_y = (SH - 29) * S
        return (HB_X, xp_y - 12 * S, HB_X + 182 * S, xp_y + 5 * S)
    if name in ("hud_durability_half",):
        ix = HB_X + 3 * S
        iy = HB_Y + 3 * S
        return (ix, iy + 12 * S, ix + 14 * S, iy + 16 * S)
    if name in ("hud_boss_half",):
        bb_x = (CX - 91) * S
        bb_y = 12 * S
        return (bb_x, bb_y - 10 * S, bb_x + 182 * S, bb_y + 6 * S)
    if name in ("hud_death",):
        by = H // 2 - 18
        return (0, by, W, by + 36)
    if name.startswith("hand_"):
        return (W * 2 // 3, H * 2 // 3, W - 8, H - 8)
    if name.startswith("overlay_"):
        return (2, 2, W - 2, H - 2)
    return (0, 0, W, H)


# Hard gate: sprite-dominated HUD ROIs where C composition can claim parity.
# Viewmodels / full-frame overlays remain soft (CAPTURE_OK / residual report only).
HARD = {
    "hud_armor_iron",
    "hud_absorption_armor",
    "hud_hurt_flash_on",
    "hud_hurt_flash_off",
    "hud_hunger_poison",
    "hud_air_partial",
    "hud_xp_half",
    "hud_durability_half",
    "hud_boss_half",
}


def load_rgb(path):
    im = Image.open(path).convert("RGB")
    a = np.asarray(im, dtype=np.int16)
    if a.shape[0] != H or a.shape[1] != W:
        out = np.zeros((H, W, 3), dtype=np.int16)
        h, w = a.shape[:2]
        y0 = max(0, (H - h) // 2)
        x0 = max(0, (W - w) // 2)
        ys = min(H, h)
        xs = min(W, w)
        out[y0:y0 + ys, x0:x0 + xs] = a[:ys, :xs]
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--cframes", required=True)
    ap.add_argument("--margin", type=float, default=2.0)
    ap.add_argument("--report", default="")
    args = ap.parse_args()

    ids = [
        "hud_armor_iron", "hud_absorption_armor",
        "hud_hurt_flash_on", "hud_hurt_flash_off",
        "hud_hunger_poison", "hud_air_partial", "hud_xp_half",
        "hud_durability_half", "hud_boss_half", "hud_death",
        "hand_bow_pull20", "hand_eat_mid", "hand_block_shield",
        "overlay_inside_stone", "overlay_inside_grass",
        "overlay_portal_050", "overlay_fire", "overlay_underwater",
    ]

    # Reject contaminated legacy name if still present.
    legacy = os.path.join(args.goldens, "hand_block_sword_a.png")
    if os.path.isfile(legacy):
        print("FAIL: contaminated golden hand_block_sword_* present; "
              "delete and recapture as hand_block_shield", file=sys.stderr)

    print("%-24s %10s %10s %10s %10s  %s" % (
        "state", "noise", "C-vs-J", "gate", "verdict", "roi"))
    n_fail = 0
    n_residual = 0
    blocked = []
    residuals = []
    rows = []
    for sid in ids:
        ja_p = os.path.join(args.goldens, "%s_a.png" % sid)
        jb_p = os.path.join(args.goldens, "%s_b.png" % sid)
        c_p = os.path.join(args.cframes, "c_%s.ppm" % sid)
        rect = roi_rect(sid)
        if not (os.path.isfile(ja_p) and os.path.isfile(jb_p)):
            print("%-24s %10s %10s %10s %10s  MISSING JAVA" % (
                sid, "-", "-", "-", "FAIL"))
            blocked.append(sid)
            n_fail += 1
            rows.append({
                "id": sid, "noise": None, "c_vs_j": None, "gate": None,
                "verdict": "FAIL", "roi": list(rect), "hard": sid in HARD,
                "reason": "missing_java",
            })
            continue
        if not os.path.isfile(c_p):
            print("%-24s %10s %10s %10s %10s  MISSING C" % (
                sid, "-", "-", "-", "FAIL"))
            blocked.append(sid)
            n_fail += 1
            rows.append({
                "id": sid, "noise": None, "c_vs_j": None, "gate": None,
                "verdict": "FAIL", "roi": list(rect), "hard": sid in HARD,
                "reason": "missing_c",
            })
            continue
        ja_full = load_rgb(ja_p)
        jb_full = load_rgb(jb_p)
        c_full = load_ppm(c_p)
        # Compare only where C painted (non-gray): owned HUD/overlay/viewmodel.
        # Gray C backdrop is composition isolation, NOT live-world equivalence.
        GRAY = 40
        painted_full = np.abs(c_full.astype(np.int16) - GRAY).max(axis=2) > 8

        ja = crop(ja_full, rect)
        jb = crop(jb_full, rect)
        c = crop(c_full, rect)
        painted = crop(painted_full.astype(np.uint8), rect).astype(bool)
        h = min(ja.shape[0], jb.shape[0], c.shape[0], painted.shape[0])
        w = min(ja.shape[1], jb.shape[1], c.shape[1], painted.shape[1])
        ja, jb, c = ja[:h, :w], jb[:h, :w], c[:h, :w]
        painted = painted[:h, :w]

        noise = mean_abs(ja, jb)
        ab = np.abs(ja.astype(np.int16) - jb.astype(np.int16)).mean(axis=2)
        stable = ab <= max(2.0, noise * 3.0 + 1.0)
        if stable.any():
            noise = float(np.abs(ja - jb)[stable].mean())

        m = painted & stable if stable.any() else painted
        n_painted = int(painted.sum())
        if m.any():
            diff = float(np.abs(c.astype(np.int16) - ja.astype(np.int16))[m].mean())
        elif n_painted == 0:
            diff = float("nan")
        else:
            diff = float(np.abs(c.astype(np.int16) - ja.astype(np.int16))[painted].mean())

        gate = noise + args.margin
        hard = sid in HARD
        noise_lim = NOISE_MAX.get(sid, NOISE_MAX_DEFAULT)
        capture_ok = (noise == noise) and noise <= noise_lim and n_painted > 0

        if not capture_ok:
            verdict = "FAIL"
            n_fail += 1
            reason = "capture_noise" if noise > noise_lim else "c_empty"
        elif hard:
            ok = (diff == diff) and diff <= max(gate, args.margin + 1.0)
            if ok:
                verdict = "PASS"
                reason = "hard_parity"
            else:
                # Hard residual: capture integrity held; C does not match Java.
                # Nonzero exit — do not claim parity.
                verdict = "RESIDUAL"
                n_residual += 1
                reason = "hard_residual"
                residuals.append({
                    "id": sid, "noise": noise, "c_vs_j": diff, "gate": gate,
                })
        else:
            # Soft: capture-only success. Metrics reported; no parity claim.
            verdict = "CAPTURE_OK"
            reason = "soft_capture"

        print("%-24s %10.3f %10.3f %10.3f %10s  painted=%d %s" % (
            sid, noise, diff if diff == diff else -1.0, gate, verdict,
            n_painted, rect))
        rows.append({
            "id": sid, "noise": noise, "c_vs_j": diff, "gate": gate,
            "verdict": verdict, "roi": list(rect), "hard": hard,
            "n_painted": n_painted, "noise_limit": noise_lim, "reason": reason,
        })

    report = {
        "margin": args.margin,
        "fail": n_fail,
        "residual": n_residual,
        "blocked": blocked,
        "residuals": residuals,
        "rows": rows,
        "notes": (
            "PASS = hard C-vs-J within gate. "
            "RESIDUAL = hard capture OK but C residual (nonzero exit). "
            "CAPTURE_OK = soft state capture integrity only (no parity claim). "
            "FAIL = missing/noise/empty."
        ),
    }
    if args.report:
        with open(args.report, "w") as f:
            json.dump(report, f, indent=2)
        print("report -> %s" % args.report)

    print("blocked: %s" % (blocked if blocked else "none"))
    print("open residuals (hard, no parity claim):")
    if residuals:
        for r in residuals:
            print("  %s  noise=%.3f  C-vs-J=%.3f  gate=%.3f" % (
                r["id"], r["noise"], r["c_vs_j"], r["gate"]))
    else:
        print("  none")
    # Nonzero on capture FAIL or hard RESIDUAL.
    exit_code = 1 if (n_fail or n_residual) else 0
    status = "PASS" if exit_code == 0 else (
        "FAIL" if n_fail else "RESIDUAL")
    print("ui_hud oracle ROI gate: %s (fail=%d residual=%d)" % (
        status, n_fail, n_residual))
    if os.path.isfile(legacy):
        exit_code = 1
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
