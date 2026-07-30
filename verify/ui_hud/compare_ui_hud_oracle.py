#!/usr/bin/env python3
"""Feature-specific ROI compare gate for ui_hud oracle goldens.

Core hard HUD (armor/absorption/hearts/hunger/air/xp/durability/boss):
  Oracle-derived complete feature masks UNION C feature (Java∪C). Missing Java
  pixels and extra C both score. Hard PASS requires n_hard_px==0 and mean
  within measured A/B noise (no margin floor). HARD_THR=2 max-channel.

Absorption (closed): GuiIngame gold hearts (icons.png U=160/169) — exact
  C-vs-J with oracle∪C masks (c_vs_j=0, hard_px=0) on committed A/B.

Durability (local ownership, not thr surgery and not a global painted_full
  drop): every nonzero-alpha wood_pickaxe.png texel at the exact scaled GUI
  slot, the complete 13x2 strip, and a C-extra mask that scores any unowned
  icon-cell pixel that is not the exact isolation underlay (HUD_HOTBAR/
  widgets alpha-composited over GRAY with hud_blend_px math). Broad mx/chroma
  threshold holes are not used; Java world underlay is not imported. Opaque
  body + strip already bit-match Java/PNG; prior residual was composition
  isolation. Other core states still use painted_full. PASS also requires
  colored strip fill.

Full-screen blend-off inside-block overlays (overlay_inside_stone/grass):
  Strict full-ROI compare on A/B-stable pixels (Java HUD flicker excluded).
  No painted-only mask (gray C holes must count). No mean dilution sole gate.
  Explicit A/B noise (mean + max). hard_thr is always 0 (bit-exact C vs
  Java_a on stable pixels). PASS only if noise_max==0 AND hard_px==0.
  Never use ceil(noise_max) as a PASS tolerance — that let C=Java_a /
  Java_a+1 claim parity when A/B still had maxch=1 residuals. Rejects
  erase-90%, blank-to-one, +1 single-channel, 2-4px shifts, recolor, extras.

overlay_underwater (hard full-ROI, honest residual):
  Same-scene glass-pool ambient + renderWaterOverlayTexture. Full A/B-stable
  ROI hard_px gate (same exact bar as inside-block; no painted filter, no
  noise/mean budget to claim parity). Measured residual ~4.97/ch stays
  RESIDUAL until hard_px==0. Air partial is a separate hard HUD state.

Portal (overlay_portal_050) — GuiIngame.renderPortal, not ItemRenderer:
  Full-frame hard_px on A/B-stable pixels (Java∪C owned = full portal feature).
  No mean/noise budgets as pass criterion, no color-only purple masks, no
  fitted black underlay claim. Gray C isolation vs outdoor Java underlay
  under translucent alpha (0.25 at t=0.5) is a product residual until a
  same-scene underlay exists. Capture needs sticky portal_phase so warp
  A/B is stable (pin_texture_animations freezes the atlas tile).
  Real portal A/B has residual maxch=1 on hundreds of pixels: product
  verdict is CAPTURE_BLOCKED (never PASS under any C, including C=Java_a,
  Java_b, midpoint, or Java_a+1). Synthetic bit-exact A=B controls may PASS.

GuiGameOver (hud_death*):
  - Opaque chrome is hard: full button rectangles; title/score body+shadow
    via oracle-derived complete feature masks (Java+C union).
  - Full-frame hud_death is soft (world underlay residual).
  - hud_death_tint_pair is hard: pure gradient bands over known C underlay.

Hand viewmodels (hand_bow_pull20 / hand_eat_mid / hand_block_shield):
  Complete Java∪C subject ownership on the lower-band ROI. Subject = pixels
  farther than HAND_SUBJECT_THR from that image's own ROI-border backdrop
  (GRAY isolation for C candidate; wall median for Java goldens). Missing
  Java-only silhouette and C-only extras both score. hard_thr is always 0.
  PASS only if noise_max==0 AND hard_px==0 (bit-exact). No mean budget, no
  legacy hard_parity label, no painted-only holes. A/B maxch residual =>
  CAPTURE_BLOCKED. Gray C isolation vs Java wall is residual until same-scene
  underlay; sticky-pin shield golden is exact capture but C parity remains
  OPEN when hard_px>0.

Verdicts (capture integrity first; no false parity claims):
  FAIL             - missing files, capture noise over ceiling, empty/unstable
  CAPTURE_OK       - soft state: A/B frozen + feature present; no hard C parity
  CAPTURE_BLOCKED  - hard state but A/B maxch residual > 0 (no C may PASS)
  PASS             - hard_px==0 and noise_max==0 (bit-exact parity claim)
  RESIDUAL         - A/B bit-exact but C residual (nonzero exit)

Hard RESIDUAL and CAPTURE_BLOCKED return nonzero. Soft states never claim
pixel parity. Gray C backdrop is composition isolation only; not a live-world
equivalence claim. Inside-block C uses real atlas particle UVs (not solid
synthetic texels).
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
HARD_THR = 2  # max-channel delta counts as a hard pixel

# Capture noise ceiling (must match driver intent; no 40 loophole).
NOISE_MAX_DEFAULT = 2.0
NOISE_MAX = {
    "hud_hurt_flash_on": 3.0,
    "hud_hurt_flash_off": 3.0,
    "hand_bow_pull20": 3.0,
    # Portal: sticky portal_phase + pin_texture_animations must freeze A/B.
    "overlay_portal_050": 3.0,
    "overlay_fire": 35.0,
    "hud_death": 5.0,
    "overlay_inside_stone": 3.0,
    "overlay_inside_grass": 3.0,
    "overlay_underwater": 3.0,
}

# Core survival HUD hard gates: complete oracle∪C feature masks, noise floor.
CORE_HARD = {
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

# Full-screen hard_px gates (A/B-stable ROI, hard_thr always 0).
# inside-*: ItemRenderer.renderBlockInHand blend-off replace.
# underwater: renderWaterOverlayTexture same-scene residual (exact bar).
# portal: GuiIngame.renderPortal full-frame translucent (Java∪C = full feature).
FULLSCREEN_REPLACE = {
    "overlay_inside_stone",
    "overlay_inside_grass",
    "overlay_underwater",
    "overlay_portal_050",
}

# Hand viewmodel hard states: Java∪C subject, hard_thr=0, A/B exact for PASS.
HAND_HARD = {
    "hand_bow_pull20",
    "hand_eat_mid",
    "hand_block_shield",
}
# Max-channel distance from per-image ROI-border backdrop for subject pixels.
# Matches GRAY_EPS on isolation frames; wall goldens use border median.
HAND_SUBJECT_THR = GRAY_EPS
HAND_BORDER_BAND = 6
# Per-pixel mean-ch A/B above this is "unstable" (Java HUD chrome flicker).
STABLE_AB_THR = 2.0
# hard_thr is always 0: bit-exact C vs Java_a on the stable set. Never
# ceil(noise_max) as PASS tolerance — with noise_max=1 that let C=Java_a,
# midpoint, and Java_a+1 all PASS while A/B still disagreed. PASS requires
# noise_max==0 AND hard_px==0. noise_max>0 => CAPTURE_BLOCKED (no C may PASS).
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
        # Full owned slot-0 icon (16x16 GUI) + complete 13x2 durability strip.
        ix = HB_X + 3 * S
        iy = HB_Y + 3 * S
        return (ix, iy, ix + 16 * S, iy + 16 * S)
    if name in ("hud_boss_half",):
        # Bar at (cx-91, 12) plus name chrome above (y = 12-9).
        bb_x = (CX - 91) * S
        bb_y = 12 * S
        return (bb_x, max(0, bb_y - 12 * S), bb_x + 182 * S, bb_y + 6 * S)
    if name in ("hud_death",):
        # Full-frame soft residual (gradient over world / composition).
        return (0, 0, W, H)
    if name in ("hud_death_title",):
        return DEATH_TITLE
    if name in ("hud_death_score",):
        return DEATH_SCORE
    if name in ("hud_death_btn_respawn",):
        return DEATH_BTN0
    if name in ("hud_death_btn_title",):
        return DEATH_BTN1
    if name.startswith("hand_"):
        # Non-hotbar viewmodel band above hotbar chrome (GUI y=sh-22).
        # Mid-eat swings toward center — wider lower band for honest residual.
        hb_y = (SH - 22) * S
        y1 = max(H * 2 // 3 + 8, hb_y - 4)
        if name == "hand_eat_mid":
            return (W // 3, H // 2, W - 8, y1)
        x0, y0 = W * 2 // 3, H * 2 // 3
        return (x0, y0, W - 8, y1)
    if name.startswith("overlay_"):
        return (2, 2, W - 2, H - 2)
    return (0, 0, W, H)


# Hard gate: core HUD, hands, inside-block, underwater full-ROI, portal swirl,
# and opaque GuiGameOver chrome. Full-frame death tint and fire stay soft.
HARD = set(CORE_HARD) | set(HAND_HARD) | {
    "overlay_inside_stone",
    "overlay_inside_grass",
    "overlay_underwater",
    # GuiIngame.renderPortal: full-frame A/B-stable hard_px (no soft CAPTURE_OK).
    "overlay_portal_050",
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


def hand_backdrop_rgb(img, band=HAND_BORDER_BAND):
    """Per-image ROI backdrop: GRAY isolation if border is near-gray, else median.

    C candidate frames are solid GRAY outside the viewmodel. Java goldens use
    a wall; border median is the wall estimate. Subject is distance from this
    backdrop (not a painted-only C hole).
    """
    h, w = img.shape[:2]
    b = max(1, min(band, h // 4, w // 4))
    border = np.concatenate([
        img[:b].reshape(-1, 3),
        img[-b:].reshape(-1, 3),
        img[:, :b].reshape(-1, 3),
        img[:, -b:].reshape(-1, 3),
    ], axis=0)
    med = np.median(border.astype(np.float64), axis=0)
    if float(np.abs(med - float(GRAY)).max()) <= float(HAND_SUBJECT_THR + 4):
        return np.array([GRAY, GRAY, GRAY], dtype=np.int16)
    return med.astype(np.int16)


def hand_subject_mask(img, thr=HAND_SUBJECT_THR):
    """Non-backdrop subject pixels in a hand ROI crop (HxWx3)."""
    bg = hand_backdrop_rgb(img)
    return np.abs(img.astype(np.int16) - bg.astype(np.int16)).max(axis=2) > thr


def hand_owned_mask(ja, c):
    """Complete Java∪C subject ownership (missing Java + C-extra both score)."""
    return hand_subject_mask(ja) | hand_subject_mask(c)


def evaluate_hand_exact(sid, ja_full, jb_full, c_full):
    """Hand viewmodel exact gate: Java∪C subject, hard_thr=0, A/B exact PASS.

    Replaces the legacy painted-mean + margin hard_parity path. PASS only when
    noise_max==0 and hard_px==0 on the owned subject. Any A/B maxch residual is
    CAPTURE_BLOCKED. No mean budget.
    """
    rect = roi_rect(sid)
    noise_lim = NOISE_MAX.get(sid, NOISE_MAX_DEFAULT)

    ja = crop(ja_full, rect)
    jb = crop(jb_full, rect)
    c = crop(c_full, rect)
    h = min(ja.shape[0], jb.shape[0], c.shape[0])
    w = min(ja.shape[1], jb.shape[1], c.shape[1])
    ja, jb, c = ja[:h, :w], jb[:h, :w], c[:h, :w]

    j_subj = hand_subject_mask(ja)
    c_subj = hand_subject_mask(c)
    owned = j_subj | c_subj
    n_owned = int(owned.sum())
    n_j = int(j_subj.sum())
    n_c = int(c_subj.sum())
    n_only_j = int((j_subj & ~c_subj).sum())
    n_only_c = int((c_subj & ~j_subj).sum())
    n_painted = int(painted_mask(c).sum())  # isolation-paint diagnostic

    ab_ch = np.abs(ja.astype(np.int16) - jb.astype(np.int16)).astype(np.float64)
    ab_maxch_px = ab_ch.max(axis=2)
    residual_locs = []
    residual_bbox = None
    n_ab_maxch_ge1 = 0

    if n_owned > 0:
        noise = float(ab_ch[owned].mean())
        noise_max = float(ab_maxch_px[owned].max())
        n_ab_maxch_ge1 = int((ab_maxch_px[owned] >= 1).sum())
        diff_ch = np.abs(c.astype(np.int16) - ja.astype(np.int16)).astype(
            np.float64)
        diff_mean_px = diff_ch.mean(axis=2)
        diff_maxch_px = diff_ch.max(axis=2)
        diff = float(diff_mean_px[owned].mean())
        max_diff = float(diff_maxch_px[owned].max())
        hard_thr = 0
        hard_mask = owned & (diff_maxch_px > hard_thr)
        hard_px = int(hard_mask.sum())
        # C-painted diagnostic (legacy false-PASS surface under mean budget).
        c_paint = painted_mask(c)
        if c_paint.any():
            c_paint_nz = int((diff_maxch_px[c_paint] > 0).sum())
            c_paint_maxch = int(diff_maxch_px[c_paint].max())
            c_paint_mean = float(diff_mean_px[c_paint].mean())
        else:
            c_paint_nz = 0
            c_paint_maxch = 0
            c_paint_mean = float("nan")
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
                    "c": [int(c[yi, xi, k]) for k in range(3)],
                    "j": [int(ja[yi, xi, k]) for k in range(3)],
                })
    else:
        noise = float(ab_ch.mean()) if ab_ch.size else float("nan")
        noise_max = float(ab_maxch_px.max()) if ab_maxch_px.size else 0.0
        n_ab_maxch_ge1 = int((ab_maxch_px >= 1).sum()) if ab_maxch_px.size else 0
        diff = float("nan")
        max_diff = float("nan")
        hard_thr = 0
        hard_px = 0
        c_paint_nz = 0
        c_paint_maxch = 0
        c_paint_mean = float("nan")

    gate = noise  # diagnostic only; PASS is hard_px + noise_max, no mean budget
    capture_ok = (
        (noise == noise)
        and noise <= noise_lim
        and n_owned > 0
    )
    if not capture_ok:
        verdict = "FAIL"
        if noise != noise:
            reason = "capture_bad"
        elif noise > noise_lim:
            reason = "capture_noise"
        else:
            reason = "empty_subject"
    elif noise_max > 0:
        verdict = "CAPTURE_BLOCKED"
        reason = "ab_maxch_nonzero"
    elif hard_px == 0 and (diff == diff):
        verdict = "PASS"
        reason = "hand_exact"
    else:
        verdict = "RESIDUAL"
        reason = "hard_residual"

    return {
        "id": sid,
        "noise": noise,
        "noise_max": noise_max,
        "n_ab_maxch_ge1": n_ab_maxch_ge1,
        "c_vs_j": diff,
        "max_diff": max_diff,
        "hard_px": hard_px,
        "hard_thr": hard_thr,
        "gate": gate,
        "verdict": verdict,
        "roi": list(rect),
        "hard": True,
        "hand_exact": True,
        "n_painted": n_painted,
        "n_owned": n_owned,
        "n_j_subj": n_j,
        "n_c_subj": n_c,
        "n_only_j": n_only_j,
        "n_only_c": n_only_c,
        "c_paint_nz": c_paint_nz,
        "c_paint_maxch": c_paint_maxch,
        "c_paint_mean": c_paint_mean,
        "noise_limit": noise_lim,
        "reason": reason,
        "rule": "hand_java_union_c_subject_exact",
        "residual_bbox": residual_bbox,
        "residual_locs": residual_locs,
    }


def evaluate_fullscreen_replace(sid, ja_full, jb_full, c_full):
    """Full A/B-stable ROI hard_px gate for inside-block and portal overlays.

    Java∪C owned = full portal/inside feature (fullscreen ROI). hard_thr is
    always 0 (bit-exact C vs Java_a). PASS only if noise_max==0 AND hard_px==0.
    Any stable A/B maxch residual blocks PASS (CAPTURE_BLOCKED) — no C,
    including C=Java_a / Java_b / midpoint / Java_a+1, may claim parity.
    No mean budgets, no color-only masks, no ceil(noise_max) tolerance.
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
    n_ab_maxch_ge1 = 0
    if n_stable > 0:
        noise = float(ab_ch[stable].mean())
        noise_max = float(ab_maxch_px[stable].max())
        n_ab_maxch_ge1 = int((ab_maxch_px[stable] >= 1).sum())
        diff_ch = np.abs(c.astype(np.int16) - ja.astype(np.int16)).astype(
            np.float64)
        diff_mean_px = diff_ch.mean(axis=2)
        diff_maxch_px = diff_ch.max(axis=2)
        diff = float(diff_mean_px[stable].mean())
        max_diff = float(diff_maxch_px[stable].max())
        # Bit-exact bar only. Never ceil(noise_max) as a PASS tolerance.
        hard_thr = 0
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
        n_ab_maxch_ge1 = int((ab_maxch_px >= 1).sum()) if ab_maxch_px.size else 0
        diff = float("nan")
        max_diff = float("nan")
        hard_thr = 0
        hard_px = n_roi

    gate = noise  # no margin floor; hard_px + noise_max gate PASS
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
    elif noise_max > 0:
        # A/B not bit-exact: product cannot claim PASS for any C (including
        # C=Java_a / Java_b / midpoint / Java_a+1).
        verdict = "CAPTURE_BLOCKED"
        reason = "ab_maxch_nonzero"
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
        "n_ab_maxch_ge1": n_ab_maxch_ge1,
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


