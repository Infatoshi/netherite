"""Zoom-out mosaic video over N live batched envs (the launch-thread shot).

Steps a real VecBlaze batch (t0 snapshots, the bench's random action mix) so
every tile is a genuinely live world, colorizes each env's 64x36 semantic
camera with the rl/make_videos.py palette + depth shade + edge darken, then
encodes a single exponential zoom from one env out to the full NxM grid,
ending on a "netherite" title card. The zoom stops at exactly the batch
size - claim only what is on screen.

Run (GPU0):
  cd magma && CUDA_VISIBLE_DEVICES=0 uv run --no-project \
      --with numpy,torch,pillow python blaze/env/make_zoom_video.py \
      [--n 8192] [--out /path/zoom.mp4]
"""
import argparse
import os
import subprocess
import sys

import numpy as np
import torch
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
from blaze import VecBlaze, CUDA_SO                      # noqa: E402
from make_videos import COLORS, CAM_W, CAM_H             # noqa: E402

W, H, FPS = 1920, 960, 30
ZOOM_FRAMES, HOLD_FRAMES = 270, 74      # 344 total, like the original cut


def snap_paths():
    out = os.path.join(os.path.dirname(HERE), "out", "snaps")
    names = sorted(f for f in os.listdir(out) if f.endswith("_t0.bsnp"))
    return [os.path.join(out, f) for f in names]


def rand_actions(n, g):
    """verify_cuda.py run_bench random full-decode action mix (t0)."""
    def ri(hi):
        return torch.randint(0, hi, (n,), generator=g, dtype=torch.int64)
    a = torch.zeros(n, 12, dtype=torch.float64)
    a[:, 0] = ri(3).double() - 1
    a[:, 1] = ri(3).double() - 1
    a[:, 2] = (ri(41).double() - 20) * 9
    a[:, 3] = (ri(21).double() - 10) * 9
    a[:, 4] = (ri(10) == 0).double()
    a[:, 7] = (ri(4) != 3).double()
    return a


def tile_rgb(cam, dep, edge):
    img = np.full((CAM_H, CAM_W, 3), (90, 90, 90), np.uint8)
    for bid, col in COLORS.items():
        img[cam == bid] = col
    solid = cam != 0
    shade = np.clip(1.0 - dep / 512.0, 0.0, 1.0)[..., None]
    img[solid] = (img[solid] * shade[solid]).astype(np.uint8)
    img[edge] = (img[edge] * 0.55).astype(np.uint8)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=8192)
    ap.add_argument("--cols", type=int, default=0, help="0 = auto (2:1 grid)")
    ap.add_argument("--steps", type=int, default=40)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(HERE),
                                                  "out", "zoom_8192.mp4"))
    args = ap.parse_args()
    n = args.n
    cols = args.cols or int(round((2 * n) ** 0.5 / 2)) * 2
    while n % cols:
        cols += 1
    rows = n // cols
    print(f"grid {cols}x{rows} = {n}")

    env = VecBlaze(n, device=0, so_path=CUDA_SO)
    env.load_snapshots(snap_paths())
    env.assign([i % env.n_snaps for i in range(n)])
    env.reset()
    g = torch.Generator(device="cpu").manual_seed(11)
    dev = torch.device("cuda:0")
    for _ in range(args.steps):
        cam, dep, edge, *_ = env.step(rand_actions(n, g).to(dev), repeat=4)
    cam = cam.cpu().numpy().astype(np.int32).reshape(n, CAM_H, CAM_W)
    dep = dep.cpu().numpy().astype(np.float64).reshape(n, CAM_H, CAM_W)
    edge = edge.cpu().numpy().astype(bool).reshape(n, CAM_H, CAM_W)
    env.close()

    mosaic = np.zeros((rows * CAM_H, cols * CAM_W, 3), np.uint8)
    for i in range(n):
        r, c = divmod(i, cols)
        mosaic[r * CAM_H:(r + 1) * CAM_H,
               c * CAM_W:(c + 1) * CAM_W] = tile_rgb(cam[i], dep[i], edge[i])
    mh, mw = mosaic.shape[:2]

    # center start tile; viewport keeps the 2:1 output aspect throughout
    cr, cc = rows // 2, cols // 2
    cy, cx = (cr + 0.5) * CAM_H, (cc + 0.5) * CAM_W
    w0, w1 = float(CAM_W), float(mw)
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
    font = ImageFont.truetype(font_path, 96)

    enc = subprocess.Popen(
        ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264",
         "-crf", "18", "-pix_fmt", "yuv420p", args.out], stdin=subprocess.PIPE)
    total = ZOOM_FRAMES + HOLD_FRAMES
    for f in range(total):
        t = min(f / (ZOOM_FRAMES - 1), 1.0)
        # smoothstep on the exponent: eases both ends of the zoom
        s = t * t * (3 - 2 * t)
        vw = w0 * (w1 / w0) ** s
        vh = vw * H / W
        # drift the center toward the mosaic middle as we zoom out
        ccx = cx + (mw / 2 - cx) * s
        ccy = cy + (mh / 2 - cy) * s
        x0 = np.clip(ccx - vw / 2, 0, mw - vw)
        y0 = np.clip(ccy - vh / 2, 0, mh - vh)
        ix0, iy0 = int(x0), int(y0)
        ix1 = min(int(np.ceil(x0 + vw)) + 1, mw)
        iy1 = min(int(np.ceil(y0 + vh)) + 1, mh)
        crop = Image.fromarray(mosaic[iy0:iy1, ix0:ix1])
        # resample the integer crop; sub-pixel drift is invisible at 30fps
        frame = crop.resize((W, H), Image.NEAREST if vw < mw * 0.6
                            else Image.BILINEAR)
        if f >= ZOOM_FRAMES - 10:
            a = min((f - (ZOOM_FRAMES - 10)) / 20.0, 1.0)
            d = ImageDraw.Draw(frame, "RGBA")
            tw = d.textlength("netherite", font=font)
            bx, by = (W - tw) / 2, H / 2 - 64
            d.rounded_rectangle([bx - 40, by - 24, bx + tw + 40, by + 120],
                                radius=24, fill=(18, 16, 14, int(230 * a)))
            d.text((bx, by), "netherite", font=font,
                   fill=(245, 240, 235, int(255 * a)))
        enc.stdin.write(np.asarray(frame.convert("RGB")).tobytes())
    enc.stdin.close()
    enc.wait()
    assert enc.returncode == 0
    print(f"wrote {args.out} ({total} frames @ {FPS}fps, grid {cols}x{rows})")


if __name__ == "__main__":
    main()
