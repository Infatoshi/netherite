"""Encode oracle|magma side-by-side MP4 from dense-frame combat scenario tapes."""
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path("/home/infatoshi/dev/netherite")
TAPES = ROOT / "c/magma/raster/verify/tapes"
OUT_TRACE = ROOT / "c/magma/raster/verify/trace/out"
OUT_MP4 = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "demos/combat_sbs.mp4"
NAMES = sys.argv[2:]  # tape stem names

def load_rgb(p):
    return np.asarray(Image.open(p).convert("RGB"))

def label_bar(w, text, h=36):
    im = Image.new("RGB", (w, h), (20, 16, 14))
    ImageDraw.Draw(im).text((8, 8), text, fill=(220, 200, 180))
    return np.asarray(im)

frames = []
W, H = 854 * 2, 480 + 36
title = Image.new("RGB", (W, H), (12, 10, 9))
d = ImageDraw.Draw(title)
msg = (
    "netherite combat scenario demo\n"
    "LEFT = real Java MC 1.11.2 oracle    RIGHT = magma C port\n"
    "Scripted scenarios, replayed tick-exact from oracle input tapes\n"
    "8/8 scenario+canonical tapes: physics clean (1e-9), pixel gates PASS\n"
    "blaze bow fight  |  ender dragon from The End entry"
)
y = 70
for line in msg.split("\n"):
    d.text((40, y), line, fill=(230, 210, 190))
    y += 42
for _ in range(30):
    frames.append(np.asarray(title))

for name in NAMES:
    fdir = TAPES / f"{name}_frames"
    fr_path = OUT_TRACE / f"tape_{name}/magma_frames.npy"
    tk_path = OUT_TRACE / f"tape_{name}/magma_frames.ticks.npy"
    if not (fdir.exists() and fr_path.exists() and tk_path.exists()):
        raise SystemExit(f"missing frames for {name}: {fdir} {fr_path}")
    fr = np.load(fr_path)
    tk = np.load(tk_path)
    label = name.replace("scenario_", "").split("_2026")[0]
    for i, t in enumerate(tk):
        p = fdir / f"f_{int(t):06d}.png"
        if not p.exists():
            continue
        o = load_rgb(p)
        c = fr[i][..., :3]
        if o.shape != c.shape:
            o = np.asarray(Image.fromarray(o).resize((c.shape[1], c.shape[0])))
        sheet = np.concatenate([o, c], axis=1)
        bar = label_bar(sheet.shape[1], f"ORACLE | MAGMA  -  {label}  t={int(t)}")
        frames.append(np.concatenate([bar, sheet], axis=0))

h = max(f.shape[0] for f in frames)
w = max(f.shape[1] for f in frames)
w += w % 2
h += h % 2

def fit(f):
    o = np.zeros((h, w, 3), dtype=np.uint8)
    o[: f.shape[0], : f.shape[1]] = f[:h, :w]
    return o

ff = subprocess.Popen(
    ["ffmpeg", "-y", "-loglevel", "error",
     "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{w}x{h}",
     "-r", "10", "-i", "-",
     "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", str(OUT_MP4)],
    stdin=subprocess.PIPE,
)
for f in frames:
    ff.stdin.write(fit(f).tobytes())
ff.stdin.close()
if ff.wait() != 0:
    raise SystemExit("ffmpeg failed")
print(f"wrote {OUT_MP4} ({len(frames)} frames @ 10fps)")
