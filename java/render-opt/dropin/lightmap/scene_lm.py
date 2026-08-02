from pathlib import Path
# Deterministic low-animation scene for the lightmap heavy-buffer drop-in.
# The lightmap is a GLOBAL lighting table, so any view shows its effect. To keep the
# native-vs-off noise floor low we look at STATIC terrain (not flowing water, not
# cloud-animated sky): fluid() only pushes the client past GuiDownloadTerrain, then we
# pitch DOWN at the ground and settle a fixed tick count. Fixed noon, no day/weather cycle.
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from qrl_client import NetheriteEnv
e = NetheriteEnv(); e.s.settimeout(120)
def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)
print("reset ok:", e.reset({"seed": 0, "mode": "creative"}).get("ok"))
for c in ["/gamerule doDaylightCycle false", "/time set 6000",
          "/gamerule doWeatherCycle false", "/weather clear 1000000"]:
    print(c, safe(e.do, c))
print("fluid", safe(e.fluid, "water", 4))
print("kill", safe(e.do, "/kill @e[type=!Player]"))
# pitch down ~45deg to fill the frame with static lit terrain (lightmap drives its shading)
for _ in range(3): safe(e.step, {"pitch": 1})
for _ in range(30): safe(e.step, {})
o = safe(e.obs) or {}
print("pitch now:", o.get("pitch"))
print("SCENE_READY")
