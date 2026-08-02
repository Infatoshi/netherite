from pathlib import Path
# Pixel-diff the AO drop-in frames vs the vanilla baseline, over the auto-detected
# rendered-window bbox. native-vs-vanilla should be ~0 (kernel 13 is bit-exact, so the C
# kernel produces the same packed AO brightness as the vanilla method); sabotage-vs-vanilla
# should be large (AO brightness forced to 0 -> smooth-lit geometry goes dark).
import numpy as np
from PIL import Image
D = str(Path(__file__).resolve().parent) + "/"
def load(n): return np.asarray(Image.open(D + n).convert("RGB")).astype(np.int16)
base = load("frame_vanilla.png")
mask = base.max(axis=2) > 8
ys, xs = np.where(mask)
y0, y1, x0, x1 = ys.min(), ys.max()+1, xs.min(), xs.max()+1
win = (slice(y0, y1), slice(x0, x1))
print(f"window bbox rows[{y0}:{y1}] cols[{x0}:{x1}]")
def diff(n):
    a = load(n)
    if a.shape != base.shape: return f"{n}: shape {a.shape} != {base.shape}"
    d = np.abs(a - base)[win]; per = d.max(axis=2); differ = int((per>0).sum())
    return (f"{n:22s} max/ch={int(d.max()):3d} mean={d.mean():6.3f} "
            f"differ={differ}/{per.size} ({100.0*differ/per.size:5.2f}%)")
print(diff("frame_native.png"))
print(diff("frame_sabotage.png"))
