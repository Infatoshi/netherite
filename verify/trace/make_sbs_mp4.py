#!/usr/bin/env python3
"""Side-by-side oracle|magma MP4 from a replayed tape.

Usage:
  uv run --no-project --with numpy --with pillow python make_sbs_mp4.py \
      ../tapes/<TAPE>.jsonl [--t0 N] [--t1 N] [--fps 20] [--out X.mp4]

Reads the oracle frames (../tapes/<name>_frames/f_%06d.png) and the magma
replay frames (out/tape_<name>/magma_frames.npy + .ticks.npy), pairs them by
tick, hstacks, and pipes raw RGB into ffmpeg. Oracle records every 2nd tick by
default; whatever ticks exist in BOTH sides are used.
"""
import argparse
import os
import subprocess
import sys

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tape")
    ap.add_argument("--t0", type=int, default=None)
    ap.add_argument("--t1", type=int, default=None)
    ap.add_argument("--fps", type=int, default=20)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    name = os.path.splitext(os.path.basename(a.tape))[0]
    here = os.path.dirname(os.path.abspath(__file__))
    odir = os.path.join(os.path.dirname(os.path.abspath(a.tape)),
                        name + "_frames")
    cdir = os.path.join(here, "out", "tape_" + name)
    fr = np.load(os.path.join(cdir, "magma_frames.npy"))
    tk = np.load(os.path.join(cdir, "magma_frames.ticks.npy"))
    out = a.out or os.path.join(here, "out", name + "_sbs.mp4")

    pairs = []
    for i, t in enumerate(tk):
        t = int(t)
        if a.t0 is not None and t < a.t0:
            continue
        if a.t1 is not None and t > a.t1:
            continue
        p = os.path.join(odir, "f_%06d.png" % t)
        if os.path.exists(p):
            pairs.append((t, i, p))
    if not pairs:
        sys.exit("no overlapping oracle/magma ticks in range")

    h, w = fr.shape[1], fr.shape[2]
    ff = subprocess.Popen(
        ["ffmpeg", "-y", "-loglevel", "error",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{2*w}x{h}",
         "-r", str(a.fps), "-i", "-",
         "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", out],
        stdin=subprocess.PIPE)
    for t, i, p in pairs:
        o = np.asarray(Image.open(p).convert("RGB"))
        if o.shape[:2] != (h, w):
            o = np.asarray(Image.open(p).convert("RGB").resize((w, h)))
        c = fr[i][..., :3]
        ff.stdin.write(np.concatenate([o, c], axis=1).tobytes())
    ff.stdin.close()
    ff.wait()
    print("wrote", out, f"({len(pairs)} frames, {2*w}x{h} @ {a.fps}fps)")


if __name__ == "__main__":
    main()
