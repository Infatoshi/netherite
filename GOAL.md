# GOAL - native command surface

Invoke with `/goal` + this file. Owner of the loop: the top-level session.
Everything below is standing instruction for that loop and every delegate.

Speak ASD-STE100 to the user. One meaning per word. Active voice. Short
sentences. No filler.

## The goal

`SPEC.md` is the product. The product has no Python runtime, no Python
build step, and no LibTorch. The product has no project shell script
except the Gradle wrapper.

Java oracle: `./gradlew` in `java/Minecraft`. First clone:
`make -C java bootstrap-oracle`. No root `make oracle`.

Make is the native surface:

```text
make            host native backends (magma game; Metal on Darwin)
make assets     magma/assets/build.c
make test       short native units only (<180s)
make play       magma_game
make clean      owner clean targets
```

Do not add root `make verify` or `make train` until those C binaries
exist and are the only path. Runtime knobs are `--conf` / `--set`.
No env knobs.

A slice is done only when the native replacement exists, its output
matches the current owner, every caller moved, and the old file is
deleted in the same commit. Do not keep two command paths.

## Already done. Do not redo

- Repo layout: `out/<owner>/`, no `CLAUDE.md`, demo media are temporary.
- `magma/assets/build.c` writes the 13 texture headers.
  `make -C magma assets` and `make -C magma test-asset-build` pass.
  Sound manifest is C (`magma/assets/build_sound.c`).
  `scripts/bootstrap_assets.sh` is gone. `make assets` is the only path.
- `blaze/nn` has CPU, CUDA (cuDNN/cuBLAS), and Metal policy ABIs.
- `blaze/rl/ppo.c` is the native trainer gate. Torch trainers still exist.
- `make -C java bootstrap-oracle` and `make -C verify public-export` exist.

## Tonight

Advance the SPEC command surface. Prefer delete over translate.
The flywheel bottleneck is compile / test / one short replay, not tokens.
Every compile and test has a wall-clock cap. Default 60s. Hard max 180s
unless this file names a longer cap. Kill the process at the cap.
Do not run a full tape, a long sim, or an overnight train.

### P0 CENSUS (one read-only delegate, 20 min)

List every remaining `.py` and project `.sh` on the product path.
Owner, callers, keep / replace / delete. Write the list to
`out/verify/native_surface_census.txt`. No code edits.

Priority order for later phases:

1. Root Makefile wrappers around live Make targets.
2. `scripts/` that are now one-line wrappers. Delete them.
3. Sound manifest is C. Skip.
4. Verify hot path: tape read, replay drive, pixel compare.
5. Sweep: `netherite_sweep.sh` becomes `make test` / `make verify`.
6. Blaze Python trainers and `blaze/rl/native/ppo_native.cpp` (LibTorch).
7. Unified `netherite/config.def`. Do not invent keys.

### P1 ROOT MAKE (one Mac delegate)

Add a root `Makefile` that calls into `magma/`, `blaze/`, `java/`,
`verify/`. Wire only what already exists:

- `make` -> host backends (`make -C magma game`, Metal on Mac)
- `make assets` -> `make -C magma assets`
- `make test` -> native unit tests that finish under 180s
- `make play` -> `make -C magma game` then document the binary
- `make clean` -> remove `out/` build products the Makefiles own

Do not invent `make verify` or `make train` until those binaries exist.
Delete `scripts/bootstrap_assets.sh` after `make assets` is the only path.
Update `AGENTS.md` and `docs/BOOTSTRAP.md` in the same slice.

Acceptance: `make assets` and `make test` from the repo root, each
under the cap. `scripts/bootstrap_assets.sh` is gone.

### P2 SOUND MANIFEST (one Mac delegate)

Replace `magma/assets/build_sound_manifest.py` with C next to
`magma/assets/build.c`. Same SPEC change rule. No jar. No env.
`make -C magma assets` may keep texture and sound as two steps
if that is simpler. Delete the Python file in the same slice.

Acceptance: ASan/UBSan test under 30s. Header data matches.

### P3 VERIFY READER (one Mac or Anvil delegate)

Start the C tape path. Do not port `pxdiff.py` tonight.

Write `verify/tape/` C that reads one committed tape jsonl header
and tick rows enough to print tick count, first pose, and hash.
Cap 60s. Compare against a known tape. Then stop.

Do not replay frames. Do not start the oracle. Do not touch
`known_divergences.json` or gate thresholds.

Acceptance: `make -C verify tape-info TAPE=...` prints those fields
and exits 0 in under 60s on
`verify/tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl`.

### P4 BLAZE CUTOVER MAP (one read-only delegate, 20 min)

Map what `blaze/rl/ppo.c` already owns vs what `ppo_coal.py`,
`ppo_break.py`, `vec_env.py`, and `blaze/rl/native/ppo_native.cpp`
still own. Output `out/blaze/trainer_cutover.txt`. No code edits.
Do not train. Do not touch the GPU unless nvidia-smi shows it idle
and the work is a unit fixture under 60s.

### P5 MERGE

Top-level session only. Review each worktree diff. Re-run that
slice's acceptance in the main tree. Merge serially. Append a
dated `docs/DEVLOG.md` section: slices landed, files deleted,
caps hit, work left.

## Hierarchy

- Top level: this session. Owns merges, census accept, and judgment.
- Workers: isolated worktrees via `scripts/agent_worktree.sh NAME`.
  One slice each. Grok or Codex. Prompt is a spec with the
  acceptance command and the timeout. They run the command.
- Delegates NEVER: merge, push, start the oracle, edit gate
  thresholds, edit `known_divergences.json`, run a tape replay,
  run a train loop, or touch files outside their worktree.

## Hard constraints

- Tokens are cheap. Wall time is not. Cap every compile and test.
- `/tmp` on anvil is tmpfs. Use `TMPDIR=$HOME/dev/nw/.tmp` and
  `UV_CACHE_DIR=$HOME/.cache/uv` if a leftover Python tool must run.
- GPU0 only. `nvidia-smi` first. Skip if busy. Never GPU1.
- Private repo only. No public push.
- `java/oracle-src` is read-only. Never quote it at length.
- Do not translate a tool that has no permanent owner. Delete it.
- Do not change asset format from headers to a pack tonight.
- Do not start a second Metal or CUDA policy path.
- Suspicious measurement: re-run once under the same cap before
  you believe it.

## Morning

`docs/DEVLOG.md` names what landed, what was deleted, what still
has two command paths, and the next one-slice pick.
