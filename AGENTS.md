# netherite (AGENTS.md)

Home: **Anvil-primary** - canonical at `anvil:~/dev/netherite`. Build and run
here. MacBook is control plane / Moonlight / image viewing only.

Git remote: **https://github.com/Infatoshi/netherite**. That is the only
GitHub repo. `origin` on both machines points there.

This file is the **only agent entry**. Do not hunt other root markdown for
instructions. How-tos and history live under `docs/`; living contracts live
next to the code they govern.

## Platform support

| OS | Role |
|----|------|
| **Linux x86_64** | Full stack. Build CPU/CUDA, run Java oracle, train blaze, sweep. Canonical host: anvil (Ubuntu). Needs JDK 8 + NVIDIA CUDA for GPU paths. |
| **macOS** | Build and verify the CPU and Metal game backends. Also serves as the control plane and image/video review host. Do not expect native `runClient` or CUDA here. |
| **Windows** | Not a supported build/run host for this monorepo. Use WSL2 Linux if you must, or a remote Linux box. |

Prism / MultiMC / official launcher: optional jar source for assets. Fresh
boxes do **not** need Prism credentials; `make -C java bootstrap-oracle` pulls
MC 1.11.2 via ForgeGradle (you must own the game).

First clone: `make -C java bootstrap-oracle`, then `make assets` and `make test`.

## What this repo is

From-scratch C reimplementation of Minecraft 1.11.2 (magma + blaze),
bit-verified against the real Java game, with CPU, CUDA, and Metal backends.
Product name: **netherite**. Trees:

- `java/` - the oracle: Forge+Malmo/NetheriteMod (mod id qrl) client, launch scripts, oracle-src
  (bootstrap), render-opt kernel lab (closed)
- `blaze/` - the simulation: reference CPU, production CUDA tick, and Metal
  observation backend over the exact CPU tick; batched RL env (`blaze/env/`)
  and trainers (`blaze/rl/`)
- `magma/` - the playable fidelity tier: CPU tick plus CPU, CUDA, or Metal raster
- `verify/` - cross-stack harness: tapes, scenarios, gates, nightly sweep

`blaze/` is the simulation. The vanilla nether mob is "blaze mob".

## Where to read (stop when you have enough)

| Need | Open |
|------|------|
| Product architecture and native target | `SPEC.md` |
| First clone / no oracle-src | `docs/BOOTSTRAP.md` |
| How to play, VNC, NetheriteMod, sweep | `docs/RUNBOOK.md` |
| Ship criteria / gate status | `docs/GATES.md` |
| Is X in the game? cut / pinned / open / unrecoverable | `docs/SCOPE.md` |
| Fidelity procedure | `magma/VERIFY.md` |
| Product contract / open bugs | `magma/PRODUCT.md`, `OPEN_DIVERGENCES.md` (closed forensics: `CLOSED_DIVERGENCES.md`) |
| Second bridge (magma -> blaze) open rows | `blaze/OPEN_DIVERGENCES.md` |
| Delegated lane routine (sub-agents) | `docs/SUBAGENT.md` |
| Current Magma or Blaze detail | that tree's `SPEC.md` |
| History / lessons | `docs/DEVLOG.md` |
| Old reports | `docs/archive/` (ignore by default) |

## Orchestrating divergence lanes (parent agent)

The two divergence files are the work queue. A parent agent keeps
sub-agents pointed at them without waiting for a human to pick items:

1. Pick from `magma/OPEN_DIVERGENCES.md` class A (top down) and
   `blaze/OPEN_DIVERGENCES.md` unported rows (dependency order). One lane
   per item. Skip items whose evidence (tape, golden) is not on this Mac.
2. Stage: `bash scripts/lane_stage.sh <lane> <host> [--tape NAME]...`
   (gamer for magma CPU work; anvil for oracle captures, CUDA, blaze M2;
   Metal half of a twins lane builds on the Mac).
3. Prompt = `docs/SUBAGENT.md` + a goal block: the item text, the gate
   command, the documented baseline numbers, the hard goal, and what is
   forbidden. Launch from the lane worktree, in the background.
4. On return: read the report, review the diff, re-run the lane's gates and
   root `make test` on the remote clone yourself, then merge `--no-ff` to
   master, run `make test` locally, push. Never absorb a delegate's numbers.
   Doc conflicts (DEVLOG, divergence files) keep both sides, newest first.
5. Clean up: worktree, local and remote lane branch, `~/nlanes/<lane>`, the
   lane tmux session. Then pick the next item.