def cells_fg(img, x0, y0, n, pitch, cw, ch, thr=40):
    """Per-icon cell feature: pixel far from that cell's border median.

    Oracle-derived: independent of C. thr is high enough that flat wall bleed
    in icon cells is rejected while sprite texels remain.
    """
    h, w = img.shape[:2]
    m = np.zeros((h, w), dtype=bool)
    for i in range(n):
        xa = x0 + i * pitch
        xb = min(w, xa + cw)
        ya = y0
        yb = min(h, ya + ch)
        if xa >= w or ya >= h or xa >= xb or ya >= yb:
            continue
        cell = img[ya:yb, xa:xb].astype(np.int16)
        border = np.concatenate([
            cell[0, :].reshape(-1, 3),
            cell[-1, :].reshape(-1, 3),
            cell[:, 0].reshape(-1, 3),
            cell[:, -1].reshape(-1, 3),
        ], axis=0)
        bg = np.median(border, axis=0)
        d = np.abs(cell - bg).max(axis=2)
        m[ya:yb, xa:xb] |= d >= thr
    return m


def icon_row_cells(y0, x0=None, n=10, pitch=None, cw=None, ch=None):
    """Boolean ownership of atlas icon-cell rectangles (full frame)."""
    if x0 is None:
        x0 = HB_X
    if pitch is None:
        pitch = 8 * S
    if cw is None:
        cw = 9 * S
    if ch is None:
        ch = 9 * S
    m = np.zeros((H, W), dtype=bool)
    for i in range(n):
        xa = x0 + i * pitch
        m[y0:y0 + ch, xa:xa + cw] = True
    return m


