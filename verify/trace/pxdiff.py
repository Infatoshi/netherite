#!/usr/bin/env python3
"""pxdiff.py - zoom into a golden-vs-candidate pixel diff and name its cause.

grind.py ranks whole frames; pixel_gate.py decides pass/fail. This is the third
tool: once you know WHICH frame is wrong, it tells you which clusters are wrong,
lets you zoom to the texel, and runs the discriminators that separate the
divergence families we actually keep hitting.

Sources (any subcommand takes one of these):
  --tape NAME --tick N    golden tapes/<NAME>_frames/f_%06d.png vs the newest
                          out/tape_<NAME>/magma_frames.npy row for that tick
  --a A.png --b B.png     any pair (mc_capture / ui_hud / ui_entities gates)

Subcommands:
  survey     one-shot triage: overview.png with numbered boxes + a zoom
             triptych per top cluster + survey.json. Agents START here.
  clusters   labeled cluster table + a cause verdict per cluster
  zoom       write a golden|candidate|heat triptych PNG, zoomed to a cluster
             or an explicit rect, nearest-neighbour with a texel grid
  pixels     exact RGB pairs for a small rect, as text
  probe      every discriminator for one cluster or rect, verbose
  frames     rank a whole tape's frames by unexplained cluster pixels
  selftest   synthetic mutations with known causes; verifies the verdicts

Reading the output:
  px counts  survey/clusters count connected-component MEMBERS; probe and
             pixels count every differing pixel inside the (padded) rect, so
             probe's count is >= the cluster's px. Both are correct.
  gate class mirrors pixel_gate's masking (positional accept masks, then the
             tape's known-divergence sidecar classes): it says which budget
             absorbed the pixels, NOT which renderer subsystem is at fault. A
             report's soak_from marks spill from an over-budget class.
  unresolved big unresolved clusters are auto-refined into tile verdicts
             (refined: line / children field); frame-level notes flag the two
             whole-frame patterns no single cluster can name (global shift,
             camera/pose error). Never report unresolved as a diagnosis.

Verdicts (what the discriminators mean):
  texel-selection  candidate values are golden values from the local
                   neighbourhood, reshuffled: nearest-neighbour minification
                   picking a different source texel. Either zero-mean with
                   high sigma, or a LOCAL one-texel slip (bands of screen rows
                   sampling the neighbouring texture row) - the latter shows up
                   as best_shift (-1,0)/(0,-1) with a high texel_selection_tol4.
  shading-offset   uniform signed bias, low sigma, structure_corr high: a
                   lighting / fog / tint scalar is wrong, the geometry and
                   texture are right. If the bias is uniform but the golden
                   structure is gone (structure_corr <= 0.5), it is content.
  registration     an integer pixel shift beats dx=dy=0 by a wide margin: the
                   content is right and placed wrong.
  content          one side has structure the other lacks: missing or extra
                   geometry, the only family that is a hard bug by itself.
  edge             differing pixels hug golden gradients: silhouette / AA.
  cutout-sky+/-    a real coverage difference: >15 percent of the differing
                   pixels have one side at the background colour and the other
                   not, so a CUTOUT surface (foliage, cross plants) is letting
                   through more (+) or less (-) background than the oracle.
                   sky_align alone does NOT establish this - on a minified
                   canopy the delta is zero-mean with sigma 50-70 and a mean of
                   +4 along the sky axis gives align 0.997 at 3 percent holes.

Usage:
  uv run --no-project --with numpy,scipy,pillow python pxdiff.py clusters \\
      --tape scenario_soulsand_ice_20260723T001810Z --tick 50
  uv run ... python pxdiff.py zoom --tape T --tick 50 --cluster 0 -o /tmp/z.png
  uv run ... python pxdiff.py selftest
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pixel_gate as pg  # noqa: E402


# ---------------------------------------------------------------- sources

def _tape_frames_dir(tape):
    """tapes/<tape>_frames, falling back to tapes/retired/<tape>_frames."""
    d = os.path.join(HERE, "..", "tapes", tape + "_frames")
    if os.path.isdir(d):
        return d
    alt = os.path.join(HERE, "..", "tapes", "retired", tape + "_frames")
    return alt if os.path.isdir(alt) else d


def _tape_meta(tape):
    """tapes/<tape>.meta.json, falling back to tapes/retired/."""
    m = os.path.join(HERE, "..", "tapes", tape + ".meta.json")
    if os.path.exists(m):
        return m
    return os.path.join(HERE, "..", "tapes", "retired",
                        tape + ".meta.json")


def tape_hide_gui(tape):
    """Mirror replay_tape.tape_hide_gui: no HUD was drawn on either side."""
    if not tape:
        return False
    meta = _tape_meta(tape)
    try:
        with open(meta) as f:
            return bool(json.load(f).get("capture", {}).get("hide_gui", False))
    except (OSError, ValueError, TypeError):
        return False


def tape_hand_from_tick(tape):
    """Mirror replay_tape.tape_hand_from_tick: first tick with a hand again."""
    if not tape:
        return None
    meta = _tape_meta(tape)
    try:
        with open(meta) as f:
            v = json.load(f).get("capture", {}).get("hand_from_tick")
        return None if v is None else int(v)
    except (OSError, ValueError, TypeError):
        return None


def load_known(tape):
    if not tape:
        return []
    return pg.load_known_divergences(
        os.path.join(HERE, "..", "tapes", tape + ".jsonl"))


def load_pair(args):
    """Return (golden, candidate) as int16 HxWx3, plus a label."""
    if args.a and args.b:
        a = np.asarray(Image.open(args.a).convert("RGB"), dtype=np.int16)
        b = np.asarray(Image.open(args.b).convert("RGB"), dtype=np.int16)
        if a.shape != b.shape:
            sys.exit(f"shape mismatch: {a.shape} vs {b.shape}")
        return a, b, f"{os.path.basename(args.a)} vs {os.path.basename(args.b)}"
    if not (args.tape and args.tick is not None):
        sys.exit("need --tape/--tick or --a/--b")
    gp = os.path.join(_tape_frames_dir(args.tape), f"f_{args.tick:06d}.png")
    if not os.path.exists(gp):
        sys.exit(f"no golden for tick {args.tick}: {gp}")
    g = np.asarray(Image.open(gp).convert("RGB"), dtype=np.int16)
    out = os.path.join(HERE, "out", "tape_" + args.tape)
    npy, tks = os.path.join(out, "magma_frames.npy"), os.path.join(
        out, "magma_frames.ticks.npy")
    if not os.path.exists(npy):
        sys.exit(f"no candidate frames: {npy} (run replay_tape.py first)")
    fr, tk = np.load(npy, mmap_mode="r"), np.load(tks)
    hit = np.nonzero(tk == args.tick)[0]
    if not len(hit):
        sys.exit(f"tick {args.tick} not in {tks}")
    c = np.asarray(fr[int(hit[0])][..., :3], dtype=np.int16)
    return g, c, f"{args.tape} t={args.tick}"


# ---------------------------------------------------------- discriminators

def _neighbour_values(g, ys, xs, radius=1):
    """Fraction of differing pixels whose candidate value exists in the
    golden's (2r+1)^2 neighbourhood. High -> the renderer picked a real
    neighbouring texel, i.e. sampling, not shading."""
    h, w = g.shape[:2]
    return [(np.clip(ys + dy, 0, h - 1), np.clip(xs + dx, 0, w - 1))
            for dy in range(-radius, radius + 1)
            for dx in range(-radius, radius + 1)]


def texel_selection_frac(g, c, ys, xs, radius=1, tol=0):
    """tol=0 is exact equality. A real minified surface usually also carries a
    small per-row light/fog difference on top of the swapped texel, so the
    tolerant form is the one that fires on the texel-row-selection family."""
    hits = np.zeros(len(ys), dtype=bool)
    for iy, ix in _neighbour_values(g, ys, xs, radius):
        d = np.abs(g[iy, ix].astype(np.int32) - c[ys, xs]).max(axis=1)
        hits |= d <= tol
    return float(hits.mean()) if len(ys) else 0.0


def best_shift(g, c, box, span=3):
    """Best integer (dy,dx) alignment inside a padded box, and its mean abs."""
    y0, x0, y1, x1 = box
    h, w = g.shape[:2]
    p = span + 1
    Y0, X0 = max(0, y0 - p), max(0, x0 - p)
    Y1, X1 = min(h, y1 + p + 1), min(w, x1 + p + 1)
    gg, cc = g[Y0:Y1, X0:X1], c[Y0:Y1, X0:X1]
    # A cluster spanning the whole frame leaves no room to crop the roll
    # wraparound off; shrink the guard band rather than comparing empty slices.
    py = min(p, max(0, (Y1 - Y0 - 1) // 3))
    px = min(p, max(0, (X1 - X0 - 1) // 3))
    def _in(a):
        return a[py:a.shape[0] - py or None, px:a.shape[1] - px or None]
    best = None
    for dy in range(-span, span + 1):
        for dx in range(-span, span + 1):
            gs = np.roll(np.roll(gg, dy, axis=0), dx, axis=1)
            m = float(np.abs(_in(gs).astype(np.int32) - _in(cc)).mean())
            if best is None or m < best[2]:
                best = (dy, dx, m)
    zero = float(np.abs(_in(gg).astype(np.int32) - _in(cc)).mean())
    return best, zero


def edge_frac(g, ys, xs):
    """Fraction of differing pixels sitting on a golden luminance gradient."""
    lum = g.mean(axis=2)
    gy, gx = np.gradient(lum)
    mag = np.hypot(gy, gx)
    thr = max(8.0, float(np.percentile(mag, 90)))
    return float((mag[ys, xs] > thr).mean()) if len(ys) else 0.0


def content_frac(g, c, ys, xs):
    """How one-sided the cluster is: max channel delta well beyond texture
    noise on one side only reads as missing / extra geometry."""
    gd, cd = g[ys, xs].mean(axis=1), c[ys, xs].mean(axis=1)
    if not len(ys):
        return 0.0, 0.0
    return float((gd - cd).mean()), float(np.abs(gd - cd).std())


def sky_alignment(g, c, ys, xs):
    """Cosine between the mean delta and (sky - surface).

    CUTOUT foliage fails by showing the wrong amount of background through the
    gaps, not by shading: if the delta points straight at the sky colour the
    candidate is letting more sky through (thinner canopy), and if it points
    away it is covering sky the oracle shows. Either way the direction is the
    tell, because a lighting error moves all three channels together instead."""
    sky = np.median(g[:8].reshape(-1, 3), axis=0).astype(np.float64)
    surf = g[ys, xs].mean(axis=0).astype(np.float64)
    d = (c[ys, xs].astype(np.float64) - g[ys, xs]).mean(axis=0)
    axis = sky - surf
    na, nd = np.linalg.norm(axis), np.linalg.norm(d)
    if na < 1e-6 or nd < 1e-6:
        return 0.0
    return float(np.dot(axis / na, d / nd))


def structure_corr(g, c, ys, xs):
    """Pearson correlation of golden vs candidate luminance over the cluster.

    A shading / fog / tint error shifts every pixel by the same scalar, so the
    two sides stay perfectly correlated; replacing the content destroys the
    correlation. One side flat while the other is textured is the content case
    caught red-handed (a solid slab drawn over real texture); both sides flat
    means there is no structure to disagree on and the bias alone decides."""
    gl = g[ys, xs].astype(np.float64).mean(axis=1)
    cl = c[ys, xs].astype(np.float64).mean(axis=1)
    gs, cs = float(gl.std()), float(cl.std())
    if gs < 2 and cs < 2:
        return 1.0, gs, cs
    if gs < 2 or cs < 2:
        return 0.0, gs, cs
    return float(np.corrcoef(gl, cl)[0, 1]), gs, cs


def sky_coverage(g, c, ys, xs):
    """Count pixels where one side actually shows background and the other does
    not. Direction alone is not enough: on a minified canopy the delta is
    zero-mean with sigma 50-70, and a mean of +4 along the sky axis makes
    sky_align 0.997 while only 3 percent of the pixels are real holes. Measured
    on the canonical tape t=260 canopy, which this tool first mislabelled."""
    sky = np.median(g[:8].reshape(-1, 3), axis=0)
    dg = np.abs(g[ys, xs] - sky).max(axis=1)
    dc = np.abs(c[ys, xs] - sky).max(axis=1)
    if not len(ys):
        return 0.0, 0.0
    hole = float(((dc < 30) & (dg > 60)).mean())   # candidate leaks background
    fill = float(((dg < 30) & (dc > 60)).mean())   # candidate covers background
    return hole, fill


def verdict(g, c, ys, xs, box):
    d = (c[ys, xs].astype(np.int32) - g[ys, xs])
    # Saturated channels lie about the offset: +40 of fog on a bright surface
    # clips at 255, drags the channel mean down and inflates sigma until a
    # clean shading-offset reads as unresolved. Mask them out of the moments;
    # a channel that is clipped everywhere falls back to the raw stats.
    clip = ((g[ys, xs] <= 0) | (g[ys, xs] >= 255) |
            (c[ys, xs] <= 0) | (c[ys, xs] >= 255))
    dm = np.ma.array(d, mask=clip)
    mean = np.where(dm.count(axis=0) > 0,
                    np.ma.filled(dm.mean(axis=0), 0.0), d.mean(axis=0))
    sigma = np.where(dm.count(axis=0) > 1,
                     np.ma.filled(dm.std(axis=0), 0.0), d.std(axis=0))
    clip_frac = float(clip.any(axis=1).mean()) if len(ys) else 0.0
    sel = texel_selection_frac(g, c, ys, xs)
    sel_tol = texel_selection_frac(g, c, ys, xs, tol=4)
    (bdy, bdx, bmean), zero = best_shift(g, c, box)
    ef = edge_frac(g, ys, xs)
    bias, spread = content_frac(g, c, ys, xs)
    sky = sky_alignment(g, c, ys, xs)
    hole, fill = sky_coverage(g, c, ys, xs)
    corr, g_std, c_std = structure_corr(g, c, ys, xs)
    facts = {
        "mean_delta": [round(float(v), 2) for v in mean],
        "sigma": [round(float(v), 1) for v in sigma],
        "texel_selection_frac": round(sel, 3),
        "texel_selection_tol4": round(sel_tol, 3),
        "best_shift": [bdy, bdx],
        "shift_mean": round(bmean, 2),
        "zero_shift_mean": round(zero, 2),
        "edge_frac": round(ef, 3),
        "bias": round(bias, 2),
        "bias_sigma": round(spread, 1),
        "sky_align": round(sky, 3),
        "sky_hole_frac": round(hole, 3),
        "sky_fill_frac": round(fill, 3),
        "structure_corr": round(corr, 3),
        "lum_std": [round(g_std, 1), round(c_std, 1)],
        "clip_frac": round(clip_frac, 3),
    }
    biased = np.abs(mean).max() > 0.6 * sigma.max() and np.abs(mean).max() > 3
    local = (box[2] - box[0] + 1) * (box[3] - box[1] + 1) < 0.05 * g.shape[0] * g.shape[1]
    if (bdy, bdx) != (0, 0) and bmean < 0.6 * zero:
        # A LOCAL one-texel slip whose pixels also nearly match a neighbour is
        # the minified-surface texel-row family, not a camera misregistration.
        cause = ("texel-selection" if local and max(abs(bdy), abs(bdx)) <= 1
                 and sel_tol > 0.55 else "registration")
    elif (sel > 0.55 or sel_tol > 0.75) and not biased:
        # sel_tol carries the real surfaces: a swapped texel usually also has a
        # small per-row light/fog difference on top, so exact equality misses it.
        cause = "texel-selection"
    elif biased and sigma.max() < 12:
        # A uniform bias with the structure intact is a wrong scalar; a uniform
        # bias that also flattened or replaced the structure is new content
        # wearing a low sigma (a solid slab over near-flat golden ground reads
        # mean_delta 175 sigma 8 - measured on the validation set 2026-08-02).
        cause = "shading-offset" if corr > 0.5 else "content"
    elif max(hole, fill) > 0.15 and abs(sky) > 0.9:
        # Ahead of "content" on purpose: a cutout leak IS a content difference,
        # and naming the surface type is the more useful answer.
        cause = "cutout-sky+" if hole >= fill else "cutout-sky-"
    elif (abs(bias) > 40 and spread > 25) or (
            np.abs(mean).max() > 40 and corr <= 0.5):
        # Second arm: a recolor can cancel in the grey bias (+104 red, -15
        # green) while any single channel screams; low structure_corr says the
        # golden texture is gone, which grey-mean arithmetic cannot hide.
        cause = "content"
    elif ef > 0.6:
        cause = "edge"
    else:
        cause = "unresolved"
    return cause, facts


# ------------------------------------------------------------------ core

def cluster_list(g, c, thresh, min_px, known=None, tick=None, hide_gui=False,
                 hide_hand=None):
    """Label the diff the way the gate does, then name each cluster's cause.

    This mirrors pixel_gate.gate_frame_ex's masking order on purpose: positional
    accepts are topology BARRIERS removed before labeling (so an accepted HUD
    pixel cannot bridge two terrain blobs into one), and the tape's
    known_divergences sidecar is subtracted at the pixel level. Skip either and
    a frame inside a filed window - the canonical tape's 1800-2100 rain, say -
    reports six figures of UNEXPLAINED that the real gate absorbs, and whoever
    reads the output goes and chases weather.
    """
    from scipy import ndimage
    h, w = g.shape[:2]
    d = np.abs(c - g).max(axis=2)
    mask = d > thresh
    bossbar, hud, viewmodel = pg._positional_accept_masks(
        h, w, hide_gui, hide_hand)
    accept = bossbar | hud | viewmodel
    entries = pg._active_known(known, tick)
    protected = pg._solid_box_mask(
        c, mask if mask.any() else np.zeros((h, w), dtype=bool))

    groups = [(mask & bossbar, "bossbar"), (mask & hud, "hud"),
              (mask & viewmodel, "viewmodel")]
    rest = mask & ~accept
    for known_mask, cls in pg._known_pixel_masks(entries, g, c, rest, protected):
        groups.append((known_mask, cls))
        rest = rest & ~known_mask
    groups.append((rest, None))

    gb, cb = g.sum(axis=2), c.sum(axis=2)
    out = []
    for gmask, fixed in groups:
        if not gmask.any():
            continue
        lab, n = ndimage.label(gmask, structure=np.ones((3, 3), dtype=int))
        sizes = np.bincount(lab.ravel())
        for i in range(1, n + 1):
            if sizes[i] < min_px:
                continue
            ys, xs = np.nonzero(lab == i)
            box = (int(ys.min()), int(xs.min()), int(ys.max()), int(xs.max()))
            cls = fixed or pg._classify(
                gb, cb, ys, xs, w, h, hide_gui=hide_gui,
                hide_hand=hide_gui if hide_hand is None else hide_hand)
            if cls == "UNEXPLAINED" and entries:
                cls = (pg._known_cluster_class(entries, ys, xs, protected)
                       or cls)
            cause, facts = verdict(g, c, ys, xs, box)
            out.append({"px": int(sizes[i]), "box": box, "gate_class": cls,
                        "cause": cause, **facts})
    out.sort(key=lambda r: -r["px"])
    return out


def render_zoom(g, c, box, scale, grid, pad):
    y0, x0, y1, x1 = box
    h, w = g.shape[:2]
    y0, x0 = max(0, y0 - pad), max(0, x0 - pad)
    y1, x1 = min(h - 1, y1 + pad), min(w - 1, x1 + pad)
    gc = g[y0:y1 + 1, x0:x1 + 1].astype(np.uint8)
    cc = c[y0:y1 + 1, x0:x1 + 1].astype(np.uint8)
    d = np.abs(c[y0:y1 + 1, x0:x1 + 1] - g[y0:y1 + 1, x0:x1 + 1]).max(axis=2)
    heat = np.stack([np.clip(d * 4, 0, 255),
                     np.clip(d * 4 - 255, 0, 255),
                     np.zeros_like(d)], axis=2).astype(np.uint8)
    gap = np.full((gc.shape[0], 2, 3), 60, np.uint8)
    strip = np.concatenate([gc, gap, cc, gap, heat], axis=1)
    big = np.asarray(Image.fromarray(strip).resize(
        (strip.shape[1] * scale, strip.shape[0] * scale), Image.NEAREST)).copy()
    if grid and scale >= 4:
        big[::scale, :, :] = np.minimum(big[::scale, :, :] + 40, 255)
        big[:, ::scale, :] = np.minimum(big[:, ::scale, :] + 40, 255)
    return big, (y0, x0, y1, x1)


# -------------------------------------------------------------- selftest

def selftest():
    rng = np.random.default_rng(7)
    base = rng.integers(40, 200, size=(120, 160, 3)).astype(np.int16)
    # texel selection: swap each differing pixel for a real 3x3 neighbour
    tex = base.copy()
    ys, xs = np.nonzero(rng.random((120, 160)) < 0.35)
    keep = (ys > 1) & (ys < 118) & (xs > 1) & (xs < 158)
    ys, xs = ys[keep], xs[keep]
    dy = rng.integers(-1, 2, size=len(ys))
    dx = rng.integers(-1, 2, size=len(ys))
    tex[ys, xs] = base[ys + dy, xs + dx]
    # shading offset: uniform bias on one surface. A whole-frame sub-threshold
    # wash never forms a cluster at all - that is mild_shift's job, not this
    # tool's - so the cluster-level control has to clear DIFF_THRESH.
    sha = base.copy()
    sha[20:70, 30:130] = np.clip(sha[20:70, 30:130] + 30, 0, 255)
    # registration: whole frame shifted one pixel
    reg = np.roll(base, 1, axis=1)
    # content: a solid block only one side draws
    con = base.copy()
    con[40:80, 40:90] = 250
    # content over NEAR-FLAT golden: delta sigma is small, so before the
    # structure_corr discriminator this family read as shading-offset. A mildly
    # textured floor replaced by a solid slab must still say content.
    flat = base.copy()
    flat[40:80, 40:90] = 100 + rng.integers(-8, 9, size=(40, 50, 3))
    conf = flat.copy()
    conf[40:80, 40:90] = 180
    # texel swap PLUS a small light difference on top: exact match fails, the
    # tolerant one must still catch it. This is the shape of every real
    # minified surface in the tapes.
    texn = tex.copy()
    texn[ys, xs] = np.clip(texn[ys, xs] + 2, 0, 255)
    # a genuine cutout hole: candidate shows sky where golden shows leaf
    cut = base.copy()
    cut[:8] = [140, 180, 255]
    # 60 percent density so the holes percolate into one component above
    # MIN_CLUSTER; scattered single texels are per-pixel noise by policy.
    hy, hx = np.nonzero(rng.random((120, 160)) < 0.6)
    sel_h = (hy > 50) & (hy < 90) & (hx > 30) & (hx < 120)
    cut[hy[sel_h], hx[sel_h]] = [140, 180, 255]
    basec = base.copy()
    basec[:8] = [140, 180, 255]
    cases = [("texel-selection", tex, base), ("texel-selection", texn, base),
             ("shading-offset", sha, base), ("registration", reg, base),
             ("content", con, base), ("content", conf, flat),
             ("cutout-sky+", cut, basec)]
    ok = True
    for want, cand, ref in cases:
        cl = cluster_list(ref, cand, pg.DIFF_THRESH, pg.MIN_CLUSTER)
        got = cl[0]["cause"] if cl else "no-cluster"
        flag = "ok " if got == want else "FAIL"
        if got != want:
            ok = False
        print(f"  {flag} {want:16s} -> {got:16s} "
              f"px={cl[0]['px'] if cl else 0} "
              f"sel={cl[0]['texel_selection_frac'] if cl else '-'} "
              f"shift={cl[0]['best_shift'] if cl else '-'}")
    # a clean pair must produce nothing at all
    if cluster_list(base, base.copy(), pg.DIFF_THRESH, pg.MIN_CLUSTER):
        print("  FAIL identical pair produced clusters")
        ok = False
    else:
        print("  ok  identical            -> no clusters")
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def frame_shift_note(clusters):
    """A global 1px misregistration fragments into small clusters that each
    read as texel-selection locally; the tell is every cluster preferring the
    SAME nonzero shift. Say so, because no single cluster can."""
    from collections import Counter
    shifts = [tuple(r["best_shift"]) for r in clusters
              if tuple(r["best_shift"]) != (0, 0)
              and r["shift_mean"] < 0.6 * r["zero_shift_mean"]]
    if len(clusters) >= 3 and shifts:
        (s, n), = Counter(shifts).most_common(1)
        if n >= 3 and n >= 0.6 * len(clusters):
            return (f"frame-level: {n}/{len(clusters)} clusters prefer shift "
                    f"{s} - a whole-frame registration error fragments into "
                    "local texel-selection verdicts; suspect camera/projection "
                    "before texture sampling")
    return None


def refine_unresolved(g, c, r, thresh):
    """Tile a big unresolved cluster and verdict each tile: a cluster that
    mixes families (or hides one family under another's pixels) often refuses
    a whole-cluster verdict while its tiles resolve cleanly. Returns
    {cause: fraction_of_differing_px} over resolving tiles, or None."""
    y0, x0, y1, x1 = r["box"]
    hh, ww = y1 - y0 + 1, x1 - x0 + 1
    ny, nx = min(4, max(1, hh // 40)), min(4, max(1, ww // 40))
    if ny * nx < 2:
        return None
    d = np.abs(c - g).max(axis=2)
    counts = {}
    for iy in range(ny):
        for ix in range(nx):
            ty0, ty1 = y0 + iy * hh // ny, y0 + (iy + 1) * hh // ny - 1
            tx0, tx1 = x0 + ix * ww // nx, x0 + (ix + 1) * ww // nx - 1
            m = np.zeros(d.shape, dtype=bool)
            m[ty0:ty1 + 1, tx0:tx1 + 1] = True
            m &= d >= thresh
            ys, xs = np.nonzero(m)
            if len(ys) < 30:
                continue
            cause, _ = verdict(g, c, ys, xs, (ty0, tx0, ty1, tx1))
            counts[cause] = counts.get(cause, 0) + len(ys)
    if not counts:
        return None
    total = sum(counts.values())
    return {k: round(v / total, 2) for k, v in
            sorted(counts.items(), key=lambda kv: -kv[1])}


def frame_pose_note(clusters, frame_px):
    """A sub-block camera/pose error remaps most of the scene: huge cluster,
    no family fits, structure gone, no integer shift wins. The next probe is
    the PLAYER STATE, not more pixels - measured on nether_elytra t=176 where
    a 0.93-block landing-lag Y offset produced exactly this signature."""
    big = [r for r in clusters
           if r["px"] > 0.15 * frame_px and r["cause"] == "unresolved"
           and r.get("structure_corr", 1.0) <= 0
           and tuple(r["best_shift"]) == (0, 0)]
    if big:
        return (f"frame-level: {len(big)} unresolved cluster(s) span >15% of "
                "the frame with structure_corr<=0 and no shift win - the "
                "scene itself moved. Diff the pose first: tape jsonl "
                "x/y/z/on_ground vs out/tape_<NAME>/magma_state.jsonl for "
                "this tick, then re-triage.")
    return None


# ------------------------------------------------------------------ main

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["clusters", "zoom", "pixels", "probe",
                                    "frames", "survey", "selftest"])
    ap.add_argument("--tape")
    ap.add_argument("--tick", type=int)
    ap.add_argument("--a")
    ap.add_argument("--b")
    ap.add_argument("--cluster", type=int, help="cluster index from `clusters`")
    ap.add_argument("--at", help="X,Y centre for an explicit rect")
    ap.add_argument("--size", default="48x32", help="WxH for --at")
    ap.add_argument("--scale", type=int, default=8)
    ap.add_argument("--pad", type=int, default=4)
    ap.add_argument("--no-grid", action="store_true")
    ap.add_argument("--thresh", type=int, default=pg.DIFF_THRESH)
    ap.add_argument("--min-px", type=int, default=pg.MIN_CLUSTER)
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("-o", "--out", default="/tmp/pxdiff.png")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if args.cmd == "selftest":
        return selftest()

    if args.cmd == "frames":
        if not args.tape:
            sys.exit("frames needs --tape")
        out = os.path.join(HERE, "out", "tape_" + args.tape)
        fr = np.load(os.path.join(out, "magma_frames.npy"), mmap_mode="r")
        tk = np.load(os.path.join(out, "magma_frames.ticks.npy"))
        known = load_known(args.tape)
        hide_gui = tape_hide_gui(args.tape)
        hand_from = tape_hand_from_tick(args.tape)
        rows = []
        for i, t in enumerate(tk):
            gp = os.path.join(_tape_frames_dir(args.tape), f"f_{int(t):06d}.png")
            if not os.path.exists(gp):
                continue
            g = np.asarray(Image.open(gp).convert("RGB"), dtype=np.int16)
            c = np.asarray(fr[i][..., :3], dtype=np.int16)
            cl = cluster_list(g, c, args.thresh, args.min_px,
                              known=known, tick=int(t), hide_gui=hide_gui,
                              hide_hand=hide_gui and (hand_from is None
                                                      or int(t) < hand_from))
            unex = sum(r["px"] for r in cl if r["gate_class"] == "UNEXPLAINED")
            rows.append((int(t), unex, sum(r["px"] for r in cl),
                         float(np.abs(c - g).mean()),
                         cl[0]["cause"] if cl else "-"))
        rows.sort(key=lambda r: (-r[1], -r[2]))
        if args.json:
            print(json.dumps([dict(zip(
                ("tick", "unexplained_px", "cluster_px", "mean_abs",
                 "top_cause"), r)) for r in rows], indent=1))
        else:
            print(f"{'tick':>6} {'unexpl':>8} {'clust':>8} {'mean/ch':>8}  top cause")
            for r in rows[:args.top]:
                print(f"{r[0]:6d} {r[1]:8d} {r[2]:8d} {r[3]:8.2f}  {r[4]}")
        return 0

    g, c, label = load_pair(args)
    _hg = tape_hide_gui(args.tape)
    _hf = tape_hand_from_tick(args.tape)
    clusters = cluster_list(g, c, args.thresh, args.min_px,
                            known=load_known(args.tape), tick=args.tick,
                            hide_gui=_hg,
                            hide_hand=_hg and (_hf is None
                                               or (args.tick or 0) < _hf))

    if args.cmd in ("clusters", "survey"):
        for r in clusters:
            if r["cause"] == "unresolved" and r["px"] >= 4 * args.min_px:
                ch = refine_unresolved(g, c, r, args.thresh)
                if ch:
                    r["children"] = ch

    notes = [n for n in (frame_shift_note(clusters),
                         frame_pose_note(clusters, g.shape[0] * g.shape[1]))
             if n]

    if args.cmd == "clusters":
        if args.json:
            print(json.dumps({"source": label, "clusters": clusters,
                              "frame_notes": notes}, indent=1))
            return 0
        print(f"{label}: {len(clusters)} clusters >= {args.min_px}px "
              f"at thresh {args.thresh}, whole frame mean "
              f"{float(np.abs(c - g).mean()):.3f}/ch")
        print(f"{'#':>3} {'px':>6}  {'bbox y0,x0,y1,x1':<22} {'gate':<12} "
              f"{'cause':<16} sel  tol4  shift  mean_delta")
        for i, r in enumerate(clusters):
            y0, x0, y1, x1 = r["box"]
            print(f"{i:3d} {r['px']:6d}  {f'{y0},{x0},{y1},{x1}':<22} "
                  f"{r['gate_class']:<12} {r['cause']:<16} "
                  f"{r['texel_selection_frac']:.2f} "
                  f"{r['texel_selection_tol4']:.2f} "
                  f"{str(tuple(r['best_shift'])):>7} {r['mean_delta']}")
            if "children" in r:
                kids = ", ".join(f"{k} {v:.0%}" for k, v in
                                 r["children"].items())
                print(f"           refined (tile majority): {kids}")
        for n in notes:
            print(n)
        return 0

    if args.cmd == "survey":
        outdir = args.out
        if outdir.endswith(".png"):
            outdir = outdir[:-4] + "_survey"
        os.makedirs(outdir, exist_ok=True)
        k = min(len(clusters), args.top, 5)
        top = clusters[:k]
        from PIL import ImageDraw
        over = Image.fromarray(c.astype(np.uint8))
        draw = ImageDraw.Draw(over)
        for i, r in enumerate(top):
            y0, x0, y1, x1 = r["box"]
            draw.rectangle([x0 - 1, y0 - 1, x1 + 1, y1 + 1],
                           outline=(255, 0, 255))
            draw.text((x0 + 2, max(0, y0 - 11)), str(i), fill=(255, 0, 255))
        over_path = os.path.join(outdir, "overview.png")
        over.save(over_path)
        report = {"source": label, "frame_notes": notes, "clusters": []}
        for i, r in enumerate(top):
            y0, x0, y1, x1 = r["box"]
            cw = (x1 - x0 + 1 + 2 * args.pad) * 3 + 4
            scale = max(2, min(12, 1200 // max(1, cw)))
            big, used = render_zoom(g, c, r["box"], scale,
                                    not args.no_grid, args.pad)
            zp = os.path.join(outdir, f"zoom_{i}.png")
            Image.fromarray(big).save(zp)
            report["clusters"].append({**r, "zoom": zp, "zoom_scale": scale,
                                       "zoom_rect": list(used)})
        with open(os.path.join(outdir, "survey.json"), "w") as f:
            json.dump(report, f, indent=1)
        print(f"{label}: top {k} of {len(clusters)} clusters -> {outdir}")
        print(f"{'#':>3} {'px':>6}  {'bbox y0,x0,y1,x1':<22} {'gate':<12} "
              f"{'cause':<16} zoom")
        for i, r in enumerate(report["clusters"]):
            y0, x0, y1, x1 = r["box"]
            print(f"{i:3d} {r['px']:6d}  {f'{y0},{x0},{y1},{x1}':<22} "
                  f"{r['gate_class']:<12} {r['cause']:<16} {r['zoom']}")
            if "children" in r:
                kids = ", ".join(f"{k} {v:.0%}" for k, v in
                                 r["children"].items())
                print(f"           refined (tile majority): {kids}")
        for n in notes:
            print(n)
        print(f"overview (numbered boxes): {over_path}")
        return 0

    if args.at:
        cx, cy = (int(v) for v in args.at.split(","))
        bw, bh = (int(v) for v in args.size.lower().split("x"))
        box = (cy - bh // 2, cx - bw // 2, cy + bh // 2, cx + bw // 2)
    elif args.cluster is not None:
        if args.cluster >= len(clusters):
            sys.exit(f"only {len(clusters)} clusters")
        box = clusters[args.cluster]["box"]
    else:
        sys.exit("need --cluster N or --at X,Y")

    if args.cmd == "zoom":
        big, used = render_zoom(g, c, box, args.scale, not args.no_grid,
                                args.pad)
        Image.fromarray(big).save(args.out)
        print(f"{label}: golden | candidate | heat  rect y[{used[0]},{used[2]}]"
              f" x[{used[1]},{used[3]}] at {args.scale}x -> {args.out}")
        return 0

    if args.cmd == "pixels":
        y0, x0, y1, x1 = box
        n = 0
        for y in range(max(0, y0), min(g.shape[0], y1 + 1)):
            for x in range(max(0, x0), min(g.shape[1], x1 + 1)):
                gv, cv = g[y, x], c[y, x]
                if np.array_equal(gv, cv):
                    continue
                print(f"({x:4d},{y:4d}) golden {tuple(int(v) for v in gv)!s:>18}"
                      f"  cand {tuple(int(v) for v in cv)!s:>18}"
                      f"  d {tuple(int(a) - int(b) for a, b in zip(cv, gv))}")
                n += 1
                if n >= 400:
                    print("... truncated at 400 differing pixels")
                    return 0
        print(f"{n} differing pixels in rect")
        return 0

    if args.cmd == "probe":
        d = np.abs(c - g).max(axis=2)
        m = np.zeros_like(d, dtype=bool)
        y0, x0, y1, x1 = box
        m[max(0, y0):y1 + 1, max(0, x0):x1 + 1] = True
        m &= d >= args.thresh
        ys, xs = np.nonzero(m)
        if not len(ys):
            print("no differing pixels in rect")
            return 0
        cause, facts = verdict(g, c, ys, xs, box)
        print(f"{label}  rect y[{y0},{y1}] x[{x0},{x1}]  {len(ys)} differing px"
              " (every differing px in the rect; cluster px counts are"
              " connected-component members, so this can be larger)")
        print(f"  cause: {cause}")
        for k, v in facts.items():
            print(f"    {k:22s} {v}")
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