If a delegate dies (session exit, host reboot), its branch and remote
clone survive; relaunch with a restart note stating the branch state, what
was measured, and where the logs are.

## Commands

Java (oracle). Gradle. Anvil only for `runClient`.

```bash
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
make -C java bootstrap-oracle
cd java/Minecraft && ./gradlew -g run/gradle build
cd java/Minecraft && ./gradlew -g run/gradle runClient
```

Make (C / CUDA / Metal):

```bash
make                 # magma_game; Metal on Darwin
make assets
make test            # short native units, <180s
make play            # prints magma/magma_game
make -C blaze/nn test
make -C blaze/nn test-metal
# anvil: sm_120; gamer: sm_86
make -C blaze/nn test-cuda BLAZE_SM=sm_120
make -C blaze/rl test
make -C blaze/rl smoke-metal
make -C blaze/rl smoke-cuda BLAZE_SM=sm_120
make -C verify tape-info TAPE=verify/tapes/<tape>.jsonl
make -C verify replay
out/verify/replay --tape verify/tapes/<tape>.jsonl --ticks 32
```

No root `make oracle`. No root `make verify` or `make train` until those C
binaries are the only path.

Leftover Python during migration (UV only):

```bash
uv run --no-project python blaze/oracle/runner.py <name>

# touching magma/cuda/raster_cuda.cu, magma/metal/raster_kernels.metal,
# blaze/core/obs_camera.h, or blaze/env/blaze_metal_obs.metal?
# Hash-paired in verify/kernels/parity_manifest.json: edit BOTH twins, then
# prove on both machines and re-record (kernel_pairs.py --update):
bash scripts/kernel_parity_gate.sh   # anvil: cpu==cuda; mac: cpu==metal + obs

# wrapper-vs-owr worldgen census pin (CPU; blessed residuals in sidecar):
bash verify/worldgen/wrapper_gate.sh              # rc=0 exact match vs known_divergences.json
# bash verify/worldgen/wrapper_diff.sh            # diagnostic report + load-order probe
# bash verify/worldgen/wrapper_gate.sh --update   # re-bless only with maintainer judgment

# optional filtered copy (no oracle-src, no generated atlas headers, no media)
make -C verify public-export DEST=/absolute/path
```

## Repository storage

- `AGENTS.md` is the only agent entry. Do not add `CLAUDE.md` or another
  agent instruction file.
- Committed inputs use a specific name and stay with their owner:
  `verify/tapes/`, `verify/fixtures/`, `blaze/rl/fixtures/`, or
  `magma/assets/`. Do not create a generic `data/` directory.
- All persistent generated output goes under `out/<owner>/`, where owner is
  `java`, `verify`, `magma`, or `blaze`. The entire root `out/` tree is
  ignored and disposable. Tests may also use a temporary system directory.
- Human evidence is not a fixture. Keep the conclusion in `docs/DEVLOG.md`,
  then delete screenshots, videos, traces, and receipts that no gate reads.
- Demo videos and their one-use generator scripts are temporary. Create both
  at the repository root so the mess is visible. Review them, then delete
  them. Never commit a demo directory, demo video, or one-use video script.
- `assets` means build or runtime game input. Do not use it for reports or
  generated media.

## Runtime knobs: config, never env vars

Behavior toggles NEVER ride on environment variables. Where they live:

- magma binary: one `CFG_*` line in `magma/core/config.def` (THE registry) ->
  `magma.conf` / `--conf PATH` / `--set key=value`. The config holds the full
  run description, including backend and device. Key name = old env name
  minus `MAGMA_`, lowercased. `--dump-config` prints the effective set.
- blaze env: `blaze/blaze.conf` -> `VecBlaze(config=PATH)`. The same keys select
  CPU, CUDA, or Metal. Legacy Python arguments remain direct overrides.
- native trainer config is `blaze/rl/ppo.conf`: flat `key = value` with
  `--conf PATH`, repeatable `--set key=value`, and `--dump-config`. One
  `out/blaze/rl/ppo` binary supports CPU and the host GPU backend. Linux uses
  CUDA. macOS uses the CPU tick with Metal observation and policy work.
- Existing Python tools use argparse flags during the native migration. Do not
  add a Python tool. A native replacement deletes its Python owner and callers
  in the same milestone.
- Java oracle: `java/qrl_launch.json` + `java/fast.yaml` / `java/vanilla.yaml`.