def icon_row_chrome(img, y0, x0=None, n=10, pitch=None, cw=None, ch=None,
                    thr=40):
    """Atlas-geometry icon row: cells_fg body + black outlines + mid-gray ~61.

    thr=40 alone drops armor mid-gray shadows (~61) when cell border median is
    world gray ~92 (dist 31). Own those atlas chrome colors inside the cell
    rectangles so erasing them cannot drop them from the Java∪C union.
    Black outlines are also forced inside the same geometry (independent of thr).
    """
    if x0 is None:
        x0 = HB_X
    if pitch is None:
        pitch = 8 * S
    if cw is None:
        cw = 9 * S
    if ch is None:
        ch = 9 * S
    m = cells_fg(img, x0, y0, n, pitch, cw, ch, thr=thr)
    cells = icon_row_cells(y0, x0=x0, n=n, pitch=pitch, cw=cw, ch=ch)
    r = img[:, :, 0].astype(np.int16)
    g = img[:, :, 1].astype(np.int16)
    b = img[:, :, 2].astype(np.int16)
    # icons.png black outline
    m |= cells & (r < 20) & (g < 20) & (b < 20)
    # armor/heart mid-gray shadow texel ~61 (omitted by thr=40 vs world~92)
    m |= cells & (np.abs(r - 61) <= 2) & (np.abs(g - 61) <= 2) & (
        np.abs(b - 61) <= 2)
    return m


def boss_bar_band():
    """Atlas bars.png pink boss strip: GUI (cx-91, 12) size 182x5, scale S."""
    bb_x = (CX - 91) * S
    bb_y = 12 * S
    m = np.zeros((H, W), dtype=bool)
    m[bb_y:bb_y + 5 * S, bb_x:bb_x + 182 * S] = True
    return m


def boss_pink_gamut(r, g, b):
    """Full pink / dark-pink boss-bar gamut (magenta family, g near 0).

    Covers bright fill (~236,0,184), mid (~140,0,109), and dark empty/bg
    (~73,0,57), (~57,0,44), (~38,0,30). Neutral world grays at bar corners
    are excluded (chroma / g gate) to avoid world bleed.
    """
    return ((g < 50) & (r > 25) & (b > 15) & (r > g) & (b >= g))


def oracle_core_feature(img, sid):
    """Oracle-derived complete feature mask for core hard HUD (full frame).

    Independent of C so missing Java feature pixels always score. Prefer
    atlas/source geometry ownership (icon cells, boss bar band) over loose
    full-frame color thresholds. Distinctive color classes still cover
    features C might omit (gold absorption, red hearts, bubbles, xp green,
    boss name) and are scored after crop to the state ROI.
    """
    r = img[:, :, 0].astype(np.int16)
    g = img[:, :, 1].astype(np.int16)
    b = img[:, :, 2].astype(np.int16)
    m = np.zeros(img.shape[:2], dtype=bool)

    if sid in ("hud_armor_iron", "hud_absorption_armor",
               "hud_hurt_flash_on", "hud_hurt_flash_off"):
        # Hearts row at J1 (always).
        m |= icon_row_chrome(img, J1)
        # red heart body (full / half) — restricted to heart/armor ROI band
        heart_band = icon_row_cells(J1)
        if sid in ("hud_armor_iron", "hud_absorption_armor"):
            heart_band |= icon_row_cells(J1 - 10 * S)
        if sid == "hud_absorption_armor":
            heart_band |= icon_row_cells(J1 - 20 * S)
        m |= heart_band & (r > 140) & (r > g + 30) & (r > b + 30)
        # flash-white heart overlays
        m |= heart_band & (r > 220) & (g > 220) & (b > 220)

        if sid == "hud_armor_iron":
            # Armor row sits at J1-10*S (no absorption lift).
            m |= icon_row_chrome(img, J1 - 10 * S)
        elif sid == "hud_absorption_armor":
            # Absorption lifts armor to J1-20*S; gold abs hearts occupy J1-10*S.
            # Do not duplicate J1-10*S as the armor row.
            m |= icon_row_chrome(img, J1 - 20 * S)  # lifted armor + black outlines
            m |= icon_row_chrome(img, J1 - 10 * S)  # gold absorption hearts row
            gold = ((r > 160) & (g > 100) & (g < 230) & (b < 130) &
                    (r > g + 10) & (g > b))
            m |= icon_row_cells(J1 - 10 * S) & gold

    elif sid == "hud_hunger_poison":
        x1 = HB_X + 182 * S
        for i in range(10):
            xa = x1 - (i + 1) * 8 * S
            m |= icon_row_chrome(img, J1, x0=xa, n=1)
        # brown haunch + poison tint (hunger ROI only via later crop)
        hunger_cells = np.zeros((H, W), dtype=bool)
        for i in range(10):
            xa = x1 - (i + 1) * 8 * S
            hunger_cells |= icon_row_cells(J1, x0=xa, n=1)
        m |= hunger_cells & (r > 90) & (r > g) & (g >= b - 5) & (b < 110) & (
            (r - b) > 25)
        m |= hunger_cells & (g > r + 5) & (g > b) & (g > 50) & (g < 200)

    elif sid == "hud_air_partial":
        air_y = (SH - 49) * S
        x1 = (CX + 91) * S
        air_cells = np.zeros((H, W), dtype=bool)
        for i in range(10):
            xa = x1 - (i + 1) * 8 * S
            m |= icon_row_chrome(img, air_y, x0=xa, n=1)
            air_cells |= icon_row_cells(air_y, x0=xa, n=1)
        # bubble cyan / white highlights inside air cells
        m |= air_cells & (b > 110) & (g > 90) & (b >= r - 5)
        m |= air_cells & (r > 200) & (g > 200) & (b > 200)

    elif sid == "hud_xp_half":
        xp_y = (SH - 29) * S
        rr = r[xp_y:xp_y + 5 * S, HB_X:HB_X + 182 * S]
        gg = g[xp_y:xp_y + 5 * S, HB_X:HB_X + 182 * S]
        bb = b[xp_y:xp_y + 5 * S, HB_X:HB_X + 182 * S]
        bar = ((gg > rr) & (gg > bb) & (gg > 40)) | ((rr < 25) & (gg < 25) & (bb < 25))
        m[xp_y:xp_y + 5 * S, HB_X:HB_X + 182 * S] |= bar
        ty0 = xp_y - 12 * S
        rr = r[ty0:xp_y, HB_X:HB_X + 182 * S]
        gg = g[ty0:xp_y, HB_X:HB_X + 182 * S]
        bb = b[ty0:xp_y, HB_X:HB_X + 182 * S]
        # level glyph green 0x80FF20-ish + black outline
        m[ty0:xp_y, HB_X:HB_X + 182 * S] |= (
            ((gg > 180) & (rr < 160) & (bb < 80)) |
            ((rr < 20) & (gg < 20) & (bb < 20))
        )
        # XP green only inside bar+glyph band (avoid world bleed)
        xp_band = np.zeros((H, W), dtype=bool)
        xp_band[ty0:xp_y + 5 * S, HB_X:HB_X + 182 * S] = True
        m |= xp_band & (g > 100) & (g > r + 10) & (g > b + 10)

    elif sid == "hud_durability_half":
        # Ownership for durability lives in durability_compare_mask (atlas
        # alpha + strip + C-extra). Keep a weak image-side hint here only for
        # callers that still invoke oracle_core_feature alone.
        ix = HB_X + 3 * S
        iy = HB_Y + 3 * S
        m |= wood_pickaxe_alpha_mask()
        m[iy + 13 * S:iy + 15 * S, ix + 2 * S:ix + 15 * S] = True
        mx = np.maximum(np.maximum(r, g), b)
        mn = np.minimum(np.minimum(r, g), b)
        m |= (mx - mn) > 50  # colored durability fill

    elif sid == "hud_boss_half":
        # bars.png PINK strip geometry + full pink/dark-pink gamut (not r>120 only)
        in_bar = boss_bar_band()
        m |= in_bar & boss_pink_gamut(r, g, b)
        # name body (white) + FontRenderer drop shadow — name band above bar
        bb_y = 12 * S
        bb_x = (CX - 91) * S
        name_band = np.zeros((H, W), dtype=bool)
        name_band[max(0, bb_y - 12 * S):bb_y, bb_x:bb_x + 182 * S] = True
        m |= name_band & (r > 200) & (g > 200) & (b > 200)
        m |= name_band & ((np.abs(r - 63) <= 3) & (np.abs(g - 63) <= 3) &
                          (np.abs(b - 63) <= 3))

    return m


def c_painted_mask(c_full):
    return np.abs(c_full.astype(np.int16) - GRAY).max(axis=2) > 8


# Cached 16x16 nonzero-alpha of items/wood_pickaxe.png (gui_atlas ITEM_270).
_WOOD_PICK_ALPHA_16 = None


