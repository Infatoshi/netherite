"""Launch zoom video, story cut: oracle -> exact render -> policy obs ->
zoom out over the LIVE batched farm.

Timeline (30 fps, 650 frames, ~21.7 s):
  A   0..49    real Java oracle POV (hold_dig_dense goldens, every tick)
  B  50..107   slider wipe L->R: magma's exact render of the SAME ticks
  C 108..167   second wipe to the center env's semantic camera (what the
               policy sees), full-bleed chunky pixels
  D 168..517   pure zoom out (fixed anchor, exponential scale) over the
               recorded live batch mosaic (16:9 tiles on a 9:8 grid fills
               2:1, so the end frame fills 1920x960)
  E 518..547   full farm + title fade (batch still stepping)

Inputs: scripts/batch_obs_record.py output (live mosaic memmap) and the
blaze_bow_demo tape goldens + magma replay frames.

Run: cd netherite && uv run --no-project --with numpy,pillow python \
       scripts/make_zoom_story.py
"""
import json
import os
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERIFY = os.path.join(ROOT, "c", "magma", "raster", "verify")
TAPE = "scenario_hold_dig_dense_20260725T031854Z"
GOLD_DIR = os.path.join(VERIFY, "tapes", f"{TAPE}_frames")
MAGMA_NPY = os.path.join(VERIFY, "trace", "out", f"tape_{TAPE}",
                         "magma_frames.npy")
MAGMA_TICKS = os.path.join(VERIFY, "trace", "out", f"tape_{TAPE}",
                           "magma_frames.ticks.npy")
OBS = "/home/infatoshi/dev/nw/.tmp/batchobs"
W, H, FPS = 1920, 960, 30
A_END, B_END, C_END, D_END, TOTAL = 50, 108, 168, 518, 548
TICK0 = 0                       # dig tape is dense from t=0


def game_frame(img854):
    """854x480 -> center 2:1 crop -> 1920x960."""
    a = np.asarray(img854)[26:453]
    return Image.fromarray(a).resize((W, H), Image.LANCZOS)


def main():
    meta = json.load(open(os.path.join(OBS, "meta.json")))
    cols, rows = meta["cols"], meta["rows"]
    cw, ch = meta["cam_w"], meta["cam_h"]
    mosaic = np.load(os.path.join(OBS, "mosaic.npy"), mmap_mode="r")
    F = mosaic.shape[0]
    mw, mh = cols * cw, rows * ch

    magma = np.load(MAGMA_NPY, mmap_mode="r")
    ticks = list(np.load(MAGMA_TICKS))
    tick_ix = {int(t): i for i, t in enumerate(ticks)}

    # pick the liveliest bright center-region env and swap it to dead center
    cr, cc = rows // 2, cols // 2
    best, best_v = (cr, cc), -1.0
    for r in range(cr - 6, cr + 7):
        for c in range(cc - 6, cc + 7):
            tile = np.asarray(
                mosaic[:120, r * ch:(r + 1) * ch, c * cw:(c + 1) * cw],
                dtype=np.int16)
            lum = tile.mean()
            motion = np.abs(np.diff(tile[::10], axis=0)).mean()
            if lum > 70 and motion + lum / 50 > best_v:
                best, best_v = (r, c), motion + lum / 50
    swap = np.empty((F, ch, cw, 3), np.uint8)
    br, bc = best
    if best != (cr, cc):
        swap[:] = mosaic[:, br * ch:(br + 1) * ch, bc * cw:(bc + 1) * cw]
    print(f"center env: slot {best} (score {best_v:.1f}) swapped to "
          f"({cr},{cc})")

    def mosaic_frame(d):
        m = np.asarray(mosaic[min(d, F - 1)])
        if best != (cr, cc):
            m = m.copy()
            a = m[cr * ch:(cr + 1) * ch, cc * cw:(cc + 1) * cw].copy()
            m[cr * ch:(cr + 1) * ch, cc * cw:(cc + 1) * cw] = \
                swap[min(d, F - 1)]
            m[br * ch:(br + 1) * ch, bc * cw:(bc + 1) * cw] = a
        return m

    ax, ay = (cc + 0.5) * cw, (cr + 0.5) * ch
    w0, w1 = float(cw), float(mw)
    font = ImageFont.truetype(
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 96)

    enc = subprocess.Popen(
        ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264",
         "-crf", "18", "-pix_fmt", "yuv420p",
         os.path.join(ROOT, "demos", "zoom_8192_story.mp4")],
        stdin=subprocess.PIPE)

    def obs_band(d):
        m = mosaic_frame(d)
        band = m[cr * ch + 2:cr * ch + 34, cc * cw:(cc + 1) * cw]
        return Image.fromarray(band).resize((W, H), Image.NEAREST)

    for f in range(TOTAL):
        tick = TICK0 + f
        if f < C_END and tick in tick_ix:
            gold = game_frame(Image.open(
                os.path.join(GOLD_DIR, f"f_{tick:06d}.png")).convert("RGB"))
            mag = game_frame(Image.fromarray(
                np.asarray(magma[tick_ix[tick]])[..., :3]))
        if f < A_END:
            frame = gold
        elif f < B_END:
            x = int(W * (f - A_END + 1) / (B_END - A_END))
            frame = gold.copy()
            frame.paste(mag.crop((0, 0, x, H)), (0, 0))
            d = ImageDraw.Draw(frame)
            d.rectangle([x - 2, 0, x + 2, H], fill=(245, 240, 235))
        elif f < C_END:
            x = int(W * (f - B_END + 1) / 30)
            frame = mag.copy()
            if x < W:
                frame.paste(obs_band(f - B_END).crop((0, 0, min(x, W), H)),
                            (0, 0))
                d = ImageDraw.Draw(frame)
                d.rectangle([x - 2, 0, x + 2, H], fill=(245, 240, 235))
            else:
                frame = obs_band(f - B_END)
        else:
            d_i = f - B_END              # obs stream continues from wipe C
            t = min((f - C_END) / (D_END - C_END - 1), 1.0)
            s = t * t * (3 - 2 * t)
            vw = w0 * (w1 / w0) ** s
            vh = min(vw / 2, float(mh))   # 96x85 grid is 0.4% shy of 2:1
            x0 = float(np.clip(ax - vw / 2, 0, mw - vw))
            y0 = float(np.clip(ay - vh / 2, 0, mh - vh))
            m = mosaic_frame(d_i)
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
