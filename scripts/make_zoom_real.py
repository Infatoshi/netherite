"""Launch-thread zoom video: 8192 LIVE agent worlds, exact renderer.

Every tile is a random-action agent rollout in its own world, rendered by
the exact rasteriser (scripts/zoom_rollouts.py renders them first; hero =
trained chain policy via scripts/zoom_hero_clip.py). The grid is 128x64
SQUARE tiles, so the full mosaic is exactly 2:1 - the final frame fills
1920x960 with no bars.

Per output frame, the visible region is assembled from every visible
tile's clip at the mip level just above its on-screen size, then sampled
with one float-box transform: tiles are composited before the zoom, so
nothing can wobble, and the zoom is a pure exponential scale about the
hero tile.

Run (after zoom_rollouts.py and zoom_hero_clip.py):
  cd netherite && uv run --no-project --with numpy,pillow python \
      scripts/make_zoom_real.py
"""
import argparse
import json
import os
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = "/home/infatoshi/dev/nw/.tmp/zoomrolls"
HERO_DIR = "/home/infatoshi/dev/nw/.tmp/zoom_hero"
W, H, FPS = 1920, 960, 30
ZOOM_FRAMES, HOLD_FRAMES = 270, 74
T = 192                    # square tile size (native rollout render)
MIPS = (96, 48, 24)
COLS, ROWS = 128, 64
N = COLS * ROWS


class Clips:
    """Lazy mmap access to every env's multi-res clips."""

    def __init__(self, work):
        self.work = work
        self.cache = {}
        self.native_first = {}

    def mip(self, env, level, f):
        key = (env, level)
        if key not in self.cache:
            self.cache[key] = np.load(
                os.path.join(self.work, f"e{env}", f"clip{level}.npy"),
                mmap_mode="r")
        clip = self.cache[key]
        return clip[min(f, clip.shape[0] - 1)]

    def native(self, env, f):
        nd = os.path.join(self.work, f"e{env}", "native")
        if env not in self.native_first:
            first = open(os.path.join(nd, "FIRST.txt")).read().strip()
            self.native_first[env] = int(first.split("_")[1].split(".")[0])
        i = self.native_first[env] + f
        fp = os.path.join(nd, f"frame_{i:06d}.ppm")
        if not os.path.exists(fp):
            files = sorted(x for x in os.listdir(nd) if x.endswith(".ppm"))
            fp = os.path.join(nd, files[-1])
        return np.asarray(Image.open(fp))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep-native", type=int, default=256)
    ap.add_argument("--out",
                    default=os.path.join(ROOT, "demos", "zoom_8192_real.mp4"))
    args = ap.parse_args()
    total = ZOOM_FRAMES + HOLD_FRAMES

    missing = [e for e in range(N)
               if not os.path.exists(os.path.join(WORK, f"e{e}", "DONE"))]
    assert not missing, f"{len(missing)} rollouts missing (first {missing[:5]})"

    # slot -> env. The center block must map to envs that kept native ppms.
    cr, cc = ROWS // 2, COLS // 2
    rng = np.random.default_rng(7)
    # native-mapped block must cover every tile that can appear >96px on
    # screen: at sw=96 the viewport spans 20x10 tiles, so use 21x11
    block = [(r, c) for r in range(cr - 5, cr + 6)
             for c in range(cc - 10, cc + 11)]
    natives = [int(e) for e in rng.permutation(args.keep_native)]
    block_envs = natives[:len(block)]
    spare = natives[len(block):] + list(range(args.keep_native, N))
    others = [int(e) for e in rng.permutation(spare)]
    env_of = {}
    bi = oi = 0
    blockset = set(block)
    for r in range(ROWS):
        for c in range(COLS):
            if (r, c) in blockset:
                env_of[(r, c)] = block_envs[bi]
                bi += 1
            else:
                env_of[(r, c)] = others[oi]
                oi += 1
    clips = Clips(WORK)

    meta = json.load(open(os.path.join(HERO_DIR, "META.json")))
    hero_frames_dir = os.path.join(HERO_DIR, "frames")
    hero_files = sorted(f for f in os.listdir(hero_frames_dir)
                        if f.endswith(".ppm"))
    assert len(hero_files) >= 200

    mw, mh = COLS * T, ROWS * T          # 24576 x 12288 - exactly 2:1
    ax, ay = (cc + 0.5) * T, (cr + 0.5) * T
    w0, w1 = float(T), float(mw)
    font = ImageFont.truetype(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 96)

    enc = subprocess.Popen(
        ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264",
         "-crf", "18", "-pix_fmt", "yuv420p", args.out], stdin=subprocess.PIPE)
    for f in range(total):
        t = min(f / (ZOOM_FRAMES - 1), 1.0)
        s = t * t * (3 - 2 * t)
        vw = w0 * (w1 / w0) ** s
        vh = vw / 2
        x0 = float(np.clip(ax - vw / 2, 0, mw - vw))
        y0 = float(np.clip(ay - vh / 2, 0, mh - vh))
        sw = W * T / vw                   # tile size on screen
        level = T if sw > MIPS[0] else min(m for m in MIPS if m >= sw) \
            if sw <= MIPS[0] else T
        # visible slot range
        c0, c1 = int(x0 // T), min(int((x0 + vw) // T) + 1, COLS)
        r0, r1 = int(y0 // T), min(int((y0 + vh) // T) + 1, ROWS)
        L = level
        region = np.empty(((r1 - r0) * L, (c1 - c0) * L, 3), np.uint8)
        for r in range(r0, r1):
            for c in range(c0, c1):
                env = env_of[(r, c)]
                if L == T:
                    tile = clips.native(env, f) if env < args.keep_native \
                        else np.asarray(Image.fromarray(
                            np.asarray(clips.mip(env, 96, f))).resize(
                                (T, T), Image.NEAREST))
                else:
                    tile = clips.mip(env, L, f)
                region[(r - r0) * L:(r - r0 + 1) * L,
                       (c - c0) * L:(c - c0 + 1) * L] = tile
        k = L / T
        frame = Image.fromarray(region).transform(
            (W, H), Image.EXTENT,
            ((x0 - c0 * T) * k, (y0 - r0 * T) * k,
             (x0 - c0 * T + vw) * k, (y0 - r0 * T + vh) * k),
            resample=Image.BILINEAR)
        # hero overlay from its native square render
        scale = W / vw
        hsw = T * scale
        if hsw >= 3:
            hf = min(f, len(hero_files) - 1)
            hero = Image.open(os.path.join(hero_frames_dir, hero_files[hf]))
            hx, hy = (cc * T - x0) * scale, (cr * T - y0) * scale
            res = hero.resize((max(int(round(hsw)), 1),) * 2,
                              Image.LANCZOS if hsw < hero.width
                              else Image.BILINEAR)
            frame.paste(res, (int(round(hx)), int(round(hy))))
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
        if (f + 1) % 60 == 0:
            print(f"composed {f + 1}/{total}", flush=True)
    enc.stdin.close()
    enc.wait()
    assert enc.returncode == 0
    print(f"wrote {args.out} ({total} frames, {COLS}x{ROWS} live worlds, "
          f"hero window from t={meta['start']})")


if __name__ == "__main__":
    main()
