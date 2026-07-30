# GOAL - overnight fidelity flywheel

Invoke with `/goal` + this file. Owner of the loop: the top-level Claude
session on anvil. Everything below is standing instruction for that loop and
for every delegate it spawns.

## The goal

Every recorded tape gates rc 0 with ZERO unexplained pixels, for every
feature the oracle exposes that magma claims to implement. "Done" for a
feature means: a scenario yaml exists, a Java oracle tape exists (frames,
physics, inventory, entities, world hash), the magma replay is bit-verified
against it at the frame level on CPU and CUDA, and any residual divergence is
either fixed in C or PROVEN to be a recorder/tape limitation and filed in
`magma/OPEN_DIVERGENCES.md` + the tape's `known_divergences.json` with
evidence. The goal does not stop while any tape has an UNEXPLAINED cluster
that is not so proven.

Full pixel one-to-one has known hard limits (docs/SCOPE.md section 4:
particle placement RNG, torch flicker, pre-capture fog history, server-clock
skew). Those are closed by proof-and-file, not by silently accepting diffs,
and never by loosening a threshold.

## The flywheel (phases)

P1 CENSUS - fan out read-only delegates over `java/oracle-src` registries
   (Block.registerBlocks, Item.registerItems, entity list, mechanics) vs
   magma (`magma/game/block_registry.h`, item tables, entity_render.c,
   runtime.c). Output: `docs/CENSUS.md`, one row per block/item/entity/
   mechanic: implemented / partial / missing / cut (cut = listed in
   docs/SCOPE.md section 1). No code changes in this phase.
P2 SYNTHESIZE - for every non-cut row not covered by an existing tape
   (`verify/tapes/`), author a scenario yaml per the contract
   in `verify/scenarios/README.md`. Small, staged, one system
   per scenario. Validate with `uv run ... scenarios/test_scenario.py`.
P3 RECORD - `scripts/scenario_queue.sh LIST` records serially through the
   single oracle (flock on /tmp/qrl_25575.lock). Each recording auto-replays
   and gates; results land on the jsonl status board. ONE queue process; no
   delegate ever starts the oracle or touches qrl port 25575.
P4 FIX - every gate failure becomes one delegate task: worktree via
   `scripts/agent_worktree.sh NAME`, fix in C, acceptance via
   `scripts/delegate_gate.sh TAPE` (target tape rc 0 + regression pins rc 0
   + zero gate-config tampering). Commit to the wt/ branch only.
P5 MERGE - top-level session only: review diff, rerun delegate_gate.sh in
   the main tree, merge serially, regenerate asset headers, rebuild CPU and
   CUDA (`magma_game` and `magma_game_cuda` both - stale CUDA binaries score
   stale frames), sweep the pinned regression set.
P6 CUDA PARITY - after merges: CPU vs CUDA replay byte-agreement on every
   new tape (GPU0 only, `nvidia-smi` first). Perf work on the threaded GPU
   renderer is welcome ONLY behind unchanged bit-parity.

The loop repeats P2-P5 until the census has no uncovered non-cut rows and
the board has no unproven failures.

## Hierarchy

- Top level: Claude session (this loop). Owns merges, the recording queue,
  the board, and all judgment calls.
- Orchestrators: codex (`codex exec -c model_reasoning_effort=high`,
  background, `< /dev/null`). Each owns a domain (blocks / items / entities /
  mechanics) and spawns grok workers for atomic subsections.
- Workers: grok (`grok --always-approve -m grok-4.5 -p "..."` - flag order
  matters, -p last). One small subsection each: one census domain slice, one
  scenario yaml, or one failing tape.
- Delegates NEVER: merge, push, start the oracle, edit gate thresholds or
  divergence classes, edit known_divergences.json, touch GPU1, or touch
  files outside their worktree.

## Hard constraints (violations invalidate the night)

- Gate thresholds and divergence classes are frozen. A "pass" achieved by
  retuning is a lie; delegate_gate.sh enforces this mechanically.
- known_divergences.json entries require proof (the OPEN_DIVERGENCES.md
  written standard) and are filed by the top-level session only.
- Python: `uv run --no-project --with ...` with
  UV_CACHE_DIR=/home/infatoshi/.cache/uv TMPDIR=/home/infatoshi/dev/nw/.tmp
  (/tmp is RAM tmpfs).
- GPU0 only; check nvidia-smi before every CUDA run.
- Private repo only; nothing is pushed to the public repo overnight.
- java/oracle-src is read-only reference (decompiled Mojang source, never
  leaves the machine, never quoted at length into committed files).
- Suspicious measurement: re-derive expected before believing observed.
  pxdiff shift exactly (3,3) is the search boundary, not a real shift.

## Status

Board: `verify/trace/report/overnight_board.jsonl`
Morning report: append a dated section to docs/DEVLOG.md - tapes recorded,
pass/fail, fixes merged, residuals filed, census coverage percentage.
