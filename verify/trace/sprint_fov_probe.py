#!/usr/bin/env python3
"""sprint_fov_probe.py - record an oracle tape that exercises the sprint FOV pump.

Scene: a flat stone runway ending in a wall. The bot sprints down the runway
(FOV eases 70 -> 80.5), slams into the wall (collidedHorizontally cancels
sprint; the held sprint key re-triggers it next tick -> the FOV "pump"), rests,
and repeats. The tape is then replayed through magma and pixel-matched; the
FOV zoom must appear in both.

Usage (game live with the qrl bridge up):
  uv run --no-project --with pyarrow python sprint_fov_probe.py
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import oracle_lib as ol  # noqa: E402

# Floating arena directly above the seed-0 spawn area (chunks there are always
# loaded, so fill/setblock work): textured floor + wall so the FOV zoom is
# visible against parallax, sky behind stays cheap.
X0, Y, Z0 = 40, 120, 156   # arena min corner (floor at Y-1)
X1, Z1 = 48, 178           # arena max corner
WALL_Z = 178
START = (44.5, float(Y), 157.5, 0.0, 10.0)  # x y z yaw pitch; yaw 0 faces +Z


def build_arena(env):
    y = Y - 1
    cmds = [
        f"fill {X0} {y} {Z0} {X1} {y} {Z1} stone 0",                    # floor
        f"fill {X0} {Y} {WALL_Z} {X1} {Y + 3} {WALL_Z} planks 0",       # wall
        f"fill {X0} {Y} {Z0} {X0} {Y + 3} {WALL_Z} cobblestone 0",      # side
        f"fill {X1} {Y} {Z0} {X1} {Y + 3} {WALL_Z} cobblestone 0",      # side
        f"fill {X0 + 1} {Y} {Z0 + 1} {X1 - 1} {Y + 3} {WALL_Z - 1} air 0",
    ]
    # vanilla fill reports failure when 0 blocks changed (idempotent rerun),
    # so per-command failures are informational; hold_pose falls if no floor.
    for c in cmds:
        print(ol.runcmds(env, [c]), "<-", c)


PACE = 0.06  # s/tick while recording: the renderer must produce a fresh frame
             # between steps or the recorder tapes stale duplicates


def drive(env):
    def run(ticks, **fields):
        for _ in range(ticks):
            env.step(fields)
            time.sleep(PACE)

    run(20)                                  # baseline standstill
    for _ in range(3):                       # 3 pump cycles
        run(70, forward=1, sprint=1)         # sprint ~14 blocks, then pump on wall
        run(25)                              # release: FOV eases back to 70
    run(40, forward=1)                       # plain walk against wall (no zoom)
    run(20)


def main():
    if not ol.bridge_up():
        raise SystemExit("qrl bridge not up on 25575 (launch Run A/B first)")
    env = ol.connect()
    try:
        ol.ensure_world(env, seed=0, mode="survival")
        ol.freeze_scene(env, world_time=6000, gamemode="survival")
        ol.hold_pose(env, *START, settle_ticks=40)  # load arena chunks first
        build_arena(env)
        time.sleep(11)  # let fill feedback chat lines expire before taping
        ol.hold_pose(env, *START)
        uv = ["uv", "run", "--no-project", "--with", "pyarrow", "python",
              os.path.join(HERE, "tape.py")]
        env.close()  # single-client bridge: free it for tape.py's connection
        subprocess.run(uv + ["start", "--frames-every", "2"], check=True)
        env = ol.connect()
        try:
            drive(env)
        finally:
            env.close()
            time.sleep(0.3)
            subprocess.run(uv + ["stop"], check=True)
            env = ol.connect()
    finally:
        env.close()


if __name__ == "__main__":
    main()
