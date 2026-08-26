# SUBAGENT.md - the lane routine for one open divergence

You are a delegated agent on one lane. The parent agent staged your lane,
hands you this file plus a short goal block, verifies your work itself, and
merges. You never merge. Follow this routine in order.

## 0. Workspace and sync loop

- Mac worktree `~/dev/nw/<lane>` on branch `lane/<lane>`. Edit and commit
  there only. Never touch `~/dev/netherite` or another worktree.
- Remote clone `<host>:~/nlanes/<lane>` on the same branch. Builds, gates,
  replays, and tests run there (exception: Metal builds run on the Mac).
- Loop: commit on the Mac -> `git push origin lane/<lane>` (lane branch
  only; never master, never merge) -> on the remote
  `cd ~/nlanes/<lane> && git fetch -q origin && git reset --hard origin/lane/<lane>`
  -> build and run there.
- Anything over ~60 s runs in a tmux session named `<lane>` on the remote and
  writes its full output to `~/nlanes/<lane>/out/verify/<lane>_<what>.log`.
  Poll at >= 60 s, one ssh connection at a time. A tmux pane is not a record;
  the log file is.
- First remote run builds from scratch: `make assets && make -j8` for magma;
  for blaze rows read `blaze/README.md` and `blaze/env/port_matrix.yaml`.
- A fresh clone is missing untracked inputs that live only in the host's
  `~/dev/netherite`: `java/oracle-src`, `java/Minecraft/run`, the ui_hud
  goldens (`verify/ui_hud/goldens`, 38 files; a clone has 6) and the
  ui_entities goldens, and the canon tape jsonl under `verify/tapes`.
  `rsync -a --ignore-existing ~/dev/netherite/<dir>/ <dir>/` for each
  before the first gate; then `make assets` from YOUR branch (host-tree
  headers are stale). A gate that prints `MISSING JAVA` measured nothing.
- Python is `uv run --no-project --with <pkgs> python ...`. No pip. Pin
  `UV_CACHE_DIR=$HOME/.cache/uv TMPDIR=$HOME/dev/nw/.tmp` on anvil.

Linux validation host is `anvil` (faster CPU). Magma CPU, tape replay,
`make test`, blaze M1/M2, and `eval` run there. Do not stage those on
`gamer`. Anvil is shared: never touch tmux sessions or processes you did
not create; GPU only through `overnight-compute wait --agent <lane>
--resource gpuN`, check `nvidia-smi` first, release when the GPU run
ends, analyze on CPU. Oracle goldens come from anvil llvmpipe only.
`gamer` is 3090 / `sm_86` only.

## 1. Java first, then C

Every divergence closes by porting what Java does, not by fitting what the
golden shows.

1. Find the oracle code in `java/oracle-src` (symlinked in both trees).
   Read the whole call chain for the tick or frame in question, not one
   method. Note the tick order (World ticks players before mobs, client vs
   server, partialTicks at render).
2. When float vs double or a cast decides a bit, do not trust MCP source:
   `javap -c` on the deobfuscated jar decides the cast sites.
3. Port call for call. Same types, same order, same RNG consumption. Cite
   `File.java:line` for every constant and branch in a code comment.
4. A constant you cannot cite is not allowed. An observed value from a golden
   (0.887, a color triple, a texel bias) is never a port. If a value is
   unrecorded by the recorder, that is a Class C blocker: say so.
