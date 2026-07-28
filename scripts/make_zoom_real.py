"""Launch-thread zoom video over 8192 REAL exact-renderer tiles.

- Hero tile (zoom start) is the TRAINED chain policy's POV replayed through
  the exact renderer (scripts/zoom_hero_clip.py renders it first).
- A ring of neighbours around the hero is animated (slow scripted camera
  pans) so the opening does not look frozen; every other tile is a distinct
  scenic (world, pose) still.
- The zoom is a PURE scale about a fixed anchor (the hero tile): no lateral
  drift, no easing of the center. Sampling goes through a mip pyramid with
  float-box Image.transform, so there is no per-frame integer snapping
  (the earlier cut's jitter) and no aliasing shimmer.

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
RW, RH = 256, 144          # per-pose render size
TW, TH = 128, 72           # mosaic tile size
PAN_RATE = 0.30            # deg/frame for animated neighbour tiles


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
         "--ticks", str(ticks), "--width", str(RW), "--height", str(RH),
         "--mobs", "off", "--script", script,
         "--state-out", "/dev/null", "--frames-out", outdir],
        env=game_env(), cwd=MAGMA, capture_output=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError(f"seed {seed}: rc={r.returncode} "
                           f"{r.stderr.decode(errors='replace')[-300:]}")
    open(marker, "w").close()
    return seed


PW, PH = 512, 288          # animated-neighbour render size (crisp when big)


def render_pan(job):
    """Slow-pan clip for one animated neighbour tile."""
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
    clip = np.zeros((frames, TH, TW, 3), np.uint8)
    for t in range(frames):
        fp = os.path.join(outdir, f"frame_{3 + t + 1:06d}.ppm")
        clip[t] = np.asarray(Image.open(fp).resize((TW, TH), Image.LANCZOS))
    return outdir, clip


def tile_of(workdir, seed, pose_i):
    fp = os.path.join(workdir, f"s{seed}",
                      f"frame_{3 + 2 * pose_i + 1:06d}.ppm")
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
            usable, land, _sky = tile_stats(img)
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

    # hero: trained-policy POV clip (zoom_hero_clip.py output)
    meta = json.load(open(os.path.join(HERO_DIR, "META.json")))
    hero_frames_dir = os.path.join(HERO_DIR, "frames")
    hero_files = sorted(f for f in os.listdir(hero_frames_dir)
                        if f.endswith(".ppm"))
    assert len(hero_files) >= 200, f"hero clip too short: {len(hero_files)}"
    cr, cc = rows // 2, cols // 2

    # animated neighbour ring: 5x3 block around the hero
    ring = [(r, c) for r in range(cr - 1, cr + 2)
            for c in range(cc - 2, cc + 3) if (r, c) != (cr, cc)]
    print(f"rendering {len(ring)} animated neighbour tiles...")
    pan_jobs = [(idx_of[rc][0], idx_of[rc][1], args.poses, total,
                 args.workdir) for rc in ring]
    anim = {}
    with ProcessPoolExecutor(max_workers=min(args.jobs, len(ring))) as ex:
        for rc, (pdir, clip) in zip(ring, ex.map(render_pan, pan_jobs)):
            anim[rc] = (pdir, clip)
            r, c = rc
            mosaic[r * TH:(r + 1) * TH, c * TW:(c + 1) * TW] = clip[-1]
    hero_last = np.asarray(Image.open(
        os.path.join(hero_frames_dir, hero_files[-1])).resize(
            (TW, TH), Image.LANCZOS))
    mosaic[cr * TH:(cr + 1) * TH, cc * TW:(cc + 1) * TW] = hero_last
    mh, mw = mosaic.shape[:2]

    # mip pyramid for shimmer-free minification
    mips = [Image.fromarray(mosaic)]
    while mips[-1].width > cols * 4:
        mips.append(mips[-1].reduce(2))

    # pure zoom about the hero tile center: fixed anchor, no drift
    ax, ay = (cc + 0.5) * TW, (cr + 0.5) * TH
    w0, w1 = float(TW), float(mw)
    font = ImageFont.truetype(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 96)

    def paste_clip(frame, pdir, clip, r, c, x0, y0, scale, f):
        sx, sy = (c * TW - x0) * scale, (r * TH - y0) * scale
        sw, sh = TW * scale, TH * scale
        if sw < 2 or sx > W or sy > H or sx + sw < 0 or sy + sh < 0:
            return
        fi = min(f, len(clip) - 1)
        if sw >= 192:
            # big on screen: paste from the native 512x288 render
            src = Image.open(os.path.join(pdir, f"frame_{3 + fi + 1:06d}.ppm"))
            res = src.resize((max(int(round(sw)), 1),
                              max(int(round(sh)), 1)),
                             Image.LANCZOS if sw < PW else Image.NEAREST)
        else:
            res = Image.fromarray(clip[fi]).resize(
                (max(int(round(sw)), 1), max(int(round(sh)), 1)),
                Image.LANCZOS)
        frame.paste(res, (int(round(sx)), int(round(sy))))

    enc = subprocess.Popen(
        ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264",
         "-crf", "18", "-pix_fmt", "yuv420p", args.out], stdin=subprocess.PIPE)
    for f in range(total):
        t = min(f / (ZOOM_FRAMES - 1), 1.0)
        s = t * t * (3 - 2 * t)
        vw = w0 * (w1 / w0) ** s
        vh = vw * H / W
        x0 = float(np.clip(ax - vw / 2, 0, mw - vw))
        y0 = float(np.clip(ay - vh / 2, 0, max(mh - vh, 0)))
        scale = W / vw
        lvl = int(np.clip(np.floor(np.log2(max(vw / W, 1.0))), 0,
                          len(mips) - 1))
        d = 1 << lvl
        frame = mips[lvl].transform(
            (W, H), Image.EXTENT,
            (x0 / d, y0 / d, (x0 + vw) / d, (y0 + vh) / d),
            resample=Image.BILINEAR)
        for (r, c), (pdir, clip) in anim.items():
            paste_clip(frame, pdir, clip, r, c, x0, y0, scale, f)
        hf = min(f, len(hero_files) - 1)
        hero = Image.open(os.path.join(hero_frames_dir, hero_files[hf]))
        sx, sy = (cc * TW - x0) * scale, (cr * TH - y0) * scale
        sw, sh = TW * scale, TH * scale
        if sw >= 2:
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
