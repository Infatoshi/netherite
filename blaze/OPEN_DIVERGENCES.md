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

- **M1 transport (landed, this file).** Snapshot v3 (`blaze/env/blaze_snapshot.h`)
  carries occupied living slots after the v2 light plane. v1/v2 load as
  `n_mobs=0`. Canonical digest is `blaze_snap_mobs_digest` in that header,
  compiled by magma (`rl_parity_build` / `gm_mobs_export_snap`) and blaze-CPU
  (static loaded store; blaze does not step mobs). Cap 96
  (`ew_entity_store.h:21`), path 48 (`mob_live.h:90`).
  Field list (RlSnapMob, magma `mob_live.h` + oracle):
  slot/id/type/alive (`ew_entity_store.h`); persist
  (`mob_live.h:130`, EntityLiving.java:92); pose x/y/z double, yaw/pitch
  float, motion double, on_ground (Entity.java:111-130); yaw_body
  (`mob_live.h:57`, EntityLivingBase.java:119); health
  (`mob_live.h` EwStore.health, EntityLivingBase.java:922); hurt/death
  (`mob_live.h:137-138`, EntityLivingBase.java:101/107); task bits /
  wander / panic (`mob_live.h:47/52-53/43`); target_tasks / target_idx /
  see/stime / melee/bow timers (`mob_live.h:110-118`); attack_time
  (EwStore); swell (`mob_live.h:41`, EntityCreeper.java:51); anger
  (`mob_live.h:63`, EntityPigZombie.java:35); path buffer all 48 points +
  n/i (`mob_live.h:90-94`, Path.java:19-21); nav ticks / stuck_at /
  lastPosCheck (`mob_live.h:96-100`, PathNavigate.java:26-30); persistent
  AABB (`mob_live.h:127-128`, Entity.java:129); RNG triple seed48 +
  gaussian cache (`mob_live.h:85/131-132`, Entity.java:169).
  Hash order is slot-ascending. `--features mobs` compares BP_MOBS; the
  default-on chain feature list does not include it until blaze ticks mobs.
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