def _load_wood_pickaxe_alpha_16():
    """Return (16,16) bool: nonzero alpha of wood_pickaxe layer0.

    Source order: jar items/wood_pickaxe.png, gui_atlas.h ITEM_270 alpha,
    /tmp/wood_pickaxe.png. Same texels C blits via gm_gui_item_icon.
    """
    global _WOOD_PICK_ALPHA_16
    if _WOOD_PICK_ALPHA_16 is not None:
        return _WOOD_PICK_ALPHA_16

    alpha = None

    # 1) gui_atlas.h ITEM_270 (always in tree after make game; same RGBA C blits)
    atlas_path = os.path.abspath(os.path.join(
        os.path.dirname(__file__), "..", "..", "magma", "assets", "gui_atlas.h"))
    if os.path.isfile(atlas_path):
        try:
            text = open(atlas_path, "r").read()
            import re
            m = re.search(
                r'\{\s*"ITEM_270",\s*16,\s*16,\s*(\d+)\s*\}', text)
            if m:
                off = int(m.group(1))
                start = text.index("static const unsigned char GUI_RGBA[")
                brace = text.index("{", start)
                # Walk GUI_RGBA decimals until off + 16*16*4 bytes.
                nums = []
                i = brace + 1
                n = len(text)
                val = None
                idx = 0
                need_end = off + 16 * 16 * 4
                while i < n and idx < need_end:
                    ch = text[i]
                    if ch.isdigit():
                        if val is None:
                            val = 0
                        val = val * 10 + (ord(ch) - 48)
                    else:
                        if val is not None:
                            if idx >= off:
                                nums.append(val)
                            idx += 1
                            val = None
                            if idx >= need_end:
                                break
                    i += 1
                if len(nums) >= 16 * 16 * 4:
                    a = np.array(
                        nums[:16 * 16 * 4], dtype=np.uint8).reshape(16, 16, 4)
                    alpha = a[:, :, 3] > 0
        except Exception:
            alpha = None

    # 2) loose PNG dump / optional jar (never SystemExit on missing jar)
    if alpha is None:
        png_cands = ["/tmp/wood_pickaxe.png"]
        jar = os.environ.get("MC_JAR", "")
        if jar and os.path.isfile(jar):
            try:
                import zipfile
                from io import BytesIO
                with zipfile.ZipFile(jar) as zf:
                    raw = zf.read(
                        "assets/minecraft/textures/items/wood_pickaxe.png")
                arr = np.asarray(
                    Image.open(BytesIO(raw)).convert("RGBA"))
                if arr.shape[0] == 16 and arr.shape[1] == 16:
                    alpha = arr[:, :, 3] > 0
            except Exception:
                alpha = None
        if alpha is None:
            for cand in png_cands:
                if os.path.isfile(cand):
                    arr = np.asarray(Image.open(cand).convert("RGBA"))
                    if arr.shape[0] == 16 and arr.shape[1] == 16:
                        alpha = arr[:, :, 3] > 0
                        break

    if alpha is None or int(np.asarray(alpha).sum()) < 16:
        raise RuntimeError(
            "wood_pickaxe.png alpha unavailable (gui_atlas.h / MC_JAR /tmp)")
    _WOOD_PICK_ALPHA_16 = np.asarray(alpha, dtype=bool)
    return _WOOD_PICK_ALPHA_16


def wood_pickaxe_alpha_mask():
    """Full-frame mask: every nonzero-alpha wood_pickaxe texel at slot-0 GUI."""
    a16 = _load_wood_pickaxe_alpha_16()
    # Nearest scale (guiScale=S): each texel -> SxS block.
    a_s = np.repeat(np.repeat(a16, S, axis=0), S, axis=1)
    m = np.zeros((H, W), dtype=bool)
    ix = HB_X + 3 * S
    iy = HB_Y + 3 * S
    m[iy:iy + 16 * S, ix:ix + 16 * S] = a_s
    return m


def durability_strip_mask():
    """Complete 13x2 durability strip (black underlay + fill columns)."""
    m = np.zeros((H, W), dtype=bool)
    ix = HB_X + 3 * S
    iy = HB_Y + 3 * S
    m[iy + 13 * S:iy + 15 * S, ix + 2 * S:ix + 15 * S] = True
    return m


# Cached (16*S, 16*S, 3) expected isolation underlay under slot-0 icon.
_HOTBAR_UNDERLAY_ICON = None


def _load_hotbar_underlay_icon_rgb():
    """Expected C RGB under transparent slot-0 wood-pick texels.

    Source: HUD_HOTBAR (widgets.png hotbar strip) nearest-scaled and alpha-
    composited over isolation GRAY with the same integer formula as
    hud_blend_px_tex (separate round then add):
      sp=(src*a+127)//255; out=min(255, sp+(dst*ia+127)//255).
    Icon cell maps to hotbar sprite local (3..18, 3..18) (slot 0 at strip+3).
    Exact per-pixel colors — not a mx/chroma threshold hole and not Java
    world underlay.
    """
    global _HOTBAR_UNDERLAY_ICON
    if _HOTBAR_UNDERLAY_ICON is not None:
        return _HOTBAR_UNDERLAY_ICON

    atlas_path = os.path.abspath(os.path.join(
        os.path.dirname(__file__), "..", "..", "magma", "assets", "hud_atlas.h"))
    if not os.path.isfile(atlas_path):
        raise RuntimeError(
            "hud_atlas.h unavailable for durability underlay membership")

    text = open(atlas_path, "r").read()
    import re
    m = re.search(r'\{\s*"HOTBAR",\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', text)
    if not m:
        raise RuntimeError("HUD_HOTBAR sprite missing from hud_atlas.h")
    spr_w, spr_h, off = int(m.group(1)), int(m.group(2)), int(m.group(3))
    if spr_w < 19 or spr_h < 19:
        raise RuntimeError("HUD_HOTBAR too small for slot-0 icon cell")

    start = text.index("static const unsigned char HUD_RGBA[")
    brace = text.index("{", start)
    nums = []
    i = brace + 1
    n = len(text)
    val = None
    idx = 0
    need_end = off + spr_w * spr_h * 4
    while i < n and idx < need_end:
        ch = text[i]
        if ch.isdigit():
            if val is None:
                val = 0
            val = val * 10 + (ord(ch) - 48)
        else:
            if val is not None:
                if idx >= off:
                    nums.append(val)
                idx += 1
                val = None
                if idx >= need_end:
                    break
        i += 1
    if len(nums) < spr_w * spr_h * 4:
        raise RuntimeError("HUD_RGBA truncated while reading HOTBAR")

    spr = np.array(nums[:spr_w * spr_h * 4], dtype=np.int32).reshape(
        spr_h, spr_w, 4)
    # Slot-0 icon covers hotbar local texels [3,19) x [3,19).
    cell = spr[3:19, 3:19, :]
    a = cell[:, :, 3]
    ia = 255 - a
    under16 = np.empty((16, 16, 3), dtype=np.uint8)
    for c in range(3):
        sp = (cell[:, :, c] * a + 127) // 255
        dp = (GRAY * ia + 127) // 255
        under16[:, :, c] = np.minimum(255, sp + dp).astype(np.uint8)
    # a==0 leaves isolation gray (no hotbar paint).
    bare = a == 0
    under16[bare] = (GRAY, GRAY, GRAY)

    under = np.repeat(np.repeat(under16, S, axis=0), S, axis=1)
    _HOTBAR_UNDERLAY_ICON = under
    return _HOTBAR_UNDERLAY_ICON


def durability_isolation_underlay_mask(c_full):
    """True where C matches exact isolation underlay inside the icon cell."""
    under = _load_hotbar_underlay_icon_rgb()
    ix = HB_X + 3 * S
    iy = HB_Y + 3 * S
    m = np.zeros((H, W), dtype=bool)
    c_cell = c_full[iy:iy + 16 * S, ix:ix + 16 * S].astype(np.int16)
    m[iy:iy + 16 * S, ix:ix + 16 * S] = np.all(
        c_cell == under.astype(np.int16), axis=2)
    return m


def durability_c_extra_mask(c_full):
    """C-only extras in the icon cell: anything not exact isolation underlay.

    Owned icon = wood_pickaxe atlas-alpha ∪ full 13x2 strip. Under transparent
    texels, C shows HUD_HOTBAR blended over GRAY (composition isolation vs
    Java world). Those exact per-pixel underlay colors are excluded. Any other
    unowned paint — mid-gray mx 56..79, mid-chroma, black, bright, misplaced
    underlay RGB — is residual. No broad threshold allow holes.
    """
    owned = wood_pickaxe_alpha_mask() | durability_strip_mask()
    ix = HB_X + 3 * S
    iy = HB_Y + 3 * S
    cell = np.zeros((H, W), dtype=bool)
    cell[iy:iy + 16 * S, ix:ix + 16 * S] = True
    unowned = cell & ~owned
    underlay = durability_isolation_underlay_mask(c_full)
    return unowned & ~underlay


def durability_compare_mask(c_full):
    """Local durability ownership: atlas-alpha ∪ full strip ∪ C-extra."""
    return (wood_pickaxe_alpha_mask() |
            durability_strip_mask() |
            durability_c_extra_mask(c_full))


def core_compare_mask(sid, ja_full, c_full, painted_full):
    """Java∪C complete feature mask for a core hard HUD state (full frame).

    Durability uses source/atlas-alpha ownership + strip + C-extra (does not
    flood painted_full into hotbar underlay holes). All other core states keep
    oracle feature ∪ C feature ∪ painted_full so missing Java and extra C both
    score.
    """
    if sid == "hud_durability_half":
        return durability_compare_mask(c_full)
    j_feat = oracle_core_feature(ja_full, sid)
    c_feat = oracle_core_feature(c_full, sid) | painted_full
    return j_feat | c_feat


