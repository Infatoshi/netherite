from pathlib import Path
# Post-capture behavioral check: death is an OBSERVATION, not a screen.
# kill @p -> obs must report deaths incremented and the player alive again
# (auto-respawn), and no GuiGameOver may have opened (strip.menus blocks it).
import sys, time

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from qrl_client import NetheriteEnv

e = None
for _ in range(12):
    try:
        e = NetheriteEnv(); break
    except OSError:
        time.sleep(5)
if e is None:
    print("DEATH_CHECK FAIL (no bridge)"); sys.exit(1)
e.s.settimeout(60)

def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)

before = safe(e.obs) or {}
d0 = before.get("deaths", 0)
safe(e._cmd, {"cmd": "runcmds", "action": {"cmds": ["kill @p"]}})
ok = False
for _ in range(40):  # give the kill + auto-respawn a few ticks
    safe(e.step, {})
    o = safe(e.obs) or {}
    if isinstance(o, dict) and o.get("deaths", 0) > d0 and not o.get("dead", True) \
            and o.get("health", 0) > 0:
        ok = True
        print("death hook: deaths %s -> %s, respawned health %s" %
              (d0, o.get("deaths"), o.get("health")))
        break
    time.sleep(0.2)
print("DEATH_CHECK", "PASS" if ok else "FAIL")