Build-time make variables (`MAGMA_AUDIO_OPENAL=1`, `BLAZE_SM=...`) are build
config, not runtime knobs, and stay in make land. Exempt: system env
(`DISPLAY`, `JAVA_HOME`, `CUDA_VISIBLE_DEVICES`, `TMPDIR`, `UV_*`) and
machine-environment pointers consumed by bootstrap/build tooling (`MC_JAR`,
`MC_SM`, `MC_JAVA_DIR`, `MC_ASSET_INDEX`, `QRL_SM`).

`make -C verify env_knob_gate-check` (part of the sweep) fails on any
project-prefixed getenv/os.environ read. Adding a knob means adding a
registry line or a flag, never a getenv.

## Pixel investigation

When a tape frame is wrong, do not hand-roll numpy. The tool lives at
`verify/trace/pxdiff.py` and `--tape` resolves replay output from
`out/verify/trace/tape_<NAME>/`, so run it from `verify/trace`:

```bash
cd verify/trace
U() { uv run --no-project --with numpy,scipy,pillow python "$@"; }  # bash+zsh
U pxdiff.py selftest                                   # trust the tool first
U pxdiff.py frames  --tape <NAME>                      # rank ticks by unexplained px
U pxdiff.py survey  --tape <NAME> --tick N -o DIR      # START HERE: top<=5
#   clusters as numbered boxes on overview.png + zoom_N.png triptychs +
#   survey.json; big unresolved clusters get tile-refined causes.
U pxdiff.py clusters --tape <NAME> --tick N            # cluster table + CAUSE
U pxdiff.py zoom    --tape <NAME> --tick N --cluster 0 --scale 10 -o /tmp/z.png
U pxdiff.py probe   --tape <NAME> --tick N --cluster 0 # every discriminator
U pxdiff.py pixels  --tape <NAME> --tick N --cluster 0 # exact RGB pairs
```

Causes: `texel-selection`, `shading-offset`, `registration`, `cutout-sky+/-`,
`content`, `edge`, `unresolved`. `--a/--b` takes any PNG pair, so the same tool
drives the mc_capture / ui_hud / ui_entities gates. `grind.py` ranks a whole
tape by mean/ch; `pixel_gate.py` decides pass/fail. Never report `unresolved`
as a diagnosis, and never claim a cause the tool did not measure.

Reading it right (each of these cost a cold agent real time):

- The `gate` column is pixel_gate's mask label (which budget absorbed the
  pixels), not the faulty subsystem: a world-sized cluster classed `particles`
  is NOT a particle bug, and `soak_from` in reports means spill from an
  over-budget class. The `cause` column is the diagnosis.
- `sel` is exact-match texel selection; real minified surfaces usually carry a
  small light delta on top, so trust the `tol4` column / probe field.
- Heed the frame-level notes. Many small clusters agreeing on one shift =
  whole-frame registration. A giant `unresolved` cluster with
  `structure_corr<=0` and `best_shift (0,0)` = the CAMERA moved: stop pixel
  probing and diff the pose (tape jsonl `x/y/z/on_ground` vs
  `out/tape_<NAME>/magma_state.jsonl` at that tick) - a sub-block Y error
  remaps the whole scene (nether_elytra t=176: 0.93-block landing lag).
- px counts: survey/clusters count connected-component members; probe/pixels
  count every differing pixel in the padded rect. Both are correct.

Two things that make a pixel measurement lie, both paid for already:

- **Do not replay tapes in parallel across worktrees** unless you are on
  `b9fe039` or later. `tapes/` is symlinked into every agent worktree, and the
  `.snapshot_patch.jsonl` cache keys its staleness off `snapshot_patch.py`'s
  mtime, so concurrent replays all regenerate the same file at once. Before
  that fix they clobbered each other and a clean tape measured 3.63/ch terrain
  against a 0.94/ch baseline. If a number looks like a regression, re-measure
  with nothing else running before you believe it.
- **`/tmp` on anvil is a 46 GB RAM-backed tmpfs.** A delegated agent that puts
  a uv cache or build tree there can fill it, kill its own run, and break every
  other shell on the box (a codex run wrote 15 GB of CUDA wheels to
  `/tmp/<name>-uv-cache` and died on "Disk quota exceeded"). When launching
  delegates, pin `UV_CACHE_DIR=$HOME/.cache/uv` and
  `TMPDIR=$HOME/dev/nw/.tmp` in their environment and say so in the
  prompt.