def durability_colored_fill_ok(c_full):
    """Require non-black colored fill on the top row of the 13x2 strip."""
    ix = HB_X + 3 * S
    iy = HB_Y + 3 * S
    top = c_full[iy + 13 * S:iy + 14 * S, ix + 2 * S:ix + 15 * S]
    if top.size == 0:
        return False
    black = (top[:, :, 0] < 8) & (top[:, :, 1] < 8) & (top[:, :, 2] < 8)
    colored = (top.max(axis=2) > 20) & ~black
    return int(colored.sum()) >= 4



# Keep painted_mask (defined above) and c_painted_mask in sync.
# c_painted_mask is the core-path name; painted_mask is the overlay suite name.

def evaluate_core(sid, ja_full, jb_full, c_full, margin):
    """Core HUD oracle∪C hard gate. Returns row + fail/residual."""
    """Compare one state id. Returns row dict + fail/residual flags."""
    rect = roi_rect(sid)
    painted_full = c_painted_mask(c_full)
    core = sid in CORE_HARD

    if core:
        compare_full = core_compare_mask(sid, ja_full, c_full, painted_full)
    else:
        compare_full = painted_full

    ja = crop(ja_full, rect)
    jb = crop(jb_full, rect)
    c = crop(c_full, rect)
    painted = crop(painted_full.astype(np.uint8), rect).astype(bool)
    compare = crop(compare_full.astype(np.uint8), rect).astype(bool)

    h = min(ja.shape[0], jb.shape[0], c.shape[0], compare.shape[0], painted.shape[0])
    w = min(ja.shape[1], jb.shape[1], c.shape[1], compare.shape[1], painted.shape[1])
    ja, jb, c = ja[:h, :w], jb[:h, :w], c[:h, :w]
    painted = painted[:h, :w]
    compare = compare[:h, :w]

    # A/B noise on the compare mask (stable pixels) for a truthful floor.
    ab = np.abs(ja.astype(np.int16) - jb.astype(np.int16)).mean(axis=2)
    if compare.any():
        noise = float(np.abs(ja.astype(np.int16) - jb.astype(np.int16))[compare].mean())
    else:
        noise = mean_abs(ja, jb)
    stable = ab <= max(2.0, noise * 3.0 + 1.0)
    if (compare & stable).any():
        noise = float(np.abs(ja - jb)[compare & stable].mean())

    hard = sid in HARD
    # Core hard: score the full complete feature mask (no stable filter that
    # could drop a single mismatched chrome pixel). Other hard ROIs keep the
    # painted∩stable mean used historically.
    if core:
        m = compare
    else:
        m = compare & stable if stable.any() else compare

    n_painted = int(compare.sum()) if core else int(painted.sum())
    if m.any():
        diff = float(np.abs(c.astype(np.int16) - ja.astype(np.int16))[m].mean())
    elif n_painted == 0:
        diff = float("nan")
    else:
        diff = float(np.abs(c.astype(np.int16) - ja.astype(np.int16))[compare].mean())

    pix_err = np.abs(c.astype(np.int16) - ja.astype(np.int16)).max(axis=2)
    n_mismatch = int((m & (pix_err > 0)).sum()) if m.any() else 0
    n_hard_px = int((m & (pix_err >= HARD_THR)).sum()) if m.any() else 0

    if core:
        # Hard pass at measured A/B noise — no margin floor.
        gate = noise
    else:
        gate = noise + margin

    noise_lim = NOISE_MAX.get(sid, NOISE_MAX_DEFAULT)
    capture_ok = (noise == noise) and noise <= noise_lim and (
        n_painted > 0 if hard else True)
    # Hard states need some C paint in the ROI (composition isolation).
    if hard and int(painted.sum()) == 0:
        capture_ok = False

    dura_fill_ok = True
    if sid == "hud_durability_half":
        dura_fill_ok = durability_colored_fill_ok(c_full)

    if not capture_ok:
        verdict = "FAIL"
        reason = "capture_noise" if noise > noise_lim else "c_empty"
        fail = 1
        residual = 0
    elif hard:
        if core:
            ok = ((diff == diff) and diff <= gate + 1e-6 and
                  n_hard_px == 0 and dura_fill_ok)
        else:
            # No hidden max(gate, margin+1) floor.
            ok = (diff == diff) and diff <= gate + 1e-6
        if ok:
            verdict = "PASS"
            reason = "hard_parity"
            fail = 0
            residual = 0
        else:
            verdict = "RESIDUAL"
            if not dura_fill_ok:
                reason = "durability_no_colored_fill"
            else:
                reason = "hard_residual"
            fail = 0
            residual = 1
    else:
        verdict = "CAPTURE_OK"
        reason = "soft_capture"
        fail = 0
        residual = 0

    row = {
        "id": sid,
        "noise": noise,
        "c_vs_j": diff,
        "gate": gate,
        "verdict": verdict,
        "roi": list(rect),
        "hard": hard,
        "core_hard": core,
        "n_painted": n_painted,
        "n_compare": int(compare.sum()),
        "n_mismatch": n_mismatch,
        "n_hard_px": n_hard_px,
        "hard_px": n_hard_px,
        "hard_thr": HARD_THR,
        "noise_limit": noise_lim,
        "reason": reason,
    }
    if sid == "hud_durability_half":
        row["colored_fill"] = bool(dura_fill_ok)
    return row, fail, residual



def evaluate_state(sid, ja_full, jb_full, c_full, margin=2.0):
    """Return a result dict for one state (used by gate + mutation suite)."""
    if sid in FULLSCREEN_REPLACE:
        return evaluate_fullscreen_replace(sid, ja_full, jb_full, c_full)
    if sid in HAND_HARD:
        return evaluate_hand_exact(sid, ja_full, jb_full, c_full)
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
    if sid in CORE_HARD:
        row, _fail, _resid = evaluate_core(sid, ja_full, jb_full, c_full, margin)
        row = dict(row)
        row.setdefault("noise_max", None)
        row.setdefault("max_diff", None)
        # n_hard_px already set; map to hard_px key for table consistency
        if row.get("hard_px") is None and row.get("n_hard_px") is not None:
            row["hard_px"] = row["n_hard_px"]
        row.setdefault("hard_thr", HARD_THR)
        row.setdefault("fullscreen", False)
        row.setdefault("rule", "core_oracle_union")
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
    """Compare one state id. Routes fullscreen / hand / core / painted paths."""
    if sid in FULLSCREEN_REPLACE:
        row = evaluate_fullscreen_replace(sid, ja_full, jb_full, c_full)
        fail = 1 if row["verdict"] == "FAIL" else 0
        # CAPTURE_BLOCKED: A/B not bit-exact (no parity claim). Counts as
        # residual for exit status — same nonzero path as hard C residual.
        residual = 1 if row["verdict"] in ("RESIDUAL", "CAPTURE_BLOCKED") else 0
        return row, fail, residual
    if sid in HAND_HARD:
        row = evaluate_hand_exact(sid, ja_full, jb_full, c_full)
        fail = 1 if row["verdict"] == "FAIL" else 0
        residual = 1 if row["verdict"] in ("RESIDUAL", "CAPTURE_BLOCKED") else 0
        return row, fail, residual
    if sid in CORE_HARD:
        return evaluate_core(sid, ja_full, jb_full, c_full, margin)
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
            r_entry = {
                "id": sid,
                "noise": row["noise"],
                "noise_max": row.get("noise_max"),
                "n_ab_maxch_ge1": row.get("n_ab_maxch_ge1"),
                "c_vs_j": row["c_vs_j"],
                "gate": row["gate"],
                "hard_px": row.get("hard_px"),
                "hard_thr": row.get("hard_thr"),
                "max_diff": row.get("max_diff"),
                "verdict": row.get("verdict"),
                "reason": row.get("reason"),
                "residual_bbox": row.get("residual_bbox"),
                "residual_locs": row.get("residual_locs"),
            }
            if row.get("hand_exact"):
                r_entry.update({
                    "n_owned": row.get("n_owned"),
                    "n_painted": row.get("n_painted"),
                    "c_paint_nz": row.get("c_paint_nz"),
                    "c_paint_maxch": row.get("c_paint_maxch"),
                    "c_paint_mean": row.get("c_paint_mean"),
                    "n_only_j": row.get("n_only_j"),
                    "n_only_c": row.get("n_only_c"),
                    "rule": row.get("rule"),
                })
            residuals.append(r_entry)
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
        if row.get("hand_exact"):
            extra = (
                " owned=%s maxch=%s thr=%s c_paint_nz=%s/%s c_paint_maxch=%s%s" % (
                    row.get("n_owned"),
                    row.get("max_diff") if row.get("max_diff") == row.get("max_diff")
                    else -1.0,
                    row.get("hard_thr"),
                    row.get("c_paint_nz"),
                    row.get("n_painted"),
                    row.get("c_paint_maxch"),
                    extra))
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
            "PASS = hard parity only when noise_max==0 and hard_px==0 "
            "(bit-exact C vs Java_a on A/B-stable full ROI). Fullscreen "
            "inside-block + portal + underwater never use ceil(noise_max) as "
            "PASS tolerance. "
            "CAPTURE_BLOCKED = A/B stable maxch residual > 0 (no C may PASS, "
            "including C=Java_a / Java_b / midpoint / Java_a+1). "
            "RESIDUAL = A/B bit-exact but C residual (nonzero exit). "
            "CAPTURE_OK = soft capture integrity only (fire/"
            "full-frame death). Underwater is hard full-ROI residual. "
            "FAIL = missing/noise/empty/unstable. "
            "Core HUD: oracle∪C masks, hard_px==0 at A/B noise. "
            "hud_durability_half uses atlas-alpha∪strip∪C-extra ownership "
            "(C-extra = unowned icon pixels not equal to exact HUD_HOTBAR-over-"
            "GRAY isolation underlay; not threshold holes, not Java world). "
            "Hands: Java∪C subject ownership, hard_thr=0, A/B exact for PASS; "
            "no mean-budget hard_parity. Sticky-pin shield golden is exact "
            "capture; C residual with hard_px>0 stays RESIDUAL (not pixel-perfect). "
            "Portal: GuiIngame.renderPortal over gray C isolation; outdoor Java "
            "underlay is a same-scene product residual (not a fitted black cell). "
            "Real portal A/B has maxch=1 residuals -> CAPTURE_BLOCKED. "
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
    print("open residuals / capture blocks (hard, no parity claim):")
    if residuals:
        for r in residuals:
            extra = ""
            if r.get("hard_px") is not None:
                extra = (
                    "  hard_px=%s  thr=%s  max_diff=%s  noise_max=%s  "
                    "ab_maxch_ge1=%s  verdict=%s" % (
                        r.get("hard_px"), r.get("hard_thr"),
                        r.get("max_diff"), r.get("noise_max"),
                        r.get("n_ab_maxch_ge1"), r.get("verdict"))
                )
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


