"""Launch-thread zoom video over N REAL exact-renderer frames.

Every tile is a distinct (seed, pose) frame rendered by magma's exact
rasteriser (the one the pixel gates verify), NOT the semantic training
camera. Zoom runs from one full-bleed render out to exactly the claimed
grid (default 128x64 = 8192 tiles), with a hi-res center overlay so the
opening frames look like gameplay rather than an upscaled thumbnail.

Run (CPU renderer, parallel):
  cd netherite && uv run --no-project --with numpy,pillow python \
      scripts/make_zoom_real.py [--out demos/zoom_8192_real.mp4]
"""
import argparse
import json
import os
import subprocess
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAGMA = os.path.join(ROOT, "c", "magma")
GAME = os.path.join(MAGMA, "magma_game")
W, H, FPS = 1920, 960, 30
ZOOM_FRAMES, HOLD_FRAMES = 270, 74
RW, RH = 256, 144          # per-pose render size
TW, TH = 128, 72           # mosaic tile size


def poses_for(seed, n_poses):
    rng = np.random.default_rng(1000 + seed)
    out = []
    for p in range(n_poses):
        out.append({
            "x": float(rng.uniform(-40, 40)) + 0.5,
            "y": float(rng.uniform(74, 88)),
            "z": float(rng.uniform(-40, 40)) + 0.5,
            "yaw": float(rng.uniform(-180, 180)),
            "pitch": float(rng.uniform(16, 40)),
        })
    return out, int(rng.integers(0, 12000))