- **A retired tape used to measure as a silent PASS over zero frames.**
  The recorder bakes an ABSOLUTE golden path into every tick row, so moving
  a tape into `tapes/retired/` orphaned all of them;
  `oracle_frames_cache` skipped every missing file without a word and the
  pixel gate reported `PASS: no unexplained clusters over 0 frames`. Fixed
  2026-07-29: goldens now fall back to `<dir of the tape file>/<frames dir>/`,
  pxdiff resolves `tapes/retired/` too, and a tape that declares goldens but
  resolves none is a FATAL, not a pass. If a gate reports 0 frames checked,
  that is a harness failure - never read it as a clean tape.
- **Check what the goldens actually contain before chasing a diff.** A tape
  recorded through Malmo has `hideGUI` forced on for the whole mission, so its
  goldens have no HUD at all; `capture.hide_gui` in the tape meta is the
  measured value (`qrl_launch.hide_gui` is only what the launcher asked for)
  and replay forwards it as `--set hide_gui=1`. Oracle captures can also be
  wrong: the eat/bow viewmodel goldens are idle tips, not mid-use poses, and
  fitting C to them would be fitting to a bad reference.
- **A tape's first ~40 goldens are not steady state.** The oracle's
  `EntityRenderer.fogColor1` smoother had not converged at recstart, so t=0 is
  2-6x worse than t=10 on every tape and two tapes fail their gate on t=0
  alone. The recorder writes `fog_color1` in the header and replay seeds magma
  from it; tapes recorded before that field keep the old converged seed.
  `replay_tape.py --fog-c1-init <0..1>` overrides the seed for sweeping it on
  old tapes.
  Do not hardcode a value - it depends on the recording session, not the tape
  (see `magma/OPEN_DIVERGENCES.md`).
- **The end-crystal healing beam needs the client's `ticksExisted`.**
  `RenderDragon.renderCrystalBeams` scrolls `endercrystal_beam` by
  `-ticksExisted*0.01` per tick over a 16x256 sheet that is ~2x minified, so a
  one-tick phase error randomizes the whole glyph speckle. The recorder now
  writes it per entity (dragon field 18, crystal field 12); tapes older than
  that are reconstructed as `tick - first_seen + ent_ticks0`, default 7,
  overridable with `replay_tape.py --ent-ticks0`. The default is a measured
  sweep, not a
  guess: over the offset's full 100-tick period exactly one value is sharply
  better (76.8k differing px vs 109-114k at all 99 others). Re-sweep it rather
  than fitting anything else if a new End tape's beam looks like noise.
- **Measure a viewmodel residual against the render, not against a texel.**
  Dividing a golden by a raw atlas texel prices in shading the oracle also
  applies, and that is how a phantom "1.57x over-bright arm" got filed for a
  week. Replay twice with `--hand-from-tick` on and off, mask on the
  pixels that differ, and read golden/magma there.

During migration, existing Python uses **UV only**. Never add Python or install
a project Python package.

## Critical: anvil is headless

- Demos (png/mp4): scp to Mac; do not assume local image display.
- Human play: Moonlight/Sunshine `:0` or mcwindow (`docs/RUNBOOK.md`).
- Agent/trace: Xvfb `:1` via `bash java/start_vnc_client.sh` (VNC 5900, pw `redstone`).
- One client owns NetheriteMod port **25575** at a time.

## Gotchas

- Kill game: `pkill -9 -f '[G]radleStart'` (bracket required).
- Launch game standalone (`setsid`/`nohup`); never chain kill+launch+poll.
- Goldens from **real MC only**; C bit-match needs `-ffp-contract=off`.
- `java/oracle-src/` and generated `magma/assets/*_atlas.h` stay gitignored.
  Do not commit them. GitHub is this tree without those files.
- No emojis, no em dashes. Minimal diffs. Verify before claiming done.
- A replay that reports `magma_game failed (rc=-11)` and then
  `EOFError: No data left in file` is a **SIGSEGV in the first captured frame**,
  and the first thing to try is `make -C magma clean && make -C magma`. Seen
  2026-07-25: an incremental build in the main tree produced a binary that
  faulted inside `getenv` at the top of `gm_world_mesh_view` (a corrupted
  `environ`, i.e. heap damage). The same commit built clean in a worktree, every
  generated `assets/*.h` was byte-identical, and an ASAN build reported nothing;
  only the incremental objects were bad. Do not go hunting for a source bug
  before you have reproduced it from a clean build.
