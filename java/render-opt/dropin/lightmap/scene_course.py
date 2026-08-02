from pathlib import Path
# Real-scene variant: the user's superflat survival "course" world (seed 0, type flat).
# fluid() is the proven way to push the headless client past GuiDownloadTerrain into a
# real render; we then CLEAR the water (low animation noise) and settle at a FIXED
# viewpoint so framing is byte-identical across off/native/sabotage.
# SCENE_TIME picks noon (6000) or midnight (18000).
import os, sys, time
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from qrl_client import NetheriteEnv

TIMEVAL = os.environ.get("SCENE_TIME", "6000")
VIEW = "tp @p 533 5 22 180 28"   # south of the station row, looking north + down

e = NetheriteEnv(); e.s.settimeout(150)
def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)
def run(cmds): return safe(e._cmd, {"cmd": "runcmds", "action": {"cmds": cmds}})

ok = False
for _ in range(20):
    r = safe(e.reset, {"seed": 0, "mode": "survival", "type": "flat"})
    if isinstance(r, dict) and r.get("ok"): ok = True; break
    time.sleep(3)
print("reset ok:", ok)

run(["gamerule doDaylightCycle false", "time set " + TIMEVAL,
     "gamerule doWeatherCycle false", "weather clear 1000000", "kill @e[type=!Player]"])
# push past GuiDownloadTerrain (proven), then remove the water so the view is dry+static
print("fluid:", safe(e.fluid, "water", 4))
for _ in range(10): safe(e.step, {})
run(["fill 505 3 -6 535 9 20 air", "fill 505 3 -6 535 3 20 minecraft:grass",
     "time set " + TIMEVAL])
# deterministic final framing + settle
run([VIEW])
for _ in range(25): safe(e.step, {})
o = safe(e.obs) or {}
print("pos:", o.get("x"), o.get("y"), o.get("z"), "pitch:", o.get("pitch"))
print("SCENE_READY")
