from pathlib import Path
# Pixel-diff the course-scene frames against the vanilla (off) baseline, for a given
# time-of-day. Metrics computed over the auto-detected rendered-window bbox (the game
# window is centered in a mostly-black 1280x720 desktop, which would otherwise dilute %).
import sys
import numpy as np
from PIL import Image

T = sys.argv[1] if len(sys.argv) > 1 else "6000"
D = str(Path(__file__).resolve().parent / "course") + "/"

def load(mode):
    return np.asarray(Image.open(f"{D}frame_{mode}_t{T}.png").convert("RGB")).astype(np.int16)

off = load("off")
# bbox of non-black content in the baseline = the rendered game window
mask = off.max(axis=2) > 8
ys, xs = np.where(mask)
y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
win = (slice(y0, y1), slice(x0, x1))
nwin = (y1 - y0) * (x1 - x0)
print(f"time={T}  window bbox rows[{y0}:{y1}] cols[{x0}:{x1}]  ({nwin} px)")

def diff(mode):
    a = load(mode)
    if a.shape != off.shape:
        return f"{mode}: shape {a.shape} != off {off.shape}"
    d = np.abs(a - off)[win]
    per = d.max(axis=2)
    differ = int((per > 0).sum())
    return (f"{mode:8s} vs off: max_abs/ch={int(d.max()):3d}  mean_abs={d.mean():6.3f}  "
            f"differing={differ}/{per.size} ({100.0*differ/per.size:5.2f}%)")

print(diff("native"))
print(diff("sabotage"))
