#!/usr/bin/env python3
"""Validate Java A/B goldens: non-empty presence + A/B stability + inter-state gates.

Rejects empty-sky frames and pins that failed to change geometry (squish==size2,
identical dragon death stages, near-empty death 190). Does not invent pixels.
Exit 0 only if every requested state passes. Use before commit and after capture.

Approval is ROI geometry via Pillow/numpy — never filesize alone.
"""
from __future__ import print_function

import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

W, H = 854, 480

# Presence = mean |ROI - sky corner|. Empty sky/horizon frames sit near 0–6.
# Real subjects (pad + entity) land well above these floors.
PRESENCE_MIN = {
    "default": 12.0,
    "slime": 15.0,
    "magma": 15.0,
    "dragon": 8.0,   # high-air corpse can be sparse but must not be blank sky
    "dig": 20.0,
    "fireball": 20.0,
    "xp_orb": 20.0,
}
# A/B noise = mean |A-B| on ROI. Dig particles re-roll; allow more.
NOISE_MAX = {
    "default": 3.0,
    "dig": 8.0,
    "dragon": 5.0,
    "fireball": 4.0,
}

# Inter-state floors (mean abs RGB on shared ROI). Below = pin did nothing.
SQUISH_VS_SIZE2_MIN = 2.0
SIZE_PAIR_MIN = 1.5
DRAGON_STAGE_PAIR_MIN = 2.0
# Squish geometry: sq=1 stretches Y and compresses XZ vs idle size2
# (RenderSlime/RenderMagmaCube.preRenderCallback). Measured on center-band body mask.
SQUISH_YSPAN_RATIO_MIN = 1.08   # squish yspan / size2 yspan
SQUISH_XSPAN_RATIO_MAX = 0.95   # squish xspan / size2 xspan (thinner)
SQUISH_ASPECT_GAIN_MIN = 1.10   # (y/x)_squish / (y/x)_size2
# Dragon body/ray: non-sky subject pixels in ROI must exist at every stage.
DRAGON_SUBJECT_MIN_FRAC = 0.004
# deathTicks=190 dissolves body heavily but rays remain; reject near-empty.
DRAGON_190_SUBJECT_MIN_FRAC = 0.002


