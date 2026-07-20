# Tick-Trace Divergence Oracle

> Verification entry point: `c/craster/VERIFY.md`. Day-to-day input replay is
> `raster/verify/trace/replay_tape.py`; this dir keeps the headless physics-only
> C tracer (`trace_game`) it superseded.

Run a **fixed action tape** through BOTH the real Java Minecraft 1.11.2 game (ground
truth) and the craster C game, capture per-tick state, and find the **first tick where
they diverge** in physics and in pixels - while staying disk-efficient (never a frame
per tick; two small CSVs per run, frames materialized only at a divergence).

This is the bug-finding flywheel: the C reimplementation is driven by the SAME discrete
action space (qrl) as the Java game, so any behavioural gap shows up as a concrete
tick + field + magnitude.

## Files

- `gen_tape.py` - write a deterministic action tape (seeded pseudo-random walk).
- `trace_java.py` - replay the tape through the REAL game via the qrl bridge -> `java_phys.csv`.
- `../app/trace_main.c` + `build_c_tracer.sh` - the headless C tracer (`../trace_game`) -> `c_phys.csv`.
- `diff_trace.py` - align the two CSVs, report the first divergence + per-field summary,
  and (on `--materialize`) dump only the frames around the divergence tick.
- `out/` - all artifacts (gitignore-able): `tape.txt`, `c_phys.csv`, `java_phys.csv`,
  `diverge_<tick>/`.

## Action tape format

Plain text, one line per tick, whitespace-separated integers in the qrl action order:

```
forward back left right jump sneak sprint attack use yaw pitch
```

`forward/back/left/right/jump/sneak/sprint/attack/use` in `{0,1}`; `yaw/pitch` in
`{-1,0,1}` = 15-degree quantum aim steps (matches `java/qrl_client.py` and
`QuantizedRL.applyAction`). `#` comments and blank lines are ignored. Both tracers
consume the exact same file, so the input is identical on both sides.

## Two artifacts per side

Each tracer now emits BOTH:

1. **Legacy physics CSV** (`c_phys.csv` / `java_phys.csv`) - unchanged, for back-compat with
   `frame_oracle.py` / `world_diff.py`:
   ```
   tick,x,y,z,yaw,pitch,vx,vy,vz,on_ground,health,food,air,frame_hash
   ```
2. **Full STATE VECTOR JSONL** (`c_state.jsonl` / `java_state.jsonl`) - the per-tick, per-feature
   diff target. One JSON object per tick, SAME schema on both sides.

## State-vector schema (`*_state.jsonl`, identical keys both sides)

One JSON object per line:

```json
{"tick":0,
 "player":{
   "x","y","z","yaw","pitch","vx","vy","vz","on_ground",
   "health","food","saturation","air","fire","xp_level","xp_frac","fall_distance",
   "sprinting","sneaking","jumping",
   "held_slot","held_id","held_count","held_meta",
   "attack_cooldown","hurt_time","death_time","dead","deaths","dim","potions"},
 "inventory":[{"slot","id","count","meta"}, ...],
 "entities":[{"eid","type","x","y","z","dx","dy","dz","vx","vy","vz","yaw","pitch","health"}, ...],
 "time":{"world_time","total_time","moon_phase","raining","thundering"}}
```

**`null` = UNSIMULATED on C (a SENTINEL, not a value).** The craster game path does not
simulate whole feature categories; those emit JSON `null` so the diff reports them as
UNSIMULATED rather than "matching zero":

- **Simulated on C** (real values): player physics, look, verified vanilla vitals
  (`health`/`food`/`saturation`/`fall_distance`), held item + full 36-slot inventory,
  `sprinting`/`sneaking`/`jumping` INTENT (from the tape), `on_ground`, `dead`, `dim`.
- **UNSIMULATED on C -> `null`**: `air`, `fire`, `xp_level`/`xp_frac`, `potions`,
  `attack_cooldown`, `hurt_time`, `death_time`, the whole **`entities`** array (the craster
  game wires `nents=0`), and the whole **`time`** object (no world-clock/weather).

