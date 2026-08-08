# netherite (AGENTS.md)

Home: **Anvil-primary** - canonical at `anvil:~/dev/netherite`. Build and run
here. MacBook is control plane / Moonlight / image viewing only.

This file is the **only agent entry**. Do not hunt other root markdown for
instructions. How-tos and history live under `docs/`; living contracts live
next to the code they govern.

## Platform support

| OS | Role |
|----|------|
| **Linux x86_64** | Full stack. Build C/CUDA, run Java oracle, train blaze, sweep. Canonical host: anvil (Ubuntu). Needs JDK 8 + NVIDIA CUDA for GPU paths. |
| **macOS** | Control plane only. SSH, Moonlight/mcwindow viewer, image/video review. **Do not** expect native `runClient` or CUDA here (legacy GL under Rosetta is dead; no Blackwell/CUDA train path). |
| **Windows** | Not a supported build/run host for this monorepo. Use WSL2 Linux if you must, or a remote Linux box. |

Prism / MultiMC / official launcher: optional jar source for assets. Fresh
boxes do **not** need Prism credentials; `scripts/bootstrap_oracle.sh` pulls
MC 1.11.2 via ForgeGradle (you must own the game).

One-shot clean box: `bash scripts/setup_and_verify.sh` (then `--full` with GPU).

## What this repo is

From-scratch C/CUDA reimplementation of Minecraft 1.11.2 (magma + blaze),
bit-verified against the real Java game, plus a batched CUDA RL env (blaze).
Product name: **netherite**. Trees:

- `java/` - the oracle: Forge+Malmo/NetheriteMod (mod id qrl) client, launch scripts, oracle-src
  (bootstrap), render-opt kernel lab (closed)
- `blaze/` - the simulation: reference CPU + production CUDA tick (CPU == CUDA),
  batched RL env (`blaze/env/`) and trainers (`blaze/rl/`)
- `magma/` - the playable fidelity tier: blaze's tick + software rasterizer
- `verify/` - cross-stack harness: tapes, scenarios, gates, nightly sweep

Glossary and naming rationale (incl. the blaze mob collision): `NAMES.md`.

## Where to read (stop when you have enough)

| Need | Open |
|------|------|
| First clone / no oracle-src | `docs/BOOTSTRAP.md` |
| How to play, VNC, NetheriteMod, sweep | `docs/RUNBOOK.md` |
| Ship criteria / gate status | `docs/GATES.md` |
| Is X in the game? cut / pinned / open / unrecoverable | `docs/SCOPE.md` |
| Fidelity procedure | `magma/VERIFY.md` |
| Product contract / open bugs | `magma/PRODUCT.md`, `OPEN_DIVERGENCES.md` (closed forensics: `CLOSED_DIVERGENCES.md`) |
| Architecture for a tree | that tree's `SPEC.md` |
| History / lessons | `docs/DEVLOG.md` |
| Old reports | `docs/archive/` (ignore by default) |

## Commands

```bash
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64

# clean Linux box (bootstrap + build + sweep):
bash scripts/setup_and_verify.sh          # --quick pyramid
bash scripts/setup_and_verify.sh --demo   # + pixel SBS MP4 -> demos/pixel_match_sbs.mp4
bash scripts/setup_and_verify.sh --full   # + CUDA gates (needs free GPU)
# pixel demo alone (after bootstrap/build):
bash scripts/demo_pixel_sbs.sh


# or stepwise:
bash scripts/bootstrap_oracle.sh
bash scripts/bootstrap_assets.sh
make -C magma game
bash netherite_sweep.sh --quick

cd java/Minecraft && ./gradlew -g run/gradle build
uv run --no-project python blaze/oracle/runner.py <name>

# touching magma/cuda/raster_cuda.cu or magma/metal/raster_kernels.metal?
# The six kernels are hash-paired (verify/kernels/parity_manifest.json): edit
# BOTH twins, then prove it on both machines and re-record the manifest:
bash scripts/kernel_parity_gate.sh   # anvil: cpu==cuda; macbook: cpu==metal

# wrapper-vs-owr worldgen census pin (CPU; blessed residuals in sidecar):
bash verify/worldgen/wrapper_gate.sh              # rc=0 exact match vs known_divergences.json
# bash verify/worldgen/wrapper_diff.sh            # diagnostic report + load-order probe
# bash verify/worldgen/wrapper_gate.sh --update   # re-bless only with maintainer judgment
```

## Pixel investigation

When a tape frame is wrong, do not hand-roll numpy. The tool lives at
`verify/trace/pxdiff.py` and `--tape` resolves replay output from
`verify/trace/out/tape_<NAME>/`, so run it from `verify/trace`:

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
  and replay forwards it as `MAGMA_HIDE_GUI`. Oracle captures can also be
  wrong: the eat/bow viewmodel goldens are idle tips, not mid-use poses, and
  fitting C to them would be fitting to a bad reference.
- **A tape's first ~40 goldens are not steady state.** The oracle's
  `EntityRenderer.fogColor1` smoother had not converged at recstart, so t=0 is
  2-6x worse than t=10 on every tape and two tapes fail their gate on t=0
  alone. The recorder writes `fog_color1` in the header and replay seeds magma
  from it; tapes recorded before that field keep the old converged seed.
  `MAGMA_FOG_C1_INIT=<0..1>` overrides the seed for sweeping it on old tapes.
  Do not hardcode a value - it depends on the recording session, not the tape
  (see `magma/OPEN_DIVERGENCES.md`).
- **The end-crystal healing beam needs the client's `ticksExisted`.**
  `RenderDragon.renderCrystalBeams` scrolls `endercrystal_beam` by
  `-ticksExisted*0.01` per tick over a 16x256 sheet that is ~2x minified, so a
  one-tick phase error randomizes the whole glyph speckle. The recorder now
  writes it per entity (dragon field 18, crystal field 12); tapes older than
  that are reconstructed as `tick - first_seen + ent_ticks0`, default 7,
  overridable with `MAGMA_ENT_TICKS0`. The default is a measured sweep, not a
  guess: over the offset's full 100-tick period exactly one value is sharply
  better (76.8k differing px vs 109-114k at all 99 others). Re-sweep it rather
  than fitting anything else if a new End tape's beam looks like noise.
- **Measure a viewmodel residual against the render, not against a texel.**
  Dividing a golden by a raw atlas texel prices in shading the oracle also
  applies, and that is how a phantom "1.57x over-bright arm" got filed for a
  week. Replay twice with `MAGMA_HAND_FROM_TICK` on and off, mask on the
  pixels that differ, and read golden/magma there.

Python: **UV only** (`uv run`, never bare `pip`/`python` for project work).

## Critical: anvil is headless

- Demos (png/mp4): scp to Mac; do not assume local image display.
- Human play: Moonlight/Sunshine `:0` or mcwindow (`docs/RUNBOOK.md`).
- Agent/trace: Xvfb `:1` via `bash java/start_vnc_client.sh` (VNC 5900, pw `redstone`).
- One client owns NetheriteMod port **25575** at a time.

## Gotchas

- Kill game: `pkill -9 -f '[G]radleStart'` (bracket required).
- Launch game standalone (`setsid`/`nohup`); never chain kill+launch+poll.
- Goldens from **real MC only**; C bit-match needs `-ffp-contract=off`.
- Private remote only when oracle-src is present (decompiled Mojang source).
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