def roi_rect(state_id):
    if state_id.startswith("dragon_death"):
        return (80, 40, W - 80, H - 80)
    if state_id.startswith("dig_"):
        return (W // 2 - 120, H // 2 - 80, W // 2 + 120, H // 2 + 100)
    if state_id.startswith("fireball"):
        return (W // 2 - 100, H // 2 - 100, W // 2 + 100, H // 2 + 80)
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
        raise ValueError("%s shape %s want %dx%d" % (path, a.shape, W, H))
    return a


def crop(a, rect):
    x0, y0, x1, y1 = rect
    return a[y0:y1, x0:x1]


def mean_abs(a, b):
    return float(np.abs(a.astype(np.int16) - b.astype(np.int16)).mean())


def subject_mask(roi, fam):
    """Boolean mask of likely subject pixels (not blue sky / flat horizon)."""
    r = roi[:, :, 0].astype(np.int16)
    g = roi[:, :, 1].astype(np.int16)
    b = roi[:, :, 2].astype(np.int16)
    if fam == "dragon":
        # Body: dark purple/black; rays: bright magenta/white accents.
        dark = (r < 90) & (g < 90) & (b < 110)
        ray = (r > 160) & (b > 160) & (g < r - 20)
        # Non-sky residual (anything not pale blue)
        nonsky = ~((b > 200) & (g > 180) & (r > 140) & ((b - r) > 30))
        return dark | ray | (nonsky & ((r + g + b) < 520))
    if fam == "slime":
        # Bright slime body/gel; exclude dark grass (G-R small, G low).
        return (g > r + 40) & (g > b + 25) & (g > 130) & (r < 140)
    if fam == "magma":
        # Red/dark magma body; exclude sky. Tall squish paints near-black columns.
        sky = (b > 200) & (g > 180) & (r > 140)
        warm = (r > g + 8) & (r > b + 5) & (r > 40)
        dark = (r + g + b < 40) & ~sky
        return (warm | dark) & ~sky
    # generic: not sky-blue dominant
    return ~((b > 200) & (g > 180) & (r > 140) & ((b - r) > 30))


def body_mask_fullframe(img, fam):
    """Full-frame body mask constrained to the center subject band.

    Cross-session pad/grass must not dominate spans; the entity sits near
    screen center for the capture camera.
    """
    r = img[:, :, 0].astype(np.int16)
    g = img[:, :, 1].astype(np.int16)
    b = img[:, :, 2].astype(np.int16)
    h, w = img.shape[0], img.shape[1]
    xs = np.arange(w)[None, :]
    ys = np.arange(h)[:, None]
    # Center band around subject (capture aims entity at mid-x, mid-upper y).
    band = (xs > w // 2 - 100) & (xs < w // 2 + 100) & (ys > 40) & (ys < h - 40)
    if fam == "slime":
        body = (g > r + 40) & (g > b + 25) & (g > 130) & (r < 140)
    elif fam == "magma":
        sky = (b > 200) & (g > 180) & (r > 140)
        warm = (r > g + 8) & (r > b + 5) & (r > 40)
        dark = (r + g + b < 40) & ~sky
        body = (warm | dark) & ~sky
    else:
        body = subject_mask(img, fam)
    return body & band


def mask_spans(mask):
    if not mask.any():
        return 0, 0, 0
    ys, xs = np.where(mask)
    return int(ys.max() - ys.min()), int(xs.max() - xs.min()), int(mask.sum())


def validate_state(goldens, sid):
    ja = os.path.join(goldens, "%s_a.png" % sid)
    jb = os.path.join(goldens, "%s_b.png" % sid)
    meta = os.path.join(goldens, "meta", "%s.json" % sid)
    if not (os.path.isfile(ja) and os.path.isfile(jb)):
        return {"id": sid, "status": "MISSING", "ok": False,
                "detail": "missing a/b png"}
    # Tiny files are a hard reject, but passing size is never approval.
    if os.path.getsize(ja) < 1000 or os.path.getsize(jb) < 1000:
        return {"id": sid, "status": "EMPTY_FILE", "ok": False,
                "detail": "png too small"}
    try:
        A = load_rgb(ja)
        B = load_rgb(jb)
    except Exception as ex:
        return {"id": sid, "status": "BAD_PNG", "ok": False, "detail": str(ex)}

    rect = roi_rect(sid)
    ra, rb = crop(A, rect), crop(B, rect)
    noise = mean_abs(ra, rb)
    h, w = ra.shape[0], ra.shape[1]
    sky = A[8:8 + h, 8:8 + w]
    if sky.shape != ra.shape:
        sky = A[8:8 + ra.shape[0], 8:8 + ra.shape[1]]
    presence = mean_abs(ra, sky) if sky.size == ra.size else 0.0

    fam = family(sid)
    pmin = PRESENCE_MIN.get(fam, PRESENCE_MIN["default"])
    nmax = NOISE_MAX.get(fam, NOISE_MAX["default"])

    mean_g = float(ra[:, :, 1].mean())
    mean_b = float(ra[:, :, 2].mean())
    mean_r = float(ra[:, :, 0].mean())

    mask = subject_mask(ra, fam)
    yspan, xspan, nsub = mask_spans(mask)
    sub_frac = float(nsub) / float(mask.size) if mask.size else 0.0

    status = "PASS"
    detail = ""
    if presence < pmin:
        status = "FAIL_PRESENCE"
        detail = "presence=%.2f < min=%.2f (empty sky?)" % (presence, pmin)
    elif noise > nmax:
        status = "FAIL_AB"
        detail = "A/B noise=%.3f > max=%.3f" % (noise, nmax)
    elif fam in ("slime", "magma") and mean_b > mean_g + 25 and mean_b > mean_r + 25:
        status = "FAIL_SKY_DOMINANT"
        detail = "roi rgb mean=(%.1f,%.1f,%.1f) sky-like" % (mean_r, mean_g, mean_b)
    elif fam == "dragon" and sub_frac < DRAGON_SUBJECT_MIN_FRAC:
        status = "FAIL_DRAGON_EMPTY"
        detail = "subject_frac=%.5f < min=%.5f (no body/rays)" % (
            sub_frac, DRAGON_SUBJECT_MIN_FRAC)
    elif sid == "dragon_death_190" and sub_frac < DRAGON_190_SUBJECT_MIN_FRAC:
        status = "FAIL_DRAGON_190_EMPTY"
        detail = "190 subject_frac=%.5f near-empty" % sub_frac

    ok = status == "PASS"
    return {
        "id": sid,
        "status": status,
        "ok": ok,
        "presence": presence,
        "noise": noise,
        "pmin": pmin,
        "nmax": nmax,
        "roi": list(rect),
        "roi_mean_rgb": [mean_r, mean_g, mean_b],
        "yspan": yspan,
        "xspan": xspan,
        "subject_n": nsub,
        "subject_frac": sub_frac,
        "meta": os.path.isfile(meta),
        "detail": detail,
        "bytes_a": os.path.getsize(ja),
        "bytes_b": os.path.getsize(jb),
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
    return states


def load_roi(goldens, sid):
    path = os.path.join(goldens, "%s_a.png" % sid)
    if not os.path.isfile(path):
        return None
    A = load_rgb(path)
    return crop(A, roi_rect(sid))


def inter_state_gates(goldens, states, results_by_id):
    """Cross-state geometry gates that catch server-only pins."""
    extra = []
    present = set(states)

    def fail(tag, detail, ids):
        extra.append({"id": tag, "status": "FAIL_INTER", "ok": False,
                      "detail": detail, "ids": ids})

    def ok_gate(tag, detail, metrics=None):
        row = {"id": tag, "status": "PASS_INTER", "ok": True, "detail": detail}
        if metrics:
            row.update(metrics)
        extra.append(row)

    # --- size distinctness (slime / magma) ---
    for fam, prefix in (("slime", "slime_size"), ("magma", "magma_size")):
        sids = ["%s1" % prefix, "%s2" % prefix, "%s4" % prefix]
        if not all(s in present for s in sids):
            continue
        if not all(results_by_id.get(s, {}).get("ok") for s in sids):
            continue
        rois = {s: load_roi(goldens, s) for s in sids}
        if any(v is None for v in rois.values()):
            continue
        pairs = [("1", "2"), ("2", "4"), ("1", "4")]
        bad = []
        metrics = {}
        for a, b in pairs:
            sa, sb = prefix + a, prefix + b
            d = mean_abs(rois[sa], rois[sb])
            metrics["%s_vs_%s" % (a, b)] = d
            if d < SIZE_PAIR_MIN:
                bad.append("%s vs %s mean_abs=%.3f < %.2f" % (sa, sb, d, SIZE_PAIR_MIN))
        if bad:
            fail("%s_sizes_distinct" % fam, "; ".join(bad), sids)
        else:
            ok_gate("%s_sizes_distinct" % fam, "size pairs distinct", metrics)

    # --- squish vs size2: material RGB + taller/thinner geometry ---
    for fam, size2, squish in (
        ("slime", "slime_size2", "slime_squish"),
        ("magma", "magma_size2", "magma_squish"),
    ):
        if size2 not in present or squish not in present:
            continue
        p2 = os.path.join(goldens, "%s_a.png" % size2)
        pq = os.path.join(goldens, "%s_a.png" % squish)
        if not (os.path.isfile(p2) and os.path.isfile(pq)):
            fail("%s_squish_vs_size2" % fam, "missing frames", [size2, squish])
            continue
        try:
            A2 = load_rgb(p2)
            Aq = load_rgb(pq)
        except Exception as ex:
            fail("%s_squish_vs_size2" % fam, "bad png: %s" % ex, [size2, squish])
            continue
        # Expanded ROI mean-abs (includes tall squish into former sky).
        rect = roi_rect(size2)
        d = mean_abs(crop(A2, rect), crop(Aq, rect))
        # Center-band body mask on full frame for vertical/horizontal geometry.
        m2 = body_mask_fullframe(A2, fam)
        mq = body_mask_fullframe(Aq, fam)
        y2, x2, n2 = mask_spans(m2)
        yq, xq, nq = mask_spans(mq)
        metrics = {
            "mean_abs": d,
            "size2_yspan": y2, "size2_xspan": x2, "size2_n": n2,
            "squish_yspan": yq, "squish_xspan": xq, "squish_n": nq,
        }
        reasons = []
        if n2 < 50 or nq < 50:
            reasons.append("body mask too sparse (size2_n=%d squish_n=%d)" % (n2, nq))
        if d < SQUISH_VS_SIZE2_MIN:
            reasons.append("mean_abs=%.3f < min=%.2f (pin inert?)" % (
                d, SQUISH_VS_SIZE2_MIN))
        yratio = xratio = aspect_gain = None
        if y2 > 0 and yq > 0:
            yratio = float(yq) / float(y2)
            metrics["yspan_ratio"] = yratio
        if x2 > 0 and xq > 0:
            xratio = float(xq) / float(x2)
            metrics["xspan_ratio"] = xratio
        if y2 > 0 and x2 > 0 and yq > 0 and xq > 0:
            a2 = float(y2) / float(x2)
            aq = float(yq) / float(xq)
            aspect_gain = aq / a2 if a2 > 0 else 0.0
            metrics["aspect_size2"] = a2
            metrics["aspect_squish"] = aq
            metrics["aspect_gain"] = aspect_gain
        # Expected geometry for sq=1: taller and/or thinner (aspect gain).
        # Pass if (taller AND thinner) OR strong aspect gain with material RGB.
        geom_ok = False
        if yratio is not None and xratio is not None:
            if (yratio >= SQUISH_YSPAN_RATIO_MIN
                    and xratio <= SQUISH_XSPAN_RATIO_MAX):
                geom_ok = True
            elif (aspect_gain is not None
                  and aspect_gain >= SQUISH_ASPECT_GAIN_MIN
                  and xratio <= SQUISH_XSPAN_RATIO_MAX):
                geom_ok = True
            elif (aspect_gain is not None
                  and aspect_gain >= SQUISH_ASPECT_GAIN_MIN
                  and yratio >= SQUISH_YSPAN_RATIO_MIN):
                geom_ok = True
        if not geom_ok:
            reasons.append(
                "geometry not taller/thinner (yspan_ratio=%s xspan_ratio=%s "
                "aspect_gain=%s)" % (
                    ("%.3f" % yratio) if yratio is not None else "n/a",
                    ("%.3f" % xratio) if xratio is not None else "n/a",
                    ("%.3f" % aspect_gain) if aspect_gain is not None else "n/a"))
        if reasons:
            fail("%s_squish_vs_size2" % fam, "; ".join(reasons), [size2, squish])
            extra[-1].update(metrics)
        else:
            ok_gate("%s_squish_vs_size2" % fam, "squish differs from size2", metrics)

    # --- dragon stages pairwise distinct + retain subject ---
    d_ids = ["dragon_death_50", "dragon_death_100", "dragon_death_190"]
    if all(s in present for s in d_ids):
        rois = {s: load_roi(goldens, s) for s in d_ids}
        if all(v is not None for v in rois.values()):
            pairs = [
                ("dragon_death_50", "dragon_death_100"),
                ("dragon_death_100", "dragon_death_190"),
                ("dragon_death_50", "dragon_death_190"),
            ]
            bad = []
            metrics = {}
            for a, b in pairs:
                d = mean_abs(rois[a], rois[b])
                key = "%s_vs_%s" % (a.replace("dragon_death_", ""),
                                    b.replace("dragon_death_", ""))
                metrics[key] = d
                if d < DRAGON_STAGE_PAIR_MIN:
                    bad.append("%s mean_abs=%.3f < %.2f" % (key, d, DRAGON_STAGE_PAIR_MIN))
            # body/ray presence per stage
            for sid in d_ids:
                m = subject_mask(rois[sid], "dragon")
                frac = float(m.sum()) / float(m.size)
                metrics["%s_subject_frac" % sid] = frac
                floor = (DRAGON_190_SUBJECT_MIN_FRAC if sid.endswith("190")
                         else DRAGON_SUBJECT_MIN_FRAC)
                if frac < floor:
                    bad.append("%s subject_frac=%.5f < %.5f" % (sid, frac, floor))
            if bad:
                fail("dragon_stages_distinct", "; ".join(bad), d_ids)
                extra[-1].update(metrics)
            else:
                ok_gate("dragon_stages_distinct", "stages pairwise distinct + body/rays",
                        metrics)

    return extra


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--states", nargs="*", default=None)
    ap.add_argument("--json-out", default=None)
    ap.add_argument("--require-meta", action="store_true")
    ap.add_argument("--skip-inter", action="store_true",
                    help="skip inter-state geometry gates (debug only)")
    args = ap.parse_args()

    if not os.path.isdir(args.goldens):
        print("FAIL: goldens dir missing: %s" % args.goldens, file=sys.stderr)
        return 2

    default_req = [
        "slime_size1", "slime_size2", "slime_size4", "slime_squish",
        "magma_size1", "magma_size2", "magma_size4", "magma_squish",
        "dragon_death_50", "dragon_death_100", "dragon_death_190",
        "dig_stone", "dig_grass",
        "fireball_small", "fireball_dragon", "xp_orb",
    ]
    states = args.states
    if not states:
        states = list_states(args.goldens)
    if not states:
        states = default_req

    results = []
    failed = 0
    for sid in states:
        r = validate_state(args.goldens, sid)
        if args.require_meta and r.get("ok") and not r.get("meta"):
            r["ok"] = False
            r["status"] = "MISSING_META"
            r["detail"] = "meta json missing"
        results.append(r)
        tag = "OK " if r["ok"] else "BAD"
        extra = r.get("detail") or ""
        print("%s  %s  presence=%.2f (min %.1f)  noise=%.3f (max %.1f)  "
              "yspan=%s xspan=%s sub=%.4f  %s  %s" % (
            tag, sid,
            r.get("presence", float("nan")),
            r.get("pmin", 0),
            r.get("noise", float("nan")),
            r.get("nmax", 0),
            r.get("yspan", "-"),
            r.get("xspan", "-"),
            r.get("subject_frac", float("nan")),
            r["status"], extra))
        if not r["ok"]:
            failed += 1

    by_id = {r["id"]: r for r in results}
    inter = []
    if not args.skip_inter:
        # Inter-state uses full default set when present on disk, not only --states,
        # so a selective recapture still checks squish vs preserved size2.
        disk_states = list_states(args.goldens) or default_req
        # Merge per-state results for disk states we did not re-validate above.
        for sid in disk_states:
            if sid not in by_id:
                by_id[sid] = validate_state(args.goldens, sid)
        inter = inter_state_gates(args.goldens, disk_states, by_id)
        for r in inter:
            tag = "OK " if r["ok"] else "BAD"
            print("%s  %s  %s  %s" % (tag, r["id"], r["status"], r.get("detail") or ""))
            if not r["ok"]:
                failed += 1
            results.append(r)

    report = {
        "results": results,
        "inter_state": inter,
        "failed": failed,
        "total": len(results),
    }
    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump(report, f, indent=2)
        print("report: %s" % args.json_out)
    print("summary: failed=%d/%d" % (failed, len(results)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
