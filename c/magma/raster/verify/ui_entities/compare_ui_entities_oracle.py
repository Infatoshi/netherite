#!/usr/bin/env python3
"""Feature-specific ROI gate for ui_entities oracle goldens vs C frame_capture.

Hard gate: c_vs_j <= max(noise, NOISE_FLOOR) + MARGIN on the feature ROI.
Presence: Java A ROI mean abs vs blank corner must exceed PRESENCE_MIN.
No broad class budgets. Prints measured clusters on FAIL.
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
MARGIN = 0.5          # mean |RGB| noise-floor allowance
NOISE_FLOOR = 0.05    # if A/B identical, still allow tiny raster residual
PRESENCE_MIN = 2.0    # feature must move ROI away from empty sky corner
HARD_ALL = True       # all states hard-gated; fail open only if --info


def roi_rect(state_id):
    """Exclusive (x0,y0,x1,y1) feature ROI. Tuned for driver camera/subject."""
    if state_id.startswith("dragon_death"):
        # mid-frame dragon body / rays
        return (80, 40, W - 80, H - 80)
    if state_id.startswith("dig_"):
        # dig target block region, lower-center
        return (W // 2 - 120, H // 2 - 80, W // 2 + 120, H // 2 + 100)
    if state_id.startswith("fireball"):
        return (W // 2 - 100, H // 2 - 100, W // 2 + 100, H // 2 + 80)
    if state_id == "xp_orb":
        return (W // 2 - 60, H // 2 - 60, W // 2 + 60, H // 2 + 40)
    # slime / magma: subject in front of camera, mid-lower
    return (W // 2 - 140, H // 3, W // 2 + 140, H - 60)


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
    return a[y0:y1, x0:x1]


def mean_abs(a, b):
    if a.size == 0 or b.size == 0:
        return float("nan")
    return float(np.abs(a.astype(np.int16) - b.astype(np.int16)).mean())


def clusters(diff_mask, min_px=32):
    """Connected-component-ish clustering via coarse grid bins."""
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--goldens", required=True)
    ap.add_argument("--candidate", required=True,
                    help="path to entity_oracle_candidate binary")
    ap.add_argument("--c-out", default="/tmp/magma_ui_entities_c")
    ap.add_argument("--info", action="store_true",
                    help="report only; do not fail process")
    ap.add_argument("--states", nargs="*", default=None)
    args = ap.parse_args()

    man_path = os.path.join(args.goldens, "capture_manifest.json")
    if os.path.isfile(man_path):
        with open(man_path) as f:
            man = json.load(f)
        states = man.get("states", [])
    else:
        states = []
        for fn in sorted(os.listdir(args.goldens)):
            if fn.endswith("_a.png"):
                states.append(fn[:-6])
    if args.states:
        states = args.states

    os.makedirs(args.c_out, exist_ok=True)
    failed = 0
    results = []

    for sid in states:
        ja = os.path.join(args.goldens, "%s_a.png" % sid)
        jb = os.path.join(args.goldens, "%s_b.png" % sid)
        meta = os.path.join(args.goldens, "meta", "%s.json" % sid)
        if not (os.path.isfile(ja) and os.path.isfile(jb) and os.path.isfile(meta)):
            print("SKIP %s (missing golden/meta)" % sid)
            failed += 1
            continue
        c_ppm = os.path.join(args.c_out, "%s.ppm" % sid)
        cmd = [args.candidate, "--state", sid, "--meta", meta, "--ppm", c_ppm,
               "--w", str(W), "--h", str(H)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.isfile(c_ppm):
            print("FAIL %s candidate rc=%d\n%s\n%s" % (
                sid, r.returncode, r.stdout, r.stderr))
            failed += 1
            results.append({"id": sid, "status": "CANDIDATE_FAIL"})
            continue

        A = load_rgb(ja)
        B = load_rgb(jb)
        C = load_ppm(c_ppm)
        rect = roi_rect(sid)
        ra, rb, rc = crop(A, rect), crop(B, rect), crop(C, rect)
        noise = mean_abs(ra, rb)
        c_vs_j = mean_abs(rc, ra)
        # presence: ROI vs top-left sky patch of same size
        h = rect[3] - rect[1]
        w = rect[2] - rect[0]
        sky = A[8:8 + max(1, h), 8:8 + max(1, w)]
        if sky.shape != ra.shape:
            sky = A[8:8 + ra.shape[0], 8:8 + ra.shape[1]]
        presence = mean_abs(ra, sky) if sky.size == ra.size else 99.0
        budget = max(noise, NOISE_FLOOR) + MARGIN
        ok_presence = presence >= PRESENCE_MIN
        ok_match = c_vs_j <= budget
        status = "PASS" if (ok_presence and ok_match) else "FAIL"
        if not ok_presence:
            status = "FAIL_PRESENCE"
        elif not ok_match:
            status = "FAIL_MATCH"

        diff = np.abs(rc.astype(np.int16) - ra.astype(np.int16)).max(axis=2) > 8
        cls = clusters(diff)
        print("%s  noise=%.3f c_vs_j=%.3f budget=%.3f presence=%.2f  %s  roi=%s" % (
            sid, noise, c_vs_j, budget, presence, status, rect))
        if status != "PASS" and cls:
            print("  clusters: %s" % cls[:8])
        if status != "PASS":
            failed += 1
        results.append({
            "id": sid, "status": status, "noise": noise, "c_vs_j": c_vs_j,
            "budget": budget, "presence": presence, "roi": list(rect),
            "clusters": cls[:8],
        })

    out_json = os.path.join(args.c_out, "gate_report.json")
    with open(out_json, "w") as f:
        json.dump({"results": results, "failed": failed}, f, indent=2)
    print("report: %s  failed=%d/%d" % (out_json, failed, len(states)))
    if failed and not args.info:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
