"""Render N live random-action agent rollouts for the zoom mosaic.

Each rollout is its own world (seed = env index): a spawned agent takes a
smooth random walk (wander + look drift + occasional jumps/digs) through
magma's rl mode while the exact renderer captures square frames. Frames
are stored as multi-resolution .npy clips (96/48/24 px) so the composer
can animate every tile at whatever scale it appears; the innermost block
keeps native ppms for full-res pasting.

Run: cd netherite && uv run --no-project --with numpy,pillow python \
       scripts/zoom_rollouts.py [--n 8192] [--jobs 30]
"""
import argparse
import json
import os
import shutil
import subprocess
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAGMA = os.path.join(ROOT, "magma")
# ZOOM_GAME/ZOOM_BACKEND select the renderer (e.g. magma_game_metal + metal
# on the MacBook); default is the CPU binary as shipped in the 8192 demo.
GAME = os.environ.get("ZOOM_GAME", os.path.join(MAGMA, "magma_game"))
BACKEND = os.environ.get("ZOOM_BACKEND", "")
CONF = os.environ.get("ZOOM_CONF", os.path.expanduser("~/dev/nw/.tmp/zoom_video.conf"))
WORK = os.environ.get("ZOOM_WORK", os.path.expanduser("~/dev/nw/.tmp/zoomrolls"))
TILE = 192                 # native square render size
FRAMES = 348
MIPS = (96, 48, 24)
KEEP_PPM = 8               # envs 0..N with chebyshev ring logic live in composer;
                           # composer asks for native ppms via --keep list file


def rand_actions(seed, n):
    """Smooth wander: correlated yaw drift, forward bias, bursts of digging."""
    rng = np.random.default_rng(50_000 + seed)
    acts = []
    yaw_v = 0.0
    pitch = 0.0
    dig = 0
    for t in range(n):
        yaw_v = 0.9 * yaw_v + rng.normal(0, 1.2)
        dpitch = np.clip(rng.normal(0, 0.8) - 0.05 * pitch, -4, 4)
        pitch = np.clip(pitch + dpitch, -35, 45)
        a = {"forward": 1.0 if rng.random() > 0.15 else 0.0,
             "dyaw": float(np.clip(yaw_v, -8, 8)),
             "dpitch": float(dpitch)}
        if rng.random() < 0.04:
            a["jump"] = 1
        # no digging: diggers end the clip inside dark tunnels, which is
        # exactly when the full mosaic is on screen
        acts.append(a)
    return acts


def render_env(job):
    seed, keep_ppm = job
    outdir = os.path.join(WORK, f"e{seed}")
    marker = os.path.join(outdir, "DONE")
    if os.path.exists(marker):
        return seed
    os.makedirs(outdir, exist_ok=True)
    fdir = os.path.join(outdir, "ppm")
    shutil.rmtree(fdir, ignore_errors=True)
    os.makedirs(fdir)
    env = dict(os.environ)
    argv = [GAME, "--rl", "--render", "off", "--pace", "unlimited",
            "--conf", CONF,
            "--seed", str(seed), "--mobs", "off",
            "--width", str(TILE), "--height", str(TILE),
            "--frames-out", fdir, "--frame-offset", "0", "--frame-every", "1",
            "--set", "hide_gui=1"]
    if BACKEND:
        argv += ["--backend", BACKEND]
    proc = subprocess.Popen(
        argv,
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, text=True, bufsize=1, cwd=MAGMA, env=env)
    for a in rand_actions(seed, FRAMES + 4):
        proc.stdin.write(json.dumps(a) + "\n")
    proc.stdin.close()
    proc.wait(timeout=900)

    files = sorted(f for f in os.listdir(fdir) if f.endswith(".ppm"))
    # skip the first frames (spawn/chunk pop-in), keep FRAMES
    files = files[len(files) - FRAMES:] if len(files) >= FRAMES else files
    if len(files) < FRAMES:
        raise RuntimeError(f"env {seed}: only {len(files)} frames")
    clips = {m: np.zeros((FRAMES, m, m, 3), np.uint8) for m in MIPS}
    for i, fn in enumerate(files):
        im = Image.open(os.path.join(fdir, fn))
        for m in MIPS:
            clips[m][i] = np.asarray(im.resize((m, m), Image.LANCZOS))
    for m in MIPS:
        np.save(os.path.join(outdir, f"clip{m}.npy"), clips[m])
    if keep_ppm:
        keep = os.path.join(outdir, "native")
        shutil.rmtree(keep, ignore_errors=True)
        os.rename(fdir, keep)
        with open(os.path.join(keep, "FIRST.txt"), "w") as f:
            f.write(files[0])
    else:
        shutil.rmtree(fdir)
    open(marker, "w").close()
    return seed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=8192)
    ap.add_argument("--jobs", type=int, default=30)
    ap.add_argument("--keep-native", type=int, default=256,
                    help="envs 0..K-1 keep native ppms for the center block")
    args = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    jobs = [(s, s < args.keep_native) for s in range(args.n)]
    done = 0
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        for _ in ex.map(render_env, jobs, chunksize=8):
            done += 1
            if done % 256 == 0:
                print(f"{done}/{args.n} rollouts rendered", flush=True)
    print(f"all {args.n} rollouts in {WORK}")


if __name__ == "__main__":
    main()
