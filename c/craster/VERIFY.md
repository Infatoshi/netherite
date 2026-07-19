# VERIFY - the craster verification flywheel

THE verification doc. PRODUCT.md is the product contract; OPEN_DIVERGENCES.md
is the open-bug board; this file is how we know craster is right. Ground truth
is ALWAYS the real Java 1.11.2 game (the oracle), never a self-captured golden
(CPU==CUDA and self-goldens can share bugs).

## Harsh make targets (from c/craster)

```bash
make verify-harsh           # structural + hard-scene + multi-scene
make test-mesh test-model-oracle test-jar-models
make hard-scene-verify      # seed-0 canopy vs mc_frame.png
make rung4-verify multi-verify
```

Kernel unit tests alone miss wrong models fed into correct math; use the composed
gates above plus the human tape loop.

## The two loops

**Computer loop** (scripted, no human): pinned poses and scripted inputs
through both games. Catches regressions and known-scene divergence. Runs
headless on anvil, no interaction needed.

**Human loop** (the flywheel that finishes the game): a human plays the REAL
game over Moonlight; every tick is taped; the tape replays through craster;
the first divergent tick+field is the next bug. If a human session replays
pixel- and physics-clean, that slice of the game is done.

## Human loop - exact procedure (~1 min turnaround after play stops)

1. Game must be live on anvil `:0` (Moonlight streams it; sunshine app
   "Minecraft 1.11.2 (mc-env)" or `java/sunshine_launch_mc.sh`). Bridge on
   127.0.0.1:25575.
2. Start taping (any time, mid-session is fine - the header records the full
   start state):

       cd c/craster/raster/verify/trace
       uv run --no-project --with pyarrow python tape.py start   # --frames-every N (default 20, 0=off)

   tape.py names the tape from the resolved launch config:
   `<UTCstamp>_<profile>_s<seed>_<mode>_<wtype>_rd<N>_<cfg8>` under
   `raster/verify/tapes/`, and writes `<name>.meta.json` with EVERY
   hyperparameter verbatim (qrl_launch.json + config-owned options.txt keys +
   git rev). cfg8 hashes the full config: any changed setting changes the
   name. The sidecar is the reproducibility record - a tape is only valid
   evidence if its meta matches the config you are debugging against.
3. Human plays. Every client tick is recorded: inputs (f/s/jump/sneak/sprint/
   atk/use/hotbar), absolute yaw/pitch, post-tick physics (pos/vel/on_ground/
   hp/food/fall), nearby entities, and a real framebuffer PNG every 20 ticks.
4. Stop + replay + report:

       uv run --no-project --with pyarrow python tape.py stop
       uv run --no-project --with numpy --with pillow --with nbt python replay_tape.py TAPE.jsonl --report

   `nbt` is used only when the tape has a `<name>_world/` recstart snapshot;
   old tapes without one follow the previous path unchanged.

   `stop` also packs `<name>.parquet` - a columnar twin of the JSONL with the
   SAME row count (one row per tick; ents as a JSON string column) for fast
   slicing and side-by-side contrast in pandas/polars. The JSONL stays the
   source of truth; `tape.py pack TAPE.jsonl` rebuilds the twin.

5. Read the output:
   - `FIRST DIVERGENCE tick T field F` - the bug. The line prints both values,
     |d|, and the inputs at T. Fix craster (or file it, below), re-run.
     Tolerances (replay_tape.py TOL): positions/velocities 1e-9 (MC physics is
     double-exact; anything bigger is real), on_ground/food exact, hp 1e-4.
   - `pixels t=...` - craster frames vs the tape's real-game frames.
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
| Pinned-pose pixel checkpoints | `bash raster/verify/trace/run_trace.sh checkpoints` | terrain mean/ch per scene vs `report/checkpoints.md` history; a scene that JUMPS is a regression, the standing worst scenes are the open leads |
| Scripted trajectory + spawns | `bash raster/verify/trace/run_trace.sh trajectory\|spawns` | position curve should stay near 0; spawn counts are oracle ground truth for future entity parity |
| Human tape replay | `replay_tape.py` (above) | NO first-divergence over a whole session = that gameplay slice is done |
| Pixel gate (structural) | on by default in `replay_tape.py` (`--no-gate` to skip); offline re-run: `regate.py --tape ... --npy out/<run>/craster_frames.npy` | every diff cluster classified vs the accepted classes (bossbar/hud/thinline/particles/viewmodel/transit); any UNEXPLAINED cluster >= 4k px or 8k px/frame fails (rc=3). Baselines: `trace/baselines/*.gate.json`; per-class drift vs baseline: `gate_baseline_diff.py` |
| Geometry oracle (dragon) | record with recstart (writes `<tape>.geom.jsonl` sidecar), replay with `CRASTER_GEOM_DUMP=<path>`, then `geom_diff.py --java <tape>.geom.jsonl --craster <path>` | per-part numeric pose diff (rotation points/angles, vanilla units). PASS = every part within 3.5 texel / 0.05 rad after the 90-tick ring warmup; a structural bug (wrong lookback/order) is orders of magnitude above that |
| Full sweep | `raster/verify/nightly_verify.sh` (background, end of any session touching render/sim; skips itself if GPU1 busy) | replays every canonical tape with goldens on GPU1, diffs per-class px vs committed baseline; report at `trace/report/nightly_<date>.md` |

