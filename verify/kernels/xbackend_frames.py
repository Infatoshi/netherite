#!/usr/bin/env python3
"""Cross-backend numeric gate: GPU window-path frames must match the CPU
rasterizer within the XB thresholds, on whichever GPU backend this machine has.

The CPU rasterizer is the shared reference: anvil proves cpu==cuda, the
macbook proves cpu==metal, so a green run on both machines proves the CUDA and
Metal kernels compute the same math (device indexing may differ; results may
not). Thresholds are the window_battery XB constants - identical on both
platforms on purpose.

Scenes exercise the window-path kernel set (cr_transform_kernel,
cr_raster_bbox_kernel, cr_raster_tiled_kernel, k_sky): plains spawn, the
seed-1000 jungle interior (heavy cutout load), and the rotating mob demo
(entities + camera motion). cr_gather_kernel and the capture path are covered
by the replay pins, which already run under --metal (mac) and cuda (anvil).

Usage:
    uv run --no-project --with numpy python verify/kernels/xbackend_frames.py \
        --game magma/magma_game_cuda --backend cuda
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

# window_battery XB thresholds (scripts/window_battery.py) - keep in sync.
XB_MAD_MAX = 0.001
XB_NDIFF_MAX = 100
XB_MAXCH_MAX = 3
XB_MAXCH_SPARSE_NDIFF = 15

# set_extra: registry key -> value (applied as repeated --set after fixed args).
SCENES = [
    ("still_spawn", {"still": "1"}, ["--seed", "0", "--view-distance", "8", "--frames", "30"]),
    ("jungle", {"still": "1"}, ["--seed", "1000", "--view-distance", "8", "--frames", "30"]),
    (
        "mob_yaw",
        {"mob_demo": "1", "still": "1", "yawrate": "3"},
        ["--seed", "0", "--view-distance", "8", "--frames", "60"],
    ),
]


def read_ppm(path):
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        sys.exit(f"FAIL: {path} is not a binary PPM")
    # header is ASCII "P6 <w> <h> <maxval>" then ONE whitespace byte, then
    # exactly w*h*3 binary bytes; slice from the tail so binary bytes that
    # happen to be whitespace cannot corrupt the parse.
    w, h = (int(t) for t in data[2:64].split()[:2])
    raw = data[-w * h * 3 :]
    return np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 3)


def run_dump(game, backend, set_extra, args, outdir):
    # Platform env only; drive/scenario knobs go through --set on argv.
    env = dict(os.environ)
    outdir.mkdir(parents=True, exist_ok=True)
    cmd = [str(game), "--backend", backend, *args]
    sets = [f"dump_dir={outdir}"]
    for k, v in sorted(set_extra.items()):
        sets.append(f"{k}={v}")
    for kv in sets:
        cmd.extend(["--set", kv])
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=900)
    frames = sorted(outdir.glob("frame_*.ppm"))
    if r.returncode != 0 or not frames:
        sys.exit(
            f"FAIL: dump run {' '.join(cmd)} rc={r.returncode} frames={len(frames)}\n"
            + r.stderr[-2000:]
        )
    return frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", required=True)
    ap.add_argument("--backend", required=True, choices=["cuda", "metal"])
    ap.add_argument("--keep", action="store_true", help="keep dump dirs")
    a = ap.parse_args()
    game = Path(a.game).resolve()
    if not game.exists():
        sys.exit(f"FAIL: binary not found: {game}")

    work = Path(tempfile.mkdtemp(prefix="xbackend_"))
    failed = False
    for name, set_extra, args in SCENES:
        cpu = run_dump(game, "cpu", set_extra, args, work / name / "cpu")
        gpu = run_dump(game, a.backend, set_extra, args, work / name / a.backend)
        if len(cpu) != len(gpu):
            print(f"FAIL {name}: frame count cpu={len(cpu)} {a.backend}={len(gpu)}")
            failed = True
            continue
        worst = {"mad": -1.0, "ndiff": 0, "maxch": 0, "hot": 0, "frame": -1}
        scene_ok = True
        for fc, fg in zip(cpu, gpu):
            ic = read_ppm(fc).astype(np.int16)
            ig = read_ppm(fg).astype(np.int16)
            d = np.abs(ic - ig)
            mad = float(d.mean())
            ndiff = int((d.max(axis=2) > 0).sum())
            maxch = int(d.max())
            # Sparse hot-pixel exemption counts only pixels ABOVE the maxch
            # limit (the battery counts every nonzero pixel, which conflates
            # 1-delta edge fringe with genuine hot pixels). A kernel-math
            # divergence produces thousands of hot pixels, not <= 15.
            hot = int((d.max(axis=2) > XB_MAXCH_MAX).sum())
            maxch_fail = maxch > XB_MAXCH_MAX and hot > XB_MAXCH_SPARSE_NDIFF
            frame_ok = mad <= XB_MAD_MAX and ndiff <= XB_NDIFF_MAX and not maxch_fail
            scene_ok &= frame_ok
            if mad > worst["mad"] or not frame_ok:
                worst = {"mad": mad, "ndiff": ndiff, "maxch": maxch, "hot": hot,
                         "frame": int(fc.stem.split("_")[1])}
        ok = scene_ok
        tag = "PASS" if ok else "FAIL"
        print(
            f"{tag} {name}: frames={len(cpu)} worst mad={worst['mad']:.6f} "
            f"ndiff={worst['ndiff']} maxch={worst['maxch']} hot={worst['hot']} "
            f"@f{worst['frame']} (MAD<={XB_MAD_MAX} ndiff<={XB_NDIFF_MAX} "
            f"maxch<={XB_MAXCH_MAX} hot<={XB_MAXCH_SPARSE_NDIFF})"
        )
        failed |= not ok
    if not a.keep:
        shutil.rmtree(work, ignore_errors=True)
    else:
        print(f"dumps kept at {work}")
    if failed:
        sys.exit(1)
    print(f"ALL PASS (cpu vs {a.backend})")


if __name__ == "__main__":
    main()
