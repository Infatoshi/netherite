"""Launch zoom video: the game -> the fast parallel renderer -> the farm.

Timeline (30 fps, 530 frames, ~17.7 s), one continuous subject - the
trained chain policy playing in seed 10:
  A   0..59    exact render POV (pixel-gated against the real game)
  B  60..149   slider wipe L->R: SAME world, SAME ticks, rendered by the
               batched sim's semantic camera (the fast parallel renderer)
  H 150..169   hold on the obs view
  D 170..499   pure zoom out (fixed anchor): the hero pane shrinks into a
               RECORDED LIVE 90x80 = 7200-env GPU batch, every pane its
               own world stepping in lockstep (repeat=1, ~1.5x real time)
  E 500..529   full farm + title fade (farm still stepping)

Inputs: scripts/zoom_hero_clip.py (exact frames + hero_obs.npz) and
scripts/batch_obs_record.py (live farm mosaic).

Run: cd netherite && uv run --no-project --with numpy,pillow python \
       scripts/make_zoom_story.py
"""
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "c", "magma", "rl"))
from make_videos import COLORS                          # noqa: E402

OBS = os.path.expanduser("~/dev/nw/.tmp/batchobs")
HERO_DIR = os.path.expanduser("~/dev/nw/.tmp/zoom_hero")
W, H, FPS = 1920, 960, 30
A_END, B_END, H_END, D_END, TOTAL = 60, 150, 170, 500, 530

PAL = np.full((4096, 3), (90, 90, 90), np.uint8)
for bid, col in COLORS.items():
    PAL[bid] = col


def colorize(cam, dep, edge):
    img = PAL[np.clip(cam, 0, 4095)].astype(np.float32)
    shade = np.clip(1.0 - dep.astype(np.float32) / 512.0, 0.0, 1.0)[..., None]
    np.multiply(img, shade, where=(cam != 0)[..., None], out=img)
    np.multiply(img, 0.55, where=edge[..., None], out=img)
    return img.astype(np.uint8)


def main():
    meta = json.load(open(os.path.join(OBS, "meta.json")))
    cols, rows = meta["cols"], meta["rows"]
    cw, ch = meta["cam_w"], meta["cam_h"]
    mosaic = np.load(os.path.join(OBS, "mosaic.npy"), mmap_mode="r")
    F = mosaic.shape[0]
    mw, mh = cols * cw, rows * ch
    assert mw * H == mh * W, "farm grid must fill the output aspect exactly"

    hero_frames_dir = os.path.join(HERO_DIR, "frames")
    hero_files = sorted(f for f in os.listdir(hero_frames_dir)
                        if f.endswith(".ppm"))
    hz = np.load(os.path.join(HERO_DIR, "hero_obs.npz"))
    hero_obs = colorize(hz["cam"], hz["depth"], hz["edge"])   # [T,36,64,3]
    HT = hero_obs.shape[0]
    assert HT >= TOTAL, f"hero obs too short: {HT} < {TOTAL}"

    cr, cc = rows // 2, cols // 2
    ax, ay = (cc + 0.5) * cw, (cr + 0.5) * ch
    w0, w1 = float(cw), float(mw)
    font = ImageFont.truetype(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 96)

    def hero_exact(f):
        im = Image.open(os.path.join(hero_frames_dir,
                                     hero_files[min(f, len(hero_files) - 1)]))
        band = np.asarray(im)[im.height // 4: 3 * im.height // 4]
        return Image.fromarray(band).resize((W, H), Image.LANCZOS)

    def hero_obs_band(f):
        band = hero_obs[min(f, HT - 1)][2:34]
        return Image.fromarray(band).resize((W, H), Image.NEAREST)

    def farm_frame(f, d_i):
        m = np.asarray(mosaic[min(d_i, F - 1)]).copy()
        m[cr * ch:(cr + 1) * ch, cc * cw:(cc + 1) * cw] = \
            hero_obs[min(f, HT - 1)]
        return m

    enc = subprocess.Popen(
        ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264",
         "-crf", "18", "-pix_fmt", "yuv420p",
         os.path.join(ROOT, "demos", "zoom_8192_story.mp4")],
        stdin=subprocess.PIPE)
    for f in range(TOTAL):
        if f < A_END:
            frame = hero_exact(f)
        elif f < B_END:
            x = int(W * (f - A_END + 1) / (B_END - A_END))
            frame = hero_exact(f)
            frame.paste(hero_obs_band(f).crop((0, 0, x, H)), (0, 0))
            d = ImageDraw.Draw(frame)
            d.rectangle([x - 2, 0, x + 2, H], fill=(245, 240, 235))
        elif f < H_END:
            frame = hero_obs_band(f)
        else:
            t = min((f - H_END) / (D_END - H_END - 1), 1.0)
            s = t * t * (3 - 2 * t)
            vw = w0 * (w1 / w0) ** s
            vh = vw / 2
            x0 = float(np.clip(ax - vw / 2, 0, mw - vw))
            y0 = float(np.clip(ay - vh / 2, 0, mh - vh))
            m = farm_frame(f, f - H_END)
            ix0, iy0 = int(x0), int(y0)
            ix1 = min(int(np.ceil(x0 + vw)) + 1, mw)
            iy1 = min(int(np.ceil(y0 + vh)) + 1, mh)
            img = Image.fromarray(m[iy0:iy1, ix0:ix1])
            bx0, by0 = x0 - ix0, y0 - iy0
            k = 1.0
            while (vw * k) / W > 2.0:
                img = img.reduce(2)
                k *= 0.5
            frame = img.transform(
                (W, H), Image.EXTENT,
                (bx0 * k, by0 * k, (bx0 + vw) * k, (by0 + vh) * k),
                resample=Image.BILINEAR if vw > W else Image.NEAREST)
        if f >= D_END - 10:
            a = min((f - (D_END - 10)) / 20.0, 1.0)
            dr = ImageDraw.Draw(frame, "RGBA")
            tw_ = dr.textlength("netherite", font=font)
            bx, by = (W - tw_) / 2, H / 2 - 64
            dr.rounded_rectangle([bx - 40, by - 24, bx + tw_ + 40, by + 120],
                                 radius=24, fill=(18, 16, 14, int(230 * a)))
            dr.text((bx, by), "netherite", font=font,
                    fill=(245, 240, 235, int(255 * a)))
        enc.stdin.write(np.asarray(frame.convert("RGB")).tobytes())
        if (f + 1) % 100 == 0:
            print(f"composed {f + 1}/{TOTAL}", flush=True)
    enc.stdin.close()
    enc.wait()
    assert enc.returncode == 0
    print(f"wrote demos/zoom_8192_story.mp4 ({TOTAL} frames, "
          f"{cols}x{rows}={cols * rows} live envs)")


if __name__ == "__main__":
    main()
