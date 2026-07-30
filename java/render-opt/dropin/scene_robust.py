# Robust scene: long socket timeout + per-command try/except so a sabotage-induced
# stall on one command does not abort the whole setup. Runs the FULL recipe (do +
# fluid + ticks) -- that recipe is what clears the GuiDownloadTerrain screen so the
# client actually renders the world -- then settles. Mirrors scene_setup.py.
import sys
sys.path.insert(0, "/home/infatoshi/dev/minecraft/mc-1.11.2-env/java")
from qrl_client import QRLEnv
e = QRLEnv(); e.s.settimeout(120)
print("reset ok:", e.reset({"seed": 0, "mode": "creative"}).get("ok"))
def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)
for c in ["/gamerule doDaylightCycle false", "/time set 6000",
          "/gamerule doWeatherCycle false", "/weather clear 1000000"]:
    print(c, safe(e.do, c))
print("fluid", safe(e.fluid, "water", 6))
for _ in range(60): safe(e.step, {"yaw": 0, "pitch": 0})
print("obs", (safe(e.obs) or {}))
print("SCENE_READY")
