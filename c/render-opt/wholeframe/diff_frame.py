#!/usr/bin/env python3
"""Whole-frame pixel-diff harness for render-opt.

This is the generalized, reusable version of the two ad-hoc spike diffs
(dropin/diff_frames.py, dropin/lightmap/diff_lm.py). It is the FINAL-ARBITER
half of the render-opt completion proof (README.md: "Final arbiter = pixel
diff"): given a vanilla baseline frame and one or more comparison frames
(native-kernels and/or sabotage), it reports per-channel max/mean/RMSE abs
error and the fraction of differing pixels, both whole-frame and over the
auto-detected game-window crop (the constant black x11grab border dilutes
whole-frame metrics, so the crop is the honest number).

It does NOT capture frames (anvil is headless and the live game is off-limits
to this task). It consumes already-captured PNGs. The capture step (routing the
verified native kernels into MC via the combined drop-in, then x11grab on :1)
is documented in README.md and must be run by an agent with game access.

Usage:
  uv run --no-project python diff_frame.py BASELINE CMP [CMP ...] [options]

Options:
  --crop auto            auto-detect the game window (default; union of
                         non-black bounding boxes across all frames)
  --crop R0:R1,C0:C1     explicit crop (rows R0..R1, cols C0..C1)
  --crop none            whole frame only
  --thr N                "differing" threshold: |delta| > N counts (default 0)
  --black N              black-border threshold for auto crop (default 8)
  --out DIR              write an amplified diff heatmap PNG per comparison
  --json                 emit machine-readable JSON instead of the table

Exit code is 0 always (this is a measurement tool, not a PASS/FAIL gate; the
interpretation - native ~ noise floor, sabotage >> native - lives in README).
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def auto_crop(frames, black):
    """Union of the non-black bounding boxes across all frames.

    The x11grab desktop has a constant pure-black border around the centered MC
    window. Any real content (even the near-black sabotage world) is > black in
    at least one channel somewhere, so the union bbox tracks the live window
    regardless of which mode is darkest."""
    h, w = frames[0].shape[:2]
    r0, r1, c0, c1 = h, 0, w, 0
    found = False
    for f in frames:
        mask = f.max(axis=2) > black
        rows = np.any(mask, axis=1)
        cols = np.any(mask, axis=0)
        if not rows.any():
            continue
        found = True
        rr = np.where(rows)[0]
        cc = np.where(cols)[0]
        r0, r1 = min(r0, int(rr[0])), max(r1, int(rr[-1]) + 1)
        c0, c1 = min(c0, int(cc[0])), max(c1, int(cc[-1]) + 1)
    if not found:
        return (0, h, 0, w)
    return (r0, r1, c0, c1)


def stats(a, b, thr):
    d = np.abs(a - b)
    per_pixel = d.max(axis=2)
    total = int(per_pixel.size)
    differ = int((per_pixel > thr).sum())
    return {
        "max_abs_per_channel": int(d.max()),
        "mean_abs": float(d.mean()),
        "rmse": float(np.sqrt((d.astype(np.float64) ** 2).mean())),
        "differing_pixels": differ,
        "total_pixels": total,
        "pct_differing": 100.0 * differ / total,
    }


def heatmap(a, b, path):
    d = np.abs(a - b).max(axis=2).astype(np.float64)
    mx = d.max()
    scaled = (d / mx * 255.0).astype(np.uint8) if mx > 0 else d.astype(np.uint8)
    Image.fromarray(scaled).save(path)


def parse_crop(arg, frames, black):
    if arg == "none":
        h, w = frames[0].shape[:2]
        return None
    if arg == "auto":
        return auto_crop(frames, black)
    try:
        rs, cs = arg.split(",")
        r0, r1 = (int(x) for x in rs.split(":"))
        c0, c1 = (int(x) for x in cs.split(":"))
        return (r0, r1, c0, c1)
    except Exception:
        raise SystemExit(f"bad --crop {arg!r}; use auto | none | R0:R1,C0:C1")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline")
    ap.add_argument("compares", nargs="+")
    ap.add_argument("--crop", default="auto")
    ap.add_argument("--thr", type=int, default=0)
    ap.add_argument("--black", type=int, default=8)
    ap.add_argument("--out", default=None)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    base = load(args.baseline)
    comps = []
    for p in args.compares:
        c = load(p)
        if c.shape != base.shape:
            raise SystemExit(f"shape {c.shape} of {p} != baseline {base.shape}")
        comps.append((p, c))

    crop = parse_crop(args.crop, [base] + [c for _, c in comps], args.black)
    if args.out:
        os.makedirs(args.out, exist_ok=True)

    results = {
        "baseline": args.baseline,
        "shape": list(base.shape),
        "crop": list(crop) if crop else None,
        "thr": args.thr,
        "comparisons": [],
    }
    for p, c in comps:
        entry = {"frame": p, "whole": stats(base, c, args.thr)}
        if crop:
            r0, r1, c0, c1 = crop
            entry["crop"] = stats(base[r0:r1, c0:c1], c[r0:r1, c0:c1], args.thr)
        if args.out:
            hp = os.path.join(args.out, "diff_" + os.path.basename(p))
            heatmap(base, c, hp)
            entry["heatmap"] = hp
        results["comparisons"].append(entry)

    if args.json:
        print(json.dumps(results, indent=2))
        return

    print(f"baseline: {args.baseline}  shape={base.shape}")
    if crop:
        r0, r1, c0, c1 = crop
        print(f"crop (game window): rows {r0}:{r1} cols {c0}:{c1} "
              f"= {(r1 - r0) * (c1 - c0)} px  (whole frame = {base.shape[0] * base.shape[1]} px)")
    print(f"differing = |delta| > {args.thr}\n")
    for e in results["comparisons"]:
        print(f"# {e['frame']}")
        for scope in ("whole", "crop"):
            if scope not in e:
                continue
            s = e[scope]
            print(f"  {scope:5s}: max/ch={s['max_abs_per_channel']:3d}  "
                  f"mean={s['mean_abs']:7.3f}  rmse={s['rmse']:7.3f}  "
                  f"differ={s['differing_pixels']}/{s['total_pixels']} "
                  f"({s['pct_differing']:.2f}%)")
        if "heatmap" in e:
            print(f"  heatmap: {e['heatmap']}")
        print()


if __name__ == "__main__":
    main()
