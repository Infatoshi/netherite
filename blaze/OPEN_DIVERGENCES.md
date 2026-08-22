# Open divergences: magma -> blaze

This file tracks the second bridge: what the batched RL environment (blaze)
does not yet reproduce of the verified magma game. The first bridge
(oracle -> magma) lives in `magma/OPEN_DIVERGENCES.md`.

Definitions, so the list stays honest:

- A **divergence** here is a MEASURED mismatch: magma-CPU vs blaze-CPU
  lockstep digest (M1), or blaze-CPU vs blaze-CUDA bitwise (M2). Metal
  simulates through the CPU driver by design (`blaze_cpu.so`; Metal supplies
  obs/NN only), so every CPU result covers Metal until the native Metal tick
  (M3) exists.
- An **unported subsystem** is a coverage gap, not a measured divergence:
  no claim exists either way. Both are listed because both block transfer.
- Gate runner: `blaze/env/port_matrix.py` over `blaze/env/port_matrix.yaml`
  (fail-closed; VERIFIED / BLOCKED / FAILED per row and tier).

Last verified: master 2026-08-22 (post lane/handscene merge).

## Verified rows (no known divergence)

| row | M1 (magma-CPU vs blaze-CPU) | M2 (blaze-CPU vs CUDA) |
|---|---|---|
| mining_slice | VERIFIED | VERIFIED |
| spawn_to_torch | VERIFIED (chain 2058 actions) | VERIFIED |
| world_dynamics | VERIFIED | VERIFIED |
| fluids | VERIFIED | **no M2 defined** |

Known gap on a ported row: `fluids` has an empty `m2` command in the matrix.
The CUDA fluid path has no bitwise gate. Define it before trusting fluid
behavior in CUDA training.

## Unported rows (coverage gaps), in dependency order

From `port_matrix.yaml` `supported: false` block reasons. Depth-1 rows can
start any time; deeper rows wait on their deps.

| row | deps | blocked on |
|---|---|---|
| random_ticks | world_dynamics | scheduling/effects not measured by both backends |
| falling_blocks | world_dynamics | falling-block state not measured by both backends (magma-side sim landed 2026-08-01; blaze has none) |
| chests | spawn_to_torch | generation, loot, GUI transfers not measured end to end |
| weather_optional | world_dynamics | weather transitions not measured by both backends |
| entity_spine | spawn_to_torch | entity lifecycle not in the common parity record |
| projectiles | world_dynamics, entity_spine | projectile lifecycle/collision not measured |
| explosions | world_dynamics, projectiles | damage + world mutation not measured |
| mobs | world_dynamics, entity_spine, projectiles | spawning, AI, combat, drops lack common evidence |
| portals_dimensions | world_dynamics | portal transfer and dimension identity not measured |
| nether_route | spawn_to_torch, portals_dimensions | no strict cross-backend fixture |
| boats_elytra_xp | fluids, entity_spine | boats/elytra/XP lack common evidence |
| dragon_victory | nether_route, mobs, explosions | not verified end to end; dragon-fight RL is out of scope per GATES |

Two consequences worth stating plainly:

- **Dimensions do not exist in blaze.** The GPU sim is overworld snapshots
  only; Nether and End (`portals_dimensions`, `nether_route`) are entirely
  on the magma side today.
- **Mobs do not exist in blaze.** The deterministic mob arc (detmob) is
  magma-CPU only. The agreed GPU design is `blaze/GPU_MOB_AI.md` (v2,
  codex-reviewed): one warp per env, lane-0 sequential mob tick with A*
  inline, magma-semantics (32x24x32 window, 48-point path cap), IntHashMap
  aliasing reproduced, all 8 detmob tapes must be blaze-exact.

## Prerequisites discovered in design review (block the entity arc)

- Snapshot v2 carries no mob or entity-RNG state (`blaze_snapshot.h:29`);
  `BP_MOBS` exists but is unimplemented (`port_parity.h:83`). A mob-capable
  snapshot revision + canonical BP_MOBS hashing must land before
  entity_spine parity can be measured.
- Device FP census: mc_atan2 is host-only (`blaze/core/mc_math.h:63`);
  sqrt/sin/cos/log need device implementations with CPU/CUDA bit-pattern
  tests before any entity math ports.
- `WalkNodeProcessor.getStart` HashSet iteration order is an admitted gap in
  the shared port (`blaze/core/path_node_processor.h:578`); needs a
  Java-backed fixture before that branch may execute.

## Transfer gaps (not parity divergences; tracked in docs/GATES.md)

- Native ppo spawn->torch t0 is 0.215 (chain4 curriculum, 510M ticks) vs the
  0.4 target; stage4->torch is 8/8 seeds. GATES row 1.
- No native transfer/eval of `ppo_ckpt.bin` into magma (torch eval scripts
  removed). GATES row 2.
- Python still owns replay/pixels/M2 verify; no binary tape; no root
  `make verify`. GATES row 13.
- Blaze native Metal tick (M3) is sequenced after CUDA survival rows pass
  M1+M2. GATES row 5.

## Do not

- No tolerances, no `--update` blessing, no fitted constants; a row is
  VERIFIED only by its matrix gate.
- Kernel twins (`blaze/core/obs_camera.h`, `blaze/env/blaze_metal_obs.metal`
  and the magma raster twins) require the two-machine parity flow; never
  edit one side alone.
- Capacities are compile-time constants; runtime toggles go through the
  config system, never env vars.
