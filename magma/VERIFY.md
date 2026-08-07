# VERIFY - the magma verification flywheel

THE verification doc. PRODUCT.md is the product contract; OPEN_DIVERGENCES.md
is the open-bug board; this file is how we know magma is right. Ground truth
is ALWAYS the real Java 1.11.2 game (the oracle), never a self-captured golden
(CPU==CUDA and self-goldens can share bugs).

## Harsh make targets (from magma)

```bash
make verify-harsh           # structural + hard-scene + multi-scene
make test-mesh test-model-oracle test-jar-models
make hard-scene-verify      # seed-0 canopy vs mc_frame.png
make rung4-verify multi-verify
bash ../../scripts/test_parity_60.sh # focused gameplay/audio promotion suite
```

Kernel unit tests alone miss wrong models fed into correct math; use the composed
gates above plus the human tape loop.

## The two loops

**Computer loop** (scripted, no human): pinned poses and scripted inputs
through both games. Catches regressions and known-scene divergence. Runs
headless on anvil, no interaction needed.

**Human loop** (the flywheel that finishes the game): a human plays the REAL
game over Moonlight; every tick is taped; the tape replays through magma;
the first divergent tick+field is the next bug. If a human session replays
pixel- and physics-clean, that slice of the game is done.

## Human loop - exact procedure (~1 min turnaround after play stops)

1. Game must be live on anvil `:0` (Moonlight streams it; sunshine app
   "Minecraft 1.11.2 (mc-env)" or `java/sunshine_launch_mc.sh`). Bridge on
   127.0.0.1:25575.
2. Start taping (any time, mid-session is fine - the header records the full
   start state):

       cd verify/trace
       uv run --no-project --with pyarrow python tape.py start   # --frames-every N (default 20, 0=off)

   tape.py names the tape from the resolved launch config:
   `<UTCstamp>_<profile>_s<seed>_<mode>_<wtype>_rd<N>_<cfg8>` under
   `verify/tapes/`, and writes `<name>.meta.json` with EVERY
   hyperparameter verbatim (qrl_launch.json + config-owned options.txt keys +
   git rev). cfg8 hashes the full config: any changed setting changes the
   name. The sidecar is the reproducibility record - a tape is only valid
   evidence if its meta matches the config you are debugging against.
3. Human plays. Every client tick is recorded: inputs (f/s/jump/sneak/sprint/
   atk/use/hotbar), absolute yaw/pitch, post-tick physics (pos/vel/on_ground/
   hp/food/fall), nearby entities, and a real framebuffer PNG every 20 ticks.
4. Stop + replay + report:

       uv run --no-project --with pyarrow python tape.py stop
       uv run --no-project --with numpy --with scipy --with pillow --with nbt python replay_tape.py TAPE.jsonl --report

   `nbt` is used only when the tape has a `<name>_world/` recstart snapshot;
   old tapes without one follow the previous path unchanged.

   `stop` also packs `<name>.parquet` - a columnar twin of the JSONL with the
   SAME row count (one row per tick; ents as a JSON string column) for fast
   slicing and side-by-side contrast in pandas/polars. The JSONL stays the
   source of truth; `tape.py pack TAPE.jsonl` rebuilds the twin.

5. Read the output:
   - `FIRST DIVERGENCE tick T field F` - the bug. The line prints both values,
     |d|, and the inputs at T. Fix magma (or file it, below), re-run.
     Tolerances (replay_tape.py TOL): positions/velocities 1e-9 (MC physics is
     double-exact; anything bigger is real), on_ground/food exact, hp 1e-4.
   - `pixels t=...` - magma frames vs the tape's real-game frames.
     Frames are tick-boundary since 2026-07-11: recordTick re-renders with
     partialTicks=1.0 before the grab ("tb":1 in the JSONL), so moving and
     static cameras are equally ground truth (smoke: walking 5.8-6.5/ch vs
     5.7 static). Tapes recorded by older mod builds (no "tb" marker) keep
     the old +-1-tick caveat (~17/ch while moving).

## Computer loop - the ladder (cheap to expensive)

| Gate | Command | Threshold / what to look for |
|------|---------|------------------------------|
| Unit goldens (models, mesh, light, raster parity) | `make verify-harsh && make test-game` | bit-exact vs decompiled-Java formulas; any FAIL is a regression, fix before anything else |
| Rasterizer vs GL | `make raster-verify` | fill-rule/subpixel noise floor only; rarely re-run (raster is stable) |
| Pinned-pose pixel checkpoints | `bash verify/trace/run_trace.sh checkpoints` | terrain mean/ch per scene vs `report/checkpoints.md` history; a scene that JUMPS is a regression, the standing worst scenes are the open leads |
| Scripted trajectory + spawns | `bash verify/trace/run_trace.sh trajectory\|spawns` | position curve should stay near 0; spawn counts are oracle ground truth for future entity parity |
| Human tape replay | `replay_tape.py` (above) | NO first-divergence over a whole session = that gameplay slice is done |
| Pixel gate (structural) | on by default in `replay_tape.py` (`--no-gate` to skip); offline re-run: `regate.py --tape ... --npy out/<run>/magma_frames.npy` | every diff cluster classified vs the accepted classes (bossbar/hud/thinline/particles/viewmodel/transit); any UNEXPLAINED cluster >= 4k px or 8k px/frame fails (rc=3). Baselines: `trace/baselines/*.gate.json`; per-class drift vs baseline: `gate_baseline_diff.py` |
| Diff inspector (why is this frame wrong) | `pxdiff.py clusters --tape <NAME> --tick N`, then `zoom`/`pixels`/`probe` on a cluster; `frames --tape <NAME>` to rank a whole tape by unexplained px | names the CAUSE of each cluster, not just its size: texel-selection, shading-offset, registration, cutout-sky+/-, content, edge. Run `pxdiff.py selftest` first if you distrust a verdict - it re-derives all of them from synthetic mutations. `--a/--b` takes any PNG pair, so it also drives the mc_capture / ui_hud / ui_entities gates. Do not report a cause this tool calls `unresolved` as if it were diagnosed |
| Geometry oracle (dragon) | record with recstart (writes `<tape>.geom.jsonl` sidecar), replay with `MAGMA_GEOM_DUMP=<path>`, then `geom_diff.py --java <tape>.geom.jsonl --magma <path>` | per-part numeric pose diff (rotation points/angles, vanilla units). PASS = every part within 3.5 texel / 0.05 rad after the 90-tick ring warmup; a structural bug (wrong lookback/order) is orders of magnitude above that |
| Full sweep | `verify/nightly_verify.sh` (background, end of any session touching render/sim; skips itself if GPU1 busy) | replays every canonical tape with goldens on GPU1, diffs per-class px vs committed baseline; report at `trace/report/nightly_<date>.md` |
| Kernel pair lockstep (CUDA<->Metal) | `bash scripts/kernel_parity_gate.sh` on BOTH machines before/after touching any GPU kernel; manifest-only check runs in default pytest (`verify/kernels/test_kernel_pairs.py`) | the six kernels in `cuda/raster_cuda.cu` and `metal/raster_kernels.metal` are hash-paired in `verify/kernels/parity_manifest.json` - a change on one side FAILS until the twin is updated and the manifest re-recorded (`kernel_pairs.py --update`); the numeric half proves cpu==cuda (anvil) and cpu==metal (mac) on window-path scenes at the XB thresholds, so green on both machines proves the kernels compute the same math |

