#!/usr/bin/env python3
"""Feature-specific ROI compare gate for ui_hud oracle goldens.

Hard HUD states (painted ROI mean):
  noise  = mean |Java_a - Java_b| over stable A/B pixels in feature ROI
  c_vs_j = mean |C - Java_a| over compare mask (painted / death chrome)
  gate   = noise + MARGIN

Full-screen blend-off inside-block overlays (overlay_inside_stone/grass):
  Strict full-ROI compare on A/B-stable pixels (Java HUD flicker excluded).
  No painted-only mask (gray C holes must count). No mean dilution sole gate.
  Explicit A/B noise (mean + max). hard_thr = ceil(noise_max) on the stable
  set: when noise_max==0 every stable pixel must be exact (thr 0; any
  maxch>0 is residual). PASS only if hard_px == 0. Rejects erase-90%,
  blank-to-one, +1 single-channel, 2-4px shifts, recolor, sparse extras.

GuiGameOver (hud_death*):
  - Opaque chrome is hard: full button rectangles (no gray painted filter);
    title/score body + shadow via oracle-derived complete feature masks
    (union of Java + C chrome so missing-Java and extra-C pixels both fail).
  - Full-frame hud_death is soft: translucent gradient/world composition
    residual is reported, never claimed exact (gray C isolation backdrop).
  - hud_death_tint_pair is hard: pure gradient bands over the known C underlay
    must match Gui.drawGradientRect + SRC_ALPHA blend bit-exactly (paired
    background / source model). World underlay parity is a separate blocker.

Verdicts (capture integrity first; no false parity claims):
  FAIL        - missing files, capture noise over ceiling, empty/unstable
  CAPTURE_OK  - soft state: A/B frozen + feature present; no hard C parity claim
  PASS        - hard state: within gate / hard_px==0 (parity claim)
  RESIDUAL    - hard capture OK but C residual (nonzero exit)

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


GRAY = 40
GRAY_EPS = 8

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


# Blend-off full-screen particle replace (ItemRenderer.renderBlockInHand).
# Gate is full A/B-stable ROI + hard max-channel pixels, not painted mean.
FULLSCREEN_REPLACE = {
    "overlay_inside_stone",
    "overlay_inside_grass",
}
# Per-pixel mean-ch A/B above this is "unstable" (Java HUD chrome flicker).
STABLE_AB_THR = 2.0
# hard_thr = ceil(noise_max) on stable A/B max-channel. noise_max==0 => thr 0
# (literal equality; any maxch>0 is residual). Never floor thr at 1 — that
# loophole let a +1 single-channel mutation pass when A/B was bit-exact.
# Capture must keep nearly all ROI A/B-stable (not a blinking mess).
MIN_STABLE_FRAC = 0.99
# Residual location samples for honest reporting (full-frame coords).
RESIDUAL_LOC_SAMPLES = 12

# GuiGameOver button rects at scale2 (gm_hud_death_layout).
DEATH_BTN0 = (226, 264, 626, 304)
DEATH_BTN1 = (226, 312, 626, 352)
DEATH_TITLE = (200, 118, 660, 150)
DEATH_SCORE = (280, 198, 580, 216)
# Pure gradient bands (no title/score/buttons): for paired-tint verification.
DEATH_PURE_BANDS = ((0, 100), (360, H))
GRAY_BACKDROP = GRAY
# GuiGameOver.drawScreen: drawGradientRect(..., 1615855616, -1602211792)
# = top 0x60500000, bottom 0xA0803030.
DEATH_GRAD_TOP = (0x60, 0x50, 0x00, 0x00)  # a,r,g,b
DEATH_GRAD_BOT = (0xA0, 0x80, 0x30, 0x30)


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
        # Non-hotbar viewmodel band above hotbar chrome (GUI y=sh-22).
        # Idle / bow / block sit lower-right. Mid-eat (transformEatFirstPerson
        # f3≈1) swings the item toward screen center — a lower-right-only ROI
        # would score a false PASS on a few edge pixels. Use a wider lower band
        # for eat so residual is measured on the actual painted feature.
        hb_y = (SH - 22) * S
        y1 = max(H * 2 // 3 + 8, hb_y - 4)
        if name == "hand_eat_mid":
            return (W // 3, H // 2, W - 8, y1)
        x0, y0 = W * 2 // 3, H * 2 // 3
        return (x0, y0, W - 8, y1)
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
    # isolation matches Java at noise floor. Gated fullscreen hard_px (below).
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


def death_grad_row(y, h=H):
    """Per-row gradient ARGB. Integer lerp matches float GL_SMOOTH quantized
    to bytes (den=h-1, +den//2). Source: Gui.drawGradientRect colors."""
    den = h - 1 if h > 1 else 1
    ta, tr, tg, tb = DEATH_GRAD_TOP
    ba, br, bg, bb = DEATH_GRAD_BOT
    a = (ta * (den - y) + ba * y + den // 2) // den
    r = (tr * (den - y) + br * y + den // 2) // den
    g = (tg * (den - y) + bg * y + den // 2) // den
    b = (tb * (den - y) + bb * y + den // 2) // den
    return a, r, g, b


def death_blend_ch(src, a, dst):
    """SRC_ALPHA, ONE_MINUS_SRC_ALPHA integer form (hud_blend_px)."""
    return (src * a + dst * (255 - a) + 127) // 255


def death_expected_over_underlay(underlay_rgb):
    """Apply GuiGameOver gradient over an HxWx3 underlay (int)."""
    out = np.empty((H, W, 3), dtype=np.int16)
    u = underlay_rgb.astype(np.int32)
    for y in range(H):
        a, r, g, b = death_grad_row(y)
        for ch, s in enumerate((r, g, b)):
            out[y, :, ch] = death_blend_ch(s, a, u[y, :, ch])
    return out


def death_pure_mask():
    m = np.zeros((H, W), dtype=bool)
    for y0, y1 in DEATH_PURE_BANDS:
        m[y0:y1, :] = True
    return m


def painted_mask(c):
    return np.abs(c.astype(np.int16) - GRAY).max(axis=2) > GRAY_EPS


def evaluate_fullscreen_replace(sid, ja_full, jb_full, c_full):
    """Full A/B-stable ROI hard_px gate for inside-block overlays.

    hard_thr = ceil(noise_max) on stable A/B max-channel. noise_max==0 => thr 0
    (exact equality). PASS only if hard_px==0.
    """
    rect = roi_rect(sid)
    hard = sid in HARD
    noise_lim = NOISE_MAX.get(sid, NOISE_MAX_DEFAULT)

    ja = crop(ja_full, rect)
    jb = crop(jb_full, rect)
    c = crop(c_full, rect)
    h = min(ja.shape[0], jb.shape[0], c.shape[0])
    w = min(ja.shape[1], jb.shape[1], c.shape[1])
    ja, jb, c = ja[:h, :w], jb[:h, :w], c[:h, :w]
    n_roi = h * w

    ab_ch = np.abs(ja.astype(np.int16) - jb.astype(np.int16)).astype(np.float64)
    ab_mean_px = ab_ch.mean(axis=2)
    ab_maxch_px = ab_ch.max(axis=2)

    painted = painted_mask(c)
    n_painted = int(painted.sum())

    # Full A/B-stable ROI: exclude Java HUD flicker only. Every stable pixel
    # counts — gray C holes fail.
    stable = ab_mean_px <= STABLE_AB_THR
    n_stable = int(stable.sum())
    stable_frac = float(n_stable) / float(n_roi) if n_roi else 0.0
    residual_locs = []
    residual_bbox = None
    if n_stable > 0:
        noise = float(ab_ch[stable].mean())
        noise_max = float(ab_maxch_px[stable].max())
        diff_ch = np.abs(c.astype(np.int16) - ja.astype(np.int16)).astype(
            np.float64)
        diff_mean_px = diff_ch.mean(axis=2)
        diff_maxch_px = diff_ch.max(axis=2)
        diff = float(diff_mean_px[stable].mean())
        max_diff = float(diff_maxch_px[stable].max())
        # Literal bar: thr tracks measured A/B only. noise_max==0 => thr 0.
        hard_thr = int(np.ceil(noise_max))
        hard_mask = stable & (diff_maxch_px > hard_thr)
        hard_px = int(hard_mask.sum())
        if hard_px > 0:
            ys, xs = np.where(hard_mask)
            # ROI-local -> full-frame coords for reporting.
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
                    "c": [int(c[yi, xi, k]) for k in range(3)],
                    "j": [int(ja[yi, xi, k]) for k in range(3)],
                })
    else:
        noise = float(ab_ch.mean())
        noise_max = float(ab_maxch_px.max()) if ab_maxch_px.size else 0.0
        diff = float("nan")
        max_diff = float("nan")
        hard_thr = 0
        hard_px = n_roi

    gate = noise  # no margin floor; hard_px is the pass criterion
    capture_ok = (
        (noise == noise)
        and noise <= noise_lim
        and n_stable > 0
        and stable_frac >= MIN_STABLE_FRAC
    )
    if not capture_ok:
        verdict = "FAIL"
        if noise > noise_lim:
            reason = "capture_noise"
        elif n_stable == 0 or stable_frac < MIN_STABLE_FRAC:
            reason = "unstable_ab"
        else:
            reason = "capture_bad"
    elif hard_px == 0 and (diff == diff):
        verdict = "PASS"
        reason = "fullscreen_exact"
    else:
        verdict = "RESIDUAL"
        reason = "hard_residual"

    return {
        "id": sid,
        "noise": noise,
        "noise_max": noise_max,
        "c_vs_j": diff,
        "max_diff": max_diff,
        "hard_px": hard_px,
        "hard_thr": hard_thr,
        "gate": gate,
        "verdict": verdict,
        "roi": list(rect),
        "hard": hard,
        "fullscreen": True,
        "n_painted": n_painted,
        "n_stable": n_stable,
        "stable_frac": stable_frac,
        "n_roi": n_roi,
        "noise_limit": noise_lim,
        "reason": reason,
        "rule": "fullscreen_replace_exact",
        "residual_bbox": residual_bbox,
        "residual_locs": residual_locs,
    }


def evaluate_state(sid, ja_full, jb_full, c_full, margin=2.0):
    """Return a result dict for one state (used by gate + mutation suite)."""
    if sid in FULLSCREEN_REPLACE:
        return evaluate_fullscreen_replace(sid, ja_full, jb_full, c_full)
    if sid == "hud_death_tint_pair":
        row, _fail, _resid = evaluate_death_tint_pair(ja_full, jb_full, c_full)
        row = dict(row)
        row.setdefault("noise_max", None)
        row.setdefault("max_diff", None)
        row.setdefault("hard_px", None)
        row.setdefault("hard_thr", None)
        row.setdefault("fullscreen", False)
        row.setdefault("rule", "death_tint_pair")
        return row
    row, _fail, _resid = evaluate_roi_painted(sid, ja_full, jb_full, c_full, margin)
    row = dict(row)
    row.setdefault("noise_max", None)
    row.setdefault("max_diff", None)
    row.setdefault("hard_px", None)
    row.setdefault("hard_thr", None)
    row.setdefault("fullscreen", False)
    row.setdefault("rule", "painted_mean" if not sid.startswith("hud_death_")
                   else "death_chrome")
    return row


def evaluate_death_tint_pair(ja_full, jb_full, c_full):
    """Hard paired-background gate for GuiGameOver gradient compositing.

    C candidate paints death over solid GRAY=40. Pure gradient bands (no
    chrome) must match the source blend model bit-exactly. Full-frame and
    pure-zone C-vs-Java residuals are reported without claiming world parity:
    Java underlay is the live stone-pad scene; living HUD goldens are not a
    valid pre-death underlay (survival chrome under the tint).
    """
    pure = death_pure_mask()
    under = np.full((H, W, 3), GRAY_BACKDROP, dtype=np.int16)
    expected = death_expected_over_underlay(under)

    c_pure = c_full[pure]
    e_pure = expected[pure]
    pix_err = np.abs(c_pure.astype(np.int16) - e_pure.astype(np.int16)).max(axis=1)
    n_mismatch = int((pix_err > 0).sum())
    c_vs_model = float(np.abs(c_pure.astype(np.int16) - e_pure.astype(np.int16)).mean())

    full_c_vs_j = mean_abs(c_full, ja_full)
    pure_c_vs_j = float(np.abs(c_full[pure].astype(np.int16) - ja_full[pure].astype(np.int16)).mean())
    pure_ab = float(np.abs(ja_full[pure].astype(np.int16) - jb_full[pure].astype(np.int16)).mean())
    full_ab = mean_abs(ja_full, jb_full)

    # Java capture vs integer model identity (unblend+reblend): GL quant bound.
    # out ≈ (s*a + d*ia + 127)//255  =>  d ≈ (out*255 - s*a - 127)//ia
    bg = np.zeros((H, W, 3), dtype=np.int32)
    for y in range(H):
        a, r, g, b = death_grad_row(y)
        ia = 255 - a
        if ia == 0:
            continue
        for ch, s in enumerate((r, g, b)):
            bg[y, :, ch] = (
                ja_full[y, :, ch].astype(np.int32) * 255 - s * a - 127
            ) // ia
    re = death_expected_over_underlay(np.clip(bg, 0, 255).astype(np.int16))
    java_model_identity = float(
        np.abs(re[pure].astype(np.int16) - ja_full[pure].astype(np.int16)).mean()
    )
    java_model_identity_max = int(
        np.abs(re[pure].astype(np.int16) - ja_full[pure].astype(np.int16)).max()
    )

    ok = n_mismatch == 0 and c_vs_model == 0.0
    row = {
        "id": "hud_death_tint_pair",
        "noise": pure_ab,
        "c_vs_j": pure_c_vs_j,
        "gate": pure_ab + 2.0,
        "verdict": "PASS" if ok else "RESIDUAL",
        "roi": [0, 0, W, H],
        "hard": True,
        "n_painted": int(pure.sum()),
        "noise_limit": NOISE_MAX.get("hud_death", 5.0),
        "reason": "paired_background_source_model" if ok else "tint_model_mismatch",
        "n_mismatch": n_mismatch,
        "c_vs_model": c_vs_model,
        "full_frame_c_vs_j": full_c_vs_j,
        "full_frame_ab_noise": full_ab,
        "pure_zone_c_vs_j": pure_c_vs_j,
        "pure_zone_ab_noise": pure_ab,
        "java_model_identity_mean": java_model_identity,
        "java_model_identity_max": java_model_identity_max,
        "world_parity": "BLOCKED",
        "blocker": (
            "Same-scene full-frame death parity needs a world underlay companion "
            "(no survival HUD) at the death pose/partialTicks. C isolation uses "
            "GRAY=40; Java death is gradient over the live stone pad. Existing "
            "living HUD goldens include survival chrome and are not a valid "
            "pre-death underlay. qrl frame{} always runs renderGameOverlay; no "
            "safe world-only companion via the current capture driver without "
            "new frame flags. Pure-band C-vs-J is world composition residual, "
            "not gradient math (c_vs_model=0 when PASS)."
        ),
    }
    residual = 0 if ok else 1
    return row, 0, residual


def evaluate_roi_painted(sid, ja_full, jb_full, c_full, margin):
    """Painted-ROI / death-chrome compare. Returns row dict + fail/residual flags."""
    rect = roi_rect(sid)
    painted_full = painted_mask(c_full)

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


def evaluate_roi(sid, ja_full, jb_full, c_full, margin):
    """Compare one state id. Routes fullscreen overlays to exact hard_px gate."""
    if sid in FULLSCREEN_REPLACE:
        row = evaluate_fullscreen_replace(sid, ja_full, jb_full, c_full)
        fail = 1 if row["verdict"] == "FAIL" else 0
        residual = 1 if row["verdict"] == "RESIDUAL" else 0
        return row, fail, residual
    return evaluate_roi_painted(sid, ja_full, jb_full, c_full, margin)


def run_compare(goldens, cframes, margin, report_path=""):
    ids = [
        "hud_armor_iron", "hud_absorption_armor",
        "hud_hurt_flash_on", "hud_hurt_flash_off",
        "hud_hunger_poison", "hud_air_partial", "hud_xp_half",
        "hud_durability_half", "hud_boss_half", "hud_death",
        "hud_death_title", "hud_death_score",
        "hud_death_btn_respawn", "hud_death_btn_title",
        "hud_death_tint_pair",
        "hand_bow_pull20", "hand_eat_mid", "hand_block_shield",
        "overlay_inside_stone", "overlay_inside_grass",
        "overlay_portal_050", "overlay_fire", "overlay_underwater",
    ]

    legacy = os.path.join(goldens, "hand_block_sword_a.png")
    if os.path.isfile(legacy):
        print("FAIL: contaminated golden hand_block_sword_* present; "
              "delete and recapture as hand_block_shield", file=sys.stderr)

    print("%-24s %10s %10s %10s %8s %10s  %s" % (
        "state", "noise", "C-vs-J", "gate", "hard_px", "verdict", "roi"))
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
        rect = roi_rect(sid) if sid != "hud_death_tint_pair" else (0, 0, W, H)
        if not (os.path.isfile(ja_p) and os.path.isfile(jb_p)):
            print("%-24s %10s %10s %10s %8s %10s  MISSING JAVA" % (
                sid, "-", "-", "-", "-", "FAIL"))
            blocked.append(sid)
            n_fail += 1
            rows.append({
                "id": sid, "noise": None, "c_vs_j": None, "gate": None,
                "verdict": "FAIL", "roi": list(rect), "hard": True,
                "reason": "missing_java",
            })
            continue
        if not os.path.isfile(c_p):
            print("%-24s %10s %10s %10s %8s %10s  MISSING C" % (
                sid, "-", "-", "-", "-", "FAIL"))
            blocked.append(sid)
            n_fail += 1
            rows.append({
                "id": sid, "noise": None, "c_vs_j": None, "gate": None,
                "verdict": "FAIL", "roi": list(rect), "hard": True,
                "reason": "missing_c",
            })
            continue
        ja_full = load_rgb(ja_p)
        jb_full = load_rgb(jb_p)
        c_full = load_ppm(c_p)
        if sid == "hud_death_tint_pair":
            row, fail, resid = evaluate_death_tint_pair(ja_full, jb_full, c_full)
        else:
            row, fail, resid = evaluate_roi(sid, ja_full, jb_full, c_full, margin)
        n_fail += fail
        n_residual += resid
        if resid:
            residuals.append({
                "id": sid,
                "noise": row["noise"],
                "noise_max": row.get("noise_max"),
                "c_vs_j": row["c_vs_j"],
                "gate": row["gate"],
                "hard_px": row.get("hard_px"),
                "hard_thr": row.get("hard_thr"),
                "max_diff": row.get("max_diff"),
                "reason": row.get("reason"),
                "residual_bbox": row.get("residual_bbox"),
                "residual_locs": row.get("residual_locs"),
            })
        if fail:
            blocked.append(sid)
        diff = row["c_vs_j"]
        hard_px_s = ("-" if row.get("hard_px") is None
                     else str(int(row["hard_px"])))
        extra = ""
        if sid == "hud_death_tint_pair":
            extra = (" c_vs_model=%.4f full_C-vs-J=%.4f world=%s" % (
                row.get("c_vs_model", -1.0),
                row.get("full_frame_c_vs_j", -1.0),
                row.get("world_parity", "?")))
        if row.get("fullscreen"):
            extra = " stable=%.3f maxch=%.1f thr=%s%s" % (
                row.get("stable_frac") or 0.0,
                row.get("max_diff") if row.get("max_diff") == row.get("max_diff")
                else -1.0,
                row.get("hard_thr"),
                extra)
        print("%-24s %10.3f %10.3f %10.3f %8s %10s  painted=%d %s%s" % (
            sid, row["noise"] if row["noise"] == row["noise"] else -1.0,
            diff if diff == diff else -1.0,
            row["gate"] if row["gate"] == row["gate"] else -1.0,
            hard_px_s, row["verdict"], row["n_painted"], rect, extra))
        rows.append(row)

    report = {
        "margin": margin,
        "fail": n_fail,
        "residual": n_residual,
        "blocked": blocked,
        "residuals": residuals,
        "rows": rows,
        "notes": (
            "PASS = hard parity. Fullscreen inside-block uses A/B-stable full "
            "ROI + hard_px with thr=ceil(noise_max) (noise_max==0 => exact). "
            "No painted-mean dilution, no thr floor of 1. "
            "RESIDUAL = hard capture OK but C residual (nonzero exit). "
            "CAPTURE_OK = soft capture integrity only (portal/fire/underwater/"
            "full-frame death). FAIL = missing/noise/empty/unstable. "
            "GuiGameOver: hard = opaque chrome (title/score body+shadow, "
            "full button rects) + hud_death_tint_pair (source gradient blend "
            "over known underlay); full-frame tint/world composition is soft "
            "and does not claim same-scene parity."
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
            extra = ""
            if r.get("hard_px") is not None:
                extra = "  hard_px=%s  thr=%s  max_diff=%s  noise_max=%s" % (
                    r.get("hard_px"), r.get("hard_thr"),
                    r.get("max_diff"), r.get("noise_max"))
            print("  %s  noise=%.3f  C-vs-J=%.3f  gate=%.3f%s" % (
                r["id"], r["noise"], r["c_vs_j"], r["gate"], extra))
            if r.get("residual_bbox"):
                print("    residual_bbox(x0,y0,x1,y1)=%s" % (
                    r["residual_bbox"],))
            locs = r.get("residual_locs") or []
            for loc in locs[:8]:
                print("    residual @(%d,%d) maxch=%d C=%s J=%s" % (
                    loc["x"], loc["y"], loc["maxch"], loc["c"], loc["j"]))
            if len(locs) > 8:
                print("    ... %d sample locs total" % len(locs))
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

    # Paired tint gate: C pure bands match source model; world residual open.
    row, fail, resid = evaluate_death_tint_pair(ja, jb, c0)
    if row["verdict"] != "PASS" or fail or resid:
        print("MUTATION SELF-TEST FAIL: hud_death_tint_pair baseline not PASS "
              "(%s c_vs_model=%s)" % (row["verdict"], row.get("c_vs_model")),
              file=sys.stderr)
        n_err += 1
    else:
        print("mutation baseline: hud_death_tint_pair PASS c_vs_model=%.4f "
              "pure_C-vs-J=%.4f full_C-vs-J=%.4f world=%s" % (
                  row["c_vs_model"], row["pure_zone_c_vs_j"],
                  row["full_frame_c_vs_j"], row["world_parity"]))
    # Mutate a pure-band pixel: tint pair must residual.
    c_mut = c0.copy()
    c_mut[10, 10] = (0, 0, 0)
    row_m, _, resid_m = evaluate_death_tint_pair(ja, jb, c_mut)
    if row_m["verdict"] == "PASS" or row_m.get("n_mismatch", 0) == 0:
        print("MUTATION SELF-TEST FAIL: pure-band pixel wipe did not trip "
              "hud_death_tint_pair", file=sys.stderr)
        n_err += 1
    else:
        print("mutation ok: %-28s -> %s  n_mismatch=%d" % (
            "tint_pair_pure_pixel", row_m["verdict"], row_m["n_mismatch"]))

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