def _must_hard_fail(sid, ja, jb, c, margin, label, min_hard_px=1):
    """Assert evaluate_roi reports hard RESIDUAL/FAIL for a mutation."""
    row, fail, resid = evaluate_roi(sid, ja, jb, c, margin)
    hard_px = int(row.get("n_hard_px") or row.get("hard_px") or 0)
    ok_fail = ((row["verdict"] in ("RESIDUAL", "FAIL")) or fail or resid)
    if ok_fail and hard_px < min_hard_px and row["verdict"] != "FAIL":
        # Fullscreen path uses hard_px; core uses n_hard_px. Require teeth.
        if sid in CORE_HARD or sid in FULLSCREEN_REPLACE:
            ok_fail = hard_px >= min_hard_px or resid or fail
    if not ok_fail:
        print("MUTATION SELF-TEST FAIL: %s did not trip hard gate "
              "(verdict=%s c_vs_j=%s hard_px=%s n=%s)" % (
                  label, row["verdict"], row["c_vs_j"], hard_px,
                  row.get("n_painted")),
              file=sys.stderr)
        return 1
    print("mutation ok: %-32s -> %s  c_vs_j=%.4f  hard_px=%s  n=%s" % (
        label, row["verdict"],
        row["c_vs_j"] if row["c_vs_j"] == row["c_vs_j"] else -1.0,
        hard_px, row.get("n_painted")))
    return 0


def _must_pass(sid, ja, jb, c, margin, label):
    """Assert evaluate_roi reports PASS for a control mutation."""
    row, fail, resid = evaluate_roi(sid, ja, jb, c, margin)
    if row["verdict"] != "PASS" or fail or resid:
        print("MUTATION SELF-TEST FAIL: %s expected PASS, got %s "
              "(c_vs_j=%s hard_px=%s)" % (
                  label, row["verdict"], row.get("c_vs_j"),
                  row.get("n_hard_px", row.get("hard_px"))),
              file=sys.stderr)
        return 1
    print("mutation control ok: %-24s -> PASS  c_vs_j=%.4f  hard_px=%s" % (
        label, row["c_vs_j"], row.get("n_hard_px", row.get("hard_px"))))
    return 0


def death_mutation_self_test(goldens, cframes, margin):
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


