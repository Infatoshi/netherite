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

From-scratch C/CUDA reimplementation of Minecraft 1.11.2 (magma + mc-sim),
bit-verified against the real Java game, plus a batched CUDA RL env (blaze).
Product name: **netherite**. Trees:

- `java/` - playable Forge+Malmo/qrl client, launch scripts, oracle-src (bootstrap)
- `c/magma/` - product C game + software rasterizer + RL
- `c/mc-sim/` - simulation kernels (CPU == CUDA)
- `c/render-opt/` - verified render kernels + JNI drop-ins (lab closed)

## Where to read (stop when you have enough)

| Need | Open |
|------|------|
| First clone / no oracle-src | `docs/BOOTSTRAP.md` |
| How to play, VNC, qrl, sweep | `docs/RUNBOOK.md` |
| Ship criteria / gate status | `docs/GATES.md` |
| Fidelity procedure | `c/magma/VERIFY.md` |
| Product contract / open bugs | `c/magma/PRODUCT.md`, `OPEN_DIVERGENCES.md` |
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
make -C c/magma game
bash netherite_sweep.sh --quick

cd java/Minecraft && ./gradlew -g run/gradle build
uv run --no-project python c/mc-sim/oracle/runner.py <name>
```

## Pixel investigation

When a tape frame is wrong, do not hand-roll numpy. `c/magma/raster/verify/trace`:

```bash
U="uv run --no-project --with numpy,scipy,pillow python"
$U pxdiff.py selftest                                   # trust the tool first
$U pxdiff.py frames  --tape <NAME>                      # rank ticks by unexplained px
$U pxdiff.py clusters --tape <NAME> --tick N            # cluster table + CAUSE
$U pxdiff.py zoom    --tape <NAME> --tick N --cluster 0 --scale 10 -o /tmp/z.png
$U pxdiff.py probe   --tape <NAME> --tick N --cluster 0 # every discriminator
$U pxdiff.py pixels  --tape <NAME> --tick N --cluster 0 # exact RGB pairs
```

Causes: `texel-selection`, `shading-offset`, `registration`, `cutout-sky+/-`,
`content`, `edge`, `unresolved`. `--a/--b` takes any PNG pair, so the same tool
drives the mc_capture / ui_hud / ui_entities gates. `grind.py` ranks a whole
tape by mean/ch; `pixel_gate.py` decides pass/fail. Never report `unresolved`
as a diagnosis, and never claim a cause the tool did not measure.

Two things that make a pixel measurement lie, both paid for already:

- **Do not replay tapes in parallel across worktrees** unless you are on
  `b9fe039` or later. `tapes/` is symlinked into every agent worktree, and the
  `.snapshot_patch.jsonl` cache keys its staleness off `snapshot_patch.py`'s
  mtime, so concurrent replays all regenerate the same file at once. Before
  that fix they clobbered each other and a clean tape measured 3.63/ch terrain
  against a 0.94/ch baseline. If a number looks like a regression, re-measure
  with nothing else running before you believe it.
- **Check what the goldens actually contain before chasing a diff.** A tape
  recorded through Malmo has `hideGUI` forced on for the whole mission, so its
  goldens have no HUD at all; `capture.hide_gui` in the tape meta is the
  measured value (`qrl_launch.hide_gui` is only what the launcher asked for)
  and replay forwards it as `MAGMA_HIDE_GUI`. Oracle captures can also be
  wrong: the eat/bow viewmodel goldens are idle tips, not mid-use poses, and
  fitting C to them would be fitting to a bad reference.

Python: **UV only** (`uv run`, never bare `pip`/`python` for project work).

## Critical: anvil is headless

- Demos (png/mp4): scp to Mac; do not assume local image display.
- Human play: Moonlight/Sunshine `:0` or mcwindow (`docs/RUNBOOK.md`).
- Agent/trace: Xvfb `:1` via `bash java/start_vnc_client.sh` (VNC 5900, pw `redstone`).
- One client owns qrl port **25575** at a time.

## Gotchas

- Kill game: `pkill -9 -f '[G]radleStart'` (bracket required).
- Launch game standalone (`setsid`/`nohup`); never chain kill+launch+poll.
- Goldens from **real MC only**; C bit-match needs `-ffp-contract=off`.
- Private remote only when oracle-src is present (decompiled Mojang source).
- No emojis, no em dashes. Minimal diffs. Verify before claiming done.
- A replay that reports `magma_game failed (rc=-11)` and then
  `EOFError: No data left in file` is a **SIGSEGV in the first captured frame**,
  and the first thing to try is `make -C c/magma clean && make -C c/magma`. Seen
  2026-07-25: an incremental build in the main tree produced a binary that
  faulted inside `getenv` at the top of `gm_world_mesh_view` (a corrupted
  `environ`, i.e. heap damage). The same commit built clean in a worktree, every
  generated `assets/*.h` was byte-identical, and an ASAN build reported nothing;
  only the incremental objects were bad. Do not go hunting for a source bug
  before you have reproduced it from a clean build.
