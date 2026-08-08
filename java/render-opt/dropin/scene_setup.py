from pathlib import Path
# Phase C: deterministic flowing-water + fixed-sky scene via the NetheriteMod bridge, then
# settle a FIXED number of ticks (tick-synced, not wall-clock) so all three qsin_mode
# runs land on an identical frame. Run with: uv run --no-project python scene_setup.py
import sys, time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from qrl_client import NetheriteEnv

e = NetheriteEnv()
print("reset:", e.reset({"seed": 0, "mode": "creative"}))
# fixed sky + no daylight cycle + no weather, kill clouds-side animation drivers we can
for c in ["/gamerule doDaylightCycle false", "/time set 6000",
          "/gamerule doWeatherCycle false", "/weather clear 1000000"]:
    print(c, e.do(c))
# flowing water -> visible sin-driven fluid slope surface
print("fluid:", e.fluid("water", radius=6))
# settle deterministically: fixed tick count, no-op step (yaw/pitch 0)
for _ in range(40):
    e.step({"yaw": 0, "pitch": 0})
print("OBS:", e.obs())
print("SCENE_READY")
