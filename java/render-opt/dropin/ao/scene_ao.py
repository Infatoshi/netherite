from pathlib import Path
# Controlled AO scene for the getAoBrightness drop-in (kernel 13). All qao AO modes disable
# Forge's light pipeline so the VANILLA smooth-AO path is live. We need (a) peaceful difficulty
# so no mobs spawn/move (otherwise native-vs-vanilla shows motion noise, not parity), and (b)
# skylit faces with 3D relief so AO is visible: a lit grass platform high in clear sky with a
# grid of 3-tall stone pillars, viewed at an angle. native == vanilla; sabotage removes the AO
# darkening around the pillar bases (and dims skylit faces).
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
run(["difficulty peaceful", "gamerule doMobSpawning false", "kill @e[type=!Player]",
     "gamerule doDaylightCycle false", "time set 6000",
     "gamerule doWeatherCycle false", "weather clear 1000000"])
print("fluid:", safe(e.fluid, "water", 3))   # push past GuiDownloadTerrain
for _ in range(10): safe(e.step, {})
o = safe(e.obs) or {}
sx, sy, sz = int(o.get("x", 0)), int(o.get("y", 64)), int(o.get("z", 0))
fy = sy + 30                                  # platform top y, high in clear sky
# lit grass platform with a clear air column above; a grid of 3-tall stone pillars for AO relief
cmds = [f"fill {sx-14} {fy-1} {sz-14} {sx+14} {fy-1} {sz+14} minecraft:grass",
        f"fill {sx-14} {fy} {sz-14} {sx+14} {fy+12} {sz+14} air"]
for dx in (-6, 0, 6):
    for dz in (2, 8, 14):
        cmds.append(f"fill {sx+dx} {fy} {sz+dz} {sx+dx} {fy+2} {sz+dz} minecraft:stone")
run(cmds)
# settle + force relight/re-mesh: kill any stragglers, set time again, wait
run(["kill @e[type=!Player]", "time set 6000"])
for _ in range(20): safe(e.step, {})
# camera: stand on platform behind the pillars, look forward (+z) and down at the relief
run([f"tp @p {sx}.5 {fy+1} {sz-5}.5 0 40", "time set 6000"])
for _ in range(25): safe(e.step, {})
o = safe(e.obs) or {}
print("pos:", o.get("x"), o.get("y"), o.get("z"), "pitch:", o.get("pitch"))
print("SCENE_READY")