5. Add a unit test for each ported semantic (`magma/tests`, `magma/game/test_*`,
   or the blaze test that the row's gate runs).

## 2. Baseline before code

Reproduce the documented FAIL numbers on the remote host first. If your
baseline differs from the docs, stop and report; do not start from an
unknown state. Keep the baseline log.

## 3. Harness, by evidence type

Tape digests (sim ports):
```bash
cd verify/trace
uv run --no-project --with numpy --with pillow --with nbt python replay_tape.py \
    ../tapes/<tape>.jsonl --cpu --no-gate --report
```
Read `physics`, `inventory`, `entities`, `world_hash`, and `FIRST MISMATCH`.
The first divergent tick is the fact; everything after it is downstream.
Dump the tape rows around it (`make -C verify tape-info`) before theorizing.
`replay_tape.py` loads the magma build under `magma/`; rebuild it after
every edit (`make -C magma`), root `make test` does not.

Tape pixels (render ports): `verify/trace/pxdiff.py` (`survey` first, then
`clusters`, `probe`, `pixels`). The AGENTS.md "Pixel investigation" section
lists how to read it and the traps already paid for. Never report
`unresolved` as a diagnosis.

Capture gates (HUD, hand, overlays, entities, GUI):
```bash
bash verify/ui_hud/run_ui_hud_gates.sh
bash verify/ui_entities/run_oracle_gate.sh
bash verify/mc_capture/run_gui_verify.sh
```
A row is PASS at `nz==0`, PASS-LSB only through the guarded tier (A/B noise
0, every pixel <= 1 per channel, count <= 2% of ROI, mutation guard green),
else RESIDUAL. Compare goldens as rendered, not divided by texels.

Blaze port rows:
```bash
uv run --no-project --with pyyaml python blaze/env/port_matrix.py --subsystem <row> --tier m1
uv run --no-project --with pyyaml python blaze/env/port_matrix.py --subsystem <row> --tier m2   # GPU
```
M1 is magma-CPU vs blaze-CPU lockstep digest; M2 is blaze-CPU vs CUDA
bitwise. `port_matrix.py` does not build. Root `make` does not rebuild
`blaze/env/blaze_cpu.so` or `blaze_cuda.so`; after every fetch/reset run
`rm -f blaze/env/blaze_cpu.so blaze/env/blaze_cuda.so && make -C magma blaze_so blaze_cuda_so`
(CUDA needs `PATH=/usr/local/cuda/bin:$PATH CUDA_HOME=/usr/local/cuda`).
A stale library from another branch gives a clean FAIL or a false PASS. A new row needs fixtures under `verify/fixtures/port` and
`blaze/rl/fixtures`, an `m1`/`m2` command in `port_matrix.yaml`, and
`supported: true` only after both tiers pass. Rows already VERIFIED must
stay VERIFIED.

Kernel twins (`magma/cuda/raster_cuda.cu`, `magma/metal/raster_kernels.metal`,
`blaze/core/obs_camera.h`, `blaze/env/blaze_metal_obs.metal`): edit both
sides of a pair, prove cpu==cuda on anvil and cpu==metal on the Mac with
`bash scripts/kernel_parity_gate.sh`, then re-record hashes with
`kernel_pairs.py --update`. Never edit one twin alone.

## 4. Hard rules

- Never fake a PASS. No tolerance or threshold edits, no `--update` or bless
  flags, no fitted constants, no skipped frames, no retired goldens.
- Runtime toggles go through the config registry (`magma/core/config.def`,
  `blaze.conf`, `ppo.conf`), never env vars.
- Temporary traces and scripts are removed before you finish.
- Root `make test` on the remote clone must pass before you report.
- Do not grind items the divergence file marks "Do not grind".
- An honest negative is a valid result: if the evidence shows a recorder gap
  or an unrecorded oracle state, reclassify the item (Class C) with the
  measurement that proves it, and stop.

## 5. Docs

- `magma/OPEN_DIVERGENCES.md` or `blaze/OPEN_DIVERGENCES.md`, whichever owns
  the item: update the numbers; on close leave a one-line stub in place and
  move the forensics to `magma/CLOSED_DIVERGENCES.md`.
- Prepend a dated entry to `docs/DEVLOG.md`: baseline, cause with Java
  citations, after numbers, what stays open.

## 6. Final report (the parent reads only this)

1. Files changed, one line each.
2. Oracle citations (`File.java:line`) for every ported constant or branch.
3. Verbatim gate tails before and after, from the remote host, with log paths.
4. Remaining residual numbers.
5. What is not closed and why.

Leave the lane branch pushed. Do not merge.
