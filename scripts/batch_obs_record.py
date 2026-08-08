"""Record a LIVE GPU batch's semantic-camera mosaic for the zoom video.

Runs VecBlaze with N = 90x80 = 7200 envs (9:8 grid of 16:9 obs tiles fills
2:1 exactly), steps it at repeat=1 (~1.5x real time at 30fps) with a gentle
correlated walker per env (forward bias, smooth yaw drift, pitch sprung
toward the horizon, occasional jumps), and colorizes every env's 64x36
camera per tick into one mosaic frame. Snapshot assignment is SHUFFLED so
repeated worlds do not form diagonal stripes, and a content report counts
near-constant (sky/ocean-stare) tiles at the end.

Output: ~/dev/nw/.tmp/batchobs/mosaic.npy [F, 2880, 5760, 3]
        + meta.json.

Run (GPU0): cd netherite && CUDA_VISIBLE_DEVICES=0 uv run --no-project \
    --with numpy,torch,pillow python scripts/batch_obs_record.py
"""
import json
import os
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
MAGMA = os.path.join(os.path.dirname(HERE), "c", "magma")
sys.path.insert(0, os.path.join(MAGMA, "rl", "blaze"))
sys.path.insert(0, os.path.join(MAGMA, "rl"))
from blaze import VecBlaze, CUDA_SO                     # noqa: E402
from make_videos import COLORS, CAM_W, CAM_H            # noqa: E402

OUT = os.path.expanduser("~/dev/nw/.tmp/batchobs")
GCOLS, GROWS = 90, 80
N = GCOLS * GROWS
FRAMES = 470
REPEAT = 1

PAL = np.full((4096, 3), (90, 90, 90), np.uint8)
for bid, col in COLORS.items():
    PAL[bid] = col


def colorize(cam, dep, edge):
    img = PAL[np.clip(cam, 0, 4095)].astype(np.float32)
    shade = np.clip(1.0 - dep.astype(np.float32) / 512.0, 0.0, 1.0)[..., None]
    np.multiply(img, shade, where=(cam != 0)[..., None], out=img)
    np.multiply(img, 0.55, where=edge[..., None], out=img)
    return img.astype(np.uint8)


class Walker:
    """Correlated random walk: smooth, camera stays near the horizon."""

    def __init__(self, n, seed=7):
        self.rng = np.random.default_rng(seed)
        self.n = n
        self.yaw_v = np.zeros(n, np.float64)
        self.pitch = self.rng.uniform(0, 15, n)

    def step(self):
        r = self.rng
        self.yaw_v = 0.92 * self.yaw_v + r.normal(0, 0.7, self.n)
        dyaw = np.clip(self.yaw_v, -5, 5)
        # spring the pitch toward 12 deg below horizon, small noise
        dpitch = np.clip(0.08 * (12 - self.pitch) + r.normal(0, 0.5, self.n),
                         -2.5, 2.5)
        self.pitch += dpitch
        a = np.zeros((self.n, 12), np.float64)
        a[:, 0] = (r.random(self.n) > 0.10).astype(np.float64)   # forward
        a[:, 2] = dyaw
        a[:, 3] = dpitch
        a[:, 4] = (r.random(self.n) < 0.03).astype(np.float64)   # jump
        a[:, 7] = 1.0                                            # sprint-ish
        return torch.from_numpy(a)


def main():
    os.makedirs(OUT, exist_ok=True)
    snaps_dir = os.path.join(MAGMA, "rl", "out", "snaps")
    # only land-locked spawns: ocean-facing worlds (s10/s16/s32/s33...)
    # measured 58-80% flat sky/sea panes; these five measure 0.8-6.7%
    GOOD = ("s2_", "s11_", "s29_", "s44_", "s46_")
    paths = [os.path.join(snaps_dir, f)
             for f in sorted(os.listdir(snaps_dir))
             if f.endswith("_t0.bsnp") and f.startswith(GOOD)]
    env = VecBlaze(N, device=0, so_path=CUDA_SO)
    env.load_snapshots(paths)
    rng = np.random.default_rng(3)
    env.assign([int(x) for x in rng.integers(0, len(paths), N)])
    env.reset()
    dev = torch.device("cuda:0")
    walker = Walker(N)

    mosaic = np.lib.format.open_memmap(
        os.path.join(OUT, "mosaic.npy"), mode="w+", dtype=np.uint8,
        shape=(FRAMES, GROWS * CAM_H, GCOLS * CAM_W, 3))
    for d in range(FRAMES):
        cam, dep, edge, *_ = env.step(walker.step().to(dev), repeat=REPEAT)
        c = cam.cpu().numpy().astype(np.int32).reshape(N, CAM_H, CAM_W)
        z = dep.cpu().numpy().reshape(N, CAM_H, CAM_W)
        e = edge.cpu().numpy().astype(bool).reshape(N, CAM_H, CAM_W)
        tiles = colorize(c, z, e)
        mosaic[d] = tiles.reshape(GROWS, GCOLS, CAM_H, CAM_W, 3) \
            .transpose(0, 2, 1, 3, 4).reshape(GROWS * CAM_H, GCOLS * CAM_W, 3)
        del c, z, e, tiles
        if (d + 1) % 50 == 0:
            mosaic.flush()
            done = env.done.cpu().numpy()
            if done.any():
                env.reset(done.astype(np.uint8))
            print(f"tick {d + 1}/{FRAMES}", flush=True)
    mosaic.flush()
    env.close()

    # content report: tiles that stare at flat sky/ocean in the final frame
    last = mosaic[FRAMES - 1].reshape(GROWS, CAM_H, GCOLS, CAM_W, 3) \
        .transpose(0, 2, 1, 3, 4)
    flat = sum(1 for r in range(GROWS) for c in range(GCOLS)
               if last[r, c].std(axis=(0, 1)).mean() < 6)
    print(f"flat tiles in final frame: {flat}/{N} ({100 * flat / N:.1f}%)")
    with open(os.path.join(OUT, "meta.json"), "w") as f:
        json.dump({"cols": GCOLS, "rows": GROWS, "n": N,
                   "decisions": FRAMES, "cam_w": CAM_W, "cam_h": CAM_H,
                   "center_rc": [GROWS // 2, GCOLS // 2]}, f)
    print(f"recorded {FRAMES} mosaic frames of {N} live envs -> {OUT}")


if __name__ == "__main__":
    main()
