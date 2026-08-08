from pathlib import Path
# Dragon-fight coverage probe (runs in-session after the End frame capture):
# fly the camera to the main island with the ender dragon + crystals in view, let the
# coverage hook sample the live render for a few hundred ticks, then assert that the
# dragon-fight render paths hit our VERIFIED kernels (entity limb anim 37 / model box
# gen 38 via EntityDragon, end-dim meshing, no-skylight lighting). The dragon cannot
# be pixel-pinned across launches (AI phases are RNG-driven), so its oracle proof is:
# per-kernel bitwise verification (already PASS) + this fired-kernels coverage check.
import sys, time

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from qrl_client import NetheriteEnv

COVLOG = str(Path(__file__).resolve().parents[3] / "render-opt/coverage.log")

e = None
for _ in range(12):
    try:
        e = NetheriteEnv(); break
    except OSError:
        time.sleep(5)
if e is None:
    print("DRAGON_COVERAGE FAIL (no bridge)"); sys.exit(1)
e.s.settimeout(60)

def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)

def run(cmds): return safe(e._cmd, {"cmd": "runcmds", "action": {"cmds": cmds}})

# perch above the obsidian arrival platform, facing the island center (yaw 90 = -x)
run(["tp @p 90.5 75 0.5 90 15"])
for i in range(300):
    safe(e.step, {})
    if i % 50 == 0: time.sleep(0.1)

o = safe(e.obs) or {}
ents = [x.get("type") for x in o.get("entities", [])]
print("entities in range:", ents)

time.sleep(3)  # let the 2s coverage flusher write
try:
    with open(COVLOG) as f:
        lines = f.read().splitlines()
except OSError as ex:
    print("DRAGON_COVERAGE FAIL (no coverage.log: %s)" % ex); sys.exit(1)

def has(method, name):
    return any(l.split("\t")[0] == method and name in l for l in lines if "\t" in l)

checks = [
    ("setRotationAngles", "EntityDragon",  "kernel 37 entity_limb_anim on the dragon"),
    ("TexturedQuad.draw", "EntityDragon",  "kernel 38 model_box_gen on the dragon"),
    ("renderQuadsSmooth", "end_stone",     "kernel 24 meshing on end terrain"),
    ("updateLightmap",    "lightmap",      "kernel 11 lightmap (end no-skylight)"),
]
ok = True
for method, name, why in checks:
    hit = has(method, name)
    ok &= hit
    print("%s %s/%s (%s)" % ("HIT " if hit else "MISS", method, name, why))
# crystals render as EnderCrystal entities when alive (informational, not a gate:
# they may already be destroyed in a reused end dimension)
print("info: EnderCrystal seen:", any("EnderCrystal" in l for l in lines))
print("DRAGON_COVERAGE", "PASS" if ok else "FAIL")
