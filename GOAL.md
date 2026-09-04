# Runtime architecture repair

Authorized 2026-09-04 after the architecture review at `4ccbf35`.

## Required result

- Native replay rejects child failures, incomplete or malformed traces, and
  measured state differences. Regression tests exercise the executable contract.
- Magma owns player-controller state per runtime. Blaze and Magma share its
  simulation declaration; rendering and audio transients remain explicit.
  Interleaved runtimes must not affect one another. Snapshot formats stay stable.
- Blaze keeps private mutable world regions for each supplied dimension across
  round trips, computes portal arrivals from world contents, and reports missing
  dimension data as an error. CPU and CUDA execute the same transfer semantics.
  Fixed snapshot regions remain the supported world boundary; missing world
  coverage must not be treated as air during portal search.
- Current architecture comments and specifications describe the implemented
  paths and their remaining coverage limits.

## Validation and integration

Linux builds and validation run in isolated Anvil worktrees based on `4ccbf35`.
Existing Anvil worktrees and GPU jobs remain owned by their current users.
Native regression tests, controller isolation, portal round trips and mutation
preservation, Magma/Blaze CPU comparisons, supported CPU/CUDA comparisons, and
root native tests must pass on the integrated tree. GPU checks require an idle
leased device. No changed tolerance or skipped evidence can close a failure.

## Integrated result and remaining validation

Implementation is pushed on `lane/runtime-architecture`, based on the reviewed
`4ccbf35` from the pre-existing `wip/nn-fable` branch. Do not merge its unrelated
base changes into master as part of this task. Anvil main was `fae41d7` at
inspection and remains untouched. Final validation uses the isolated
`anvil:~/nlanes/runtime-architecture` at source revision `30b2fb6`, including the
Mac graph-compilation and replay-declaration fixes.

Replay and controller lanes are integrated. Native replay passes 32 executable
contract cases, including sanitizer validation, and an eight-action real tape.
All 41 mutable controller variables have explicit runtime ownership. Dimension
tests pass 10,841 public-ABI checks; the strict 346-action Magma/Blaze CPU round
trip passes player, portal, dimension, world and random-tick comparisons.
Creation planning, negative coordinates, fluid scheduler retention and lava
cadence have native unit coverage. The portal tape has no active fluid evidence.

Independent CPU audit at `9959957` passed root make test and 29 configured
subsystem rows. The remaining mining row's original v1 snapshot could not
support resume because it lacked light. The matrix now uses the complete v2
seed-10 fixture: direct 1,000-tick and N=968/M=32 resume comparisons both pass.
The original fixture remains intact.

Final Linux root, lifecycle, mining (including resume) and portal gates at
`75430fe` all returned zero. Their logs and `.rc` files are under
`out/verify/architecture-final-*`, copied to the Mac for review. The independent
subsystem audit is `out/verify/integrated-9959957-summary.tsv`.
`architecture-final-cpu.sha256` records the tested Anvil artifacts:

- `magma/magma_game`: `6554e977a3dfb5f156b2cd3412776d9edeb29d63a5b2930cb8d33d829fb0433b`
- `blaze/env/blaze_cpu.so`: `47cfba67d389f2478d2823f23ab58c88c5c33596a58b6e14eb14908c95c3542e`

Final CUDA compilation passed with both scalar and warp paths using the dev
profile. This is not runtime parity or performance evidence. The CPU/CUDA/game
binary hashes are in `out/verify/architecture-final-artifacts.sha256`. Both
libraries were deleted and rebuilt again after integrating `30b2fb6`.
The final integrated audit passed root `make test` (176 seconds), the native
dimension lifecycle test, and all 30 supported CPU comparison rows with their
configured resume gates. `out/verify/architecture-integrated-cpu/summary.tsv`
records 32 zero-return checks; `exit-code` is zero. Source revision and binary
hashes were checked before and after the audit. Logs are copied to the Mac
under that same relative path.

Both Anvil GPUs are occupied by unrelated training. The bounded detached
validation job is `runtime-architecture-gpu` in tmux, using
`out/verify/architecture-gpu-validation.sh`. It waits up to 24 hours for the
final build, a passing integrated CPU audit and an actually idle GPU1, then
acquires the coordinator lease `netherite-architecture-30b2fb6`. It checks the
pinned source revision and GPU
occupancy again before execution. It runs the public CUDA lifecycle test and
all 30 supported CPU/CUDA rows, including each configured kernel and resume
check. It never stops another job.

GPU preflight at `30b2fb6` found all required artifacts for the 29 runnable
rows. `mobs_det`, the deterministic mob-AI row, is CPU-only despite its shared
`supported: true` flag: it deliberately has no CUDA command or setter. This is
a pre-existing blocked row, documented in `blaze/OPEN_DIVERGENCES.md`; porting
that AI is outside these architecture repairs. The watcher records its rc=3
instead of skipping or passing it, so even 29 clean runnable rows will leave
the full-matrix exit code nonzero. Inspect individual results and preserve
that distinction when reporting GPU coverage.

Watch `out/verify/architecture-gpu-watch.log`; results, revision and binary
hashes go under `out/verify/architecture-gpu/`. `results.tsv` records each
return code and `exit-code` records completion. A waiting process is not a
passed gate. Review every nonzero row before retrying or promotion. Retain the
validation worktree until GPU review is complete; then remove completed task
lanes and the generated watcher script.

Mac CPU and Metal game builds pass. Root make test exposed a pre-existing
Metal neural-network shutdown race: asynchronous MPSGraph compilation can
outlive process teardown. A fresh unpatched build crashed in two of five runs;
the minimal synchronous-compilation fix passed ten of ten. It is integrated in
`30b2fb6`, alongside the Mac replay-test declaration fix. Final Mac root
`make test` passed on that integrated revision in 93 seconds, including Metal
NN and all 32 replay contract cases. Evidence is copied to the main Mac checkout:
`out/verify/architecture-mac-root-final.log`, its `.rc` file (zero), and
`out/verify/architecture-metal-ten-runs.tsv`. The tested isolated Metal
executable SHA256 is
`ec13cc9b878cbbbd0866e6d1d8e429863bb531a87ff40c135f67c2d2f5fc1624`.
