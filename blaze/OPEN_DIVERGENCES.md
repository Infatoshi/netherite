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

Last verified: lane/projectiles 2026-08-22 (projectiles M1+M2; weather_optional, falling_blocks, entity_spine, random_ticks already on master).
Last verified: lane/chests 2026-08-22 (chests M1+M2; falling_blocks, entity_spine, and random_ticks already on master).
Last verified: lane/weather 2026-08-22 (weather_optional M1+M2; falling_blocks, entity_spine, random_ticks already on master).

## Verified rows (no known divergence)

| row | M1 (magma-CPU vs blaze-CPU) | M2 (blaze-CPU vs CUDA) |
|---|---|---|
| mining_slice | VERIFIED | VERIFIED |
| spawn_to_torch | VERIFIED (chain 2058 actions) | VERIFIED |
| world_dynamics | VERIFIED | VERIFIED |
| fluids | VERIFIED | VERIFIED (chain 61 actions) |
| entity_spine | VERIFIED (chain 32 actions, `--features mobs`) | VERIFIED (64 CUDA lanes) |
| random_ticks | VERIFIED (200 idle ticks, 27 tickable-cell mutations) | VERIFIED (64 CUDA lanes) |
| falling_blocks | VERIFIED (chain 64 actions, `--features falling_blocks`) | VERIFIED (64 CUDA lanes) |
| weather_optional | VERIFIED (chain 64 idle, rain flip t=50, `--features weather`) | VERIFIED (64 CUDA lanes) |
| projectiles | VERIFIED (chain 64 draw/release, `--features projectiles`) | VERIFIED (64 CUDA lanes) |
| chests | VERIFIED (chain 41 actions, `--features chests`) | VERIFIED (64 CUDA lanes) |

## Unported rows (coverage gaps), in dependency order

From `port_matrix.yaml` `supported: false` block reasons. Depth-1 rows can
start any time; deeper rows wait on their deps.

| row | deps | blocked on |
|---|---|---|
| falling_blocks | world_dynamics | closed 2026-08-22: EntityFallingBlock / BlockFalling sand+gravel live tick; anvil/dragon-egg and item drop on failed mayPlace stay out |
| weather_optional | world_dynamics | closed 2026-08-22: WorldInfo rain/thunder timers + worldTime; strength fade and sky stay magma-inert |
| chests | spawn_to_torch | closed 2026-08-22: placed single-chest TE + PICKUP/QUICK_MOVE transfers; worldgen loot tables and double chests stay out |
| entity_spine | spawn_to_torch | closed 2026-08-22: living Entity.move/travel spine; AI stays on `mobs` |
| projectiles | world_dynamics, entity_spine | closed 2026-08-22: magma bow/skeleton arrow tick; fireballs/eye-of-ender, inGround/pickup, Java ray-trace stay out |
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
- **Mob AI does not exist in blaze.** Snapshot living slots now tick the
  Entity.move / land-travel spine (`entity_spine`, M1+M2 VERIFIED). Pathfinding,
  targeting, and combat stay magma-CPU (detmob). The agreed GPU design is
  `blaze/GPU_MOB_AI.md` (v2, codex-reviewed): one warp per env, lane-0
  sequential mob tick with A* inline, magma-semantics (32x24x32 window,
  48-point path cap), IntHashMap aliasing reproduced, all 8 detmob tapes
  must be blaze-exact.

## Prerequisites discovered in design review (block the entity arc)

- **M1 transport (landed).** Snapshot v3 (`blaze/env/blaze_snapshot.h`)
  carries occupied living slots after the v2 light plane. v1/v2 load as
  `n_mobs=0`. Canonical digest is `blaze_snap_mobs_digest` in that header,
  compiled by magma (`rl_parity_build` / `gm_mobs_export_snap`) and blaze.
  Cap 96 (`ew_entity_store.h:21`), path 48 (`mob_live.h:90`). Hash order is
  slot-ascending. `--features mobs` compares BP_MOBS.
- **Living spine (landed, `entity_spine`).** Magma `--mobs off` and blaze-CPU
  / CUDA tick Entity.move + land travel with zero AI intents
  (`blaze/core/entity_spine.h`). AI/path/combat stay on the `mobs` row.
- Device FP census: zero-intent spine skips `moveRelative` sqrt/sin/cos
  (`Entity.java:1430`, `f < 1.0e-4F`). 256-sample CPU vs CUDA of
  `(float)sqrt((double)f)` and `mc_sin` is bitwise. `mc_atan2` is still
  host-only (`blaze/core/mc_math.h:63`); the spine does not call it.
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
