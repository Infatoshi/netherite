from pathlib import Path
# Re-assert the FIXED viewpoint and tick in place to keep the world rendered (used by
# capture_course.sh as a retry if the first grab caught the loading screen). Final state
# is identical to scene_course.py's so framing matches across modes.
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from qrl_client import NetheriteEnv
VIEW = "tp @p 533 5 22 180 28"
e = NetheriteEnv(); e.s.settimeout(150)
def safe(fn, *a):
    try: return fn(*a)
    except Exception as ex: return "ERR:" + str(ex)
for _ in range(10): safe(e.step, {"forward": 1})
for _ in range(10): safe(e.step, {"back": 1})
safe(e._cmd, {"cmd": "runcmds", "action": {"cmds": [VIEW]}})
for _ in range(25): safe(e.step, {})
print("PUSH_DONE")
