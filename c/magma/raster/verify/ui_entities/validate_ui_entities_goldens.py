#!/usr/bin/env python3
"""Validate Java A/B goldens: non-empty presence + A/B stability. No C path.

Rejects empty-sky frames. Does not invent pixels. Exit 0 only if every requested
state passes. Use before commit and after capture_ui_entities.sh.
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


def roi_rect(state_id):
    if state_id.startswith("dragon_death"):
        return (80, 40, W - 80, H - 80)
    if state_id.startswith("dig_"):
        return (W // 2 - 120, H // 2 - 80, W // 2 + 120, H // 2 + 100)
    if state_id.startswith("fireball"):
        return (W // 2 - 100, H // 2 - 100, W // 2 + 100, H // 2 + 80)
    if state_id == "xp_orb":
        return (W // 2 - 60, H // 2 - 60, W // 2 + 60, H // 2 + 40)
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


def validate_state(goldens, sid):
    ja = os.path.join(goldens, "%s_a.png" % sid)
    jb = os.path.join(goldens, "%s_b.png" % sid)
    meta = os.path.join(goldens, "meta", "%s.json" % sid)
    if not (os.path.isfile(ja) and os.path.isfile(jb)):
        return {"id": sid, "status": "MISSING", "ok": False,
                "detail": "missing a/b png"}
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

    # Green/magma tint hint for cube families: ROI should not be pure blue sky.
    mean_g = float(ra[:, :, 1].mean())
    mean_b = float(ra[:, :, 2].mean())
    mean_r = float(ra[:, :, 0].mean())

    status = "PASS"
    detail = ""
    if presence < pmin:
        status = "FAIL_PRESENCE"
        detail = "presence=%.2f < min=%.2f (empty sky?)" % (presence, pmin)
    elif noise > nmax:
        status = "FAIL_AB"
        detail = "A/B noise=%.3f > max=%.3f" % (noise, nmax)
    elif fam in ("slime", "magma") and mean_b > mean_g + 25 and mean_b > mean_r + 25:
        # ROI dominated by sky blue — subject missed the frame.
        status = "FAIL_SKY_DOMINANT"
        detail = "roi rgb mean=(%.1f,%.1f,%.1f) sky-like" % (mean_r, mean_g, mean_b)

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--states", nargs="*", default=None)
    ap.add_argument("--json-out", default=None)
    ap.add_argument("--require-meta", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(args.goldens):
        print("FAIL: goldens dir missing: %s" % args.goldens, file=sys.stderr)
        return 2

    # Default required set when no states listed and no manifest yet.
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
        print("%s  %s  presence=%.2f (min %.1f)  noise=%.3f (max %.1f)  %s  %s" % (
            tag, sid,
            r.get("presence", float("nan")),
            r.get("pmin", 0),
            r.get("noise", float("nan")),
            r.get("nmax", 0),
            r["status"], extra))
        if not r["ok"]:
            failed += 1

    report = {"results": results, "failed": failed, "total": len(states)}
    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump(report, f, indent=2)
        print("report: %s" % args.json_out)
    print("summary: failed=%d/%d" % (failed, len(states)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