def core_mutation_self_test(goldens, cframes, margin):
    """Prove erase/missing/shift/recolor/extra mutations fail core hard gates."""
    n_err = 0

    def load_triple(sid):
        ja_p = os.path.join(goldens, "%s_a.png" % sid)
        jb_p = os.path.join(goldens, "%s_b.png" % sid)
        c_p = os.path.join(cframes, "c_%s.ppm" % sid)
        if not (os.path.isfile(ja_p) and os.path.isfile(jb_p) and os.path.isfile(c_p)):
            return None
        return load_rgb(ja_p), load_rgb(jb_p), load_ppm(c_p)

    # ---- armor: baseline PASS + full mutation suite ----
    trip = load_triple("hud_armor_iron")
    if trip is None:
        print("mutation self-test: SKIP hud_armor_iron (missing frames)",
              file=sys.stderr)
        return 1
    ja, jb, c0 = trip
    row, fail, resid = evaluate_roi("hud_armor_iron", ja, jb, c0, margin)
    if row["verdict"] != "PASS" or fail or resid:
        print("MUTATION SELF-TEST FAIL: baseline hud_armor_iron not PASS (%s)" %
              row["verdict"], file=sys.stderr)
        n_err += 1
    else:
        print("mutation baseline: hud_armor_iron PASS c_vs_j=%.4f hard_px=%d" % (
            row["c_vs_j"], row["n_hard_px"]))

    rect = roi_rect("hud_armor_iron")

    # (1) erase 90% of C feature pixels
    c = c0.copy()
    painted = c_painted_mask(c)
    p = crop(painted.astype(np.uint8), rect).astype(bool)
    ys, xs = np.where(p)
    rng = np.random.RandomState(0)
    if len(ys) < 10:
        print("MUTATION SELF-TEST FAIL: too few painted armor pixels",
              file=sys.stderr)
        n_err += 1
    else:
        for i in rng.choice(len(ys), int(0.9 * len(ys)), replace=False):
            c[rect[1] + ys[i], rect[0] + xs[i]] = GRAY
        n_err += _must_hard_fail(
            "hud_armor_iron", ja, jb, c, margin, "erase_90pct")

    # (2) one-pixel survivor
    c = c0.copy()
    painted = c_painted_mask(c0)
    p = crop(painted.astype(np.uint8), rect).astype(bool)
    ys, xs = np.where(p)
    c[rect[1]:rect[3], rect[0]:rect[2]] = GRAY
    c[rect[1] + ys[0], rect[0] + xs[0]] = c0[rect[1] + ys[0], rect[0] + xs[0]]
    n_err += _must_hard_fail(
        "hud_armor_iron", ja, jb, c, margin, "one_pixel_survivor")

    # (3) missing heart row
    c = c0.copy()
    c[J1:J1 + 9 * S, HB_X:HB_X + 10 * 8 * S] = GRAY
    n_err += _must_hard_fail(
        "hud_armor_iron", ja, jb, c, margin, "missing_heart")

    # (4) shift feature +2 px
    c = np.full_like(c0, GRAY)
    src = crop(c0, rect)
    c[rect[1]:rect[3], rect[0] + 2:rect[2]] = src[:, :-2]
    n_err += _must_hard_fail(
        "hud_armor_iron", ja, jb, c, margin, "shift")

    # (5) recolor painted pixels
    c = c0.copy()
    painted = c_painted_mask(c)
    p = crop(painted.astype(np.uint8), rect).astype(bool)
    patch = crop(c, rect).copy()
    patch[p, 0] = np.clip(patch[p, 0].astype(np.int16) + 80, 0, 255)
    patch[p, 1] = np.clip(patch[p, 1].astype(np.int16) - 20, 0, 255)
    c[rect[1]:rect[3], rect[0]:rect[2]] = patch
    n_err += _must_hard_fail(
        "hud_armor_iron", ja, jb, c, margin, "recolor")

    # (6) extra pixel
    c = c0.copy()
    painted = c_painted_mask(c)
    p = crop(painted.astype(np.uint8), rect).astype(bool)
    free = ~p
    fys, fxs = np.where(free)
    if len(fys) == 0:
        print("MUTATION SELF-TEST FAIL: no free pixel for extra", file=sys.stderr)
        n_err += 1
    else:
        mid = len(fys) // 2
        c[rect[1] + int(fys[mid]), rect[0] + int(fxs[mid])] = (255, 0, 0)
        n_err += _must_hard_fail(
            "hud_armor_iron", ja, jb, c, margin, "extra_pixel")

    # (7) armor_midgray_shadow_erase: thr=40 hole — mid-gray ~61 atlas shadows
    c = c0.copy()
    mg = ((c0[:, :, 0] == 61) & (c0[:, :, 1] == 61) & (c0[:, :, 2] == 61))
    n_mg = int(mg.sum())
    if n_mg < 50:
        print("MUTATION SELF-TEST FAIL: too few midgray~61 armor pixels (%d)" %
              n_mg, file=sys.stderr)
        n_err += 1
    else:
        c[mg] = GRAY
        n_err += _must_hard_fail(
            "hud_armor_iron", ja, jb, c, margin, "armor_midgray_shadow_erase",
            min_hard_px=min(200, n_mg // 2))
    # control: erase midgray only outside armor ROI — still PASS
    c = c0.copy()
    x0, y0, x1, y1 = rect
    mg_out = mg.copy()
    mg_out[y0:y1, x0:x1] = False
    c[mg_out] = GRAY
    n_err += _must_pass(
        "hud_armor_iron", ja, jb, c, margin, "armor_midgray_shadow_control")

    # ---- missing bubble (air) ----
    trip = load_triple("hud_air_partial")
    if trip is None:
        print("mutation self-test: SKIP hud_air_partial", file=sys.stderr)
        n_err += 1
    else:
        ja, jb, c0 = trip
        rect = roi_rect("hud_air_partial")
        c = c0.copy()
        painted = c_painted_mask(c0)
        p = crop(painted.astype(np.uint8), rect).astype(bool)
        ys, xs = np.where(p)
        for i in range(min(50, len(ys))):
            c[rect[1] + ys[i], rect[0] + xs[i]] = GRAY
        n_err += _must_hard_fail(
            "hud_air_partial", ja, jb, c, margin, "missing_bubble")

    # ---- missing xp fill ----
    trip = load_triple("hud_xp_half")
    if trip is None:
        print("mutation self-test: SKIP hud_xp_half", file=sys.stderr)
        n_err += 1
    else:
        ja, jb, c0 = trip
        c = c0.copy()
        xp_y = (SH - 29) * S
        c[xp_y:xp_y + 5 * S, HB_X:HB_X + 91 * S] = GRAY
        n_err += _must_hard_fail(
            "hud_xp_half", ja, jb, c, margin, "missing_xp_fill")

    # ---- durability: PASS control + non-vacuous mutations (all must fail) ----
    trip = load_triple("hud_durability_half")
    if trip is None:
        print("mutation self-test: SKIP hud_durability_half", file=sys.stderr)
        n_err += 1
    else:
        ja, jb, c0 = trip
        ix = HB_X + 3 * S
        iy = HB_Y + 3 * S
        row, fail, resid = evaluate_roi(
            "hud_durability_half", ja, jb, c0, margin)
        if row["verdict"] != "PASS" or fail or resid:
            print("MUTATION SELF-TEST FAIL: baseline hud_durability_half "
                  "not PASS (%s c_vs_j=%s hard_px=%s n=%s)" % (
                      row["verdict"], row["c_vs_j"], row.get("n_hard_px"),
                      row["n_painted"]), file=sys.stderr)
            n_err += 1
        else:
            print("mutation baseline: hud_durability_half PASS c_vs_j=%.4f "
                  "hard_px=%d n=%d" % (
                      row["c_vs_j"], row["n_hard_px"], row["n_painted"]))

        a16 = _load_wood_pickaxe_alpha_16()
        a_s = np.repeat(np.repeat(a16, S, axis=0), S, axis=1)
        strip_m = np.zeros((16 * S, 16 * S), dtype=bool)
        strip_m[13 * S:15 * S, 2 * S:15 * S] = True
        body_ys, body_xs = np.where(a_s)
        free_ys, free_xs = np.where((~a_s) & (~strip_m))
        if len(body_ys) < 1 or len(free_ys) < 4:
            print("MUTATION SELF-TEST FAIL: wood-pick alpha mask empty",
                  file=sys.stderr)
            n_err += 1
        else:
            # Missing icon body (strip kept).
            c = c0.copy()
            strip = c[iy + 13 * S:iy + 15 * S, ix + 2 * S:ix + 15 * S].copy()
            c[iy:iy + 16 * S, ix:ix + 16 * S] = (39, 38, 14)
            c[iy + 13 * S:iy + 15 * S, ix + 2 * S:ix + 15 * S] = strip
            n_err += _must_hard_fail(
                "hud_durability_half", ja, jb, c, margin,
                "durability_missing_icon")

            # One dark body texel (large channel Δ >= HARD_THR=2).
            c = c0.copy()
            bi = len(body_ys) // 3
            by, bx = int(body_ys[bi]), int(body_xs[bi])
            c[iy + by, ix + bx] = (5, 5, 5)
            n_err += _must_hard_fail(
                "hud_durability_half", ja, jb, c, margin,
                "durability_one_dark_texel")

            # Shift icon+strip one GUI px right inside the cell.
            c = c0.copy()
            src = c0[iy:iy + 16 * S, ix:ix + 16 * S].copy()
            c[iy:iy + 16 * S, ix:ix + 16 * S] = (39, 38, 14)
            c[iy:iy + 16 * S, ix + S:ix + 16 * S] = src[:, :16 * S - S]
            n_err += _must_hard_fail(
                "hud_durability_half", ja, jb, c, margin,
                "durability_shift")

            # Unowned extras: any non-exact isolation underlay must RESIDUAL.
            # Covers prior black/dark/bright plus mid-gray mx 56..79 and
            # mid-chroma classes that broad threshold holes falsely allowed.
            extra_rgbs = [
                ((0, 0, 0), "durability_black_extra"),
                ((5, 5, 5), "durability_dark_extra"),
                ((255, 0, 0), "durability_bright_extra"),
                ((60, 60, 60), "durability_midgray60_extra"),
                ((70, 70, 70), "durability_midgray70_extra"),
                ((79, 79, 79), "durability_midgray79_extra"),
                ((45, 20, 20), "durability_midchroma_452020"),
                ((40, 0, 0), "durability_midchroma_400000"),
                ((55, 30, 30), "durability_midchroma_553030"),
                ((40, 40, 40), "durability_isolation_gray_extra"),
                ((128, 128, 128), "durability_midgray128_extra"),
                ((20, 60, 20), "durability_midchroma_green"),
                ((30, 30, 60), "durability_midchroma_blue"),
                ((90, 40, 40), "durability_mx90_chroma"),
            ]
            under_cell = _load_hotbar_underlay_icon_rgb()
            for ei, (rgb, label) in enumerate(extra_rgbs):
                fi = ei % len(free_ys)
                fy, fx = int(free_ys[fi]), int(free_xs[fi])
                exp = tuple(int(x) for x in under_cell[fy, fx])
                if rgb == exp:
                    # Pick another free pixel whose underlay differs.
                    for fj in range(len(free_ys)):
                        fy2, fx2 = int(free_ys[fj]), int(free_xs[fj])
                        if tuple(int(x) for x in under_cell[fy2, fx2]) != rgb:
                            fy, fx = fy2, fx2
                            break
                c = c0.copy()
                c[iy + fy, ix + fx] = rgb
                n_err += _must_hard_fail(
                    "hud_durability_half", ja, jb, c, margin, label)

            # Boundary: single-channel / off-by-one from true underlay.
            fy0, fx0 = int(free_ys[0]), int(free_xs[0])
            base = [int(x) for x in under_cell[fy0, fx0]]
            boundary = [
                ((min(255, base[0] + 1), base[1], base[2]),
                 "durability_underlay_plus1_r"),
                ((base[0], min(255, base[1] + 1), base[2]),
                 "durability_underlay_plus1_g"),
                ((base[0], base[1], min(255, base[2] + 1)),
                 "durability_underlay_plus1_b"),
                ((max(0, base[0] - 1), base[1], base[2]),
                 "durability_underlay_minus1_r"),
            ]
            for rgb, label in boundary:
                if rgb == tuple(base):
                    continue
                c = c0.copy()
                c[iy + fy0, ix + fx0] = rgb
                n_err += _must_hard_fail(
                    "hud_durability_half", ja, jb, c, margin, label)

            # Misplaced underlay color (valid underlay RGB at wrong texel).
            if len(free_ys) >= 2:
                fy_a, fx_a = int(free_ys[0]), int(free_xs[0])
                col_a = tuple(int(x) for x in under_cell[fy_a, fx_a])
                moved = False
                for fj in range(1, len(free_ys)):
                    fy_b, fx_b = int(free_ys[fj]), int(free_xs[fj])
                    col_b = tuple(int(x) for x in under_cell[fy_b, fx_b])
                    if col_b != col_a:
                        c = c0.copy()
                        c[iy + fy_a, ix + fx_a] = col_b
                        n_err += _must_hard_fail(
                            "hud_durability_half", ja, jb, c, margin,
                            "durability_misplaced_underlay")
                        moved = True
                        break
                if not moved:
                    print("MUTATION SELF-TEST FAIL: no distinct underlay "
                          "pair for misplaced test", file=sys.stderr)
                    n_err += 1

            # Missing strip (icon body kept).
            c = c0.copy()
            c[iy + 13 * S:iy + 15 * S, ix + 2 * S:ix + 15 * S] = (39, 38, 14)
            n_err += _must_hard_fail(
                "hud_durability_half", ja, jb, c, margin,
                "durability_missing_strip")

            # Recolored strip fill.
            c = c0.copy()
            c[iy + 13 * S:iy + 14 * S, ix + 2 * S:ix + 15 * S] = (200, 0, 200)
            n_err += _must_hard_fail(
                "hud_durability_half", ja, jb, c, margin,
                "durability_recolor_strip")

            # Recolor one owned body texel (not underlay path).
            c = c0.copy()
            bi = len(body_ys) // 2
            by, bx = int(body_ys[bi]), int(body_xs[bi])
            c[iy + by, ix + bx] = (200, 50, 50)
            n_err += _must_hard_fail(
                "hud_durability_half", ja, jb, c, margin,
                "durability_recolor_body")

            # Black underlay only (no colored fill).
            c = c0.copy()
            c[iy + 13 * S:iy + 15 * S, ix + 2 * S:ix + 15 * S] = 0
            n_err += _must_hard_fail(
                "hud_durability_half", ja, jb, c, margin,
                "durability_black_only")

    # ---- boss: erase name chrome ----
    trip = load_triple("hud_boss_half")
    if trip is None:
        print("mutation self-test: SKIP hud_boss_half", file=sys.stderr)
        n_err += 1
    else:
        ja, jb, c0 = trip
        row_b, _, _ = evaluate_roi("hud_boss_half", ja, jb, c0, margin)
        if row_b["verdict"] != "PASS":
            print("MUTATION SELF-TEST FAIL: baseline hud_boss_half not PASS (%s)" %
                  row_b["verdict"], file=sys.stderr)
            n_err += 1
        else:
            print("mutation baseline: hud_boss_half PASS c_vs_j=%.4f hard_px=%d" % (
                row_b["c_vs_j"], row_b["n_hard_px"]))
        c = c0.copy()
        bb_x = (CX - 91) * S
        bb_y = 12 * S
        c[max(0, bb_y - 12 * S):bb_y, bb_x:bb_x + 182 * S] = GRAY
        n_err += _must_hard_fail(
            "hud_boss_half", ja, jb, c, margin, "boss_missing_name")

        # boss_dark_pink_erase: full dark-pink gamut (~1820 px) must score
        c = c0.copy()
        bar = c0[bb_y:bb_y + 5 * S, bb_x:bb_x + 182 * S]
        br = bar[:, :, 0].astype(np.int16)
        bg = bar[:, :, 1].astype(np.int16)
        bb = bar[:, :, 2].astype(np.int16)
        # dark / mid pink empty+bg (not the bright fill r>180)
        dp = boss_pink_gamut(br, bg, bb) & (br <= 120)
        n_dp = int(dp.sum())
        if n_dp < 1000:
            print("MUTATION SELF-TEST FAIL: too few boss dark-pink pixels (%d)" %
                  n_dp, file=sys.stderr)
            n_err += 1
        else:
            patch = c[bb_y:bb_y + 5 * S, bb_x:bb_x + 182 * S].copy()
            patch[dp] = GRAY
            c[bb_y:bb_y + 5 * S, bb_x:bb_x + 182 * S] = patch
            n_err += _must_hard_fail(
                "hud_boss_half", ja, jb, c, margin, "boss_dark_pink_erase",
                min_hard_px=min(1500, n_dp // 2))
        # control: erase dark-pink only outside the bar band — still PASS
        c = c0.copy()
        r0 = c0[:, :, 0].astype(np.int16)
        g0 = c0[:, :, 1].astype(np.int16)
        b0 = c0[:, :, 2].astype(np.int16)
        dp_full = boss_pink_gamut(r0, g0, b0) & (r0 <= 120)
        dp_full &= ~boss_bar_band()
        c[dp_full] = GRAY
        n_err += _must_pass(
            "hud_boss_half", ja, jb, c, margin, "boss_dark_pink_control")

    # ---- absorption: baseline PASS + gold-row erase/recolor + black outline ----
    trip = load_triple("hud_absorption_armor")
    if trip is None:
        print("mutation self-test: SKIP hud_absorption_armor", file=sys.stderr)
        n_err += 1
    else:
        ja, jb, c0 = trip
        row, fail, resid = evaluate_roi(
            "hud_absorption_armor", ja, jb, c0, margin)
        if row["verdict"] != "PASS" or fail or resid:
            print("MUTATION SELF-TEST FAIL: baseline hud_absorption_armor "
                  "not PASS (%s c_vs_j=%s hard_px=%s)" % (
                      row["verdict"], row["c_vs_j"], row.get("n_hard_px")),
                  file=sys.stderr)
            n_err += 1
        else:
            print("mutation baseline: hud_absorption_armor PASS "
                  "c_vs_j=%.4f hard_px=%d" % (
                      row["c_vs_j"], row["n_hard_px"]))
        # Erase second heart row (gold absorption icons at J1-10*S).
        c = c0.copy()
        c[J1 - 10 * S:J1 - 10 * S + 9 * S, HB_X:HB_X + 10 * 8 * S] = GRAY
        n_err += _must_hard_fail(
            "hud_absorption_armor", ja, jb, c, margin, "missing_gold_abs_row")
        # Recolor gold row to pure red (wrong sprite class).
        c = c0.copy()
        y0, y1 = J1 - 10 * S, J1 - 10 * S + 9 * S
        x0, x1 = HB_X, HB_X + 10 * 8 * S
        gold = ((c[:, :, 0] > 160) & (c[:, :, 1] > 100) & (c[:, :, 1] < 230) &
                (c[:, :, 2] < 130) & (c[:, :, 0] > c[:, :, 1] + 10) &
                (c[:, :, 1] > c[:, :, 2]))
        m = np.zeros(c.shape[:2], dtype=bool)
        m[y0:y1, x0:x1] = gold[y0:y1, x0:x1]
        c[m] = (255, 19, 19)
        n_err += _must_hard_fail(
            "hud_absorption_armor", ja, jb, c, margin, "recolor_gold_to_red")
        # Black-outline erase on lifted armor row must residual.
        c = c0.copy()
        y_arm = J1 - 20 * S
        blk = ((c0[:, :, 0] < 20) & (c0[:, :, 1] < 20) & (c0[:, :, 2] < 20))
        cells = icon_row_cells(y_arm)
        blk_arm = blk & cells
        n_blk = int(blk_arm.sum())
        if n_blk < 100:
            print("MUTATION SELF-TEST FAIL: too few absorption armor black "
                  "outline pixels (%d)" % n_blk, file=sys.stderr)
            n_err += 1
        else:
            c[blk_arm] = GRAY
            n_err += _must_hard_fail(
                "hud_absorption_armor", ja, jb, c, margin,
                "absorption_armor_black_erase", min_hard_px=min(200, n_blk // 2))
        # control: erase black only outside owned icon rows — still PASS
        c = c0.copy()
        owned = (icon_row_cells(J1) | icon_row_cells(J1 - 10 * S) |
                 icon_row_cells(J1 - 20 * S))
        blk_out = blk & ~owned
        c[blk_out] = GRAY
        n_err += _must_pass(
            "hud_absorption_armor", ja, jb, c, margin,
            "absorption_armor_black_control")

    # ---- underwater: honest RESIDUAL baseline + omission/extra must not PASS ----
    trip = load_triple("overlay_underwater")
    if trip is None:
        print("mutation self-test: SKIP overlay_underwater", file=sys.stderr)
        n_err += 1
    else:
        ja, jb, c0 = trip
        row = evaluate_state("overlay_underwater", ja, jb, c0, margin)
        if row["verdict"] == "PASS":
            print("MUTATION SELF-TEST FAIL: overlay_underwater must stay "
                  "honest RESIDUAL (got PASS)", file=sys.stderr)
            n_err += 1
        elif row["verdict"] == "FAIL":
            print("MUTATION SELF-TEST FAIL: overlay_underwater capture FAIL "
                  "(%s)" % row.get("reason"), file=sys.stderr)
            n_err += 1
        else:
            print("mutation baseline: overlay_underwater RESIDUAL "
                  "c_vs_j=%.4f hard_px=%s (honest open)" % (
                      row["c_vs_j"], row.get("hard_px")))
        # Omission: wipe most of C to ambient/gray — must not PASS
        c = c0.copy()
        c[:, :] = GRAY
        row_m = evaluate_state("overlay_underwater", ja, jb, c, margin)
        if row_m["verdict"] == "PASS":
            print("MUTATION SELF-TEST FAIL: underwater full wipe still PASS",
                  file=sys.stderr)
            n_err += 1
        else:
            print("mutation ok: %-32s -> %s  hard_px=%s" % (
                "underwater_omission_wipe", row_m["verdict"],
                row_m.get("hard_px")))
        # Extra: paint a bright wrong block in center — must not PASS
        c = c0.copy()
        c[H // 2 - 20:H // 2 + 20, W // 2 - 20:W // 2 + 20] = (255, 0, 255)
        row_m = evaluate_state("overlay_underwater", ja, jb, c, margin)
        if row_m["verdict"] == "PASS":
            print("MUTATION SELF-TEST FAIL: underwater extra block still PASS",
                  file=sys.stderr)
            n_err += 1
        else:
            print("mutation ok: %-32s -> %s  hard_px=%s" % (
                "underwater_extra_block", row_m["verdict"],
                row_m.get("hard_px")))

    if n_err:
        print("ui_hud core mutation self-test: FAIL (%d)" % n_err)
        return 1
    print("ui_hud core mutation self-test: PASS")
    return 0



def mutation_self_test(goldens, cframes, margin):
    """Death chrome + core HUD + underwater mutation self-tests."""
    n = 0
    n += death_mutation_self_test(goldens, cframes, margin)
    n += core_mutation_self_test(goldens, cframes, margin)
    if n:
        print("ui_hud mutation self-test: FAIL (death+core combined rc=%d)" % n)
        return 1
    print("ui_hud mutation self-test: PASS (death + core + underwater)")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--cframes", required=True)
    ap.add_argument("--margin", type=float, default=2.0)
    ap.add_argument("--report", default="")
    ap.add_argument("--mutation-self-test", action="store_true",
                    help="Prove death/core/underwater mutations trip hard gates")
    args = ap.parse_args()

    if args.mutation_self_test:
        return mutation_self_test(args.goldens, args.cframes, args.margin)

    code, _ = run_compare(args.goldens, args.cframes, args.margin, args.report)
    return code


if __name__ == "__main__":
    sys.exit(main())
