#!/usr/bin/env python3
"""Feature-specific ROI compare gate for ui_hud oracle goldens.

For each state ID:
  noise  = mean |Java_a - Java_b| over ROI
  c_vs_j = mean |C - Java_a| over compare mask
  gate   = noise + MARGIN

Verdicts (capture integrity first; no false parity claims):
  FAIL        - missing files, capture noise over ceiling, C drew nothing on hard
  CAPTURE_OK  - soft state: A/B frozen + feature ROI present; no hard C parity claim
  PASS        - hard state: C painted pixels within noise+margin (parity claim)
  RESIDUAL    - hard state: capture OK but C-vs-J exceeds gate (nonzero exit)

Hard RESIDUAL returns nonzero. Soft states never claim pixel parity.
Gray C backdrop is composition isolation only; not a live-world equivalence claim.
Inside-block C uses real atlas particle UVs (not solid synthetic texels).

GuiGameOver (hud_death*):
  - Opaque chrome is hard: full button rectangles (no gray painted filter);
    title/score body + shadow via oracle-derived complete feature masks
    (union of Java + C chrome so missing-Java and extra-C pixels both fail).
  - Full-frame hud_death is soft: translucent gradient/world composition
    residual is reported, never claimed exact.
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

# GuiGameOver button rects at scale2 (gm_hud_death_layout).
DEATH_BTN0 = (226, 264, 626, 304)
DEATH_BTN1 = (226, 312, 626, 352)
DEATH_TITLE = (200, 118, 660, 150)
DEATH_SCORE = (280, 198, 580, 216)


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
        # RenderItem.renderItemOverlayIntoGUI: black 13x2 at (icon+2, icon+13),
        # colored fill 13x1 on the top row of that strip. Feature ROI is the
        # strip only (not icon/hotbar alpha over world backdrop).
        ix = HB_X + 3 * S
        iy = HB_Y + 3 * S
        return (ix + 2 * S, iy + 13 * S, ix + 15 * S, iy + 15 * S)
    if name in ("hud_boss_half",):
        bb_x = (CX - 91) * S
        bb_y = 12 * S
        return (bb_x, bb_y - 10 * S, bb_x + 182 * S, bb_y + 6 * S)
    if name in ("hud_death",):
        # Full-frame soft residual (gradient over world / composition).
        return (0, 0, W, H)
    if name in ("hud_death_title",):
        # 2x title "You died!" at GUI y=60 -> fb y=120.
        return DEATH_TITLE
    if name in ("hud_death_score",):
        return DEATH_SCORE
    if name in ("hud_death_btn_respawn",):
        return DEATH_BTN0
    if name in ("hud_death_btn_title",):
        return DEATH_BTN1
    if name.startswith("hand_"):
        # Non-hotbar lower-right viewmodel: above hotbar chrome (GUI y=sh-22).
        hb_y = (SH - 22) * S
        x0, y0 = W * 2 // 3, H * 2 // 3
        x1, y1 = W - 8, max(y0 + 8, hb_y - 4)
        return (x0, y0, x1, y1)
    if name.startswith("overlay_"):
        return (2, 2, W - 2, H - 2)
    return (0, 0, W, H)


# Hard gate: HUD, first-person use viewmodels, inside-block ROIs, and opaque
# GuiGameOver chrome. Full-frame death tint, fire/portal, and underwater are soft.
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
    "hand_bow_pull20",
    "hand_eat_mid",
    "hand_block_shield",
    # Block-in-hand: replace tex*0.1 + perspective UV + real particle atlas
    # (stone / dirt-for-grass). Inside solid, world is black so composition
    # isolation matches Java at noise floor.
    "overlay_inside_stone",
    "overlay_inside_grass",
    "hud_death_title",
    "hud_death_score",
    "hud_death_btn_respawn",
    "hud_death_btn_title",
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


def death_text_chrome_mask(img):
    """Opaque GuiGameOver text chrome: body + vanilla drop-shadow colors.

    Body: white (title / "Score: ") and yellow (score digits, 0xFFFF55).
    Shadow: FontRenderer drop shadow (rgb & 0xFCFCFC) >> 2:
      white -> (63,63,63); yellow 0xFFFF55 -> (63,63,21).
    Fixed complete color classes (not C-derived-only).
    """
    r = img[:, :, 0]
    g = img[:, :, 1]
    b = img[:, :, 2]
    white = (r > 200) & (g > 200) & (b > 200)
    yellow = (r > 200) & (g > 180) & (b < 160)
    # Tight shadow matches; allow ±2 for any mild atlas/blend noise.
    sh_white = (
        (np.abs(r.astype(np.int16) - 63) <= 2) &
        (np.abs(g.astype(np.int16) - 63) <= 2) &
        (np.abs(b.astype(np.int16) - 63) <= 2)
    )
    sh_yellow = (
        (np.abs(r.astype(np.int16) - 63) <= 2) &
        (np.abs(g.astype(np.int16) - 63) <= 2) &
        (np.abs(b.astype(np.int16) - 21) <= 2)
    )
    return white | yellow | sh_white | sh_yellow


def death_compare_mask(sid, ja, c, painted):
    """Return boolean mask for C-vs-J over a death-related ROI crop.

    - buttons: full rectangle (ignore gray painted filter)
    - title/score: oracle body+shadow UNION C body+shadow (missing + extra)
    - full-frame soft: non-gray painted composition residual
    """
    h, w = ja.shape[:2]
    if sid in ("hud_death_btn_respawn", "hud_death_btn_title"):
        return np.ones((h, w), dtype=bool)
    if sid in ("hud_death_title", "hud_death_score"):
        # Oracle-derived complete feature mask, union C so extra C fails too.
        return death_text_chrome_mask(ja) | death_text_chrome_mask(c)
    # Soft full-frame: real composition residual where C painted.
    return painted


def evaluate_roi(sid, ja_full, jb_full, c_full, margin):
    """Compare one state id. Returns row dict + fail/residual flags."""
    rect = roi_rect(sid)
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

    base = "hud_death" if sid.startswith("hud_death") else sid
    if base == "hud_death":
        compare = death_compare_mask(sid, ja, c, painted)
    else:
        compare = painted

    noise = mean_abs(ja, jb)
    ab = np.abs(ja.astype(np.int16) - jb.astype(np.int16)).mean(axis=2)
    stable = ab <= max(2.0, noise * 3.0 + 1.0)
    if stable.any():
        noise = float(np.abs(ja - jb)[stable].mean())

    hard = sid in HARD
    death_hard = hard and sid.startswith("hud_death_")
    # Death opaque chrome: compare the full feature mask (no A/B "stable"
    # filter). A single missing/extra/shifted chrome pixel must residual.
    # Other hard ROIs keep the stable-pixel mean used elsewhere.
    if death_hard:
        m = compare
    else:
        m = compare & stable if stable.any() else compare
    n_painted = int(compare.sum())
    if m.any():
        diff = float(np.abs(c.astype(np.int16) - ja.astype(np.int16))[m].mean())
    elif n_painted == 0:
        diff = float("nan")
    else:
        diff = float(np.abs(c.astype(np.int16) - ja.astype(np.int16))[compare].mean())

    n_mismatch = 0
    if death_hard and m.any():
        pix_err = np.abs(c.astype(np.int16) - ja.astype(np.int16)).max(axis=2)
        n_mismatch = int((m & (pix_err > 0)).sum())

    gate = noise + margin
    noise_lim = NOISE_MAX.get(sid, NOISE_MAX_DEFAULT)
    # Buttons use full-rect compare; n_painted is the rect size.
    # Soft full-frame needs any painted pixels for capture_ok.
    capture_ok = (noise == noise) and noise <= noise_lim and n_painted > 0

    if not capture_ok:
        verdict = "FAIL"
        reason = "capture_noise" if noise > noise_lim else "c_empty"
        fail = 1
        residual = 0
    elif hard:
        if death_hard:
            # Bit-exact opaque chrome vs Java_a (noise floor = zero mismatch).
            ok = (diff == diff) and n_mismatch == 0
        else:
            ok = (diff == diff) and diff <= max(gate, margin + 1.0)
        if ok:
            verdict = "PASS"
            reason = "hard_parity"
            fail = 0
            residual = 0
        else:
            verdict = "RESIDUAL"
            reason = "hard_residual"
            fail = 0
            residual = 1
    else:
        verdict = "CAPTURE_OK"
        reason = "soft_capture"
        fail = 0
        residual = 0

    row = {
        "id": sid, "noise": noise, "c_vs_j": diff, "gate": gate,
        "verdict": verdict, "roi": list(rect), "hard": hard,
        "n_painted": n_painted, "noise_limit": noise_lim, "reason": reason,
    }
    if death_hard:
        row["n_mismatch"] = n_mismatch
    return row, fail, residual


def run_compare(goldens, cframes, margin, report_path=""):
    ids = [
        "hud_armor_iron", "hud_absorption_armor",
        "hud_hurt_flash_on", "hud_hurt_flash_off",
        "hud_hunger_poison", "hud_air_partial", "hud_xp_half",
        "hud_durability_half", "hud_boss_half", "hud_death",
        "hud_death_title", "hud_death_score",
        "hud_death_btn_respawn", "hud_death_btn_title",
        "hand_bow_pull20", "hand_eat_mid", "hand_block_shield",
        "overlay_inside_stone", "overlay_inside_grass",
        "overlay_portal_050", "overlay_fire", "overlay_underwater",
    ]

    legacy = os.path.join(goldens, "hand_block_sword_a.png")
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
        base = sid
        if sid.startswith("hud_death_"):
            base = "hud_death"
        ja_p = os.path.join(goldens, "%s_a.png" % base)
        jb_p = os.path.join(goldens, "%s_b.png" % base)
        c_p = os.path.join(cframes, "c_%s.ppm" % base)
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
        row, fail, resid = evaluate_roi(sid, ja_full, jb_full, c_full, margin)
        n_fail += fail
        n_residual += resid
        if resid:
            residuals.append({
                "id": sid, "noise": row["noise"], "c_vs_j": row["c_vs_j"],
                "gate": row["gate"],
            })
        if fail:
            blocked.append(sid)
        diff = row["c_vs_j"]
        print("%-24s %10.3f %10.3f %10.3f %10s  painted=%d %s" % (
            sid, row["noise"],
            diff if diff == diff else -1.0,
            row["gate"], row["verdict"], row["n_painted"], rect))
        rows.append(row)

    report = {
        "margin": margin,
        "fail": n_fail,
        "residual": n_residual,
        "blocked": blocked,
        "residuals": residuals,
        "rows": rows,
        "notes": (
            "PASS = hard C-vs-J within gate. "
            "RESIDUAL = hard capture OK but C residual (nonzero exit). "
            "CAPTURE_OK = soft state capture integrity only (no parity claim). "
            "FAIL = missing/noise/empty. "
            "GuiGameOver: hard = opaque chrome (title/score body+shadow, "
            "full button rects); full-frame tint/world composition is soft."
        ),
    }
    if report_path:
        with open(report_path, "w") as f:
            json.dump(report, f, indent=2)
        print("report -> %s" % report_path)

    print("blocked: %s" % (blocked if blocked else "none"))
    print("open residuals (hard, no parity claim):")
    if residuals:
        for r in residuals:
            print("  %s  noise=%.3f  C-vs-J=%.3f  gate=%.3f" % (
                r["id"], r["noise"], r["c_vs_j"], r["gate"]))
    else:
        print("  none")
    exit_code = 1 if (n_fail or n_residual) else 0
    status = "PASS" if exit_code == 0 else (
        "FAIL" if n_fail else "RESIDUAL")
    print("ui_hud oracle ROI gate: %s (fail=%d residual=%d)" % (
        status, n_fail, n_residual))
    if os.path.isfile(legacy):
        exit_code = 1
    return exit_code, report


def _must_hard_fail(sid, ja, jb, c, margin, label):
    """Assert evaluate_roi reports hard RESIDUAL/FAIL for a mutation."""
    row, fail, resid = evaluate_roi(sid, ja, jb, c, margin)
    ok_fail = (row["verdict"] in ("RESIDUAL", "FAIL")) or fail or resid
    if not ok_fail:
        print("MUTATION SELF-TEST FAIL: %s did not trip hard gate "
              "(verdict=%s c_vs_j=%s n=%s)" % (
                  label, row["verdict"], row["c_vs_j"], row["n_painted"]),
              file=sys.stderr)
        return 1
    print("mutation ok: %-28s -> %s  c_vs_j=%.4f  n=%d" % (
        label, row["verdict"],
        row["c_vs_j"] if row["c_vs_j"] == row["c_vs_j"] else -1.0,
        row["n_painted"]))
    return 0


def mutation_self_test(goldens, cframes, margin):
    """Prove missing button face, missing shadow, shifted glyph, extra pixel fail."""
    ja_p = os.path.join(goldens, "hud_death_a.png")
    jb_p = os.path.join(goldens, "hud_death_b.png")
    c_p = os.path.join(cframes, "c_hud_death.ppm")
    if not (os.path.isfile(ja_p) and os.path.isfile(jb_p) and os.path.isfile(c_p)):
        print("mutation self-test: SKIP (need hud_death goldens + c frame)",
              file=sys.stderr)
        return 1

    ja = load_rgb(ja_p)
    jb = load_rgb(jb_p)
    c0 = load_ppm(c_p)
    n_err = 0

    # Baseline hard chrome must PASS (sanity).
    for sid in ("hud_death_title", "hud_death_score",
                "hud_death_btn_respawn", "hud_death_btn_title"):
        row, fail, resid = evaluate_roi(sid, ja, jb, c0, margin)
        if row["verdict"] != "PASS" or fail or resid:
            print("MUTATION SELF-TEST FAIL: baseline %s not PASS (%s)" % (
                sid, row["verdict"]), file=sys.stderr)
            n_err += 1
        else:
            print("mutation baseline: %s PASS c_vs_j=%.4f n=%d" % (
                sid, row["c_vs_j"], row["n_painted"]))

    # Soft full-frame must remain soft (not hard PASS).
    row, _, _ = evaluate_roi("hud_death", ja, jb, c0, margin)
    if row["hard"] or row["verdict"] != "CAPTURE_OK":
        print("MUTATION SELF-TEST FAIL: full-frame hud_death must be soft "
              "CAPTURE_OK (hard=%s verdict=%s)" % (row["hard"], row["verdict"]),
              file=sys.stderr)
        n_err += 1
    else:
        print("mutation baseline: hud_death soft CAPTURE_OK residual=%.4f" %
              row["c_vs_j"])

    # (1) Missing button face: wipe Respawn button rect to composition gray.
    c = c0.copy()
    x0, y0, x1, y1 = DEATH_BTN0
    c[y0:y1, x0:x1] = 40
    n_err += _must_hard_fail(
        "hud_death_btn_respawn", ja, jb, c, margin, "missing_button_face")

    # (2) Missing shadow: zero white-shadow (63,63,63) in title ROI.
    c = c0.copy()
    tx0, ty0, tx1, ty1 = DEATH_TITLE
    tit = c[ty0:ty1, tx0:tx1]
    sh = (
        (np.abs(tit[:, :, 0].astype(np.int16) - 63) <= 2) &
        (np.abs(tit[:, :, 1].astype(np.int16) - 63) <= 2) &
        (np.abs(tit[:, :, 2].astype(np.int16) - 63) <= 2)
    )
    if not sh.any():
        print("MUTATION SELF-TEST FAIL: no title shadow pixels to erase",
              file=sys.stderr)
        n_err += 1
    else:
        tit = tit.copy()
        tit[sh] = 40
        c[ty0:ty1, tx0:tx1] = tit
        n_err += _must_hard_fail(
            "hud_death_title", ja, jb, c, margin, "missing_shadow")

    # (3) Shifted glyph: move a white body pixel block by +2 x in title.
    c = c0.copy()
    tit = c[ty0:ty1, tx0:tx1].copy()
    white = (tit[:, :, 0] > 200) & (tit[:, :, 1] > 200) & (tit[:, :, 2] > 200)
    ys, xs = np.where(white)
    if len(ys) < 8:
        print("MUTATION SELF-TEST FAIL: not enough title body pixels to shift",
              file=sys.stderr)
        n_err += 1
    else:
        # Clear a small body cluster and paste it shifted right.
        cy, cx = int(ys[len(ys) // 3]), int(xs[len(xs) // 3])
        block = tit[cy:cy + 4, cx:cx + 4].copy()
        tit[cy:cy + 4, cx:cx + 4] = 40
        nx = min(tit.shape[1] - 4, cx + 2)
        tit[cy:cy + 4, nx:nx + 4] = block
        c[ty0:ty1, tx0:tx1] = tit
        n_err += _must_hard_fail(
            "hud_death_title", ja, jb, c, margin, "shifted_glyph")

    # (4) Extra glyph pixel: paint a white body pixel where neither had chrome.
    c = c0.copy()
    tit_j = ja[ty0:ty1, tx0:tx1]
    tit_c = c[ty0:ty1, tx0:tx1].copy()
    chrome = death_text_chrome_mask(tit_j) | death_text_chrome_mask(tit_c)
    free = ~chrome
    fys, fxs = np.where(free)
    if len(fys) == 0:
        print("MUTATION SELF-TEST FAIL: no free title pixel for extra glyph",
              file=sys.stderr)
        n_err += 1
    else:
        # Prefer a mid-band free pixel away from edges.
        mid = len(fys) // 2
        fy, fx = int(fys[mid]), int(fxs[mid])
        tit_c[fy, fx] = (255, 255, 255)
        c[ty0:ty1, tx0:tx1] = tit_c
        n_err += _must_hard_fail(
            "hud_death_title", ja, jb, c, margin, "extra_glyph_pixel")

    if n_err:
        print("ui_hud death mutation self-test: FAIL (%d)" % n_err)
        return 1
    print("ui_hud death mutation self-test: PASS")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--cframes", required=True)
    ap.add_argument("--margin", type=float, default=2.0)
    ap.add_argument("--report", default="")
    ap.add_argument("--mutation-self-test", action="store_true",
                    help="Prove death chrome mutations trip the hard gate")
    args = ap.parse_args()

    if args.mutation_self_test:
        return mutation_self_test(args.goldens, args.cframes, args.margin)

    code, _ = run_compare(args.goldens, args.cframes, args.margin, args.report)
    return code


if __name__ == "__main__":
    sys.exit(main())