Notes on the scripted trajectory: it drives the oracle through bridge steps,
which applies inputs with a one-tick offset relative to craster's script
replay (that alignment artifact inflated an early W+jump measurement to 7.7
blocks/100 ticks; the tape flywheel measured the same inputs at 4e-6). For
input-replay questions ALWAYS use the tape; the trajectory mode is for
long-horizon drift and regression curves.

## Canonical tapes (2026-07-12)

Two tapes are ground truth; nothing else is a match target:

- PHYSICS canonical: `raster/verify/tapes/20260712T055346Z_fast_s0_survival_default_rd8_77b5b462.jsonl`
  - fresh seed-0 world, tape.py-recorded, 3,121 ticks, 157 frames at 854x480,
  no mid-session resize. Replays with NO physics divergence end-to-end
  (2026-07-12, ten fix classes later); any regression here is a real bug.
- PIXEL poses: the 12k human tape `/tmp/play_tape.jsonl`, archived with frames
  + sidecar at `raster/verify/tapes/play_tape_12k_human_20260710.*`. Its two
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
- Never tune craster to a craster-derived golden. Ground truth = oracle tape/
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
- `replay_tape.py --cuda` renders on GPU1 via craster_game_cuda in ONE game
  run (the old separate physics pass was merged in - raster never feeds the
  sim, states are byte-identical). Sim always stays CPU; the GPU owns the
  raster buffers plus the resident chunk-mesh slab pool and the sky pass,
  all cudaMalloc'd once (allocate-once rule; per-frame host buffers are
  cudaHostRegister-pinned). All 4 terrain layers draw as ONE gather +
  transform + raster chain (per-tri layer boundaries in the tiled kernel;
  pixel-identical to sequential launches). Frames go npy-direct: a
  `--frames-out X.npy` path streams every rendered frame into one uint8
  [N,H,W,3] file that IS craster_frames.npy (no per-frame PPMs; directory
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
  CRASTER_CPU_SKY, CRASTER_NO_DEVMESH, CRASTER_NO_DEFER,
  CRASTER_NO_PREFETCH, CRASTER_NO_LAYERMERGE, CRASTER_NO_PIPELINE. Pixel tolerance: day frames bit-exact; night frames
  may differ in isolated star pixels only (device sinf in hash21; measured
  <=67px/frame, no clusters) - sim state stays bit-exact always.
- Keep this loop FAST: game stays resident (bridge reconnects are stateless),
  craster rebuilds are seconds (`make game`). Replay measured 2026-07-11 on
  the 12k tape (10 min of play, 609 keyframes): CUDA (the replay_tape.py
  default, GPU1) 9.2 s wall = 7.3 s craster run (0.4 s sim + init/render) +
  ~1.9 s python diff/report; CPU raster (`--cpu`) 43 s, 97% of it keyframe
  rendering at ~69 ms/frame. Verdicts and pixel means identical across
  backends. Physics-only replay (no frames in the tape) is sub-second.
  Anything slower than that is itself a bug to fix - the human should only
  ever wait on tokens.

## Where things are

- Tape recorder: qrl mod `recstart`/`recstop` (QuantizedRL.java), tape format
  documented in `raster/verify/trace/replay_tape.py` docstring.
- Replay + first-divergence + pixel diff: `raster/verify/trace/replay_tape.py`.
- Pinned scenes / trajectory / spawns: `raster/verify/trace/` (run_trace.sh,
  checkpoints.py, trajectory.py, spawns.py, oracle_lib.py). Reports committed
  in `report/`, raw artifacts gitignored in `out/`.
- Tick-tape physics tracer (headless C, no frames): `trace/` - superseded for
  day-to-day use by replay_tape.py but keeps the fast physics-only C harness.
- Frame-capture one-shots (rung 4, GUI, sky): `raster/verify/mc_capture/` -
  legacy single-frame gates, still wired to `make rung4-verify` etc.
- Open bugs: `OPEN_DIVERGENCES.md` (repro command per entry).
