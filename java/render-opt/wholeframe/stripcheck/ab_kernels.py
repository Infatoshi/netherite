from pathlib import Path
# Fast in-session kernel A/B: with the client ALREADY RUNNING and a scene set up
# (any scene_*.py), flip all four kernels off->native via the NetheriteMod "kmode" op and
# pixel-diff frames grabbed seconds apart on the SAME world state. Zero cross-launch
# noise by construction: same tick-frozen world, same entities, same chunks.
# The two-launch run_verify*.sh remains the final independent-process proof; this is
# the seconds-per-iteration tool for development.
#
#   uv run --no-project --with numpy --with pillow python ab_kernels.py [out_prefix]
#
# Requires: client launched with qao_mode "vanilla" or "native" (AO vanilla path live);
# no animated textures in frame (the platform scenes satisfy this).
import subprocess, sys, time

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from qrl_client import NetheriteEnv

HERE = str(Path(__file__).resolve().parent)
PREFIX = sys.argv[1] if len(sys.argv) > 1 else "ab"

e = NetheriteEnv(); e.s.settimeout(120)

def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)

def kmode(mode):
    ao = "vanilla" if mode == "off" else mode   # AO mode 0 baseline is "vanilla"
    r = safe(e._cmd, {"cmd": "kmode", "action":
                      {"sin": mode, "lm": mode, "biome": mode, "ao": ao}})
    print("kmode", mode, "->", r)
    return isinstance(r, dict) and r.get("ok")

def grab(path):
    # settle: loadRenderers() rebuilt every chunk mesh; give the worker threads time
    for _ in range(40): safe(e.step, {})
    time.sleep(4)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-f", "x11grab",
                    "-video_size", "1280x720", "-i", ":1", "-frames:v", "1", "-y", path],
                   env={"DISPLAY": ":1", "PATH": "/usr/bin:/bin"}, check=True)
    print("grabbed", path)

# purge time-animated entities (dropped items spin continuously; the two grabs are
# seconds apart in ONE session, so any animated entity in frame is a false positive)
safe(e._cmd, {"cmd": "runcmds", "action": {"cmds": ["kill @e[type=!Player]"]}})

frames = {}
for mode in ("off", "native"):
    if not kmode(mode):
        print("AB FAIL: kmode rejected"); sys.exit(1)
    frames[mode] = "%s/frame_%s_%s.png" % (HERE, PREFIX, mode)
    grab(frames[mode])

import numpy as np
from PIL import Image
a = np.asarray(Image.open(frames["off"]).convert("RGB")).astype(int)
b = np.asarray(Image.open(frames["native"]).convert("RGB")).astype(int)
d = np.abs(a - b)
n = int((d.sum(axis=2) > 0).sum())
print("AB RESULT: max/ch=%d mean=%.4f differ=%d/%d (%.2f%%)"
      % (d.max(), d.mean(), n, d.shape[0] * d.shape[1],
         100.0 * n / (d.shape[0] * d.shape[1])))
print("AB", "PASS (bit-identical)" if n == 0 else "DIFF - inspect frames")
