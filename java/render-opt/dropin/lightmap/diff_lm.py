from pathlib import Path
# Pixel-diff the three qlm_mode frames against the vanilla (off) baseline.
# native-vs-off should be SMALL (native lightmap is bit-identical to vanilla per kernel 11;
# residual is cross-launch torchFlickerX/animation noise). sabotage-vs-off should be LARGE
# (lightmapColors forced to 0xFF000000 -> global darkening, visibly wrong).
import numpy as np
from PIL import Image

D = str(Path(__file__).resolve().parent) + "/"
def load(n):
    return np.asarray(Image.open(D + n).convert("RGB")).astype(np.int16)

off = load("frame_off.png")
def diff(name):
    a = load(name)
    if a.shape != off.shape:
        return f"shape {a.shape} != off {off.shape}"
    d = np.abs(a - off)
    maxd = int(d.max())
    per_pixel = d.max(axis=2)
    differ = int((per_pixel > 0).sum())
    total = per_pixel.size
    pct = 100.0 * differ / total
    meand = float(d.mean())
    return f"max_abs_per_channel={maxd}  mean_abs={meand:.3f}  differing_pixels={differ}/{total} ({pct:.2f}%)"

print("native   vs off:", diff("frame_native.png"))
print("sabotage vs off:", diff("frame_sabotage.png"))
