from pathlib import Path
# Sky-dominated low-noise scene. fluid() reliably pushes the client past the
# terrain-download screen into a real render; /kill removes wandering-entity noise;
# pitching up ~60deg makes the sin/cos-driven sky+sun dominate the frame so the
# sabotage (sin=0) effect is not diluted by static terrain.
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from qrl_client import NetheriteEnv
e = NetheriteEnv(); e.s.settimeout(120)
def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)
print("reset ok:", e.reset({"seed": 0, "mode": "creative"}).get("ok"))
for c in ["/gamerule doDaylightCycle false", "/time set 6000",
          "/gamerule doWeatherCycle false", "/weather clear 1000000"]:
    print(c, safe(e.do, c))
print("fluid", safe(e.fluid, "water", 6))
print("kill", safe(e.do, "/kill @e[type=!Player]"))
for _ in range(4): safe(e.step, {"pitch": -1})   # look up ~60deg
for _ in range(25): safe(e.step, {"yaw": 0, "pitch": 0})
o = safe(e.obs) or {}
print("pitch now:", o.get("pitch"))
print("SCENE_READY")
