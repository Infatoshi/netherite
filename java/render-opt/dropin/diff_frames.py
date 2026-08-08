from pathlib import Path
# Phase D: pixel-diff the three qsin_mode frames against the vanilla baseline.
# Reports max abs per-channel diff and % of differing pixels.
import numpy as np
from PIL import Image

D = str(Path(__file__).resolve().parent) + "/"
def load(n):
    return np.asarray(Image.open(D+n).convert("RGB")).astype(np.int16)

off = load("frame_off.png")
def diff(name):
    a = load(name)
    if a.shape != off.shape:
        return f"shape {a.shape} != off {off.shape}"
    d = np.abs(a - off)
    maxd = int(d.max())
    per_pixel = d.max(axis=2)         # max channel diff per pixel
    differ = int((per_pixel > 0).sum())
    total = per_pixel.size
    pct = 100.0 * differ / total
    return f"max_abs_per_channel={maxd}  differing_pixels={differ}/{total} ({pct:.2f}%)"

print("native vs off  :", diff("frame_native.png"))
print("sabotage vs off:", diff("frame_sabotage.png"))
