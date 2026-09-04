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

## Current work

Replay contract: `lane/replay-contract`. Controller ownership:
`lane/player-state`. CUDA dimension allocation and dispatch:
`lane/cuda-dimensions`. Shared transfer semantics and integration: primary agent.

Anvil main was `fae41d7` at inspection; isolated validation uses
`~/nlanes/runtime-architecture`. Both GPUs were busy at initial inspection.
