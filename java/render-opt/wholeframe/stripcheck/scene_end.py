from pathlib import Path
# End checkpoint scene: cross-dim teleport to the End (dim 1). The vanilla arrival
# obsidian platform is at FIXED coords (100, 49, 0), so the End is naturally
# capture-friendly. The pixel-diff platform is built far (+300x) from the main island:
# the dragon's AI circles the pillars near (0,0) and cannot enter the frame (camera
# faces +x, away). End stone + obsidian are static; the end sky is a static texture.
# Exercises the End's fixed-skylight lighting model + end-block meshing live.
# The dragon itself is verified separately by dragon_coverage.py (its flight is
# RNG-driven and cannot be pinned across launches).
import sys, time

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from qrl_client import NetheriteEnv

e = None
for _ in range(60):
    try:
        e = NetheriteEnv(); break
    except OSError:
        time.sleep(5)
if e is None:
    print("FATAL: NetheriteMod bridge never came up"); sys.exit(1)
e.s.settimeout(180)

def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)

def run(cmds): return safe(e._cmd, {"cmd": "runcmds", "action": {"cmds": cmds}})

ready = False
for _ in range(90):
    o = safe(e.obs)
    if isinstance(o, dict) and o.get("ok"): ready = True; break
    time.sleep(2)
print("world ready:", ready)

print("dim:", safe(e._cmd, {"cmd": "dim", "action": {"id": 1}}))
in_dim = False
for _ in range(60):
    safe(e.step, {}); time.sleep(0.1)
    o = safe(e.obs)
    if isinstance(o, dict) and o.get("dim") == 1: in_dim = True; break
print("in end:", in_dim)
if not in_dim:
    print("FATAL: dim change never landed"); sys.exit(1)

# (no fluid render-kick: obsolete since the Teleporter portal-vec NPE fix, and the
# lava ignites the player - the burning overlay + hurt hearts ruin the pixel diff)
# generous settle: chunk streaming right after a dim change races PlayerChunkMap
for _ in range(30): safe(e.step, {})
time.sleep(2)

# platform DIRECTLY ABOVE the fixed arrival platform (100,49,0): those chunks are
# guaranteed streamed after the settle ((132,0) raced chunk load and the fill partially
# failed -> player sank through). Camera faces NORTH (yaw 180); the pillar ring,
# crystals, and the dragon's orbit are WEST (bearing ~90 deg off-view, outside fov 70).
px, pz = 100, 0
PLAT_Y = 80
def at(dx, y, dz): return "%d %d %d" % (px + dx, y, pz + dz)
fills = run(["fill %s %s minecraft:end_stone" % (at(-30, PLAT_Y, -30), at(30, PLAT_Y, 30)),
     "fill %s %s minecraft:obsidian" % (at(-6, PLAT_Y + 1, -14), at(6, PLAT_Y + 3, -14)),
     "fill %s %s minecraft:end_stone" % (at(-3, PLAT_Y + 1, -10), at(-3, PLAT_Y + 4, -10)),
     "fill %s %s minecraft:end_stone" % (at(3, PLAT_Y + 1, -10), at(3, PLAT_Y + 4, -10))])
print("fills:", fills)
check = run(["testforblock %s minecraft:end_stone" % at(0, PLAT_Y, 0),
             "testforblock %s minecraft:obsidian" % at(0, PLAT_Y + 2, -14),
             "testforblock %s minecraft:end_stone" % at(-3, PLAT_Y + 2, -10),
             "testforblock %s minecraft:end_stone" % at(3, PLAT_Y + 2, -10)])
print("geometry check:", check)
if not isinstance(check, dict) or check.get("failed", 9) != 0:
    print("FATAL: platform geometry incomplete"); sys.exit(1)
START = "%d.5 %d %d.5" % (px, PLAT_Y + 1, pz + 10)
run(["tp @p %s 180 30" % START])
for _ in range(40): safe(e.step, {})

# strict stability check: standing EXACTLY on the platform, not sinking/falling
# (a partially-failed fill once let the player sink through at y-0.08 and a loose
# +-2 check called it ready)
for _ in range(10): safe(e.step, {})
o = safe(e.obs)
if not isinstance(o, dict): o = {}
print("end pose:", o.get("x"), o.get("y"), o.get("z"),
      "yaw", o.get("yaw"), "pitch", o.get("pitch"),
      "dim", o.get("dim"), "dead", o.get("dead"), "vy", o.get("vy"))
if o.get("ok") and o.get("dim") == 1 \
        and abs(o.get("y", 0) - (PLAT_Y + 1)) < 0.05 \
        and abs(o.get("vy", 1)) < 0.09:
    print("SCENE_READY")
else:
    print("FATAL: end scene not in position")