**Inventory id caveat**: Java uses vanilla registry ids (`Item.getIdFromItem`), C uses mc-sim
`IC_*` ids - a DIFFERENT namespace. The diff reports inventory occupancy + counts + full-tuple
separately and flags the id-namespace mismatch, so "ids differ" is not mistaken for a bug.

## Legacy CSV columns

`x/y/z` = world FEET coords, `yaw/pitch` = MC-convention degrees, `vx/vy/vz` = motion,
`on_ground/health/food` scalars, `air` = -1 on the C side (the interim vitals model does
not track air; excluded from the diff by default), `frame_hash` = 64-bit FNV-1a of the
rendered RGBA framebuffer (C only; the Java side writes 0 - frames are not grabbed per
tick, by design).

## Spawn alignment (do this FIRST or the diff is meaningless)

The C tracer spawns at the craster worldgen origin column; Java spawns at the REAL world
spawn. The C tracer writes its exact tick-0 pose to `c_spawn.txt` (`X Y Z YAW PITCH`, world
feet / MC degrees). `trace_java.py --spawn-file trace/out/c_spawn.txt` teleports the Java
player there (a `tp` via `runcmds`) AFTER reset and BEFORE the first tape tick, so both sides
start from the SAME state. `run_oracle.sh` wires this automatically. (Fix landed here too: the
C tracer now rebuilds the collision AABB after repositioning the spawn - previously it set
`posY` but not the box, so physics snapped back to the init Y and the spawn was silently
ignored.)

## One command

```bash
cd c/craster && bash trace/run_oracle.sh          # TICKS=300 SEED=0 by default
```

Builds the tracer, gens the tape, runs C, and - if the qrl bridge is up on `:25575` -
spawn-aligns + runs Java + prints the per-feature diff. If the bridge is down it prints a
self-diff (harness proof: c vs copy == ZERO divergence). `PLATFORM=N` (default 21) fills the
grounding pad; `PLATFORM=0` uses raw terrain (Java free-falls).

## Confounds to keep honest (the oracle EXPOSES these; it does not hide them)

- **Two different worldgens.** The craster world and the Java world are NOT the same terrain
  even at seed 0: craster's origin column (8,8) has surface y=80, while the real Java world at
  that column is near-void (ground ~y=3). So a bare pose-tp drops the Java player into a
  ~76-block free-fall that ends in death at ~tick 25, cascading into look/vitals/death. The
  `--platform` pad neutralizes this so the per-tick diff measures the PHYSICS MODEL, not the
  terrain gap. Walking off a small pad re-introduces the fall (seen as a late vitals/look
  divergence ~tick 70 on a 21-pad); enlarge the pad or shorten the tape to avoid it (a very
  large fill can time the socket out, so it is split tp->fill->tp).
- **`deaths` is session-cumulative** in the mod (never reset across `reset()`), so on a reused
  bridge it starts nonzero; `trace_java.py` prints the tick-0 baseline. On a fresh client it is
  0. This is a bookkeeping artifact, not a sim divergence.
- **Inventory/held id namespaces differ** (Java vanilla registry vs C mc-sim `IC_*`), and the C
  craster player is pre-loaded with 64 cobblestone while a fresh Java survival player has an
  empty hand -- so held/inventory diverge at tick 0 for two independent reasons.
- **`sprinting`/`sneaking`/`jumping` on C are the tape INTENT**, not a simulated persistent
  state; Java reports the real `isSprinting()`/`isJumping` (which gate on hunger, collision,
  etc.), so these diverge once the two disagree on whether the state actually engaged.

## One-command usage

