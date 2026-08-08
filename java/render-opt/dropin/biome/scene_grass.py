from pathlib import Path
# Controlled grass scene for the biome grass-blend drop-in (kernel 18). The default
# spawn is forest (foliage, not grass), and pitch-90 over flood-water shows no grass, so
# we build a dedicated lit grass platform and look down at it: native == vanilla grass,
# sabotage = magenta grass (unmistakable). Setup via runcmds (one tick) dodges the
# per-/command bridge timeouts.
import sys, time
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from qrl_client import NetheriteEnv
e = NetheriteEnv(); e.s.settimeout(150)
def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)
def run(cmds): return safe(e._cmd, {"cmd": "runcmds", "action": {"cmds": cmds}})

ok = False
for _ in range(20):
    r = safe(e.reset, {"seed": 0, "mode": "creative"})
    if isinstance(r, dict) and r.get("ok"): ok = True; break
    time.sleep(2)
print("reset ok:", ok)
run(["gamerule doDaylightCycle false", "time set 6000",
     "gamerule doWeatherCycle false", "weather clear 1000000", "kill @e[type=!Player]"])
print("fluid:", safe(e.fluid, "water", 3))   # push past GuiDownloadTerrain
for _ in range(10): safe(e.step, {})
o = safe(e.obs) or {}
sx, sy, sz = int(o.get("x", 0)), int(o.get("y", 64)), int(o.get("z", 0))
py = sy + 30
# lit grass platform above the loaded spawn chunk, player looking straight down at it
run([f"fill {sx-12} {py-1} {sz-12} {sx+12} {py-1} {sz+12} minecraft:grass",
     f"fill {sx-12} {py} {sz-12} {sx+12} {py+3} {sz+12} air",
     "time set 6000",
     f"tp @p {sx}.5 {py} {sz}.5 0 75"])
for _ in range(25): safe(e.step, {})
o = safe(e.obs) or {}
print("pos:", o.get("x"), o.get("y"), o.get("z"), "pitch:", o.get("pitch"))
print("SCENE_READY")