def render_seed(job):
    seed, n_poses, workdir = job
    outdir = os.path.join(workdir, f"s{seed}")
    marker = os.path.join(outdir, "DONE")
    if os.path.exists(marker):
        return seed
    os.makedirs(outdir, exist_ok=True)
    poses, wtime = poses_for(seed, n_poses)
    script = os.path.join(outdir, "script.jsonl")
    with open(script, "w") as f:
        f.write(json.dumps({"tick": 0, "type": "set_time",
                            "value": wtime}) + "\n")
        for i, p in enumerate(poses):
            f.write(json.dumps({"tick": 3 + 2 * i, "type": "set_pose",
                                **p}) + "\n")
    ticks = 3 + 2 * n_poses + 2
    env = dict(os.environ, MAGMA_HIDE_GUI="1",
               MAGMA_CONF="/home/infatoshi/dev/nw/.tmp/zoom_video.conf")
    r = subprocess.run(
        [GAME, "--headless", "--world", "default", "--seed", str(seed),
         "--ticks", str(ticks), "--width", str(RW), "--height", str(RH),
         "--mobs", "off", "--script", script,
         "--state-out", "/dev/null", "--frames-out", outdir],
        env=env, cwd=MAGMA, capture_output=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError(f"seed {seed}: rc={r.returncode} "
                           f"{r.stderr.decode(errors='replace')[-300:]}")
    open(marker, "w").close()
    return seed


def tile_of(workdir, seed, pose_i):
    fp = os.path.join(workdir, f"s{seed}", f"frame_{3 + 2 * pose_i + 1:06d}.ppm")
    return np.asarray(Image.open(fp).resize((TW, TH), Image.LANCZOS))


def tile_stats(img):
    """(usable, land, sky_frac) - rejects dark/inside-terrain/flat frames."""
    a = img.astype(np.int16)
    blueish = (a[..., 2] - np.maximum(a[..., 0], a[..., 1])) > 20
    lum = a.mean(axis=2)
    land = 1.0 - float(blueish.mean())
    dark = float((lum < 25).mean())
    usable = (lum.mean() >= 60 and dark <= 0.25 and float(lum.std()) >= 18)
    return usable, land, float(blueish.mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=1024,
                    help="worlds to render; tiles are the best-scoring "
                         "poses, so render more than n/poses")
    ap.add_argument("--min-land", type=float, default=0.30)
    ap.add_argument("--poses", type=int, default=16)
    ap.add_argument("--cols", type=int, default=128)
    ap.add_argument("--jobs", type=int, default=24)
    ap.add_argument("--workdir",
                    default="/home/infatoshi/dev/nw/.tmp/zoomtiles")
    ap.add_argument("--out",
                    default=os.path.join(ROOT, "demos", "zoom_8192_real.mp4"))
    args = ap.parse_args()
    cols = args.cols
    n = 8192
    assert n % cols == 0
    rows = n // cols
    print(f"grid {cols}x{rows} = {n} tiles from "
          f"{args.seeds} worlds x {args.poses} poses, "
          f"land filter {args.min_land}")

    os.makedirs(args.workdir, exist_ok=True)
    jobs = [(s, args.poses, args.workdir) for s in range(args.seeds)]
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        for i, _ in enumerate(ex.map(render_seed, jobs)):
            if (i + 1) % 64 == 0:
                print(f"rendered {i + 1}/{args.seeds} worlds")

    # score every rendered pose, keep the n best above the land floor,
    # shuffle so grid neighbours differ in world
    print("scoring tiles...")
    good, spare = [], []
    for seed in range(args.seeds):
        for pose_i in range(args.poses):
            img = tile_of(args.workdir, seed, pose_i)
            usable, land, sky = tile_stats(img)
            row = (land, seed, pose_i, img, sky)
            if usable and land >= args.min_land:
                good.append(row)
            else:
                spare.append((usable, row))
    good.sort(key=lambda x: -x[0])
    kept = good[:n]
    if len(kept) < n:
        spare.sort(key=lambda x: (-int(x[0]), -x[1][0]))
        fill = [r for _, r in spare[:n - len(kept)]]
        print(f"only {len(kept)} usable tiles; filling {len(fill)} "
              "from rejects (best first)")
        kept += fill
    print(f"kept {len(kept)} tiles, land {kept[-1][0]:.2f}..{kept[0][0]:.2f}")
    # zoom center: a vista - lots of land AND a visible horizon band of sky
    vista = [r for r in kept if 0.08 <= r[4] <= 0.35]
    center_row = max(vista or kept, key=lambda r: r[0])
    rng = np.random.default_rng(7)
    order = rng.permutation(n)
    mosaic = np.zeros((rows * TH, cols * TW, 3), np.uint8)
    idx_of = {}
    for slot in range(n):
        score, seed, pose_i, img, _sky = kept[int(order[slot])]
        r, c = divmod(slot, cols)
        idx_of[(r, c)] = (seed, pose_i)
        mosaic[r * TH:(r + 1) * TH, c * TW:(c + 1) * TW] = img
    # the chosen vista becomes the zoom center
    cr, cc = rows // 2, cols // 2
    best = center_row
    for (r, c), sp in idx_of.items():
        if sp == (best[1], best[2]):
            swap = idx_of[(cr, cc)]
            idx_of[(r, c)] = swap
            idx_of[(cr, cc)] = (best[1], best[2])
            a = mosaic[r * TH:(r + 1) * TH, c * TW:(c + 1) * TW].copy()
            mosaic[r * TH:(r + 1) * TH, c * TW:(c + 1) * TW] = \
                mosaic[cr * TH:(cr + 1) * TH, cc * TW:(cc + 1) * TW]
            mosaic[cr * TH:(cr + 1) * TH, cc * TW:(cc + 1) * TW] = a
            break
    mh, mw = mosaic.shape[:2]

    # hi-res center render: same (seed, pose, time) as the center tile
    cseed, cpose = idx_of[(cr, cc)]
    hipath = os.path.join(args.workdir, f"center_hi_s{cseed}_p{cpose}")
    if not os.path.exists(os.path.join(hipath, "DONE")):
        os.makedirs(hipath, exist_ok=True)
        poses, wtime = poses_for(cseed, args.poses)
        script = os.path.join(hipath, "script.jsonl")
        with open(script, "w") as f:
            f.write(json.dumps({"tick": 0, "type": "set_time",
                                "value": wtime}) + "\n")
            f.write(json.dumps({"tick": 3, "type": "set_pose",
                                **poses[cpose]}) + "\n")
        env = dict(os.environ, MAGMA_HIDE_GUI="1",
                   MAGMA_CONF="/home/infatoshi/dev/nw/.tmp/zoom_video.conf")
        subprocess.run(
            [GAME, "--headless", "--world", "default", "--seed", str(cseed),
             "--ticks", "6", "--width", "1920", "--height", "1080",
             "--mobs", "off", "--script", script,
             "--state-out", "/dev/null", "--frames-out", hipath],
            env=env, cwd=MAGMA, check=True, capture_output=True, timeout=600)
        open(os.path.join(hipath, "DONE"), "w").close()
    hi = Image.open(os.path.join(hipath, "frame_000004.ppm"))

    cy, cx = (cr + 0.5) * TH, (cc + 0.5) * TW
    w0, w1 = float(TW), float(mw)
    font = ImageFont.truetype(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 96)

    enc = subprocess.Popen(
        ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264",
         "-crf", "18", "-pix_fmt", "yuv420p", args.out], stdin=subprocess.PIPE)
    total = ZOOM_FRAMES + HOLD_FRAMES
    for f in range(total):
        t = min(f / (ZOOM_FRAMES - 1), 1.0)
        s = t * t * (3 - 2 * t)
        vw = w0 * (w1 / w0) ** s
        vh = vw * H / W
        ccx = cx + (mw / 2 - cx) * s
        ccy = cy + (mh / 2 - cy) * s
        x0 = float(np.clip(ccx - vw / 2, 0, mw - vw))
        y0 = float(np.clip(ccy - vh / 2, 0, max(mh - vh, 0)))
        ix0, iy0 = int(x0), int(y0)
        ix1 = min(int(np.ceil(x0 + vw)) + 1, mw)
        iy1 = min(int(np.ceil(y0 + vh)) + 1, mh)
        crop = Image.fromarray(mosaic[iy0:iy1, ix0:ix1])
        frame = crop.resize((W, H), Image.BILINEAR)
        # overlay the true-resolution center render while it is big on screen
        sx = (cc * TW - x0) / vw * W
        sy = (cr * TH - y0) / vh * H
        sw = TW / vw * W
        if sw > 40:
            sh = sw * TH / TW
            ov = hi.resize((max(int(sw), 1), max(int(sh * 1080 / 960 * 960
                                                     / 1080), 1)),
                           Image.LANCZOS)
            frame.paste(ov, (int(sx), int(sy)))
        if f >= ZOOM_FRAMES - 10:
            a = min((f - (ZOOM_FRAMES - 10)) / 20.0, 1.0)
            d = ImageDraw.Draw(frame, "RGBA")
            tw_ = d.textlength("netherite", font=font)
            bx, by = (W - tw_) / 2, H / 2 - 64
            d.rounded_rectangle([bx - 40, by - 24, bx + tw_ + 40, by + 120],
                                radius=24, fill=(18, 16, 14, int(230 * a)))
            d.text((bx, by), "netherite", font=font,
                   fill=(245, 240, 235, int(255 * a)))
        enc.stdin.write(np.asarray(frame.convert("RGB")).tobytes())
    enc.stdin.close()
    enc.wait()
    assert enc.returncode == 0
    print(f"wrote {args.out} ({total} frames, {cols}x{rows} real tiles)")


if __name__ == "__main__":
    main()
