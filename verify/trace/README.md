# trace - oracle-vs-magma trace/replay verification flywheel

> Verification entry point: `magma/VERIFY.md`. This dir implements the
> computer loop (checkpoints/trajectory/spawns) and the human tape replay
> (`replay_tape.py`).

Start the REAL Java Minecraft 1.11.2 oracle and the magma C game at the EXACT
same pixels (same seed-0 worldgen, same feet pose, same 854x480 FOV-70 camera)
and measure pixel divergence at scripted checkpoints and over time on a fixed
input trajectory; also record oracle mob spawns as ground truth for future
spawn parity. First real report: `report/` (committed). Raw artifacts land in
`out/` (gitignored).

## One entry point

```bash
bash run_trace.sh [--wipe] [--seed N] checkpoints|trajectory|spawns|all
```

`run_trace.sh` probes the qrl bridge (127.0.0.1:25575) with a real socket
connect, launches the headless stack (`java/start_vnc_client.sh`, Xvfb :1,
llvmpipe SW GL) if it is down, waits up to 420 s for the world, and builds
`magma_game` if missing. `--wipe` deletes `saves/qrl_<seed>` and restarts the
game first - do this whenever a previous session mutated the world (a stale
save is the #1 source of bogus diffs; the pinned GUI-capture platform polluted
one report before this flag existed).

## Sub-commands

- **checkpoints** (`checkpoints.py` + `checkpoints.json`): for each named pose
  (feet x/y/z, MC yaw/pitch, world time) teleport-and-pin the oracle there,
  grab its frame with the qrl `frame` command (in-process framebuffer -> PNG),
  render the identical pose in magma headless (`--script` with
  `set_time`+`set_pose`, frame after 2 ticks), and diff with
  `render-opt/wholeframe/diff_frame.py` over whole | terrain-crop | HUD-strip.
  Report: `report/checkpoints.md` (+ side-by-side and heat-map PNGs).
- **trajectory** (`trajectory.py`): both games replay the SAME 200-tick input
  tape from the same pinned start (forward x60, 90 deg right turn as 6x15 deg
  qrl yaw steps -> magma `dyaw:15`, forward x60, jumps at t=130/140/150,
  stand). Oracle obs recorded per tick; magma `--state-out` per tick. Outputs
  the per-tick position-divergence CSV + curve and frame-pair pixel diffs every
  20 ticks (each side at its OWN simulated pose - physics drift included, that
  is the point). Report: `report/trajectory.md`.
- **spawns** (`spawns.py`): frozen midnight, `doMobSpawning` on, player parked
  in creative at spawn for 2400 ticks; records the nearest-8 `entities` obs per
  tick and the FULL loaded-entity list (qrl `getentities`) every 20 ticks.
  Ground-truth traces in `out/spawns/*.jsonl`, summary in `report/spawns.md`.
  magma is NOT asked to reproduce spawns yet.

## Adding a checkpoint

Append to `checkpoints.json`: `name`, feet `x/y/z`, MC `yaw/pitch` (yaw 180
faces -Z, positive pitch looks down), optional `time` (world time, default noon
6000) and `note`. Pick GROUNDED poses (feet on the surface): both sides run
survival gravity, and the harness treats the oracle's SETTLED pose as
authoritative (re-read from obs after pinning, then used for the magma
render), so a floating pose would sag before the grab. Surface heights can be
read from the live world with the qrl `getblocks` command.

## Conventions and hard-won gotchas

- Pose passthrough: qrl obs / `tp` / magma `set_pose` all use FEET coords +
  MC yaw/pitch; the camera eye is 1.62 above feet on both sides. The
  MC-degrees -> magma-camera conversion lives in `game/view.h`
  (`magma_yaw = 180 - mc_yaw`); `game/frame_capture.c` now uses it (it
  previously used `yaw - 180`, which X-mirrors every view except yaw 180).
- Frame stabilization: after a teleport, llvmpipe takes tens of seconds to
  build chunk meshes and the builds can stall >1 s and resume. A single early
  grab silently returns sky where terrain belongs. `grab_stable_frame` keeps
  grabbing (re-pinning the pose, stepping a tick) until 3 consecutive grabs
  match under 0.2 mean/ch and >=8 s elapsed. Animated water/entities in view
  can keep a frame from ever "stabilizing"; the WARN path keeps the last grab.
- Kill worldgen animals (`killentities`) before captures: magma renders no
  entities, and a wandering sheep both inflates the diff and defeats
  stabilization.
- MC options pinned by earlier capture work and relied on here (run/options.txt):
  `renderDistance:8` (== magma `--view-distance 8`), `fancyGraphics:false`,
  `ao:0`, `gamma:0.0`, `renderClouds:false`, `fov:0.0` (70 deg).
- The qrl `close` command exits the whole client, not just the world. To wipe
  a save you must kill the game, delete the save, and relaunch (`--wipe`).
- Known irreducible-for-now diff sources (measured in report/checkpoints.md):
  magma headless draws the hand but no hotbar/hearts HUD and no crosshair;
  MC night applies its darkened lightmap to terrain while magma night terrain
  stays near day-bright (biggest terrain divergence, mean ~49/ch); under-canopy
  smooth lighting differs; magma sky is a flat gradient without MC's horizon
  band.