```bash
cd c/craster

# 1. build the C tracer (no Makefile edits; mirrors the craster_game link line)
bash trace/build_c_tracer.sh

# 2. generate a 300-tick tape
uv run --no-project python trace/gen_tape.py --ticks 300 --seed 0 --out trace/out/tape.txt

# 3. C side (headless, CPU; renders a small 320x180 frame for the hash)
./trace_game --tape trace/out/tape.txt --seed 0 --out trace/out/c_phys.csv

# 4. Java side - needs the live game (see below), then:
uv run --no-project python trace/trace_java.py \
    --tape trace/out/tape.txt --out trace/out/java_phys.csv --seed 0

# 5. diff, and materialize frames around the first divergence
uv run --no-project python trace/diff_trace.py \
    --java trace/out/java_phys.csv --c trace/out/c_phys.csv \
    --materialize --tape trace/out/tape.txt --seed 0
```

Tool self-check (proves the diff is correct): a log diffed against a copy of itself must
report **ZERO** divergence.

```bash
cp trace/out/c_phys.csv /tmp/c_copy.csv
uv run --no-project python trace/diff_trace.py --java trace/out/c_phys.csv --c /tmp/c_copy.csv
```

## Launching the Java game (anvil, headless on :1)

The Java tracer needs the client running with the qrl bridge on `127.0.0.1:25575`
(root `AGENTS.md` / `docs/RUNBOOK.md`, Run B/C):

```bash
cd java && setsid nohup bash start_vnc_client.sh >/tmp/mc_launch.out 2>&1 &
# wait until a TCP connect to 127.0.0.1:25575 succeeds (~20s), then run trace_java.py.
# on a fresh checkout you may need MC_GRADLE_ONLINE=1 (see AGENTS.md / docs/RUNBOOK.md).
```

`trace_java.py` calls `reset({"seed":0})`, which auto-loads the world and polls until
ready. Software GL (llvmpipe) is used on purpose so this stays off the shared GPU.

## Disk efficiency

A clean run costs only the two small CSVs. Frames are stored **only** when a divergence
is found and `--materialize` is passed: `diff_trace.py` re-runs the C tracer with
`--dump-dir out/diverge_<tick> --dump-lo T-K --dump-hi T+K` to write just that window as
PPMs (`c_<tick>.ppm`). K is `--window` (default 2).

### Grabbing the matching REAL-MC frame at a divergence tick

The Java side does not store frames per tick. To capture the real MC frame at tick `T`,
reuse the ffmpeg x11grab approach from `raster/verify/mc_capture/capture.sh`: with the
live game on display `:1`, replay the tape up to tick `T` (a trimmed tape via
`trace_java.py`), locate the MC window with `xwininfo -root -tree | grep 'Minecraft'`,
then `ffmpeg -f x11grab -video_size WxH -i :1.0+AX,AY -frames:v 1 out/diverge_T/mc_T.png`.
Place it next to the C `c_<tick>.ppm` for a side-by-side pixel comparison.

## Pose-forced FIRST-MINUTE FRAME ORACLE (`frame_oracle.py`)

The tick-trace above finds physics divergence. The **frame oracle** finds *rendering*
divergence over the first ~minute of scripted play - terrain lighting, missing assets,
rotated UVs, the hand, the HUD - as a named **tick + screen region + magnitude**.

The hard part is that the two games do **not** stay at the same player pose (physics
differs; there is already a tick-0 spawn divergence), so a naive frame-vs-frame diff is
meaningless. The oracle is **pose-forced**: at CHECKPOINT ticks (every `--cadence`, e.g.
every 60 ticks = ~20 checkpoints over a 1200-tick / 60s tape) it takes craster's exact
player pose from `c_phys.csv`, **teleports the Java player there**, grabs the Java frame,
renders the **craster** frame at the SAME pose, and pixel-diffs. Both cameras are then
identical, so a diff measures what the two RENDERERS draw differently - full stop.

