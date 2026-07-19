"""Re-run the pixel gate offline over a finished replay's stored frames.

A gated replay (replay_tape.py) already saves every craster frame to
craster_frames.npy; the oracle goldens live next to the tape. Tuning a
classifier in pixel_gate.py therefore never needs a re-replay - this script
re-diffs the stored pairs and rewrites the .gate.json baseline in minutes.

Usage:
  uv run --no-project --with numpy,scipy,pillow python regate.py \
      --tape ../tapes/<tape>.jsonl --npy out/<run>/craster_frames.npy \
      [--report report/<tape>.gate.json]
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

import pixel_gate as pg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tape", required=True)
    ap.add_argument("--npy", required=True)
    ap.add_argument("--report", default=None)
    ap.add_argument("--frame-every", type=int, default=2)
    args = ap.parse_args()

    gold_dir = args.tape.replace(".jsonl", "") + "_frames"
    if not os.path.isdir(gold_dir):
        sys.exit(f"golden frame dir not found: {gold_dir}")

    rows = []
    with open(args.tape) as fh:
        for line in fh:
            line = line.strip()
            if line:
                rows.append(json.loads(line))

    frames = np.load(args.npy, mmap_mode="r")
    n, h, w, _ = frames.shape
    per_tick = {}
    for i in range(n):
        t = i * args.frame_every
        gp = os.path.join(gold_dir, f"f_{t:06d}.png")
        if not os.path.exists(gp):
            continue
        o16 = np.asarray(Image.open(gp).convert("RGB"), dtype=np.int16)
        c16 = np.asarray(frames[i], dtype=np.int16)
        per_tick[t] = pg.gate_frame(o16, c16, w, h)
        if i % 500 == 0:
            print(f"[regate] {i}/{n}", flush=True)

    summary = pg.summarize(per_tick, transit=pg.transit_ticks(rows))
    for cls, s in sorted(summary["classes"].items()):
        print(f"[gate] class {cls:<12} frames {s['frames']:>5} "
              f"px {s['px']:>9} max_cluster {s['max_cluster']}")
    nf = len(summary["failed_frames"])
    if nf:
        worst = summary["failed_frames"][0]
        print(f"[gate] FAIL: {nf} frames; worst t={worst['tick']} "
              f"({worst['unexplained_px']} px)")
    else:
        print("[gate] PASS")
    out = args.report or (
        "report/tape_"
        + os.path.basename(args.tape).replace(".jsonl", "")
        + ".gate.json")
    with open(out, "w") as fh:
        json.dump(summary, fh, indent=1)
    print(f"[gate] baseline -> {out}")
    return 3 if nf else 0


if __name__ == "__main__":
    sys.exit(main())
