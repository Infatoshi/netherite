from pathlib import Path
# Nether checkpoint scene: cross-dim teleport (NetheriteMod "dim" op), then the proven
# lava-kick + built-platform recipe to get past GuiDownloadTerrain into a real nether
# render. Nether = 100% blocklight, so this exercises the no-skylight lightmap branch
# (kernel 11), nether light queries (14-16), and nether-block meshing (21-24) live.
# Requires determinism.pin_flicker (blocklight texels scale by the flicker RNG).
# Camera pitched steeply down: the frame is the built platform only (glowstone,
# netherrack, quartz, soul sand are all STATIC textures; the portal that the vanilla
# Teleporter builds at arrival is animated and stays far out of frame).
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

# overworld -> nether (arrival ~ overworld spawn / 8, i.e. near (63, 0))
print("dim:", safe(e._cmd, {"cmd": "dim", "action": {"id": -1}}))
in_dim = False
for _ in range(60):
    safe(e.step, {}); time.sleep(0.1)
    o = safe(e.obs)
    if isinstance(o, dict) and o.get("dim") == -1: in_dim = True; break
print("in nether:", in_dim)
if not in_dim:
    print("FATAL: dim change never landed"); sys.exit(1)

# GET OUT OF THE ARRIVAL PORTAL IMMEDIATELY: the Teleporter builds a real portal and
# places the player inside it; standing there ~4s teleports them back to the overworld
# mid-scene. (8.5, 65, 8.5) is the measured deterministic nether ground at spawn.
run(["tp @p 8.5 65 8.5"])
# generous settle: chunk streaming right after a dim change races PlayerChunkMap
# (vanilla CME crash) if we hammer fills immediately
for _ in range(30): safe(e.step, {})
time.sleep(2)

# (no fluid render-kick: it was a workaround for the GuiDownloadTerrain hang, whose
# root cause - the Teleporter portal-vec NPE - is fixed in the NetheriteMod dim op; worse, the
# lava spawns at the player's feet and sets them ON FIRE, and the animated first-person
# flame overlay + damaged hearts destroy the pixel diff)
for _ in range(20): safe(e.step, {})

# fixed absolute platform high in the netherrack, near the arrival point (loaded
# chunks). /fill caps at 32768 blocks, so the air-clear is sliced into 8-high slabs.
# Arrival is NOT overworld/8: with no portal to place into, the player lands at the
# pinned dimension spawn (8.5, 65, 8.5) - deterministic, measured. Anchor there.
px, pz = 8, 8
def at(dx, y, dz): return "%d %d %d" % (px + dx, y, pz + dz)
fills = run(["fill %s %s air" % (at(-30, 88, -30), at(30, 95, 30)),
     "fill %s %s air" % (at(-30, 96, -30), at(30, 103, 30)),
     # the vanilla Teleporter builds an ANIMATED portal somewhere near arrival;
     # purge any portal blocks from the visible band so no animation reaches pixels
     "fill %s %s minecraft:air 0 replace minecraft:portal" % (at(-30, 80, -30), at(30, 87, 30)),
     "fill %s %s minecraft:netherrack" % (at(-30, 87, -30), at(30, 87, 30)),
     "fill %s %s minecraft:glowstone" % (at(-8, 87, -14), at(8, 87, -14)),
     "fill %s %s minecraft:quartz_ore" % (at(-4, 88, -10), at(-4, 90, -10)),
     "fill %s %s minecraft:soul_sand" % (at(2, 88, -10), at(4, 88, -8)),
     "fill %s %s minecraft:glowstone" % (at(-2, 92, -6), at(2, 92, -4)),
     "kill @e[type=!Player]"])
print("fills:", fills)
# assert the built geometry directly (fill can miss silently on an unloaded-chunk
# race, and air-clears legitimately report 0 in open caverns - counts can't tell):
check = run(["testforblock %s minecraft:netherrack" % at(0, 87, 0),
             "testforblock %s minecraft:glowstone" % at(0, 87, -14),
             "testforblock %s minecraft:quartz_ore" % at(-4, 89, -10),
             "testforblock %s minecraft:glowstone" % at(0, 92, -5),
             # air-clear slabs actually applied (normally solid netherrack here)
             "testforblock %s minecraft:air" % at(-25, 94, -25),
             "testforblock %s minecraft:air" % at(25, 99, 25)])
print("geometry check:", check)
if not isinstance(check, dict) or check.get("failed", 9) != 0:
    print("FATAL: platform geometry incomplete"); sys.exit(1)
START = "%d.5 88 %d.5" % (px, pz + 10)
run(["tp @p %s 180 70" % START])
for _ in range(40): safe(e.step, {})

# strict stability check: standing EXACTLY on the platform, not sinking/falling
for _ in range(10): safe(e.step, {})
o = safe(e.obs)
if not isinstance(o, dict): o = {}
print("nether pose:", o.get("x"), o.get("y"), o.get("z"),
      "yaw", o.get("yaw"), "pitch", o.get("pitch"),
      "dim", o.get("dim"), "dead", o.get("dead"), "vy", o.get("vy"))
if o.get("ok") and o.get("dim") == -1 \
        and abs(o.get("y", 0) - 88.0) < 0.05 \
        and abs(o.get("vy", 1)) < 0.09:
    print("SCENE_READY")
else:
    print("FATAL: nether scene not in position")