Pose maths (per checkpoint row of `c_phys.csv`): the CSV stores world FEET coords and
MC-convention `yaw/pitch`. Java `tp` sets FEET directly (`tp @a x y z mc_yaw mc_pitch`);
craster renders with the EYE (`eye_y = feet_y + 1.62`, `PSV_EYE_HEIGHT`) and its own
convention `craster_yaw = 180 - mc_yaw`, `craster_pitch = -mc_pitch`. MC eye and craster
eye then coincide.

Three regions are diffed per checkpoint (via `render-opt/wholeframe/diff_frame.py`):
**whole** frame, a **terrain crop** (central band, excludes top sky + bottom HUD - isolates
lighting/geometry), and a **HUD region** (bottom strip: hotbar / vitals / hand base). The
per-checkpoint table localizes each divergence (`TERRAIN`, `HUD/hand`, or `near-floor`).

The target is the fill-rule noise floor (~0.02% of pixels; our rasterizer != GL), **not**
literally 0. Today the numbers are large by design - the lighting model differs and craster
draws no hand/HUD - and the oracle's job is to make that concrete and localized so we can
drive it down.

### Files

- `frame_oracle.py` - the orchestrator (one command). Runs `trace_game` -> `c_phys.csv`,
  picks checkpoints, renders each craster checkpoint frame with `game_candidate` (the
  arbitrary-pose renderer under `../raster/verify/mc_capture/`), optionally drives the live
  Java game to grab the matching frames, diffs all three regions, and prints the table +
  aggregate + ranked worst offenders. Disk-efficient: only checkpoints whose whole %-diff
  exceeds `--noise-pct` get a materialized MC / craster / heat-map trio (under
  `out/frame_oracle/worst/`).
- `../raster/verify/mc_capture/capture_at_poses.sh` - the Java-side engine: reads a poses
  FILE (one `IDX FEET_X FEET_Y FEET_Z MC_YAW MC_PITCH` per line), launches the headless
  game on `:1` (unless `--no-launch`), resets seed 0 + freezes clear noon, teleports to each
  pose, and x11grabs the MC window content region -> `mc_ck_<idx>.png`. Default gamemode is
  `survival` (draws hand + hotbar + crosshair + vitals HUD, so the oracle SEES the HUD/hand
  divergence craster lacks); `--gamemode spectator` gives a clean camera hold instead.

### One-command usage

```bash
cd c/craster

# craster-side only (no live game): renders craster checkpoints and validates the
# render+diff wiring against the existing aerial golden (mc_frame.png) as checkpoint 0.
uv run --no-project --with numpy --with pillow python trace/frame_oracle.py \
    --ticks 300 --cadence 60

# full pose-forced oracle including the live Java game on :1 (launches it itself;
# add --no-launch if a qrl bridge is already up):
uv run --no-project --with numpy --with pillow python trace/frame_oracle.py \
    --ticks 1200 --cadence 60 --run-java
```

On a fresh checkout the game launch needs `MC_GRADLE_ONLINE=1` once (see `AGENTS.md`).
Software GL (llvmpipe) keeps this off the shared GPU. The MC window is 854x480 / FOV 70,
matched by the craster render.

## Notes / known divergence sources (this is the point of the tool)

- **Spawn pose**: the C tracer spawns the player at its own worldgen origin column
  (`~8.5, surface+1, 8.5`, MC yaw 180); the Java game spawns at the REAL MC world spawn
  (a different x/y/z/pitch, on different terrain). So a raw java-vs-c diff diverges at
  tick 0 in x/y/z/pitch. To compare PHYSICS rather than spawn logistics, first teleport
  the Java player to the C spawn pose (a `tp` runcmd before the tape) or vice versa.
- **yaw** is compared as an angular difference (wraps at 360), so MC yaw 180 vs -180
  reads as zero divergence.
- **air / health**: the C side uses the *interim* vitals (no air model; MC fall-damage
  curve + slow regen, no idle hunger). `air` is excluded from the diff by default;
  `health` will diverge whenever fall damage differs. `--include-air` re-enables air.
- Tolerances are flags: `--atol` / `--rtol` (floats), ints and `on_ground` are exact.