Animated block textures and the underwater screen overlay have a dedicated
fixed-scene CPU gate:

    bash verify/mc_capture/run_anim_verify.sh

Regenerate its real-game fixture with `capture_anim.sh`. The capture holds the
exclusive `/tmp/qrl_25575.lock`, launches with texture-animation pinning off,
records every client tick through `tape.py`, and uses one replay clock for all
regions. Water uses `frame = (total_time / 2) % 32`; portal uses the recorded
`TextureAtlasSprite.frameCounter`. The verifier also shifts the magma sequence
by one portal tick or two water ticks and requires that negative control to
fail, so a phase error cannot pass through independent per-region alignment.

Notes on the scripted trajectory: it drives the oracle through bridge steps,
which applies inputs with a one-tick offset relative to magma's script
replay (that alignment artifact inflated an early W+jump measurement to 7.7
blocks/100 ticks; the tape flywheel measured the same inputs at 4e-6). For
input-replay questions ALWAYS use the tape; the trajectory mode is for
long-horizon drift and regression curves.

## Canonical tapes (2026-07-12)

Two tapes are ground truth; nothing else is a match target:

- PHYSICS canonical: `verify/tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl`
  - fresh seed-0 world, bot-recorded (progression_bot.py, no human input),
  3,617 ticks, 181 frames every 20 ticks at 854x480, no mid-session resize.
  Replays with NO physics divergence end-to-end and a PASSING pixel gate.
  Predecessor 20260712T055346Z (human-recorded, 3,121 ticks) is retired but
  its sidecar stays for regression context
  (2026-07-12, ten fix classes later); any regression here is a real bug.
