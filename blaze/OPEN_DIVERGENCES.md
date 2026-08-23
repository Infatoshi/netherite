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

Last verified: lane/expresid 2026-08-23 (explosions M1+M2 after creeper posY origin, charged 2x via screaming alias, mobGriefing isSmoking, density collision AABB, unenchanted blast-prot identity, isFlaming explosionRNG draw. EXP4 unchanged. placement, furnaces, hazards, biome_plane, biome_plane_spawn, biome_plane_ice, spawn_to_torch, world_dynamics, fluids, entity_spine, random_ticks, random_ticks_bodies, falling_blocks, weather_optional, projectiles, chests, mobs, mobs_ss, mobs_end, passives, xp_orbs, boats, elytra, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/tntsupport 2026-08-23 (placement M1+M2 after World.mayPlace + TNT flint ignite + boat recipes + getRemainingItems. spawn_to_torch, world_dynamics, explosions, furnaces, hazards, biome_plane, biome_plane_spawn, biome_plane_ice, fluids, entity_spine, random_ticks, random_ticks_bodies, falling_blocks, weather_optional, projectiles, chests, mobs, mobs_ss, mobs_end, passives, xp_orbs, boats, elytra, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/natspawn2 2026-08-23 (biome_plane_spawn + biome_plane_ice M1+M2 after magma spawn_light packed blight + HS_BIOME clip-to-region plains + BiomeSnow skeleton/stray list + BiomeSwamp appended slime weight 1. Existing biome_plane row stays no-spawn. random_ticks, random_ticks_bodies, mobs, mobs_ss, mobs_end, passives, xp_orbs, boats, elytra, projectiles, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/biomeplane 2026-08-23 (biome_plane M1+M2 after snapshot v8 per-column biome plane + HS_BIOME/rt_live_biome + TEMPERATURE_NOISE Perlin. BP_MOBS MBM3, BP_RANDOM_TICKS RTK4. v7 loads plains 1. Swamp/ice natural-spawn lockstep still diverges. random_ticks, random_ticks_bodies, mobs, mobs_ss, mobs_end, passives, xp_orbs, boats, elytra, projectiles, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/enderman 2026-08-23 (mobs_end M1+M2 after EntityEnderman shared live tick + MONSTER insert + deathTime 20 + BiomeSwamp slime weight 1. Snapshot v7 enderman fields. BP_MOBS MBM2. mobs, mobs_ss, passives, xp_orbs, boats, elytra, projectiles, random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/rtbodies 2026-08-23 (random_ticks_bodies M1+M2 after sapling STAGE / farmland / ice / snow / mycelium updateTick + updateBlocks ice/snow placement. RTK3 hashes the extra cell ids. Tree gen and lightning stay out. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp.)
Last verified: lane/rtworldrand 2026-08-23 (random_ticks M1+M2 after shared World.rand + updateLCG replace purpose-hash streams. Snapshot v6 update_lcg. RTK2 hashes the cursor. BlockFire world.rand closed. Class C: Java World.rand and updateLCG are unseeded. Fireball and EntityItem Math.random motion stay out. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp.)
Last verified: lane/spiderslime 2026-08-23 (mobs_ss M1+M2 after EntitySpider/EntitySlime shared live tick+MONSTER insert; mobs, passives, xp_orbs, boats, elytra, projectiles, random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/expdrops 2026-08-23 (explosions M1+M2 after doExplosionB drops in JDK 8 HashSet order + World.rand getDrops/spawnAsEntity. EXP4 hashes drop count/ids after the cursor. Class C: EntityItem xz motion is Math.random; table zeros mx/my/mz. Fireball and BlockFire world.rand stay out. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp.)
Last verified: lane/passives 2026-08-23 (passives M1+M2 after EntityCow/Pig/Sheep/Chicken shared spine+generic AI+CREATURE spawn; mobs, xp_orbs, boats, elytra, projectiles, random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/worldrand 2026-08-23 (explosions M1+M2 after shared World.rand: face-ray nextFloat jitter + BlockTNT chain fuse. Snapshot v5 world_rand_seed. EXP3 hashes the cursor. Class C: Java `new Random()` is unseeded so tape-exact draws are unrecorded. doExplosionB drops and BlockFire world.rand stay out. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp.)
Last verified: lane/natspawn 2026-08-22 (mobs M1+M2 after WorldEntitySpawner MONSTER + EntityLiving.despawnEntity in shared hostile_spawn.h; planted persist AI from lane/mobs stays. xp_orbs, boats, elytra, projectiles, random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/tntknock 2026-08-22 (explosions M1+M2 after doExplosionA knockback + getBlockDensity + planted EntityTNTPrimed; mobs M1+M2 after EntityLivingBase.knockBack. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp. Chain fuse / fire rand / doExplosionB drops stay out: no world.rand.)
Last verified: lane/boatsxp 2026-08-22 (xp_orbs, boats, elytra M1+M2. random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, mobs, explosions, chests, falling_blocks, weather_optional, mining_slice M1 stay VERIFIED. projectiles M1 FAIL at observation 22 magma evidence 2 vs blaze 1 is pre-existing on this base tree, lane/projground. mining_slice M2 BLOCKED: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/projground 2026-08-22 (projectiles M1+M2 after porting magma EntityArrow inGround/arrowShake/pickup into blaze; random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, mobs, explosions, chests, falling_blocks, weather_optional, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED on this clone: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/lightsync 2026-08-22 (random_ticks M1+M2 after porting magma generateSkylightMap chunk rebuild + raise-only spread into blaze_core.h; world_dynamics, spawn_to_torch, fluids, entity_spine, mobs, explosions, projectiles, chests, falling_blocks, weather_optional, mining_slice M1 stay VERIFIED. mining_slice M2 BLOCKED on this clone: missing blaze/rl/out/snaps/*_d*.bsnp).
Last verified: lane/mobs 2026-08-22 (mobs M1+M2 planted zombie+skeleton generic AI/combat/drops; explosions, projectiles, chests, weather_optional, falling_blocks, entity_spine, random_ticks already on master).
Last verified: lane/explosions 2026-08-22 (explosions M1+M2; projectiles, chests, weather_optional, falling_blocks, entity_spine, random_ticks already on master).
Last verified: lane/projectiles 2026-08-22 (projectiles M1+M2; weather_optional, falling_blocks, entity_spine, random_ticks already on master).
Last verified: lane/chests 2026-08-22 (chests M1+M2; falling_blocks, entity_spine, and random_ticks already on master).
Last verified: lane/weather 2026-08-22 (weather_optional M1+M2; falling_blocks, entity_spine, random_ticks already on master).

## Verified rows (no known divergence)

| row | M1 (magma-CPU vs blaze-CPU) | M2 (blaze-CPU vs CUDA) |
|---|---|---|
| mining_slice | VERIFIED | VERIFIED |
| spawn_to_torch | VERIFIED (chain 2058 actions) | VERIFIED |
| placement | VERIFIED (chain 96 torch-air/torch-stone/flint-TNT, `--features player,items,explosions`) | VERIFIED |
| world_dynamics | VERIFIED | VERIFIED |
| fluids | VERIFIED | VERIFIED (chain 61 actions) |
| entity_spine | VERIFIED (chain 32 actions, `--features mobs`) | VERIFIED (64 CUDA lanes) |
| random_ticks | VERIFIED (200 idle ticks, 27 tickable-cell mutations) | VERIFIED (64 CUDA lanes) |
| random_ticks_bodies | VERIFIED (200 idle ticks, planted sapling/farmland/ice/snow/mycelium) | VERIFIED (64 CUDA lanes) |
| falling_blocks | VERIFIED (chain 64 actions, `--features falling_blocks`) | VERIFIED (64 CUDA lanes) |
| weather_optional | VERIFIED (chain 64 idle, rain flip t=50, `--features weather`) | VERIFIED (64 CUDA lanes) |
| projectiles | VERIFIED (chain 64 draw/release, `--features projectiles`) | VERIFIED (64 CUDA lanes) |
| chests | VERIFIED (chain 41 actions, `--features chests`) | VERIFIED (64 CUDA lanes) |
| explosions | VERIFIED (chain 64 idle+walk, `--features explosions`) | VERIFIED (64 CUDA lanes) |
| mobs | VERIFIED (chain 64 stand/walk/melee, `--features mobs --mobs-on --natural-spawn`) | VERIFIED (64 CUDA lanes) |
| xp_orbs | VERIFIED (chain 64 idle/walk, `--features xp`) | VERIFIED (64 CUDA lanes) |
| boats | VERIFIED (chain 64 use/forward, `--features boats`) | VERIFIED (64 CUDA lanes) |
| elytra | VERIFIED (chain 64 jump/pitch, `--features elytra`) | VERIFIED (64 CUDA lanes) |
| passives | VERIFIED (chain 64 stand/walk/melee, `--features mobs,xp --mobs-on --natural-spawn-passive`) | VERIFIED (64 CUDA lanes) |
| mobs_ss | VERIFIED (chain 64 stand/walk/melee, `--features mobs,xp --mobs-on --natural-spawn`) | VERIFIED (64 CUDA lanes) |
| mobs_end | VERIFIED (chain 64 stand/walk/melee, `--features mobs,xp --mobs-on --natural-spawn`) | VERIFIED (64 CUDA lanes) |
| biome_plane | VERIFIED (chain 64, `--features random_ticks,mobs --mobs-on`, seed 7 swamp plane) | VERIFIED (64 CUDA lanes) |
| biome_plane_spawn | VERIFIED (chain 64, seed 7 swamp, `--features random_ticks,mobs --mobs-on --natural-spawn`) | VERIFIED (64 CUDA lanes) |
| biome_plane_ice | VERIFIED (chain 64, seed 42 ice plains, `--features random_ticks,mobs --mobs-on --natural-spawn`) | VERIFIED (64 CUDA lanes) |

## Unported rows (coverage gaps), in dependency order

From `port_matrix.yaml` `supported: false` block reasons. Depth-1 rows can
start any time; deeper rows wait on their deps.

| row | deps | blocked on |
|---|---|---|
| placement | explosions | closed 2026-08-23: World.mayPlace + TNT flint fuse 80 + boat recipes + getRemainingItems; ItemDoor / snow-layer / anvil-on-circuits / Unbreaking flint stay out |
| falling_blocks | world_dynamics | closed 2026-08-22: EntityFallingBlock / BlockFalling sand+gravel live tick; anvil/dragon-egg and item drop on failed mayPlace stay out |
| weather_optional | world_dynamics | closed 2026-08-22: WorldInfo rain/thunder timers + worldTime; strength fade and sky stay magma-inert |
| chests | spawn_to_torch | closed 2026-08-22: placed single-chest TE + PICKUP/QUICK_MOVE transfers; worldgen loot tables and double chests stay out |
| entity_spine | spawn_to_torch | closed 2026-08-22: living Entity.move/travel spine; AI stays on `mobs` |
| projectiles | world_dynamics, entity_spine | closed 2026-08-22: magma bow/skeleton arrow tick; 2026-08-22 lane/projground added inGround/shake/pickup; fireballs/eye-of-ender and Java ray-trace stay out |
| explosions | world_dynamics, projectiles | closed 2026-08-22: ignited creeper fuse 30 + doExplosionA crater/player damage; 2026-08-22 lane/tntknock added getBlockDensity, doExplosionA knockback, planted EntityTNTPrimed size 4.0F. 2026-08-23 lane/worldrand: shared World.rand, face-ray jitter, chain TNT fuse. 2026-08-23 lane/expdrops: doExplosionB HashSet-order drops. 2026-08-23 lane/rtworldrand: BlockFire / random ticks on the same World.rand + updateLCG. 2026-08-23 lane/expresid: creeper posY origin, charged 2x, mobGriefing isSmoking, density collision AABB, unenchanted blast-prot identity, isFlaming explosionRNG. Fireball tape-exact explosionRNG and EntityItem Math.random motion stay out |
| explosions | world_dynamics, projectiles | closed 2026-08-22: ignited creeper fuse 30 + doExplosionA crater/player damage; TNT/fireball/drops/knockback stay out |
| explosions | world_dynamics, projectiles | closed 2026-08-22: ignited creeper fuse 30 + doExplosionA crater/player damage; 2026-08-22 lane/tntknock added getBlockDensity, doExplosionA knockback, planted EntityTNTPrimed size 4.0F. 2026-08-23 lane/worldrand: shared World.rand, face-ray jitter, chain TNT fuse. 2026-08-23 lane/expdrops: doExplosionB HashSet-order drops. Fireball, BlockFire world.rand, EntityItem Math.random motion stay out |
| passives | mobs, xp_orbs | closed 2026-08-23: cow/pig/sheep/chicken Java sizes/health, EntityAnimal.canDespawn false, chicken motionY*=0.6, EntityAIPanic/WanderAvoidWater/LookIdle RNG + straight-line, CREATURE spawn cap 10*i/289 + 400-tick gate. PathNavigateGround A* / mate/tempt/follow/eat/watch stay design-gap (GPU_MOB_AI.md) |
| mobs | world_dynamics, entity_spine, projectiles | closed 2026-08-22: planted zombie+skeleton generic AI (LOS/chase/melee), player i-frames, bone/flesh drops, skeleton arrows; 2026-08-22 lane/natspawn: WorldEntitySpawner MONSTER + EntityLiving.despawnEntity (natural_spawn knob default 0). det_entity_rng A*, Java knockBack, passives stay out |
| explosions | world_dynamics, projectiles | closed 2026-08-22: ignited creeper fuse 30 + doExplosionA crater/player damage; 2026-08-22 lane/tntknock added getBlockDensity, doExplosionA knockback, planted EntityTNTPrimed size 4.0F. Fireball, chain fuse world.rand, doExplosionB drops stay out |
| mobs | world_dynamics, entity_spine, projectiles | closed 2026-08-22: planted zombie+skeleton generic AI (LOS/chase/melee), player i-frames, bone/flesh drops, skeleton arrows; 2026-08-22 lane/tntknock added EntityLivingBase.knockBack on generic melee. WorldEntitySpawner, det_entity_rng A*, passives stay out. 2026-08-23 lane/passives closed cow/pig/sheep/chicken as row `passives`. 2026-08-23 lane/spiderslime closed spider+slime as row `mobs_ss`. 2026-08-23 lane/enderman closed enderman as row `mobs_end`; witch live insert stay out |
| mobs_ss | mobs, xp_orbs | closed 2026-08-23: EntitySpider size 1.4x0.9 health 16 attack 2, climbing flag + travel ladder clamp, daylight brightness>=0.5F, string 0..2 + eye 1/3, XP 5; EntitySlime setSlimeSize hop/split/drops/XP and getCanSpawnHere slime-chunk + swamp rules. PathNavigateClimber / leap / A* stay design-gap (GPU_MOB_AI.md). 2026-08-23 lane/enderman: deathTime 20 before split; BiomeSwamp extra slime weight 1 |
| mobs_end | mobs, mobs_ss | closed 2026-08-23: EntityEnderman health 40 speed 0.3 attack 7 follow 64, isWet DROWN+teleport, daytime brightness teleport, teleportRandomly/attemptTeleport/teleportToEntity, shouldAttackPlayer stare, AIFindPlayer straight chase, HurtByTarget screaming, AITakeBlock/AIPlaceBlock RNG+world write, pearl 0..1, XP 5. PathNavigateGround A* stay design-gap (GPU_MOB_AI.md) |
| mobs | world_dynamics, entity_spine, projectiles | closed 2026-08-22: planted zombie+skeleton generic AI (LOS/chase/melee), player i-frames, bone/flesh drops, skeleton arrows; 2026-08-22 lane/tntknock: EntityLivingBase.knockBack on generic melee; 2026-08-22 lane/natspawn: WorldEntitySpawner MONSTER + EntityLiving.despawnEntity (natural_spawn knob default 0); 2026-08-23 lane/passives: cow/pig/sheep/chicken as row `passives`; 2026-08-23 lane/spiderslime: spider+slime as `mobs_ss`; 2026-08-23 lane/enderman: enderman as `mobs_end`. det_entity_rng A*, witch live insert stay out |
| portals_dimensions | world_dynamics | portal transfer and dimension identity not measured |
| nether_route | spawn_to_torch, portals_dimensions | no strict cross-backend fixture |
| boats_elytra_xp | fluids, entity_spine | closed 2026-08-22: split into xp_orbs, boats, elytra (all M1+M2). Java water accel / Mending / UNDER_* boat status / snapshot armor stay out |
| dragon_victory | nether_route, mobs, explosions | not verified end to end; dragon-fight RL is out of scope per GATES |

Two consequences worth stating plainly:

- **Dimensions do not exist in blaze.** The GPU sim is overworld snapshots
  only; Nether and End (`portals_dimensions`, `nether_route`) are entirely
  on the magma side today.
- **Detmob A* still does not exist in blaze.** Snapshot living slots tick the
  Entity.move spine (`entity_spine`) and the magma generic (det_entity_rng off)
  zombie/skeleton/creeper chase/melee path (`mobs`, M1+M2 VERIFIED). Java
  EntityAITasks + PathFinder A* stay magma-CPU (detmob). The agreed GPU design
  is `blaze/GPU_MOB_AI.md` (v2, codex-reviewed): one warp per env, lane-0
  sequential mob tick with A* inline, magma-semantics (32x24x32 window,
  48-point path cap), IntHashMap aliasing reproduced, all 8 detmob tapes
  must be blaze-exact.

## Sweep 2026-08-23 (codex full read, magma -> blaze; unverified by gate unless noted)

Ranked by RL fidelity. Each row: magma site; blaze site or absent; what
closing needs. Spot-checked by hand on 2026-08-23: rows 7 and 10 confirmed.

1. Episode region is fixed; no chunk streaming (`blaze/env/blaze_core.h:
   456-516, 4652-4659`). Magma streams chunks around the player. Every
   row is VERIFIED only inside the snapshot region. L; blocks long-horizon
   transfer.
2. Policy actions are privileged helpers (craft/interact/smelt in
   `blaze/rl/obs_pack.h:20-51`); no strafe, sneak, sprint, or slot-click
   as magma `player_ctl` sees them. `do_place` is reachable: RL `a[8]` is
   `use` (`obs_pack.h:48`, `verify_cpu.py:383`) and blaze maps use_fire to
   `act.do_place` like magma `player_ctl.c:687`. Strafe/sneak/sprint/slot-click
   stay a transfer gap. M.
3. Block light does not propagate in blaze (`blaze_core.h:689-703`) and no
   digest covers light. Magma `light.c` propagates. Spawn light checks
   therefore read different values off-region. M/L. lane/natspawn2
   2026-08-23 made magma spawn read the packed snapshot block light
   (`spawn_light`) in lockstep mode as a shared deviation; propagation in
   blaze stays open.
4. Snapshot restore drops active state: furnaces, chests, craft grid,
   bow/eat/left-click timers, projectiles, falling blocks, fluid regions,
   mob sidecars (repath/despawn/fire/tick), boat, explosion state, player
   XP/fire, armor/enchants (`blaze_snapshot.h` vs `runtime.h`). Lockstep
   starts from a different state and the difference is not digested.
   Needs a continuous-vs-resume parity gate: run N ticks, snapshot,
   restore, run M; compare with N+M continuous. M/L.
5. Focused M2 compares `k_tick_raw`, not the production warp kernel
   (`blaze/env/verify_cuda.py:655-663`). M.
6. Death/respawn: blaze stays terminal (`blaze_core.h` health<=0 ->
   dead=1). Magma `gm_runtime_respawn` is the GuiGameOver click path
   (health/food/air/fire reset, same pose). An RL episode has no death
   click, so magma also freezes at the death tick. Equality is gated up
   to that tick. Auto-respawn in blaze would diverge M1. S.
7. Player fire ticks: CLOSED 2026-08-23 lane `hazards` (PsvPlayer.fire /
   air, snapshot v9, BP_PLAYER PLY1). Forensics in magma
   `CLOSED_DIVERGENCES.md`.
8. Mob sidecars (repath, despawn, fire, tick counters) not snapshotted and
   not digested. M.
9. Item enchant payload and the 32-stack overflow queue missing in blaze.
   M.
10. TNT flint-and-steel: CLOSED 2026-08-23 lane `tntsupport`. Shared
    `block_may_place.h` (`BlockTNT.java:105-119` / `:85-96`, fuse 80).
11. Furnaces matrix row: CLOSED 2026-08-23 lane `furnaceids`. `furnaces`
    M1+M2 VERIFIED (223-tick coal+beef chain). Furnace TE still not in
    `.bsnp` (row 4).
12. Torch support / mayPlace: CLOSED 2026-08-23 lane `tntsupport`. Shared
    `ibp_may_place` (`World.java:3363-3368`, `BlockTorch.java:98-116`).
13. Potion / milk / shield use state absent. M.
14. Chest cap fixed 64 in blaze vs magma growth. S.
15. Boat mount vs bow release order differs between sides. S.

Field aliasing in the shared mob table: `swell` (slime size), `melee_delay`
(slime jumpDelay), `see_time`, `anger` bit0, `target_idx` carry per-kind
state. Any new kind that uses the same field for its Java meaning collides.
Observation exposes no mob, light, or health planes.

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
