"""Launch-thread zoom video over 8192 REAL exact-renderer tiles.

- Hero tile (zoom start) is the TRAINED chain policy's POV replayed through
  the exact renderer, window picked by camera-motion score so it never sits
  still (scripts/zoom_hero_clip.py renders it first).
- A 9x7 block around the hero is animated (slow scripted pans). While the
  viewport is inside that block, the whole view is sampled from ONE rigid
  per-frame canvas built at 512x288/tile, so neighbouring tiles cannot
  wobble against each other; the hero is overlaid from its native render.
- Outside the block, the view samples a native-256x144-per-tile mosaic
  through a mip pyramid with float-box transforms: no integer snapping,
  no shimmer, and tiles stay crisp right through the handoff.
- The zoom is a PURE exponential scale about a fixed anchor. No drift.

Run:
  1) cd c/magma && uv run --no-project --with numpy python \
         ../../scripts/zoom_hero_clip.py
  2) cd netherite && uv run --no-project --with numpy,pillow python \
         scripts/make_zoom_real.py [--jobs 16]
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
CONF = "/home/infatoshi/dev/nw/.tmp/zoom_video.conf"
HERO_DIR = "/home/infatoshi/dev/nw/.tmp/zoom_hero"
W, H, FPS = 1920, 960, 30
ZOOM_FRAMES, HOLD_FRAMES = 270, 74
TW, TH = 256, 144          # tile size (native still-render resolution)
PW, PH = 512, 288          # animated-tile render resolution
PAN_RATE = 0.30            # deg/frame for animated tiles
BLK_C, BLK_R = 9, 7        # animated block around the hero


def poses_for(seed, n_poses):
    rng = np.random.default_rng(1000 + seed)
    out = []
    for _ in range(n_poses):
        out.append({
            "x": float(rng.uniform(-40, 40)) + 0.5,
            "y": float(rng.uniform(74, 88)),
            "z": float(rng.uniform(-40, 40)) + 0.5,
            "yaw": float(rng.uniform(-180, 180)),
            "pitch": float(rng.uniform(16, 40)),
        })
    return out, int(rng.integers(0, 12000))


def game_env():
    return dict(os.environ, MAGMA_HIDE_GUI="1", MAGMA_CONF=CONF)


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
    r = subprocess.run(
        [GAME, "--headless", "--world", "default", "--seed", str(seed),
         "--ticks", str(ticks), "--width", str(TW), "--height", str(TH),
         "--mobs", "off", "--script", script,
         "--state-out", "/dev/null", "--frames-out", outdir],
        env=game_env(), cwd=MAGMA, capture_output=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError(f"seed {seed}: rc={r.returncode} "
                           f"{r.stderr.decode(errors='replace')[-300:]}")
    open(marker, "w").close()
    return seed


def render_pan(job):
    """Slow-pan clip for one animated tile; returns its dir + last frame."""
    seed, pose_i, n_poses, frames, workdir = job
    outdir = os.path.join(workdir, f"pan512_s{seed}_p{pose_i}")
    marker = os.path.join(outdir, "DONE")
    if not os.path.exists(marker):
        os.makedirs(outdir, exist_ok=True)
        poses, wtime = poses_for(seed, n_poses)
        p = poses[pose_i]
        script = os.path.join(outdir, "script.jsonl")
        with open(script, "w") as f:
            f.write(json.dumps({"tick": 0, "type": "set_time",
                                "value": wtime}) + "\n")
            for t in range(frames):
                q = dict(p)
                q["yaw"] = ((p["yaw"] + PAN_RATE * t + 180) % 360) - 180
                f.write(json.dumps({"tick": 3 + t, "type": "set_pose",
                                    **q}) + "\n")
        r = subprocess.run(
            [GAME, "--headless", "--world", "default", "--seed", str(seed),
             "--ticks", str(3 + frames + 1), "--width", str(PW),
             "--height", str(PH), "--mobs", "off", "--script", script,
             "--state-out", "/dev/null", "--frames-out", outdir],
            env=game_env(), cwd=MAGMA, capture_output=True, timeout=900)
        if r.returncode != 0:
            raise RuntimeError(f"pan {seed}/{pose_i}: "
                               f"{r.stderr.decode(errors='replace')[-200:]}")
        open(marker, "w").close()
    last = np.asarray(Image.open(
        os.path.join(outdir, f"frame_{3 + frames:06d}.ppm")).resize(
            (TW, TH), Image.LANCZOS))
    return outdir, last


def pan_frame(pdir, f):
    return Image.open(os.path.join(pdir, f"frame_{3 + f + 1:06d}.ppm"))


def tile_of(workdir, seed, pose_i):
    fp = os.path.join(workdir, f"s{seed}",
                      f"frame_{3 + 2 * pose_i + 1:06d}.ppm")
    return np.asarray(Image.open(fp))


def tile_stats(img):
    """(usable, land) - rejects dark/inside-terrain/flat frames."""
    a = img.astype(np.int16)
    blueish = (a[..., 2] - np.maximum(a[..., 0], a[..., 1])) > 20
    lum = a.mean(axis=2)
    land = 1.0 - float(blueish.mean())
    dark = float((lum < 25).mean())
    usable = (lum.mean() >= 60 and dark <= 0.25 and float(lum.std()) >= 18)
    return usable, land


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=1024)
    ap.add_argument("--poses", type=int, default=16)
    ap.add_argument("--cols", type=int, default=128)
    ap.add_argument("--jobs", type=int, default=16)
    ap.add_argument("--min-land", type=float, default=0.25)
    ap.add_argument("--workdir",
                    default="/home/infatoshi/dev/nw/.tmp/zoomtiles")
    ap.add_argument("--out",
                    default=os.path.join(ROOT, "demos", "zoom_8192_real.mp4"))
    args = ap.parse_args()
    n, cols = 8192, args.cols
    rows = n // cols
    total = ZOOM_FRAMES + HOLD_FRAMES

    os.makedirs(args.workdir, exist_ok=True)
    jobs = [(s, args.poses, args.workdir) for s in range(args.seeds)]
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        for i, _ in enumerate(ex.map(render_seed, jobs)):
            if (i + 1) % 128 == 0:
                print(f"rendered {i + 1}/{args.seeds} worlds")

    print("scoring tiles...")
    good, spare = [], []
    for seed in range(args.seeds):
        for pose_i in range(args.poses):
            img = tile_of(args.workdir, seed, pose_i)
            usable, land = tile_stats(img)
            row = (land, seed, pose_i, img)
            if usable and land >= args.min_land:
                good.append(row)
            else:
                spare.append((usable, row))
    good.sort(key=lambda x: -x[0])
    kept = good[:n]
    if len(kept) < n:
        spare.sort(key=lambda x: (-int(x[0]), -x[1][0]))
        kept += [r for _, r in spare[:n - len(kept)]]
    print(f"kept {len(kept)} tiles, land {kept[-1][0]:.2f}..{kept[0][0]:.2f}")

    rng = np.random.default_rng(7)
    order = rng.permutation(n)
    mosaic = np.zeros((rows * TH, cols * TW, 3), np.uint8)
    idx_of = {}
    for slot in range(n):
        _land, seed, pose_i, img = kept[int(order[slot])]
        r, c = divmod(slot, cols)
        idx_of[(r, c)] = (seed, pose_i)
        mosaic[r * TH:(r + 1) * TH, c * TW:(c + 1) * TW] = img

    # hero: trained-policy POV clip
    meta = json.load(open(os.path.join(HERO_DIR, "META.json")))
    hero_frames_dir = os.path.join(HERO_DIR, "frames")
    hero_files = sorted(f for f in os.listdir(hero_frames_dir)
                        if f.endswith(".ppm"))
    assert len(hero_files) >= 200, f"hero clip too short: {len(hero_files)}"
    cr, cc = rows // 2, cols // 2

    # animated block: rendered pans, sampled per frame from disk
    r0, c0 = cr - BLK_R // 2, cc - BLK_C // 2
    block = [(r, c) for r in range(r0, r0 + BLK_R)
             for c in range(c0, c0 + BLK_C) if (r, c) != (cr, cc)]
    print(f"rendering {len(block)} animated tiles...")
    pan_jobs = [(idx_of[rc][0], idx_of[rc][1], args.poses, total,
                 args.workdir) for rc in block]
    pan_dirs = {}
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        for rc, (pdir, last) in zip(block, ex.map(render_pan, pan_jobs)):
            pan_dirs[rc] = pdir
            r, c = rc
            mosaic[r * TH:(r + 1) * TH, c * TW:(c + 1) * TW] = last
    hero_last = np.asarray(Image.open(
        os.path.join(hero_frames_dir, hero_files[-1])).resize(
            (TW, TH), Image.LANCZOS))
    mosaic[cr * TH:(cr + 1) * TH, cc * TW:(cc + 1) * TW] = hero_last
    mh, mw = mosaic.shape[:2]

    mips = [Image.fromarray(mosaic)]
    while mips[-1].width > 1024:
        mips.append(mips[-1].reduce(2))

    # canvas geometry (mosaic units and 2x canvas pixels)
    K = PW // TW
    ox, oy = c0 * TW, r0 * TH
    span_w, span_h = BLK_C * TW, BLK_R * TH

    ax, ay = (cc + 0.5) * TW, (cr + 0.5) * TH
    w0, w1 = float(TW), float(mw)
    font = ImageFont.truetype(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 96)

    enc = subprocess.Popen(
        ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264",
         "-crf", "18", "-pix_fmt", "yuv420p", args.out], stdin=subprocess.PIPE)
    canvas = Image.new("RGB", (BLK_C * PW, BLK_R * PH))
    for f in range(total):
        t = min(f / (ZOOM_FRAMES - 1), 1.0)
        s = t * t * (3 - 2 * t)
        vw = w0 * (w1 / w0) ** s
        vh = vw * H / W
        x0 = float(np.clip(ax - vw / 2, 0, mw - vw))
        y0 = float(np.clip(ay - vh / 2, 0, max(mh - vh, 0)))
        in_canvas = (x0 >= ox and y0 >= oy and x0 + vw <= ox + span_w
                     and y0 + vh <= oy + span_h)
        if in_canvas:
            # one rigid hi-res layer: tiles cannot move against each other
            for (r, c) in block:
                px, py = (c - c0) * PW, (r - r0) * PH
                canvas.paste(pan_frame(pan_dirs[(r, c)],
                                       min(f, total - 1)), (px, py))
            frame = canvas.transform(
                (W, H), Image.EXTENT,
                ((x0 - ox) * K, (y0 - oy) * K,
                 (x0 - ox + vw) * K, (y0 - oy + vh) * K),
                resample=Image.BILINEAR)
        else:
            lvl = int(np.clip(np.floor(np.log2(max(vw / W, 1.0))), 0,
                              len(mips) - 1))
            d = 1 << lvl
            frame = mips[lvl].transform(
                (W, H), Image.EXTENT,
                (x0 / d, y0 / d, (x0 + vw) / d, (y0 + vh) / d),
                resample=Image.BILINEAR)
        # hero overlay from its native render, on top in both phases
        scale = W / vw
        sw, sh = TW * scale, TH * scale
        if sw >= 3:
            hf = min(f, len(hero_files) - 1)
            hero = Image.open(os.path.join(hero_frames_dir, hero_files[hf]))
            sx, sy = (cc * TW - x0) * scale, (cr * TH - y0) * scale
            res = hero.resize((max(int(round(sw)), 1),
                               max(int(round(sh)), 1)),
                              Image.LANCZOS if sw < hero.width
                              else Image.BILINEAR)
            frame.paste(res, (int(round(sx)), int(round(sy))))
        if f >= ZOOM_FRAMES - 10:
            a = min((f - (ZOOM_FRAMES - 10)) / 20.0, 1.0)
            dr = ImageDraw.Draw(frame, "RGBA")
            tw_ = dr.textlength("netherite", font=font)
            bx, by = (W - tw_) / 2, H / 2 - 64
            dr.rounded_rectangle([bx - 40, by - 24, bx + tw_ + 40, by + 120],
                                 radius=24, fill=(18, 16, 14, int(230 * a)))
            dr.text((bx, by), "netherite", font=font,
                    fill=(245, 240, 235, int(255 * a)))
        enc.stdin.write(np.asarray(frame.convert("RGB")).tobytes())
    enc.stdin.close()
    enc.wait()
    assert enc.returncode == 0
    print(f"wrote {args.out} ({total} frames, {cols}x{rows} real tiles, "
          f"hero window from t={meta['start']})")


if __name__ == "__main__":
    main()