- PIXEL poses: the 12k human tape `/tmp/play_tape.jsonl`, archived with frames
  + sidecar at `verify/tapes/play_tape_12k_human_20260710.*`. Its two
  static poses (A t0-3880, B t3900-9600) are the standing pixel baselines
  (post arm/HUD/overlay fixes: A 0.96/ch, B settled 1.66/ch). Physics on it is
  capped by save-state provenance at t9811 (OPEN_DIVERGENCES #1) - do not
  chase physics past that on this tape. If /tmp/play_tape_frames vanishes
  (/tmp is volatile; it happened 2026-07-12), restore it from the archive dir.

## Rules

- A verification run is NOT done when the report is written. It is done when
  the first divergence is either FIXED or added to `OPEN_DIVERGENCES.md` with
  a one-command repro. No third state.
- Fix at the first divergent tick. Everything after it is contaminated.
- Never tune magma to a magma-derived golden. Ground truth = oracle tape/
  frame/obs only.
- One divergence class per fix commit; re-run the repro before claiming it.
- Record tapes through `tape.py start/stop` only - never ad-hoc recstart paths.
  The canonical name + .meta.json sidecar (full config + git rev) is what makes
  a tape reproducible evidence; an unnamed tape in /tmp is not evidence.
- Record physics tapes on a FRESH world (bridge reset with `"fresh": true` -
  without it reset RE-USES the loaded world regardless of seed). Replay
  regenerates pristine worldgen from the seed; a reused save carries evolved
  state (flowed water, prior edits) the replay cannot reproduce - that class
  burned the 12k tape at t9811 (OPEN_DIVERGENCES #1).
- ...and on a WORLDGEN-VERIFIED seed (0 or the regression_suite.sh list). An
  unverified seed conflates worldgen divergence with physics: seed 20260710
  placed trees differently and broke the tape at t163 (OPEN_DIVERGENCES #5).
- Physics tapes must be HUMAN-played. Bridge-DRIVEN sessions zero the player's
  horizontal motion on the tick after a landing (server pos sync; the human
  loop never shows it - OPEN_DIVERGENCES #6). Driven tapes are pipeline smoke
  tests only.
- `replay_tape.py --cuda` renders on GPU1 via magma_game_cuda in ONE game
  run (the old separate physics pass was merged in - raster never feeds the
  sim, states are byte-identical). Sim always stays CPU; the GPU owns the
  raster buffers plus the resident chunk-mesh slab pool and the sky pass,
  all cudaMalloc'd once (allocate-once rule; per-frame host buffers are
  cudaHostRegister-pinned). All 4 terrain layers draw as ONE gather +
  transform + raster chain (per-tri layer boundaries in the tiled kernel;
  pixel-identical to sequential launches). Frames go npy-direct: a
  `--frames-out X.npy` path streams every rendered frame into one uint8
  [N,H,W,3] file that IS magma_frames.npy (no per-frame PPMs; directory
  paths still write PPMs for the other harnesses). The frame loop is a
  depth-1 CPU/GPU pipeline: frame N+1's CPU prep (lighting, meshing,
  emits) and GPU enqueue happen while frame N is still rendering - the
  CPU waits only for N's host-buffer uploads (early event), consumes N's
  readback after N+1 is queued, and the GPU never idles between frames.
  Pixels are unaffected by construction: each frame renders from its own
  device-resident snapshot, so overlap changes when the CPU works, not
  what the GPU reads. 12k-tape numbers (2026-07-11, post GPU sky +
  worldgen memo + device meshes + deferred frame end + skylight
  dirty-chunk narrowing + layer merge + npy-direct + raster hi-z +
  frame pipelining): whole replay incl. pixel diff 8.43s (was 29s at
  the start of the effort). Escape hatches for A/B isolation:
  MAGMA_CPU_SKY, MAGMA_NO_DEVMESH, MAGMA_NO_DEFER,
  MAGMA_NO_PREFETCH, MAGMA_NO_LAYERMERGE, MAGMA_NO_PIPELINE. Pixel tolerance: day frames bit-exact; night frames
  may differ in isolated star pixels only (device sinf in hash21; measured
  <=67px/frame, no clusters) - sim state stays bit-exact always.
- Keep this loop FAST: game stays resident (bridge reconnects are stateless),
  magma rebuilds are seconds (`make game`). Replay measured 2026-07-11 on
  the 12k tape (10 min of play, 609 keyframes): CUDA (the replay_tape.py
  default, GPU1) 9.2 s wall = 7.3 s magma run (0.4 s sim + init/render) +
  ~1.9 s python diff/report; CPU raster (`--cpu`) 43 s, 97% of it keyframe
  rendering at ~69 ms/frame. Verdicts and pixel means identical across
  backends. Physics-only replay (no frames in the tape) is sub-second.
  Anything slower than that is itself a bug to fix - the human should only
  ever wait on tokens.

## Where things are

- Tape recorder: qrl mod `recstart`/`recstop` (Recorder.java), tape format
  documented in `verify/trace/replay_tape.py` docstring.
- Replay + first-divergence + pixel diff: `verify/trace/replay_tape.py`.
- Pinned scenes / trajectory / spawns: `verify/trace/` (run_trace.sh,
  checkpoints.py, trajectory.py, spawns.py, oracle_lib.py). Reports committed
  in `report/`, raw artifacts gitignored in `out/`.
- Tick-tape physics tracer (headless C, no frames): `trace/` - superseded for
  day-to-day use by replay_tape.py but keeps the fast physics-only C harness.
- Frame-capture one-shots (rung 4, GUI, sky): `verify/mc_capture/` -
  legacy single-frame gates, still wired to `make rung4-verify` etc.
- Container screens: `verify/mc_capture/run_gui_verify.sh` covers
  inventory, crafting table, furnace, and single chest. Table/furnace/chest and
  inventory non-preview chrome are bit-exact (near-zero A/B noise prerequisite,
  no margin). Inventory preview is a hard open 104x144 ROI gate under qrl
  `pin_preview_anim` (ageInTicks=0 so ModelBiped idle arm Z = ±0.10); pose1
  (parked mouse) and held-out pose2 (slot A, goldens only from `capture_gui.sh`)
  each require their own near-zero A/B noise. PASS only if bit-exact; residual
  is FAIL/open (no PASS-FLOOR budget). `gui_preview_calibration.json` records
  residual only. Geometry unit test: `game/test_player_preview.sh`. Chest fails
  closed without `mc_gui_chest_{a,b}.png`. `capture_gui_actions.sh` +
  `run_gui_actions_verify.sh` verify inventory PICKUP, split/deposit,
  QUICK_MOVE, hotbar swap, THROW, cursor, counts, hover, and close against the
  real GUI after each operation. Pixel contract: all owned panel/slot/cursor
  pixels at A/B noise (hard max / hard_px; no noise+1 mean budget); every
  armor/craft/offhand/main/hotbar cell covered; OS cursor non-claim without a
  game-pixel hole; non-vacuous mutation self-tests (control PASS then corrupt
  FAIL); `08_close` state-only. Preview masked (preview gate is
  `run_gui_verify.sh`); must not overwrite pose2 goldens.
- Open bugs: `OPEN_DIVERGENCES.md` (repro command per entry).

## Mechanics pixel-coverage audit (2026-07-22)

This is a code-to-evidence inventory, not a feature claim. `Yes` means an
oracle-to-magma pixel comparison deliberately exercises the named state.
`Partial` means the mechanic happens to appear in a passing tape, is only a
static/coarse checkpoint, or is hidden by an accepted predicate; it is not a
focused regression gate. `No` means no current oracle-to-magma pixel evidence
exercises the state. In particular, the `hud`, `bossbar`, `viewmodel`,
`particles`, and `transit` classes in `verify/trace/pixel_gate.py` are
diff classifications, not verification of those internals.

The canonical 3,617-tick tape contains only Chicken, Item, Llama, Sheep, and
Skeleton rows, stays in dimension 0, never opens a GUI, and keeps health 20,
food 20, XP 0, air 300, and portal progress 0. Only the sheep (closest captured
distance 7.65 blocks) and dropped item (1.11 blocks) are close enough to count
as material entity pixels. The older 12k tape supplies two static terrain/HUD
poses, not additional mechanics coverage. Risk is the likelihood of a silent
visual divergence and therefore the priority for a focused tape: animated,
overlaid, and dynamic HUD states are High even when their simulation has good
unit coverage.

### Live gameplay systems

| Mechanic | Where implemented | Sim-tested? | Pixel-verified? | Risk | Suggested scenario |
|---|---|---|---|---|---|
| Natural mob spawning, chase, melee, wander | `game/mob_live.c:gm_mobs_tick` | `game/test_mob_live.sh` (night spawn, chase, melee, wander) | **No** | High | Set night, wait for a close spawn, then backpedal while it chases and attacks. |
| Zombie and skeleton daylight burning | `game/mob_live.c:gm_mobs_tick` | `game/test_mob_live.sh` (exposed and roofed zombie) | **No** | High | Summon zombie and skeleton at dawn beside a shaded alcove and watch ignition/damage. |
| Creeper fuse and detonation | `game/mob_live.c:gm_mobs_tick`; `game/mob_live.c:gm_mobs_take_explosion` | `game/test_runtime.sh` (30-tick fuse and damage) | **No** | High | Summon a creeper three blocks away and let it detonate beside a dirt wall. |
| Explosion block destruction and player damage | `game/runtime.c:runtime_explode` | `game/test_runtime.sh`; explosion kernel tests | **No** | High | Detonate a creeper against a striped dirt/stone wall while the player takes damage. |
| Revenge aggro and neutral spider/enderman rules | `game/mob_live.c:gm_mobs_player_attack`; `game/mob_live.c:gm_mobs_tick` | `game/test_mob_live.sh` covers passive panic; no explicit enderman look/retaliation assertion | **No** | High | At noon hit a spider, then hit an enderman and record both retaliation paths. |
| Skeleton arrows and blaze fireballs | `game/runtime.c:spawn_hostile_projectiles`; `game/runtime.c:tick_projectiles` | `game/test_runtime.sh` | **No** | High | Summon skeleton and blaze across a marked lane and strafe through several volleys. |
| Bow launch, flight, hit, and arrow consumption | `game/runtime.c:spawn_bow_arrow`; `game/runtime.c:tick_projectiles` | `game/test_runtime.sh` (release and inventory consumption) | **No** | High | Give bow/arrows, draw for 20 ticks, release into a zombie and inspect the hotbar count. |
| Passive panic and deterministic wander | `game/mob_live.c:gm_mobs_tick` | `game/test_mob_live.sh` (sheep panic and wander) | **No** | Medium | Hit a penned sheep, pig, cow, and chicken once and watch their movement. |
| Player shearing of sheep | `game/runtime.c:gm_runtime_tick`; `game/mob_live.c:gm_mobs_shear_sheep` | `trace/test_shearing.py` (7 strict Java cases, exact wool entities/RNG/tool/sound); `game/test_shearing_runtime.c` | **No** | Medium | Shear adult/child/already-sheared colored sheep with main/offhand and nearly broken enchanted shears. |
| Sheep grazing, wool regrowth, and child growth | `game/mob_live.c:gm_mobs_tick`; `game/mob_live.c:gm_mobs_sheep_graze_begin`; `game/mob_live.c:gm_mobs_sheep_graze_update` | `trace/test_grazing.py` (11 strict Java cases, exact RNG/timer/block/status/event/age); `game/test_grazing_runtime.c` | **No** | Medium | Pen sheared adult and child sheep on grass/tallgrass, toggle mobGriefing, and capture the full 40-tick head motion and regrowth. |
| Natural sheep fleece color | `game/mob_live.c:gm_mobs_random_sheep_color`; ordinary passive-spawn branch in `game/mob_live.c` | `trace/test_sheep_color.py` (13 strict Java selector/onInitialSpawn cases, exact World.rand); `game/test_sheep_color_runtime.c` | **Partial**: state tint path passes, but no real-Java six-color pixel matrix | Low | Spawn the six natural colors in a lit pen, then compare unsheared and sheared layers. |
| Hostile soft and hard despawn | `game/mob_live.c:gm_mobs_tick` | `game/test_mob_live.sh` covers hard >128; soft-delay path lacks a focused assertion | **No** | Low | Keep a named reference mob at 33 blocks, then teleport beyond 128 blocks. |
| Mob death, loot, and durability damage | `game/mob_live.c:gm_mobs_player_attack`; `game/mob_live.c:mob_drop` | `game/test_mob_live.sh` (zombie, blaze, sheep loot/XP) | **No** | High | Kill one hostile and one passive with a damaged sword and hold on the drops. |
| XP orb split, bob, attraction, pickup | `game/mob_live.c:gm_mobs_spawn_xp`; `game/mob_live.c:tick_xp_orbs` | `game/test_mob_live.sh`; `game/test_dragon_live.sh` | **No** | High | Kill a zombie at five blocks and stand still as its orbs approach and are collected. |
| Dropped-item gravity, bob/spin, merge-free pickup | `game/live_sim.c:gm_live_tick`; `game/live_sim.c:gm_live_tick_player` | `game/test_play_compose.sh`; `game/test_container_live.sh` | **Partial**: a nearby Item passes the canonical tape, but no fall/pickup sequence is isolated | High | Throw a block from inventory over a ledge, approach it, and record through pickup. |
| Dragon arena, crystals, combat, and boss health | `game/dragon_live.c:gm_dragon_init`; `game/dragon_live.c:gm_dragon_tick` | `game/test_dragon_live.sh`; dragon geometry gate | **No** | High | Enter End, look across crystals, shoot one, then melee the dragon during a pass. |
| Dragon death, rays, XP, and exit podium | `game/dragon_live.c:gm_dragon_tick`; `game/entity_render.c:emit_dragon` | `game/test_dragon_live.sh` (death and portal) | **No**; particles/rays remain open divergences #40/#46 | High | Set dragon to one hit, kill it, and hold the camera through the full death timer. |
| Nether portal ignition | `game/portal_live.c:gm_portal_ignite` | `game/test_portal_live.sh`; `game/test_portal_e2e.py` | **Partial**: E2E checks portal light, not the portal pixels/animation | High | Build and ignite an obsidian frame, then wait beside it for a full texture cycle. |
| Nether portal linking and dimension transit | `game/portal_live.c:gm_portal_find_or_make`; `game/runtime.c:gm_runtime_tick` | `game/test_dimensions_live.sh`; `game/test_portal_e2e.py` | **Partial**: destination scenes are gated, transition frames are not | High | Stand in a portal through swirl, loading screen, Nether arrival, cooldown, and return. |
| End portal eye insertion and activation | `game/portal_live.c:gm_end_portal_insert_eye` | `game/test_portal_live.sh`; `game/test_route_e2e.sh` | **Partial**: End destination is gated, insertion/activation animation is not | High | Insert the twelfth eye while looking down, pause, then step into the activated portal. |
| End return after dragon exit | `game/runtime.c:gm_runtime_tick` | `game/test_route_e2e.sh` | **No** | Medium | Kill dragon, enter the exit portal, and record the return/loading/death-screen seam. |
| Furnace fuel, cook, output, and lit block state | `game/furnace_live.c:furnace_live_tick`; `game/runtime.c:gm_runtime_tick` | `game/test_furnace_live.sh`; `game/test_route_e2e.sh` | **No**: the furnace GUI gate is empty/static | High | Smelt iron while alternating between the lit block face and live progress GUI. |
| Inventory slot clicks, split, quick-move, throw | `game/container_live.c:gm_container_click` | `game/test_container_live.sh` | **No**: empty inventory panel is informational only | High | Open inventory, split a stack, shift-move it, drag it outside, and throw one item. |
| 2x2/3x3 crafting result and close-return | `game/container_live.c:gm_container_result`; `game/container_live.c:gm_container_close` | `game/test_container_live.sh`; `game/test_route_e2e.sh` | **No**: table gate has no populated slots or result | High | Craft planks then a pickaxe, take the output, and close with an item left in-grid. |
| Water flow and falling levels | `game/fluid_live.c:gm_fluid_mark`; `game/fluid_live.c:gm_fluid_tick` | `game/test_fluid_live.sh` | **No**: static water checkpoints do not exercise a changing front | High | Break a pond wall and watch source, level-1..7, and falling-water faces settle. |
| Lava flow and Nether cadence | `game/fluid_live.c:gm_fluid_tick` | `game/test_fluid_live.sh` (Overworld and Nether decay/cadence) | **No** | High | Release lava down a stepped channel in Overworld and Nether with fixed camera poses. |
| Water/lava reaction after edits/explosions | `game/runtime.c:runtime_explode`; `game/fluid_live.c:gm_fluid_mark` | Fluid/runtime tests cover activation, not a focused reaction pixel sequence | **No** | High | Open a divider between water and lava and record the resulting block changes. |
| Unsupported plant break/cascade/drop | `game/runtime.c:break_unsupported_plants` | `game/test_plants_live.sh` | **No** | Medium | Break supports under tall grass and a three-high reed column and watch the drops. |
| Wheat random growth | `game/live_sim.c:gm_live_tick` | `game/test_play_compose.sh` | **No** | Medium | Plant a row at successive growth metadata and wait for a deterministic growth tick. |
| General block random ticks | `game/live_sim.c:gm_live_tick` (wheat-only loop) | No general random-tick test; partial/stubbed beyond wheat | **No** | Medium | Stage fire, leaves, crops, and grass in one chunk and wait 200 ticks. |
| Stronghold locate and portal-room materialization | `game/structures_live.c:gm_stronghold_locate`; `game/structures_live.c:gm_stronghold_portal_room` | `game/test_stronghold_live.sh`; `game/test_route_e2e.sh` | **No** | Medium | Teleport to the located portal room and orbit its frames, lava, and silverfish area. |
| Nether fortress locate and blaze-spawner room | `game/structures_live.c:gm_fortress_locate`; `game/structures_live.c:gm_fortress_spawner_room` | `game/test_dimensions_live.sh`; `game/test_route_e2e.sh` | **No** | Medium | Teleport to the generated fortress room, orbit the spawner, then fight a blaze. |
| World clock and clear-weather interpolation | `game/world_live.c:gm_world_tick`; `game/world_live.c:gm_world_tick_clear` | `game/test_play_compose.sh` | **Partial**: clear noon and one coarse night checkpoint only | Medium | Hold one horizon view from sunset through night, moon, dawn, and full daylight. |
| Rain/thunder state and precipitation render | `game/world_live.c:gm_world_clock_set_weather`; `game/sky.c:gm_sky_set_weather`; `game/weather_render.h:gm_weather_render` | Java/CPU/CUDA timer/strength gate; `game/test_weather_render.sh`; `game/test_weather_runtime.sh` | **Partial**: source/vertex exact and non-vacuous native render probe; real-Java weather pixel tape remains open | High | Set rain then thunder in an exposed field and record sky, fog, ground streaks, and sound-free visuals. |

### Entity and model rendering

All rows below are real mappings in `game/entity_render.c:gm_entity_type_for_name`
unless marked as unsupported. Geometry/unit render tests prove topology and UV
bounds, but they are not oracle pixels.

| Mechanic | Where implemented | Sim-tested? | Pixel-verified? | Risk | Suggested scenario |
|---|---|---|---|---|---|
| Zombie model and limb animation | `game/entity_render.c:gm_entities_emit` | `game/test_entity_render.sh`; live AI in `game/test_mob_live.sh` | **No** | High | Summon a zombie at four blocks and record idle, walk, attack, hurt, and death. |
| Husk skin variant | `game/entity_render.c:gm_entity_skin_for_name` | Name/skin map in `game/test_entity_render.sh` | **No** | High | Summon husk and zombie side by side in daylight and orbit their faces/limbs. |
| Zombie villager variant | `game/entity_render.c:gm_entity_type_for_name`; `game/entity_render.c:gm_entity_skin_for_name` | Name map only | **No**; intentionally falls back to zombie skin/layout | High | Summon zombie villager beside zombie for a close face and headwear comparison. |
| Zombie pigman skin variant | `game/entity_render.c:gm_entity_skin_for_name` | Skin map in `game/test_entity_render.sh` | **No** | High | Summon pigman in Nether, trigger aggro, and record walk/attack/hurt. |
| Skeleton model, held bow, and limb animation | `game/entity_render.c:gm_entities_emit`; `game/item_render.c:gm_held_items_emit` | Entity render and hostile projectile tests | **No**: one canonical Skeleton row is about 48 blocks away | High | Summon skeleton at eight blocks and record aiming, shot, hurt, and death. |
| Stray skin variant | `game/entity_render.c:gm_entity_skin_for_name` | Name/skin map in `game/test_entity_render.sh` | **No** | High | Summon stray and skeleton on snow, then orbit both during a shot. |
| Creeper model and leg/fuse states | `game/entity_render.c:gm_entities_emit` | Geometry test; fuse sim in `game/test_runtime.sh` | **No** | High | Summon creeper, hold close view through approach, fuse, flash, and detonation. |
| Spider model and neutral/attack animation | `game/entity_render.c:gm_entities_emit` | Geometry test; AI in `game/test_mob_live.sh` | **No** | High | Summon spider at day, hit it, and circle while it retaliates. |
| Cave-spider skin variant | `game/entity_render.c:gm_entity_skin_for_name` | Name/skin map in `game/test_entity_render.sh` | **No** | High | Summon cave spider beside spider in a lit mineshaft and orbit both. |
| Enderman model, held block, hurt/teleport effects | `game/entity_render.c:gm_entities_emit` | Geometry and revenge AI tests | **No**; teleport particles are unimplemented (#40) | High | Summon enderman holding a block, hit it, and track hurt/teleport frames. |
| Blaze rods and ranged attack | `game/entity_render.c:gm_entities_emit` | Geometry test; live fight in `game/test_mob_live.sh` | **No** | High | Summon blaze in fortress room and record idle rods, attack charge, fireball, and death. |
| Sheep fleece/color/sheared/graze animation | `game/entity_render.c:gm_entities_emit`; `game/mob_live.c:gm_mobs_fill_views` | Recorded/live-state checks in `game/test_entity_render.sh`; exact timer pose values in `game/test_grazing_runtime.c` | **Partial**: color/sheared and timer-derived tick-boundary poses pass; no real-Java color/shear/graze pixel matrix or partial-tick pose gate | Medium | Pen colored sheep, shear one, feed one, and hold close on grazing head motion. |
| Pig model and gait | `game/entity_render.c:gm_entities_emit` | `game/test_entity_render.sh` | **No** | Medium | Summon pig at four blocks and record idle, walk, hurt, and death. |
| Cow model and gait | `game/entity_render.c:gm_entities_emit` | `game/test_entity_render.sh` | **No** | Medium | Summon cow at four blocks and record idle, walk, hurt, and death. |
| Mooshroom skin/model variant | `game/entity_render.c:gm_entity_skin_for_name` | Name/skin map in `game/test_entity_render.sh` | **No** | High | Summon mooshroom beside cow and orbit the head, body, and mushroom silhouette. |
| Chicken model and wing/leg animation | `game/entity_render.c:gm_entities_emit` | `game/test_entity_render.sh` | **No**: canonical Chicken rows are over 41 blocks away | Medium | Summon chicken on a ledge and record walking plus a short fall/wing flap. |
| Squid model and tentacle animation | `game/entity_render.c:gm_entities_emit` | Type/part checks in `game/test_entity_render.sh` | **No** | High | Summon squid in a glass tank and orbit underwater while it swims. |
| Witch model, nose, and held item | `game/entity_render.c:gm_entities_emit`; `game/item_render.c:gm_held_items_emit` | Type/part checks in `game/test_entity_render.sh` | **No** | High | Summon witch and record idle nose, potion hold, throw, hurt, and death. |
| Bat model and flap animation | `game/entity_render.c:gm_entities_emit` | Type/part checks in `game/test_entity_render.sh` | **No** | High | Summon bat in a small lit cave and track hanging/flying flap poses. |
| Llama model | `game/entity_render.c:gm_entities_emit` | Type/part checks in `game/test_entity_render.sh` | **No**: canonical Llama rows are over 44 blocks away | Medium | Summon llama close, orbit face/body, provoke spit, and record walk. |
| Ghast model and tentacles | `game/entity_render.c:gm_entities_emit` | Type/part checks in `game/test_entity_render.sh` | **No** | High | Summon ghast against open Nether sky and record idle, attack face, fireball, and hurt. |
| Magma-cube model and squash/stretch | `game/entity_render.c:gm_entities_emit` | Type/part checks in `game/test_entity_render.sh` | **No** | High | Summon magma cube on a marked floor and record several complete jumps. |
| Ender dragon model and ring-buffer animation | `game/entity_render.c:emit_dragon`; `game/entity_render.c:gm_dragon_pose_tick` | Dragon geometry gate; `game/test_dragon_live.sh` | **No** | High | Record a close dragon pass with head, wings, tail, hurt, and death transition. |
| End crystal model and rotation | `game/entity_render.c:emit_crystal` | Part count in `game/test_entity_render.sh` | **No** | High | Orbit a caged and uncaged crystal, then shoot one and hold through explosion. |
| Minecart variants | `game/entity_render.c:gm_entity_type_for_name`; `game/entity_render.c:gm_entities_emit` | Type/part checks in `game/test_entity_render.sh` | **No** | Medium | Place empty, chest, furnace, hopper, and TNT carts on adjacent rails and push them. |
| Arrow, tipped-arrow, spectral-arrow geometry | `game/entity_render.c:emit_arrow` | Type/part checks in `game/test_entity_render.sh` | **No**; old ghosts lack pitch (#30) | High | Fire arrows into floor, wall, and mob, then orbit the stuck projectiles. |
| Pearl, Eye, snowball, and egg billboards | `game/entity_render.c:gm_entity_billboard_item`; `game/item_render.c:gm_items_emit_billboard` | Name/item mapping in `game/test_entity_render.sh` | **No** | High | Throw each projectile across a contrasting wall while panning with it. |
| Dropped block/item model, bob, and spin | `game/item_render.c:gm_items_emit`; `game/item_render.c:gm_items_emit_flat` | Item/hand tests | **Partial**: one nearby dropped item appears in canonical tape | Medium | Drop one block and one flat item together and hold for a full bob/spin cycle. |
| XP-orb model/color animation | `game/entity_render.c:gm_entities_emit` | Marker/part count in `game/test_entity_render.sh` | **No** | High | Spawn a spread of differently valued XP orbs and record attraction/pickup. |
| Hurt flash and death dissolve/roll | `game/entity_render.c:gm_entities_emit` | Recorded entity flags tested structurally | **No**; death roll is computed but unused, dissolve remains approximate (#47) | High | Damage then kill a close zombie against a plain wall and hold through removal. |
| Burning, sneaking, and child entity transforms | `game/entity_render.c:gm_entities_emit` | No focused transform test; tape records flags but replay is incomplete | **No** | High | Stage burning adult, sneaking humanoid, and child zombie side by side. |
| Slime model + LayerSlimeGel | `game/entity_render.c` (EntitySlime map, size/squish, `gm_slime_gel_emit`) | Size scale + gel α=0.1 geometry gates (`test_entity_render`, `ui_entities`) | **No** Java pixel goldens; gel translucency unverified | High | Summon slime sizes 1, 2, and 4; record bounce squish and outer gel pass. |
| Horse model/variants | `game/entity_render.c:gm_entity_type_for_name` (no `EntityHorse` mapping) | Negative name-map case only; stubbed/unsupported | **No** | High | Summon horse, donkey, and mule in a close lineup to expose skipped entities. |

### HUD, screens, first-person hand, and overlays

| Mechanic | Where implemented | Sim-tested? | Pixel-verified? | Risk | Suggested scenario |
|---|---|---|---|---|---|
| Hotbar background and selected slot | `game/hud.c:gm_hud_draw` | `game/test_hud.sh` | **Yes**: canonical tape and 12k static poses | Low | Scroll all nine slots while holding visibly different stacks. |
| Hotbar item icons and stack counts | `game/hud.c:gm_hud_draw`; `game/hud.c:gm_gui_item_icon` | `game/test_hud.sh` | **Partial**: canonical bot tape has an empty hotbar; old populated poses are accepted as HUD | Medium | Give 1, 2, 9, 10, and 64-count stacks and scroll across them. |
| Durability bar | `game/hud.c:hud_item_durability` | No focused durability test | **No** | High | Damage wood and diamond tools to several values and scroll between them. |
| XP bar and level number | `game/hud.c:gm_hud_draw` | XP totals in `game/test_mob_live.sh`; draw smoke only | **No**: canonical XP remains zero | High | Collect orbs across a level boundary and pause at several fractional fills. |
| Hearts, half-hearts, and backgrounds | `game/hud.c:gm_hud_draw` | `game/test_hud.sh` | **Partial**: only full-health static state is gated | High | Take one-half-heart steps, heal, and include low-health heart flash. |
| Hunger, half-hunger, and backgrounds | `game/hud.c:gm_hud_draw` | `game/test_hud.sh` | **Partial**: only full-food static state is gated | High | Sprint until hunger falls, eat, and hold on odd and even food values. |
| Air bubbles and partial bubble | `game/hud.c:gm_hud_draw` | Tape plumbing/unit draw only | **No**: canonical air remains 300 | High | Submerge until air drains and record full/partial bubbles through first damage. |
| Dragon boss bar fill and label | `game/hud.c:gm_hud_set_boss`; `game/hud.c:gm_hud_draw` | Dragon sim/geometry gates | **No**: `bossbar` accepted class does not verify contents | High | Fight dragon from full to half health and keep the bar unobstructed. |
| Crosshair inversion | `game/hud.c:hud_draw_crosshair` | `game/test_hud.sh` | **Yes**: canonical static poses | Low | Pan crosshair over black, white, sky, water, and a mob. |
| Death GuiGameOver (title, score, buttons, tint) | `game/hud.c:hud_death_draw` | `run_ui_hud_gates.sh` hard title/score/buttons + hard `hud_death_tint_pair` + soft full-frame residual + live respawn | **Partial**: opaque chrome bit-exact; gradient blend hard-exact over paired underlay (source GL model); full-frame world composition soft/open (~33 C-vs-J, gray vs live pad) | High (world underlay) | Chrome + tint-pair closed. Same-scene full-frame blocked without world-only pre-death companion (frame path always draws HUD). |
| Player inventory panel | `game/screen.c:gm_screen_draw`; `game/player_preview.c` | `game/test_screen.sh`; `game/test_player_preview.sh`; `run_gui_verify.sh` | **Yes** chrome bit-exact; preview ROI hard open gate (PASS only if bit-exact under pin; residual FAIL until equality) | High | Open a populated inventory, move cursor across armor/craft/hotbar slots, then close. |
| Single chest panel | `game/screen.c:gm_screen_draw`; `game/chest_live.c` | `run_gui_verify.sh` (fail-closed without goldens) | **Yes** for empty static panel only | Medium | Open an empty single chest and a looted stronghold chest. |
| Crafting-table panel | `game/screen.c:gm_screen_draw`; `game/screen.c:gm_screen_layout` | `game/test_screen.sh` | **Yes** for empty static panel only | Medium | Open table with a populated 3x3 recipe and hover every active slot. |
| Furnace panel | `game/screen.c:gm_screen_draw`; `game/screen.c:gm_screen_layout` | `game/test_screen.sh` | **Yes** for empty static panel only | Medium | Open a burning furnace with input/fuel/output and changing progress. |
| GUI stack icons/counts, hover, cursor stack | `game/screen.c:draw_stack`; `game/screen.c:gm_screen_draw` | Layout and container tests | **No** | High | Split and carry several stacks while hovering slots in all three GUIs. |
| Crafting output updates | `game/screen.c:gm_screen_draw`; `game/container_live.c:gm_container_result` | `game/test_container_live.sh` | **No** | High | Assemble and remove a recipe one item at a time while holding the panel open. |
| Furnace flame and cook arrow | `game/screen.c:gm_screen_draw` | Tape-state plumbing in `game/test_runtime.sh` | **No** | High | Record a furnace GUI from ignition through one completed smelt. |
| Empty-hand Steve/Alex arm, swing/equip | `game/hand.c:gm_hand_draw`; `game/hand.c:gm_hand_set_skin` | `game/test_hand.sh`; hold-dig swing period on tape `scenario_hold_dig_dense_20260725T031854Z` (frames every tick) | **Partial**: held-dig swing period is oracle-confirmed - 80 consecutive dig ticks, viewmodel frame-to-frame energy autocorrelates at lag 4 to +0.950 for both oracle and magma (anti-phase -0.974 at lag 2), mean 0.82/255 over the region; skin variants are still not isolated | Medium | Switch Steve/Alex skin setting and punch a block through a full swing. |
| Flat generated/handheld item pose | `game/hand.c:gm_hand_emit_held` | `game/test_hand.sh` (stick/shovel) | **No**; `viewmodel` acceptance masks registration residual #34 | High | Hold stick, shovel, sword, and mapless flat items while walking and swinging. |
| Held 3D block pose | `game/hand.c:gm_hand_emit_held`; `game/hand.c:emit_held_block` | `game/test_hand.sh` (dirt) | **No** | High | Hold dirt, stairs, fence, and slab while walking, jumping, and placing. |
| Held cross/torch generated geometry | `game/hand.c:gm_hand_emit_held`; `game/hand.c:emit_held_generated` | `tests/test_hand_torch.c` | **No** | High | Alternate torch, flower, sapling, and stick in front of a plain wall. |
| Bow draw poses and pull sprites | `game/hand.c:build_bow_drawn`; `game/hand.c:gm_hand_emit_held` | Bow runtime test; no focused hand-pose test | **No**; drawn registration remains open #29 | High | Draw bow for 0, 3, 13, 18, and 20 ticks, then release. |
| Eating, drinking, and blocking use transforms | `game/hand.c:gm_hand_emit_held` (generic fallback; only bow has a use branch) | No; partial/stubbed | **No** | High | Eat bread, drink a potion, and block with a shield (item 442; swords are NONE in 1.11.2) in one fixed-camera tape. |
| Selection outline | `game/overlay.c:gm_overlay_emit_sel` | Overlay emitter tests/build smoke | **Partial**: canonical tape exercises selection but accepts thin-line residuals | Medium | Aim at full cube, slab, stair, fence, torch, and plant hitboxes. |
| Destroy-stage crack animation | `game/overlay.c:gm_overlay_emit_crack` | Replay attack progression | **Partial**: canonical tape exercises it under crack predicates; model remains approximate | High | Mine stone with hand and pickaxe, pausing at every destroy stage. |
| Portal full-screen swirl | `game/overlay.c:gm_overlay_portal_screen` | Tape field plumbing only | **No** | High | Stand in portal from first contact through maximum opacity without leaving. |
| Portal camera warp | `game/overlay.c:gm_overlay_portal_warp` | No caller; implemented but unwired | **No** | High | Same portal hold, with straight grid lines to expose missing warp. |
| Loading-terrain screen | `game/overlay.c:gm_overlay_loading_screen` | Frame-capture plumbing only | **No**; `transit` is an accepted class and #28 is open | High | Record Nether entry and return with every transition frame captured. |
| Underwater fog, FOV, tint, and texture overlay | `game/underwater.c:gm_uw_eval`; `game/underwater.c:gm_uw_overlay_draw` | No dedicated state test | **No**; #11 is only mostly fixed and #51 remains open | High | Walk from shore to eye-at-surface to fully submerged, then look in four directions. |
| Player hurt camera/vignette/flash | `game/frame_capture.c:gm_frame_capture_write` (hurt fields are recorded, effect absent) | Tape schema coverage only; partial/stubbed | **No** | High | Take repeated skeleton hits against a high-contrast horizon, then recover. |

### World and render assets

| Mechanic | Where implemented | Sim-tested? | Pixel-verified? | Risk | Suggested scenario |
|---|---|---|---|---|---|
| 32-frame water texture animation | `assets/water_frames.h:CR_WATER_STILL_RGBA`; `assets/blockmodels.c:bm_atlas_set_water_time` | Atlas build/unit tests | **No**: existing water checkpoint is static/pinned | High | Hold a close oblique water-bank view for at least 64 ticks. |
| 32-frame portal block animation | `assets/portal_tex.h:CR_PORTAL_TEX`; `assets/blockmodels.c:bm_atlas_set_portal_frame` | Atlas build/unit tests | **No** | High | Ignite a portal and hold close for one full 32-frame cycle. |
| Underwater overlay texture | `assets/underwater_tex.h:CR_UNDERWATER_TEX`; `game/underwater.c:gm_uw_overlay_draw` | Generated-asset presence only | **No** | High | Submerge and slowly rotate over bright sand and dark stone backgrounds. |
| Clear-day gradient and sun | `assets/sky_atlas.h:CR_SUN_RGBA`; `game/sky.c:gm_sky_draw` | `game/test_sky.sh` | **Yes**: four-direction clear-noon sky one-shot plus checkpoints | Low | Keep as a control frame in the sunset/night tape. |
| Sunset/sunrise color bands | `game/sky.c:gm_sky_draw` | `game/test_sky.sh` does not cover horizon transition | **No** | High | Hold west horizon through sunset, then east horizon through sunrise. |
| Moon, stars, and night gradient | `assets/sky_atlas.h:CR_MOON_RGBA`; `game/sky.c:gm_sky_draw` | `game/test_sky.sh` covers sun/day only | **Partial**: one coarse night checkpoint, no hard temporal gate | High | Hold a clear horizon/zenith view across moon rise and several star fields. |
| Cloud plane and time motion | `assets/sky_atlas.h:CR_CLOUDS_RGBA`; `game/sky.c:gm_sky_draw` | `game/test_sky.sh` | **Partial**: static checkpoints include clouds but do not gate motion | Medium | Look upward and hold for 200 ticks with a mountain edge as motion reference. |
| End sky cube | `assets/sky_atlas.h:CR_END_SKY_RGBA`; `game/sky.c:gm_end_sky_draw` | `game/test_dimensions_live.sh` | **Yes**: End portal E2E hard sky anchor | Low | Keep an empty End-sky frame as a control in the dragon tape. |
| Static sky/block light and cave edges | `world/light.c:light_ensure`; `world/light.c:light_sky`; `world/light.c:light_blk` | `tests/test_light.c`; `tests/test_light_brightness.c` | **Partial**: canopy/cave/night checkpoints cover broad static cases | Medium | Orbit a cave mouth at dawn with torch, skylight shaft, leaves, and water. |
| Dynamic emissive relight and chunk-boundary propagation | `world/light.c:light_set_state`; `world/light.c:light_recheck_break_surfaces` | `tests/test_light.c` | **No** | High | Place/break torches and lava on both sides of a chunk border in a dark tunnel. |
| Grass, foliage, and water biome tint blending | `world/light.c:light_grass_color`; `world/light.c:light_foliage_color`; `world/light.c:light_water_color` | `game/test_biome_color.sh` | **Partial**: forest/water checkpoints are static, not an edge-case tint gate | Medium | Walk across sharp swamp/plains biome borders with grass, leaves, and water in view. |
| Particles: block break, explosion, portal, Enderman, dragon | `game/entity_render.c:gm_particles_emit` / `gm_block_break_particles_emit` + frame_capture/game_main passes | Geometry/UV gates (`test_entity_render`, `ui_entities`); divergences #14/#40/#46 PARTIAL | **No** Java pixel goldens; recon not a live ParticleManager | High | Break blocks, detonate creeper, enter portal, hit enderman, and kill dragon in focused tapes. |
| Enchantment glint | `game/item_render.c:gm_item_draw_block_icon` (no glint pass) | No; explicitly unimplemented in divergence #44 | **No** | High | Hold and drop an enchanted sword, then inspect it in hotbar and inventory. |

### Next tapes to record, ranked

These are ordered by expected bug-finding value, breadth of high-risk pixels,
and how cleanly the state can be scripted. Each sketch uses qrl commands for
world setup and `mcwindow_script.py` segments for timed view/input.

1. **Nether portal transit:** qrl build/ignite and face a grid wall; mcwindow
   `wait 20, forward 140, wait 80`, capturing portal texture, swirl, loading,
   arrival, cooldown, and return.
2. **Underwater and drowning:** qrl teleport to a glass-sided deep pool;
   mcwindow `forward 30, wait 360, look yaw +90, wait 40, backward 40` to cover
   surface fog, overlay/FOV, air bubbles, damage, and recovery.
3. **Creeper fuse and explosion:** qrl summon creeper three blocks from a
   striped dirt/stone wall; mcwindow `wait 50, backward 20, wait 50` through
   fuse, player hurt, particles gap, terrain edits, and drops.
4. **Daylight burning mob pair:** qrl set dawn and summon zombie/skeleton beside
   a roof edge; mcwindow `wait 120, strafe-right 30, wait 120` for fire overlay,
   shade boundary, ranged attack, hurt, death, and loot.
5. **XP and dynamic HUD:** qrl summon a one-hit zombie and set player just below
   a level boundary; mcwindow `attack 1, wait 120, forward 30, wait 60` for mob
   death, orb bob/attraction/pickup, XP fill/level, durability, and item counts.
6. **First-person use states:** qrl give bow/arrows, bread, potion, sword, and a
   block; mcwindow holds use for `3/13/18/20` bow ticks, releases, then eats,
   drinks, blocks, swings, and places from a fixed pose.
7. **Dragon fight and death:** qrl enter End, set dragon/crystal health for a
   short deterministic fight; mcwindow `look/attack`, one bow shot, then hold
   through boss-bar change, hurt flash, final hit, death rays/XP, and podium.
8. **Player hurt and death HUD:** qrl set low health and summon a fenced
   skeleton; mcwindow `wait 160` against a high-contrast horizon, capturing
   projectile, hurt camera/flash, hearts, death wash/banner, and counter.
9. **Close mob/skin lineup:** qrl summon zombie/husk/zombie-villager/pigman,
   skeleton/stray, spider/cave-spider, cow/mooshroom in lit pens; mcwindow
   slow `look dx` sweep, then one attack per pen for gait, held items, skins,
   hurt flash, and death transforms.
10. **Active furnace and container UI:** qrl give ore/coal and place furnace;
    mcwindow open GUI, click input/fuel, `wait 220`, hover/carry/split output,
    then close and face the lit block for slots, flame, arrow, and block state.

## Metal backend (macOS)

STATUS: VERIFIED - first green run 2026-07-30, master rev 0f80c50 (MacBook
M4 Max, macOS 26.5.1, Apple clang 21.0.0, MetalToolchain 17.6.109).
`scripts/mac_metal_verify.sh` passed end to end: game-metal builds, the
rung-1 gate is bit-exact CPU==Metal on all 5 layers (color AND depth,
maxdiff=0), and the smoke-zombie replay returned rc=0 with the structural
pixel gate green (no unexplained clusters over 18 frames; one sub-threshold
single-frame blip, 208 px, is recorded in the gate baseline json). Rev
0f80c50 contains the two fixes that first run surfaced: metallib search
paths in `mg_load_lib` (a missing metallib used to degrade to the CPU
fallback, which made the parity gate vacuously compare CPU with CPU) and
the full 19-symbol Darwin `CUDA_WEAK_LD` list (plain `magma_game` would not
link on macOS, breaking the replay's world-dump build). The "would NOT
prove" scope limits below still apply.

The Metal backend mirrors the CUDA raster API: `cr_raster_metal_*` is a
one-for-one rename of the `cr_raster_cuda_*` entry set
(`cuda/raster_cuda.cu` lines 595-1262), implemented in
`metal/raster_kernels.metal` + `metal/raster_metal_host.m` and built by
`make -C magma game-metal` (compiled with `-DMAGMA_METAL`, runtime switch
`--backend metal`). Host C is compiled with `-ffp-contract=off`, the clang
analog of the CUDA build's `--fmad=false`.

How to build and prove parity (MacBook, repo root):

    bash scripts/mac_metal_verify.sh

which runs, in order:

1. `make -C magma game-metal` - the binary builds.
2. `make -C magma test-raster-parity-metal` (`make/metal.mk`) - the rung-1
   gate: `tests/test_raster_parity_metal.c` renders the shared verify scene
   battery (SOLID, CUTOUT, TRANSLUCENT, MIPPED, FOG_RADIAL) with
   `cr_raster_cpu` and `cr_raster_metal_into` (alloc-once path via
   `cr_raster_metal_pre`/`post`) and requires color AND depth bit-exact on
   every layer - the same pass criteria as the CPU-vs-CUDA gate.
3. Tape replay smoke: `replay_tape.py --metal` on
   `verify/tapes/scenario_smoke_zombie_20260722T081735Z.jsonl` with the
   structural pixel gate on. rc=0 required. `--metal` routes through the same
   `oracle_lib.run_magma_script` plumbing as `--cuda`; `pixel_gate.py` is
   frozen and consumes frames identically regardless of backend. The tape,
   its `_frames` goldens, and its `_world` snapshot must be rsynced from
   anvil first (exact command in the script header).

What the full pass proves: the Metal raster is bit-exact vs the CPU raster
on the unit scene battery, and one canonical smoke tape replays through the
frozen pixel gate on Apple silicon.

Second wave, 2026-07-30, master rev 9f2d72a (same machine/toolchain): the
full regression pin set (`scripts/regression_pins.txt`) replayed on the Mac
under `--metal` with `MAGMA_METAL_REQUIRE=1` - smoke_zombie, fence_collide,
water_flow, cobweb_fall, water_dive, and dragon_kill_geared all rc=0 with
the pixel gate green. The canonical 3,617-tick survival tape
(`20260721T215812Z`) returned rc=3 with EXACTLY the failure signature filed
on Linux (mild-shift frames t=3080/t=3540 with unexplained_px=0, inventory
mismatches t=3257 slot 1 / t=3267 slot 2 - the recorded-crafting gap in
OPEN_DIVERGENCES "Late-tape item acquisition"): a known-failing tape failing
identically across backends is parity evidence, not a Metal gap.
smoke_zombie was additionally re-replayed from a cleared frame cache in a
separate session as an independent check (rc=0).

Playable window: `MAGMA_METAL_REQUIRE=1 ./magma_game_metal --backend metal`
boots on the Mac into the SDL window (`[config] render=window backend=metal`,
metallib + AGXMetal loaded, strict mode, no fallback; verified over ssh
2026-07-30). Arrow-key look rides the normal input map
(`game/input_map.c`, held-arrow quantized steps, unit-tested by
`test_input_map`). The window contents are the same frames the tape gates
prove bit-exact; what ssh cannot verify is the composited screen and live
keystrokes (macOS TCC blocks `screencapture`/osascript from a remote shell),
so the hands-on look-around check is a manual step.

What is still NOT proven for `--metal`:

- The rest of the ladder (checkpoints, nightly sweep) beyond the pin set.
  (The 12k human tape formerly listed here was retired; it no longer exists
  in `verify/tapes/`.)
- The frame-resident path (`frame_begin`/`sky`/`frame_end[_async]`), the slab
  pool, and `render_layer`/`render_gather`/`render_terrain` beyond what the
  pin tapes happen to exercise; the unit gate only drives `into`.
- Any performance claim.
- Cross-machine bit-identity of CPU frames (x86 anvil vs arm64 mac) - the
  parity gate compares CPU vs Metal on the SAME machine.

No gate disagreed in either wave, so nothing moved to OPEN_DIVERGENCES.md;
the "NOT proven" list is the open ladder for `--metal`.
