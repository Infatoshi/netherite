"""Record a LIVE GPU batch's semantic-camera mosaic for the zoom video.

Runs VecBlaze with N = 96x85 = 8160 envs (largest near-9:8 grid that fits
the card; grid aspect 2.007, the 0.4% is absorbed as an invisible
vertical stretch at full zoom-out), steps it with the
bench's random action mix, and colorizes every env's 64x36 camera per
decision into one mosaic frame (6336x3168). This is the batched training
sim's REAL observation stream - nothing is re-rendered or posed.

Output: /home/infatoshi/dev/nw/.tmp/batchobs/mosaic.npy  [F, 3168, 6336, 3]
        (memmap uint8) + meta.json with the center env's grid slot.

Run (GPU0): cd c/magma && CUDA_VISIBLE_DEVICES=0 uv run --no-project \
    --with numpy,torch python ../../scripts/batch_obs_record.py
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

OUT = "/home/infatoshi/dev/nw/.tmp/batchobs"
GCOLS, GROWS = 90, 80
N = GCOLS * GROWS
DECISIONS = 450
REPEAT = 4

PAL = np.full((4096, 3), (90, 90, 90), np.uint8)
for bid, col in COLORS.items():
    PAL[bid] = col


def colorize(cam, dep, edge):
    """[N,36,64] id/depth/edge -> [N,36,64,3] rgb (vectorized)."""
    img = PAL[np.clip(cam, 0, 4095)]
    shade = np.clip(1.0 - dep / 512.0, 0.0, 1.0)[..., None]
    solid = (cam != 0)[..., None]
    img = np.where(solid, (img * shade).astype(np.uint8), img)
    img = np.where(edge[..., None], (img * 0.55).astype(np.uint8), img)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    snaps_dir = os.path.join(MAGMA, "rl", "out", "snaps")
    paths = [os.path.join(snaps_dir, f)
             for f in sorted(os.listdir(snaps_dir)) if f.endswith("_t0.bsnp")]
    env = VecBlaze(N, device=0, so_path=CUDA_SO)
    env.load_snapshots(paths)
    env.assign([i % len(paths) for i in range(N)])
    env.reset()
    dev = torch.device("cuda:0")
    g = torch.Generator(device="cpu").manual_seed(7)

    def ri(hi):
        return torch.randint(0, hi, (N,), generator=g, dtype=torch.int64)

    def rand_actions():
        a = torch.zeros(N, 12, dtype=torch.float64)
        a[:, 0] = ri(3).double() - 1
        a[:, 1] = ri(3).double() - 1
        a[:, 2] = (ri(41).double() - 20) * 9
        a[:, 3] = (ri(21).double() - 10) * 9
        a[:, 4] = (ri(10) == 0).double()
        a[:, 7] = (ri(4) != 3).double()
        return a.to(dev)

    mosaic = np.lib.format.open_memmap(
        os.path.join(OUT, "mosaic.npy"), mode="w+", dtype=np.uint8,
        shape=(DECISIONS, GROWS * CAM_H, GCOLS * CAM_W, 3))
    for d in range(DECISIONS):
        cam, dep, edge, *_ = env.step(rand_actions(), repeat=REPEAT)
        c = cam.cpu().numpy().astype(np.int32).reshape(N, CAM_H, CAM_W)
        z = dep.cpu().numpy().astype(np.float64).reshape(N, CAM_H, CAM_W)
        e = edge.cpu().numpy().astype(bool).reshape(N, CAM_H, CAM_W)
        tiles = colorize(c, z, e)
        mosaic[d] = tiles.reshape(GROWS, GCOLS, CAM_H, CAM_W, 3) \
            .transpose(0, 2, 1, 3, 4).reshape(GROWS * CAM_H, GCOLS * CAM_W, 3)
        if (d + 1) % 25 == 0:
            done = env.done.cpu().numpy()
            if done.any():
                env.reset(done.astype(np.uint8))
            print(f"decision {d + 1}/{DECISIONS}", flush=True)
    mosaic.flush()
    env.close()
    with open(os.path.join(OUT, "meta.json"), "w") as f:
        json.dump({"cols": GCOLS, "rows": GROWS, "n": N,
                   "decisions": DECISIONS, "cam_w": CAM_W, "cam_h": CAM_H,
                   "center_rc": [GROWS // 2, GCOLS // 2]}, f)
    print(f"recorded {DECISIONS} mosaic frames of {N} live envs -> {OUT}")


if __name__ == "__main__":
    main()
