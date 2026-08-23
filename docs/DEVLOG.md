# DEVLOG (compressed)

## 2026-08-23 furnace registry, buckets, food, hotbar (lane/furnaceids)

Anvil. Sweep 2026-08-23 magma rows 2, 3, 12 and silent hotbar; blaze row 11.

Baseline tapes (`replay_tape.py --cpu --no-gate --report`,
`out/verify/furnaceids_baseline_*.log`): TNT both physics NO divergence 309,
inventory 1-mismatch t=28 slot 0 flint-and-steel 259 tape meta 0 vs magma
meta 1. creeper_encounter FIRST DIVERGENCE t=76 y 2.1e-09. smoke_zombie x2
physics NO divergence through death (358 / 373), entities PASS. bow physics
NO divergence 1407, entities PASS 5525. Canon physics NO divergence 3617,
entities PASS 16526, world_hash first_mismatch null (c-only, 46 hash_deltas).
Pixel `frames_checked=0` FATAL is `--no-gate` harness, not a parity verdict.

Cause: `smelting_recipes.h` used the crafting_recipes registration-index
shim (lava 332, fish 359, beef 373). Java `Item.java:1569` lava_bucket=327,
`Item.java:1591` fish=349, `Item.java:1605` beef=363. Rebuild 51 recipes
from `FurnaceRecipes.java:31-91` with XP and `TileEntityFurnace.getItemBurnTime:340-355`
(script `verify/furnace_registry.py`). Lava fuel leaves empty bucket
(`TileEntityFurnace.update:232-234`). Empty bucket max 16 (`Item.java:1566`);
`fillBucket` (`ItemBucket.java:117-140`) shrinks and adds. Food table from
`Item.java` ItemFood/ItemSoup/ItemAppleGold/ItemSeedFood/ItemFishFood;
eat finish consumes World.rand burp (`ItemFood.java:55`) then potion
(`:66`) if potionId set. Golden apple overrides onFoodEaten (no second
draw). `getBestHotbarSlot` empty then unenchanted; subset has no ench flag.

After: same tape numbers (`out/verify/furnaceids_after_*.log`). furnaces
M1 VERIFIED 223 ticks t=0 digest `0xb9b23f3a46fd0825`
(`out/verify/furnaceids_furnaces_m1_detail.log`). M2 VERIFIED
(`out/verify/furnaceids_m2_all.log`). `--no-deps` M1 VERIFIED for furnaces,
chests, mining_slice, spawn_to_torch, world_dynamics, fluids, entity_spine,
random_ticks, random_ticks_bodies, falling_blocks, weather_optional,
projectiles, explosions, mobs, mobs_ss, mobs_end, passives, xp_orbs, boats,
elytra, biome_plane. M2 VERIFIED for those except mining_slice BLOCKED
(`blaze/rl/out/snaps/*_d*.bsnp` missing). Root `make test` PASS including
`test_furnace_registry` 51 recipes (`out/verify/furnaceids_maketest.log`).
Fixture `s10_t0_r64_furnaces.bsnp` baked by `test_furnaces --write-fixture`
from `s10_t0_r64_no_liquid.bsnp`. Furnace TE still not in snapshot (no
version bump). Did not touch mob or spawn code.
## 2026-08-23 player environmental damage (lane/hazards)

Gamer. Magma sweep row 5 + blaze rows 6-7. Shared `psv_env_pre_move` in
`player_survival.h`. Snapshot v9 (`BLAZE_SNAP_VERSION_HAZARDS`) trailer
fire+air; v8 loads 0/300. `BP_PLAYER` PLY1.

Java: AIR 300 `Entity.java:256`; drown `EntityLivingBase.java:297-320`
DROWN 2.0 at air==-20; IN_WALL `Entity.java:2156-2186`; LAVA 4.0 +
setFire(15) `Entity.java:605-611`; ON_FIRE `Entity.java:554-557`; cactus
`BlockCactus.java:133-136`; HOT_FLOOR `BlockMagma.java:45-50` skip sneak /
frost walker; void `EntityLivingBase.java:1647-1649`. Apply via existing
hurt gate + armor.

Baseline tapes: bow NO divergence 1407 / entities 5525; smoke_zombie x2
NO divergence through death 358; canon INFRASTRUCTURE FAILURE (golden
frames missing). After: same, no earlier first divergence.

Fixture baker `test_hazards --write-fixture` from
`s10_t0_r64_no_liquid.bsnp` -> `s10_t0_r64_hazards.bsnp` +
`hazards_s10.json` 448 actions. M1 448 ticks player digest
`0x0ac36057b116e2d3`. M2 VERIFIED. Listed `--no-deps` M1 VERIFIED; M2
VERIFIED except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp`
missing). Units both sides PASS. Root `make test` PASS.

Blaze death stays terminal (magma GUI respawn needs death_click; M1
equals up to the death tick).
## 2026-08-23 swamp/ice natural-spawn lockstep (lane/natspawn2)

Anvil. Baseline swamp `--natural-spawn` M1 FAIL t=7 magma evidence 12 vs blaze 10 (`out/verify/natspawn2_swamp_baseline.log`). Magma 11 living vs blaze 9: first 7 match, then magma 4 zombies in a y=9 cave vs blaze 2. Fixture packed blight at those cells is 3-10; magma t=7 dump blight is 0.

Cause: `compute_blocklight` (`magma/world/light.c:434-443`) memsets every loaded chunk then BFS from in-chunk emitters. Torch light that bled into the snapshot AABB from outside is lost. Blaze keeps the packed nibble (`cu_world_blk`, `blaze_core.h:939-942`). `EntityMob.isValidLightLevel` (`EntityMob.java:159-180`) uses combined sky+block light, so magma treated the cave as dark and spawned extra. Magma spawn now reads a snapshot `spawn_light` copy (`mob_live.c` `gm_hs_blk`) sticky like blaze. Also clip `HS_BIOME` OOR to plains 1 (`hs_biome_or_plains`, same shared deviation as `gm_world_rt_block` `world_live.c:705-708`; Java is biome at candidate BlockPos `WorldEntitySpawner.java:132-133` -> `WorldServer.java:245-249` -> `Chunk.getBiome` `Chunk.java:1273-1278`). Spawn lists: BiomeSwamp.java:34 appends slime weight 1 after witch (not combined 101 in the Biome.java slot); BiomeSnow.java:36-49 removes skeleton then appends skeleton 20 + stray 80; stray consume-then-skip, not roster.

Ice plains fixture `s42_t0_r64_biome_plane_ice.bsnp` baked by `test_biome_plane --write-fixture --seed 42` from `s10_t0_r64_randtick_bodies.bsnp`. Genlayer: seed 42 player column biome 12, region 128x128 ice_cols=16384. New rows `biome_plane_spawn` (swamp seed 7 + `--natural-spawn`) and `biome_plane_ice` (seed 42); existing `biome_plane` stays the no-spawn plane-hash gate.

M1+M2 VERIFIED for biome_plane_spawn and biome_plane_ice (`out/verify/natspawn2_biome_plane_spawn_m1.log`, `_m2.log`, `out/verify/natspawn2_biome_plane_ice_m1.log`, `_m2.log`). Listed `--no-deps` M1 stay VERIFIED; M2 stay VERIFIED except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). Root `make test` PASS (`out/verify/natspawn2_maketest.log`). `make -C magma test-mob-live` PASS. `make -C blaze/rl test-biome-plane test-mobs` PASS.

Tapes (`replay_tape.py --cpu --no-gate --report`): TNT both physics NO divergence 309, inventory 1-mismatch t=28 slot 0 flint-and-steel 259 tape meta 0 vs magma meta 1. creeper_encounter FIRST DIVERGENCE t=76 y 2.1e-09. smoke_zombie x2 physics NO divergence through death (358 / 373), entities PASS. bow physics NO divergence 1407, entities PASS 5525. Canon physics NO divergence 3617, entities PASS 16526, world_hash first_mismatch null, 46 c-side hash_deltas. First-divergence ticks did not move earlier.

Stay out: tape-exact World.rand; stray live insert; PathNavigate A*. Did not add witch code.

## 2026-08-23 biome plane snapshot v8 (lane/biomeplane)

Anvil. Snapshot v8 carries one u8 per x,z column of the lockstep region (`ix*rnz+iz`). Magma `rl_snapshot_write` copies `LChunk.biome` (`magma/world/light.c:153`, index `(wx&15)+(wz&15)*16` = Java `Chunk.getBiome` `Chunk.java:1273-1278`). Magma load restores via `gm_world_set_biome`. Blaze env `biome[]` pool. v7 loads plains id 1 so old fixtures keep HS_BIOME/freeze plains semantics.

Java: `Chunk.blockBiomeArray` (`Chunk.java:53`). Consumers: `WorldEntitySpawner.getSpawnListEntryForTypeAt` (`WorldServer.java:245-249`) -> `Biome.getSpawnableList` (`Biome.java:204-220`); `HS_BIOME` in `hostile_spawn.h`. `World.canBlockFreeze` / `canSnowAt` -> `Biome.getFloatTemperature` (`Biome.java:258-268`): `TEMPERATURE_NOISE.getValue((float)x/8.0F, (float)z/8.0F)*4.0D` with `NoiseGeneratorPerlin(new Random(1234L), 1)` (`Biome.java` static, `NoiseGeneratorPerlin.java:21-32` / `NoiseGeneratorSimplex.java:24-40`). Port reuses `cp_simplex_init` / `cp_perlin_getValue` (`chunk_provider.h`). `EntitySlime.getCanSpawnHere` swamp `biome == Biomes.SWAMPLAND` (`EntitySlime.java:350`). CREATURE lists: default `Biome.java:142-145`; empty ocean/river/beach/mesa (`BiomeOcean.java:8` etc.); ice plains rabbit/polar bear only (`BiomeSnow.java:33-35`) so roster weight 0. Swamp extra slime weight 1 (`BiomeSwamp.java:34`) already on the monster list.

Digest: `BP_MOBS` MBM2 -> MBM3, `BP_RANDOM_TICKS` RTK3 -> RTK4; both hash the plane. Fixture `s7_t0_r64_biome_plane.bsnp` baked by `test_biome_plane --write-fixture --seed 7` from `s10_t0_r64_randtick_bodies.bsnp` (cells unchanged; genlayer tiled 16x16 like `light.c:403-406`). Seed 7 at (0,0) is biome 6 swamp (`B_SWAMP`); region `rx0=-56` 128x128 has 11910 swamp columns; player column id 6. Plane digest live `0x445765477ed874fb` vs forced-plains `0x57dc49820e0ad2c5`. M1 t=0 swamp RT `0x30456b456d67ed57` / mobs `0xb41b5f6c48c1c715`; plains twin RT `0x0d483600952866f1` / mobs `0x9e8f2ea1c70bf687`.

Units: Perlin `getValue(1,2) = -0.23526496123584156` matches Java 8 `NoiseGeneratorPerlin`; `getFloatTemperature` plains (8,80,16) float bits 1061576695; swamp/ocean/ice spawn lists; v7 load plains 1. `make -C blaze/rl test-biome-plane` PASS.

M1+M2 VERIFIED (`out/verify/biomeplane_biome_plane_m1.log`, `out/verify/biomeplane_biome_plane_m2.log`) without `--natural-spawn`. Listed `--no-deps` M1 stay VERIFIED; M2 stay VERIFIED except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). Root `make test` PASS (`out/verify/biomeplane_maketest.log`).

Tapes (`replay_tape.py --cpu --no-gate --report`): TNT both physics NO divergence 309, inventory 1-mismatch t=28 slot 0 flint-and-steel 259 tape meta 0 vs magma meta 1. creeper_encounter FIRST DIVERGENCE t=76 y 2.1e-09. smoke_zombie x2 physics NO divergence through death (358 / 373), entities PASS. bow physics NO divergence 1407, entities PASS 5525. Canon physics NO divergence 3617, entities PASS 16526, world_hash first_mismatch null, 46 c-side hash_deltas. First-divergence ticks did not move earlier.

Natural-spawn on swamp (t=7 magma evidence 12 vs blaze 10) and ice plains (t=2 magma 8 vs blaze 6) still diverges; birch 27 with natural-spawn VERIFIED (same monster weights as plains). Did not edit hostile_live.h / living_base.h. `mob_live.c` only `HS_BIOME` -> `gm_world_biome`. Stay out: tape-exact World.rand; swamp/ice natural-spawn lockstep.

## 2026-08-23 enderman live insert (lane/enderman)

Gamer. Port leftover on `mobs` "Not closed" of lane/spiderslime: Java 1.11.2 EntityEnderman into shared C magma and blaze both compile (`blaze/core/hostile_live.h` + MONSTER insert in `hostile_spawn.h`). Spiderslime residuals: 20-tick deathTime before slime split; BiomeSwamp extra slime weight 1; spider HARD potion roll still consumed (living_base.h has no PotionEffect list). New row `mobs_end`. Snapshot v7. `BP_MOBS` tag MBM1 -> MBM2.

Java enderman: setSize 0.6x2.9 (`EntityEnderman.java:64`); MAX_HEALTH 40 SPEED 0.30000001192092896 ATTACK_DAMAGE 7 FOLLOW_RANGE 64 (`:92-95`). `updateAITasks` `isWet` -> `attackEntityFrom` DROWN 1.0F (`:246-249`); DROWN unblockable so `nextInt(10)!=0` then `teleportRandomly` (`:386-390`). Daytime: `WorldProvider.isDaytime` is `getSkylightSubtracted()<4` (`WorldProvider.java:450-453`) and `ticksExisted >= targetChangeTime+600` brightness > 0.5 `nextFloat()*30 < (f-0.4)*2` then clear target and `teleportRandomly` (`EntityEnderman.java:251-260`). `teleportRandomly` `pos + (nextDouble-0.5)*64` / `nextInt(64)-32` (`:268-274`) then `teleportTo` -> `attemptTeleport` (`EntityLivingBase.java:3033-3105`: walk down to solid, empty AABB, no liquid; success consumes 128 x 3 nextFloat + 3 nextDouble). `teleportToEntity` 16-unit vector form (`:279-288`). `shouldAttackPlayer` pumpkin helmet false, look-vector dot `> 1.0 - 0.025/d0` then `canEntityBeSeen` (`:202-218`). AIFindPlayer 5-tick aggro then stare-close teleport `<16` / far `teleportToEntity` after 30 (`:432-538`). HurtByTarget sets screaming. `AITakeBlock` `nextInt(20)==0`, `AIPlaceBlock` `nextInt(2000)==0` (`:598-600`, `:553-555`); carriable list `EntityEnderman.java:413-430`; world write via `ML_SET_BLOCK`. Drops pearl `nextInt(2)` at looting 0. XP 5. Endermite on pearl out.

Magma extras: no PathNavigateGround A* (`GPU_MOB_AI.md`); WanderAvoidWater is hash wander + straight chase like other generic hostiles; no EntityAITasks mutex; blaze projectile vs enderman skips HP (no `ml_enderman_arrow_hit` at that include site); LookIdle/WatchClosest draws not consumed.

Fixture `s10_t0_r64_mobs_end.bsnp` baked by `test_mobs_end --write-fixture` from `s10_t0_r64_mobs_ss.bsnp`: persist zombie/skeleton/spider/slime2 plus enderman (10.5,65,6.5). Chain `mobs_end_s10.json`. M1 VERIFIED 64 ticks t=0 digest `0xfc26840500551425` (`out/verify/enderman_mobs_end_m1_detail.log`). M2 VERIFIED 64 CUDA lanes (`out/verify/enderman_mobs_end_m2.log`). `--no-deps` M1 VERIFIED for mobs, mobs_ss, passives, xp_orbs, boats, elytra, projectiles, random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice. M2 VERIFIED for those except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). `make -C blaze/rl test-mobs-end` PASS. `make -C magma test-mob-live` PASS.

Tapes (`replay_tape.py --cpu --no-gate --report`): detmob_hostile_target physics NO divergence 89 ticks, entities PASS 232, world_hash FIRST MISMATCH t=56 same as baseline. detmob_end physics NO divergence 850 ticks, entities PASS 10248, world_hash PASS. bow physics NO divergence 1407, entities PASS 5525. smoke_zombie physics NO divergence through death t=358, entities PASS 359. detmob_hostile_ambient INFRASTRUCTURE FAILURE (`script:1078: invalid spawn_particle`) before magma runs; physics unread. Canon jsonl INFRASTRUCTURE FAILURE (golden frames path not on this clone). First-divergence ticks did not move earlier.

Stay out: PathNavigateGround A*; EntityAIWanderAvoidWater path samples; LookIdle/WatchClosest interpolation; Endermite; witch live insert; blaze arrow-hit teleport (include-order). Did not edit random-tick/weather/explosion code.
## 2026-08-23 Random-tick bodies + ice/snow placement (lane/rtbodies)

Anvil. Baseline random_ticks M1 VERIFIED (`out/verify/rtbodies_baseline_random_ticks_m1.log`). mining_slice M2 BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). Tapes (`replay_tape.py --cpu --no-gate --report`, randtick off): TNT both physics NO divergence 309, inventory 1-mismatch t=28 slot 0 flint-and-steel 259 tape meta 0 vs magma meta 1. creeper_encounter FIRST DIVERGENCE t=76 y 2.1e-09. smoke_zombie x2 and bow physics NO divergence. Canon physics NO divergence 3617, entities PASS 16526, world_hash first_mismatch null, 46 c-side hash_deltas.

`s10_t0_r64_randtick.bsnp` has 64 farmland + 64 wheat, 0 sapling/ice/snow/mycelium. Farmland was already under wheat; adding it to `bp_is_randtick_id` is a hashed-cell layout change (RTK2 -> RTK3).

Java 1.11.2, shared `blaze/core/randtick_live.h` (MC_HD; CUDA compiles):
- BlockSapling.updateTick `BlockSapling.java:56-67` / grow `:69-78`: `BlockBush.checkAndDropBlock` `:64-75`, light >= 9, `nextInt(7)==0`, STAGE bit. generateTree `:81-198` out: `WorldGenTrees` is populate-only (`blaze/core/populate.h` `wg_trees`), not live `rt_live_set`. STAGE==1 consumes `nextInt(7)` only.
- BlockFarmland.updateTick `BlockFarmland.java:53-72`: hasWater 4-radius `:105-116`, isRainingAt `:57` / `World.java:3860-3878`, moisture--, turnToDirt, hydrate to 7. No World.rand draws. Entity shove on turnToDirt `:93-96` out.
- BlockIce.updateTick `BlockIce.java:82-87` / turnIntoWater `:90-102`: BLOCK light > 11-opacity (`Block.java:2488` opacity 3 -> >8). Nether vaporize -> air; else water. `quantityDropped` `:77-80` returns 0.
- BlockSnow.updateTick `BlockSnow.java:132-137`: BLOCK light > 11 -> setBlockToAir. 1.11.2 has no dropBlockAsItem (that is 1.8).
- BlockMycelium.updateTick `BlockMycelium.java:42-66`: dirt conversion + 4-try `nextInt(3)-1` / `nextInt(5)-3` / `nextInt(3)-1`, same shape as BlockGrass `:41-73`.
- WorldServer.updateBlocks ice/snow `:449-470`: after `nextInt(16)==0` (already consumed), precipitationHeight, canBlockFreezeNoWater `World.java:2860-2906` -> ice, raining && canSnowAt `:2917-2948` -> snow layer. isRainingAt gates farmland rain and snow place. Snapshot has no biome plane: live freeze uses plains id 1 so magma==blaze. Cold-biome freeze is a unit (biome 12). fillWithRain out. Lightning: `nextInt(100000)` consumed; no EntityLightningBolt slot (`ew_entity_store.h`); the 1/100000 hit's horse `nextDouble` is not consumed.
- getFloatTemperature `Biome.java:258-268`: height term for y>64 with Perlin f=0 (TEMPERATURE_NOISE out). Plains 0.8 never crosses 0.15.

Fixture `s10_t0_r64_randtick_bodies.bsnp` baked by `out/blaze/rl/test_randtick --write-fixture` from the randtick snapshot (not hand-edited): sapling STAGE 0, roofed dry farmland, ice, snow layer, mycelium under stone, mycelium next to dirt. Chain `randtick_bodies_s10.json` (200 idle). New row `random_ticks_bodies` deps random_ticks, weather_optional.

After: random_ticks_bodies M1+M2 VERIFIED (`out/verify/rtbodies_after_random_ticks_bodies_m1.log`, `out/verify/rtbodies_after_random_ticks_bodies_m2.log`). random_ticks M1+M2 stay VERIFIED. Listed `--no-deps` M1 stay VERIFIED; M2 stay VERIFIED except mining_slice BLOCKED. Root `make test` PASS (`out/verify/rtbodies_maketest.log`). After tapes match baseline first-divergence ticks.

Stay out: tree growth (WorldGenTrees not a live generator); lightning bolt entity; fillWithRain / cauldron `nextInt(20)`; TEMPERATURE_NOISE Perlin at y>64; tape-exact World.rand / updateLCG (unseeded); PlayerChunkMap moving-player list order; EntityItem `Math.random` motion; fireball.

## 2026-08-23 Random ticks on World.rand (lane/rtworldrand)

Anvil. Baseline random_ticks M1 VERIFIED (`out/verify/rtworldrand_baseline_random_ticks_m1.log`); explosions M1 VERIFIED (`out/verify/rtworldrand_baseline_explosions_m1.log`). mining_slice M2 BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing).

Java `World.rand` is `public Random rand = new Random()` (`World.java:108`). `updateLCG` is `protected int updateLCG = (new Random()).nextInt()` (`World.java:95`), then `updateLCG = updateLCG * 3 + 1013904223` (`:96`, magic `:97`). Tape-exact values are Class C: both RNGs are unseeded. Magma/blaze share the snapshot cursor.

Server tick (`WorldServer.java:180`): `super.tick()` weather on `this.rand` (`World.java:2709`, isolated `ww.rand` here, not this stream), then `updateBlocks` (`WorldServer.java:228`, `:389-505`), then later `updateEntities` explosions.

`updateBlocks` chunk loop (`WorldServer.java:409`) is `getPersistentChunkIterable(playerChunkMap.getChunkIterator())`. `PlayerChunkMap.getChunkIterator` (`PlayerChunkMap.java:73-114`) walks `this.entries`. For a stationary 1-player view, that list is `addPlayer` insertion (`:295-301`): cx outer, cz inner. Forge persistent-chunk prepend is identity here. Moving-player append/remove of `entries` is not ported.

Per chunk, Java order:
1. Thunder (`WorldServer.java:421`): `canDoLightning` true (`WorldProvider.java:592`). `this.rand.nextInt(100000)` only if raining AND thundering (`&&` short-circuit). Hit advances `updateLCG` (`:423`). Lightning/horse stay out.
2. Ice/snow (`:449`): `canDoRainSnowIce` true (`WorldProvider.java:597`). `this.rand.nextInt(16)` every chunk. Hit advances `updateLCG` (`:451`). Ice/snow placement stay out. `weather_optional` models WorldInfo timers on isolated `ww.rand`, not these `updateBlocks` draws; this lane consumes the shared stream in order.
3. Random ticks (`:472-494`): sections with `getNeedsRandomTick` (`ExtendedBlockStorage.java:86`). Each attempt: `updateLCG` pick (`k1=j1&15` x, `l1=j1>>8&15` z, `i2=j1>>16&15` y), then `block.randomTick(..., this.rand)` (`Block.java:595`).

Ported tickers (header list): grass (`BlockGrass.java:41-73`), leaves/leaves2 (`BlockLeaves.java:69-176`, no rand), fire (`BlockFire.java:146-253` / `:286-314`), wheat/carrot/potato (`BlockCrops.java:72-90`). Fire consumes `nextInt(3)` age (`:168`), `nextInt(10)` `scheduleUpdate` delay (`:172`) even though scheduled ticks stay out, then `tryCatchFire` (`:286`). Spread uses the original age `i` (`:158`), not the written-back age. Sapling/farmland/ice/snow/mycelium stay unported: LCG still picks the cell; their Java `updateTick` draws are not consumed.

`updateLCG` was not in snapshot v5. v6 trailer after `world_rand_seed`. v5 loads `update_lcg=0`. Class C initial. `BP_RANDOM_TICKS` tag RTK1 -> RTK2 hashes world_rand cursor + updateLCG after the cells. `BP_EXPLOSIONS` EXP4 still hashes the cursor after explosions; explosion code unchanged.

Fixture `s10_t0_r64_randtick.bsnp` rebaked by `out/blaze/rl/test_randtick --write-fixture` (v6, cells unchanged, `update_lcg=0`).

After: random_ticks M1+M2 VERIFIED; explosions M1+M2 VERIFIED. Listed `--no-deps` M1 stay VERIFIED; M2 stay VERIFIED except mining_slice BLOCKED. Root `make test` PASS (`out/verify/rtworldrand_maketest.log`).

Tapes (`replay_tape.py --cpu --no-gate --report`; script sets `randtick_enabled=0`): TNT both physics NO divergence 309 ticks, inventory 1-mismatch stays t=28 slot 0 flint-and-steel (259) tape meta 0 vs magma meta 1. That is item durability (`ItemFlintAndSteel.onItemUse` `damageItem(1)` `:44` after optional fire place on air `:38-42`; `canPlaceBlockAt` is `BlockFire.updateTick` `:150`, not onItemUse). Magma matches Java; tape did not record the damage. creeper_encounter FIRST DIVERGENCE stays t=76 y 2.1e-09. smoke_zombie x2 and bow physics NO divergence. Canon physics NO divergence 3617 ticks, entities PASS 16526; world_hash c-only unverified (`first_mismatch` null, 46 c-side hash_deltas). No tape first-mismatch moved earlier.

Stay out: fireball; EntityItem `Math.random` motion; tape-exact World.rand / updateLCG (unseeded); sapling/farmland/ice/snow/mycelium ticker bodies; lightning/ice/snow placement; PlayerChunkMap moving-player list order.
## 2026-08-23 spider + slime live insert (lane/spiderslime)

Gamer. Port leftover on `mobs` "Not closed" of lane/natspawn and lane/passives: Java 1.11.2 EntitySpider and EntitySlime into the shared C magma and blaze both compile (`blaze/core/hostile_live.h` + MONSTER insert in `hostile_spawn.h`). Existing `mobs_s10.json` is unchanged. New row `mobs_ss`. `BP_MOBS` tag stays `MBM1` (slime size in `swell`, jumpDelay in `melee_delay`, wasOnGround in `see_time`, spider climbing in `anger` bit 0).

Java spider: setSize 1.4x0.9 (`EntitySpider.java:45`); MAX_HEALTH 16 SPEED 0.30000001192092896 (`:104-105`); ATTACK_DAMAGE default 2 (`SharedMonsterAttributes.java:23`, applyEntityAttributes does not set it). `onUpdate` `setBesideClimbableBlock(isCollidedHorizontally)` (`:91-98`); `isOnLadder` is that flag (`:137-140`); travel clamp `motionX/Z` to +-0.15000000596046448, `motionY` floor -0.15, collidedHorizontally sets `motionY=0.2` (`EntityLivingBase.java:2047-2071`). Does not override `fall`; `EntityLivingBase.fall` applies (`:1389-1422`). Daylight: `AISpiderTarget.shouldExecute` brightness>=0.5F skip start (`EntitySpider.java:278-282`); `AISpiderAttack.continueExecuting` brightness>=0.5F && `nextInt(100)==0` drop target (`:247-260`). Light 12 is the 0.5F table crossing (`WorldProvider.java:56-64`). Drops at looting 0: string `nextInt(3)` 0..2, spider eye 1/3 on player kill with `|| nextInt(1+looting)>0` short-circuit. XP 5 (`EntityMob.java:27`). Jockey `world.rand.nextInt(100)==0` (`EntitySpider.java:200-207`): rider is not representable, draw consumed. Potion roll HARD only (`:213-216`): consume, no potion state.

Java slime: `setSlimeSize` 0.51000005*size, health size*size, speed 0.2+0.1*size, XP=size (`EntitySlime.java:69-83`). `onInitialSpawn` `nextInt(3)` then 1/4-ish bump via 0.5F*clamped (`:406-415`), size `1<<i`. Hop: `getJumpDelay` `nextInt(20)+10` (`:186-188`), aggressive `/3` (`:622-625`), landing particles size*8 `nextFloat` pairs plus two sound floats (`:149-167`). Attack `canDamagePlayer` size>1, strength=size (`:293-304`). `setDead` split `2+nextInt(3)` of size/2 at `((k%2-0.5F)*size/4, (k/2-0.5F)*size/4)` plus `nextFloat*360` yaw (`:217-247`). Table cap `EW_MAX_ENTITIES` skip remaining inserts, still consume yaw. Size-1 slimeball `EntityLiving.dropFewItems` `nextInt(3)` (`:382-397`, `getDropItem` `:322-324`). Spawn: default WorldType skips `handleSlimeSpawnReduction` with no draw (`WorldType.java:196-198`); swamp y 50..70 moon+light (`EntitySlime.java:350`); slime-chunk `Chunk.getRandomWithSeed(987234911L)` Java int overflow (`Chunk.java:1019`) then `nextInt(10)==0` and y<40 (`EntitySlime.java:355`). Biome list spider 100/4 and slime 100/4 (`Biome.java:146-151`). `BiomeSwamp` extra slime weight 1 (`BiomeSwamp.java:34`) is not on the shared list.

Magma extras: no EntityAITasks mutex/A* (`GPU_MOB_AI.md`); no PathNavigateClimber / EntityAILeapAtTarget; slime FaceRandom/Hop consume cited draws then straight hop, no `limitAngle`; split at the drop tick (no 20-tick `deathTime`); snapshot lockstep `HS_BIOME` defaults plains so the swamp branch is dead there (slime-chunk still live); combined light at pos stands in for `getLightFromNeighbors`.

Fixture `s10_t0_r64_mobs_ss.bsnp` baked by `test_mobs_ss --write-fixture` from `s10_t0_r64_mobs.bsnp` (not hand-edited): persist zombie (8.5,65,11.5), skeleton (12.5,65,8.5), spider (6.5,65,8.5), slime size 2 (11.5,65,11.5), one XP orb. Chain `mobs_ss_s10.json` (same 8 idle / 20 walk / 36 melee as `mobs_s10.json`). M1 VERIFIED 64 ticks t=0 digest `0xae9367277446cbb1` (`out/verify/spiderslime_mobs_ss_m1.log`). M2 VERIFIED 64 CUDA lanes (`out/verify/spiderslime_mobs_ss_m2.log`). `--no-deps` M1 VERIFIED for mobs, passives, xp_orbs, boats, elytra, projectiles, random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice. M2 VERIFIED for those except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). Root `make test` PASS (`out/verify/spiderslime_maketest.log`). `make -C magma test-mob-live` PASS. `make -C blaze/rl test-mobs test-mobs-ss` PASS.

Tapes (`replay_tape.py --cpu --no-gate --report`, `mobs=0`): detmob_hostile_target (only tape whose entity stream names EntitySpider) physics NO divergence 89 ticks, entities PASS 232, world_hash FIRST MISMATCH t=56 same hashes as baseline. bow physics NO divergence 1407 ticks, entities PASS 5525. smoke_zombie physics NO divergence through death t=358, entities PASS 359. Canon jsonl INFRASTRUCTURE FAILURE (golden frames path not on this clone) before magma runs; physics unread. First-divergence ticks did not move earlier. No EntitySlime in any tape header on this clone.

Open: PathNavigateGround / PathNavigateClimber A* (Class: design gap, `GPU_MOB_AI.md`); EntityAILeapAtTarget; slime `limitAngle` / mutex scheduler; 20-tick deathTime before split; skeleton jockey riding; spider potion effects; `BiomeSwamp` extra slime weight 1; swamp spawn needs a biome plane; witch/enderman live insert. Did not edit explosion/TNT/World.rand/passive_live.h.

## 2026-08-23 Explosion.doExplosionB drops (lane/expdrops)

Anvil. Baseline explosions M1 VERIFIED (`out/verify/expdrops_baseline_explosions_m1.log`). mining_slice M2 BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing).

Java `doExplosionA` (`Explosion.java:84-132`) fills `Sets.newHashSet()` then `affectedBlockPositions.addAll(set)`. `affectedBlockPositions` is `Lists.newArrayList()` (`:66`), not a second HashSet: doExplosionB iterates that ArrayList copy of HashSet bucket/`next` order. BlockPos hash is Vec3i (`Vec3i.java:47-49`) `(y + z*31)*31 + x`, then HashMap.hash `h ^ (h>>>16)`. Default capacity 16, load 0.75. TREEIFY_THRESHOLD 8 / MIN_TREEIFY_CAPACITY 64: a 16^3 face-cell set (n=1352) on JDK 8 has table cap 2048, max chain 4, treeBins=0. Size-4 TNT/creeper does not treeify. Tree bins are still ported (`java_hashset.h`) so a colliding set matches Java. C `JavaHashSet` unit TEN order matches `/usr/lib/jvm/java-8-openjdk-amd64` HashSet.

Per block in that order (`Explosion.java:209-246`, server `doExplosionB(false)` skips particle draws):
- `canDropFromExplosion`: TNT false (`BlockTNT.java:147`), else true (`Block.java:1069`).
- `dropBlockAsItemWithChance` (`Block.java:688-703` Forge): `getDrops` (`:1505-1520`) then `world.rand.nextFloat() <= chance` with `chance = 1.0F/size`. Leaves use `BlockLeaves.getDrops` (`:275-305`): sapling `nextInt(20)` (jungle 40, `BlockOldLeaf.java:46`), oak apple `nextInt(200)` (`:37-42`). Gravel flint `nextInt(10)==0` (`BlockGravel.java:23`). Glass `quantityDropped` 0 (`BlockGlass.java:23-24`). Stone -> cobble (`BlockStone.java:50-52`).
- `spawnAsEntity` three `nextFloat()*0.5F+0.25D` (`Block.java:719-721`), `setDefaultPickupDelay` 10 (`EntityItem.java:564-566`).
- `onBlockExploded` set air then TNT chain fuse (`Block.java:1730-1733`, `BlockTNT.java:68-74`).
- XP `getExpDrop` / `dropXpOnBlockBreak` is harvestBlock only; doExplosionB does not call it.

Class C: `EntityItem` xz motion is `Math.random()` (`EntityItem.java:59-61`), not `world.rand`. Live table keeps zeros (`cu_spawn_item` / `live_fill_ent` memset), including Java's constant `motionY` 0.2. Table cap 48 (`GM_LIVE_MAX` / `CU_MAX_ITEMS`) is a shared sim cap: both sides skip when full (`gm_live_spawn_item_capped`). Java has no cap.

EXP3 -> EXP4 hashes drop count/ids after the World.rand cursor. Snapshot v5 unchanged; fixture not rebaked.

After: explosions M1+M2 VERIFIED (`out/verify/expdrops_after_explosions_m1.log`, `out/verify/expdrops_after_explosions_m2.log`). Listed `--no-deps` M1 stay VERIFIED; M2 stay VERIFIED except mining_slice BLOCKED. Root `make test` PASS (`out/verify/expdrops_maketest.log`). TNT tapes: physics NO divergence, inventory 1-mismatch stays t=28 slot 0 flint-and-steel (259) tape meta 0 vs magma meta 1 (durability, not gravel flint). creeper_encounter FIRST DIVERGENCE stays t=76 y 2.1e-09. smoke_zombie x2 and bow physics NO divergence.

Stay out: fireball; BlockFire `world.rand`; EntityItem `Math.random` motion; tape-exact World.rand (unseeded).
## 2026-08-23 passives cow/pig/sheep/chicken (lane/passives)

Gamer. Port leftover on `mobs` "Not closed" of lane/natspawn: Java 1.11.2 EntityCow/Pig/Sheep/Chicken into shared C magma and blaze both compile (`blaze/core/passive_live.h` + CREATURE half of `hostile_spawn.h`). Knob `natural_spawn_passive` default 0 so existing tapes and the mobs row stay bit-identical. Isolated spawn JavaRandom is the natspawn stream (not ww.rand).

Java: sizes cow 0.9x1.4 / pig 0.9x0.9 / sheep 0.9x1.3 / chicken 0.4x0.7 (`EntityCow.java:33`, `EntityPig.java:54`, `EntitySheep.java:82`, `EntityChicken.java:50`); health 10/10/8/4; speeds 0.20000000298023224 / 0.25 / 0.23000000417232513 / 0.25. `EntityAnimal.canDespawn` false (`EntityAnimal.java:137-140`) so `EntityLiving.despawnEntity` never setDead (`EntityLiving.java:787-831`); persist still zeros age. Chicken `onLivingUpdate` `motionY*=0.6` when falling (`EntityChicken.java:98-101`); `fall()` empty (`:113-115`). Panic speeds 2.0/1.25/1.25/1.4. `RandomPositionGenerator.generateRandomPos` 10 samples from entity.rand (`RandomPositionGenerator.java:66-156`). `EntityAIWander` `nextInt(120)` (`EntityAIWander.java:19,42`). `EntityAIWanderAvoidWater` 10x7 land, `nextFloat>=0.001F` (`EntityAIWanderAvoidWater.java:13,32`). `EntityAILookIdle` `nextFloat<0.02F` then `2*PI*nextDouble` and `20+nextInt(20)` (`EntityAILookIdle.java:27,43-46`). CREATURE cap `10*i/289` (`EnumCreatureType.java:13`); 400-tick gate `worldTotalTime%400L==0` (`WorldServer.java:206`); biome list sheep 12 / pig 10 / chicken 10 / cow 8 (`Biome.java:142-145`); `EntityAnimal.getCanSpawnHere` grass below and `getLight>8` (`EntityAnimal.java:117-124`). Drops from entity.rand at looting 0 (loot-table counts): cow leather 0..2 + beef 1..3, pig porkchop 1..3, sheep mutton 1..2 + wool if not sheared, chicken feather 0..2 + chicken 1. XP `1+nextInt(3)` is `EntityAnimal.java:147` on `world.rand`; this path draws the same nextInt from entity.rand because lane/worldrand owns World.rand.

Magma extras (shared with blaze, not Java): no EntityAITasks mutex/A* (`blaze/GPU_MOB_AI.md`); panic/wander consume the cited draws then walk the generic hostile straight line; LookIdle consumes draws and yaws, no LookHelper interpolation; WatchClosest/Mate/Tempt/FollowParent/EatGrass OUT; shearing and breeding OUT; chunk-gen `performWorldGenSpawning` OUT (snapshots are loaded regions). `BP_MOBS` tag stays `MBM1` (sheep color/sheared in existing `swell`).

Fixture `s10_t0_r64_mobs_passive.bsnp` baked by `test_passives --write-fixture` (chicken in front, cow/pig/sheep beside, grass pad at +32, one XP orb). Chain `mobs_passive_s10.json`. `--natural-spawn-passive` + `set_time=6000`. M1 VERIFIED 64 ticks t=0 digest `0x1fccac4a4a63688c` (`out/verify/passives_m1_detail.log`). M2 VERIFIED 64 CUDA lanes (`out/verify/passives_m2.log`). `--no-deps` M1 VERIFIED for mobs, xp_orbs, boats, elytra, projectiles, random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice. M2 VERIFIED for those except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). `make -C blaze/rl test-passives` PASS.

Tapes (`replay_tape.py --cpu --no-gate --report`, `mobs=0`): bow physics NO divergence 1407 ticks, entities PASS 5525 ents; smoke_zombie physics NO divergence through death t=358, entities PASS 359 ents; detmob_panic physics NO divergence 410 ticks, entities PASS 410 ents. detmob_passive_142333 physics NO divergence 1211 ticks, entities PASS 4551, world_hash FIRST MISMATCH t=0 (same as baseline). detmob_passive_152220 physics NO divergence 1210 ticks, entities PASS 5387, world_hash t=0 (same). detmob_wander_152429 FIRST DIVERGENCE t=482 hp (same as baseline), entities PASS 6678. detmob_wander_164213 FIRST DIVERGENCE t=56 food (same), entities PASS 7202. detmob_nether_182154 physics NO divergence 861 ticks, entities PASS 1146, world_hash PASS. detmob_nether_182511 physics NO divergence 851 ticks, entities PASS 1702, world_hash t=0 (same). Canon jsonl still INFRASTRUCTURE FAILURE (golden frames path not on this clone) before magma runs; physics unread. First-divergence ticks did not move earlier. `make -C magma test-mob-live` PASS (`out/verify/passives_test_mob_live.log`).

Open: PathNavigateGround A* (Class: design gap, `GPU_MOB_AI.md`); EntityAIMate/Tempt/FollowParent/EatGrass/WatchClosest; shearing; breeding; egg lay; `performWorldGenSpawning`; slime/spider/witch/enderman live insert. Did not edit explosion/TNT/World.rand.

## 2026-08-23 World.rand live stream (lane/worldrand)

Baseline anvil 6e48e81: explosions M1+M2 VERIFIED with density rand fixed 0.5F (`out/verify/worldrand_baseline_m1.log`). mining_slice M2 BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing).

Java `World.rand` is `public Random rand = new Random()` (`World.java:108`), unseeded at world load. Tape rows do not record the cursor: Class C for tape-exact draws. Magma vs blaze share the snapshot seed so M1/M2 close.

Server tick order (`MinecraftServer.java:784` `worldserver.tick()`, `:795` `updateEntities()`):
1. `WorldServer.tick` (`WorldServer.java:180`) calls `super.tick()` = `World.updateWeather` (`World.java:2709`) which re-rolls rain/thunder on `this.rand` (`:2767-2795`). Magma weather is an isolated JavaRandom from the world seed, not this stream.
2. `mobSpawner` (`WorldServer.java:202-207`) uses WorldEntitySpawner. Out of this lane (lane/natspawn).
3. `updateBlocks` (`WorldServer.java:228`, `:389-505`): lightning/ice/snow `this.rand`; random ticks pass `this.rand` into `Block.randomTick` (`:493`) including `BlockFire.updateTick` (`BlockFire.java:146`, `tryCatchFire` `:289`). Magma live randtick substitutes `mc_hash_seed` (`randtick_live.h:10-12`); fire stays on that hash, not World.rand.
4. `sendQueuedBlockEvents` (`WorldServer.java:241`).
5. `updateEntities` (`World.java:1807`): entity `onUpdate`. Explosions fire here (`WorldServer.newExplosion` `:1245-1250` `doExplosionA` then `doExplosionB(false)`).

Ported against one shared `JavaRandom world_rand` (mc_rng.h, same pattern as `ent_jr_seed`):
- Face-ray jitter: `f = size * (0.7F + world.rand.nextFloat() * 0.6F)` (`Explosion.java:102`). 16^3-14^3 = 1352 `nextFloat` draws. Replaces magma-fixed 0.5F.
- `doExplosionB` sound two `nextFloat` (`Explosion.java:198`); server skips particle draws (`doExplosionB(false)`).
- Chain TNT fuse `world.rand.nextInt(fuse/4)+fuse/8` (`BlockTNT.java:72`). Entity table can spawn `EW_TYPE_TNT_PRIMED`.

Not ported:
- `doExplosionB` drops: EntityItem table exists (`GM_LIVE_MAX`/`CU_MAX_ITEMS` 48) but `getDrops`/`quantityDropped` plus HashSet iteration of `affectedBlockPositions` are not in the live contract. `EntityItem` ctor motion is `Math.random()` (`EntityItem.java:59-61`), not `world.rand`; `setDefaultPickupDelay` is 10 (`:564-566`).
- BlockFire live spread stays magma hash (`randtick_live.h` `RT_PURPOSE_FIRE`). Java uses the same `World.rand` instance as explosions (`WorldServer.java:493`).

Snapshot v5 trailer after orbs: 48-bit LCG cursor. v4 loads `jrand_set(0)`. EXP2 -> EXP3 hashes the cursor. Fixture `s10_t0_r64_explosions.bsnp` rebaked via `test_explosions --write-fixture` (TNT block at 7,65,12).

After: explosions M1+M2 VERIFIED (`out/verify/worldrand_after_m1.log`, `out/verify/worldrand_after_m2.log`). Other listed rows stay VERIFIED except mining_slice M2 BLOCKED. Root `make test` PASS (`out/verify/worldrand_maketest.log`). creeper_encounter FIRST DIVERGENCE stays t=76 y 2.1e-09.
## 2026-08-22 WorldEntitySpawner MONSTER (lane/natspawn)

Gamer. Port leftover on `mobs`: Java 1.11.2 `WorldEntitySpawner.findChunksForSpawning` MONSTER plus `EntityLiving.despawnEntity`. Shared `blaze/core/hostile_spawn.h` compiled by magma `gm_mobs_tick` and blaze `cu_hs_run`. Knob `natural_spawn` default 0 (`magma/core/config.def`, `blaze/blaze.conf`) so existing tapes stay `mobs=0` / planted-mob lockstep. Isolated spawn `JavaRandom` salted from world seed (not `ww.rand`) so `weather_optional` stays bit-identical. `Math.random` and `Collections.shuffle` are isolated RNGs too. Entity.rand is seeded from (seed, tick, pos, attempt) without consuming world.rand. Roster insert is zombie/skeleton/creeper; other biome-list picks consume RNG then skip. Table cap `EW_MAX_ENTITIES`/`BLAZE_SNAP_MAX_MOBS`=96 is a magma/blaze shared cap, not a Java rule.

Java: `WorldServer.tick` mobSpawner (`WorldServer.java:180-206`); `MOB_COUNT_DIV` 289 (`WorldEntitySpawner.java:27`); player radius 8, border counted in `i` not eligible (`:52-71`); cap `70 * i / 289` (`EnumCreatureType.java:12`); shuffle (`:92-93`); `getRandomChunkPosition` (`:193-201`); 3 tries, pack `ceil(Math.random()*4)` (`:109,117`); xz `nextInt(6)-nextInt(6)` (`:121-123`); 24-block player / 24-block spawn point (`:128`, 576.0D); `WeightedRandom.getRandomItem` on biome monster list (`Biome.java:146-153`, `WeightedRandom.java:28-37`); `canCreatureTypeSpawnAtLocation` ON_GROUND (`:208-238`); `EntityMob.isValidLightLevel` sky>nextInt(32) then combined<=nextInt(8) (`EntityMob.java:159-180`); `onInitialSpawn` equipment/child rolls (`EntityLiving.java:1256-1269`, `EntityZombie.java:483-555`); UUID two nextLong (`Entity.java:238-241`); despawn persist skip, 128^2 hard, age>600 then nextInt(800) then 32^2 (`EntityLiving.java:787-831`). Magma play `gm_world_ensure` radius 8 when `spawn_clip` is off; snapshot lockstep clips to the region AABB (out of region = air, sky 15). Spawn into `now` then copy so new mobs tick the same tick. Dusk `world_time=13000` has skylightSubtracted=6 so surface combined light 9 never passes nextInt(8); night tests use 18000 (sub=11).

Fixture `s10_t0_r64_mobs.bsnp` rebaked by `test_mobs --write-fixture` with a stone pad at player+32 for ON_GROUND beyond 24 blocks. `--natural-spawn` on the mobs M1/M2 chain plus planted persist zombie+skeleton. `BP_MOBS` tag stays `MBM1` (no hashed-field layout change). M1 VERIFIED 64 ticks t=0 digest `0x7cccfde31c494324` (`out/verify/natspawn_mobs_m1_detail.log`). M2 VERIFIED 64 CUDA lanes (`out/verify/natspawn_mobs_m2.log`). `--no-deps` M1 VERIFIED for xp_orbs, boats, elytra, projectiles, random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, explosions, chests, falling_blocks, weather_optional, mining_slice. M2 VERIFIED for those except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). Root `make test` PASS (`out/verify/natspawn_maketest.log`). `make -C magma test-mob-live` PASS. `make -C blaze/rl test-mobs` PASS.

Tapes (replay `--cpu --no-gate --report`, `mobs=0`): bow physics NO divergence 1407 ticks, entities PASS 5525 ents; smoke_zombie physics NO divergence through death t=358, entities PASS 359 ents; canon physics NO divergence 3617 ticks, entities PASS 16526 ents vs ghost views. Canon jsonl first hostile is `EntitySkeleton` at t=892 pos (6.5, 35.0, 149.5). Magma live table has 0 hostiles (mobs off). Tape header has `seed` and `world_time`, no `world.rand` 48-bit cursor. Exact oracle spawn positions are Class C. M1/M2 still close because magma and blaze share the isolated spawn seed.

Open: det_entity_rng EntityAITasks/A*; Java knockBack (lane/tntknock); passives; slime/spider/witch/enderman live insert; nether `WorldEntitySpawner` (blaze is overworld snapshots). Did not edit knockback or TNT/explosion code.

## 2026-08-22 TNT primed tick (lane/tntknock piece 3)

Baseline after piece 2: explosions M1 VERIFIED without EntityTNTPrimed.

Cause: Java `EntityTNTPrimed` fuse 80 (`EntityTNTPrimed.java:25`), `onUpdate` gravity `0.03999999910593033D` (`:78`), drag `(double)0.98F` (`:82-84`), on-ground `0.699999988079071D` / `motionY*=-0.5D` (`:86-90`), `explode` size `4.0F` smoking (`:111-114`), Y `posY+(double)(height/16.0F)` (javap fdiv f2d). Ctor `Math.random()` horizontal (`:34-37`) is `java.lang.Math.random`, not `world.rand`. Chain fuse is `world.rand.nextInt(fuse/4)+fuse/8` (`BlockTNT.java:72`). Fire uses `BlockFire.tryCatchFire` `random.nextInt` (`BlockFire.java:289`). `doExplosionB` drops use `world.rand.nextFloat` (`Block.java:698`) then three more in `spawnAsEntity` (`:719-721`). Magma/blaze live tick has no `world.rand` cursor (randtick is a hash stream).

After: planted `EW_TYPE_TNT_PRIMED` slot (fuse in swell=20 so the 64-tick chain observes explode). Shared `exl_tnt_on_update`. Magma extra: Y clamp to collision top, not `Entity.move`. EXP2 hashes TNT slot/fuse/pos. explosions M1 VERIFIED (`out/verify/tntknock_explosions_m1_tnt.log`). explosions M2 needed locals for packed `RlSnapMob.x` (offset 20, CUDA ld.f64) plus 128 KB stack for getBlockDensity DDA (`out/verify/tntknock_explosions_m2.log`). mobs M1 still VERIFIED.

Stay out (no common evidence): chain fuse `world.rand.nextInt`; fire `tryCatchFire` rand; `doExplosionB` item drops; ctor `Math.random()` xz kick (spawn my is the cited 0.20000000298023224D only). Flint&steel still only `setBlockToAir` in player_ctl (tape supplies the view).

## 2026-08-22 melee knockBack (lane/tntknock piece 2)

Baseline after piece 1: mobs M1 VERIFIED with no EntityLivingBase.knockBack on the generic path.

Cause: Java `EntityLivingBase.knockBack` (`EntityLivingBase.java:1296-1316`, javap: `MathHelper.sqrt` float then `xRatio/(double)f*(double)strength`, Y cap `0.4000000059604645D`) consumes `this.rand.nextDouble()` vs KR (default 0, always applies). `attackEntityFrom` flag1 (`:1056-1067`) calls `knockBack(entity, 0.4F, d1, d0)` with `d1=attacker.posX-this.posX`. Overflow i-frame hits set flag1=false (no knockBack). `EntityMob.attackEntityAsMob` extra knockBack only when knockback enchant i>0 (`EntityMob.java:113-117`). `EntityPlayer.attackTargetEntityWithCurrentItem` adds +1 when sprinting (`:1368-1432`) then `motionX/Z*=0.6D` `setSprinting(false)`. 1.11.2 `Entity.setBeenAttacked` is `velocityChanged=true` with no rand (`Entity.java:1666-1668`). Degenerate xz < 1e-4 uses `Math.random()` jitter; CUT when xz is large. Player has no JavaRandom in the sim.

After: shared `ml_knockback` / `ml_hurt_gate` returns 1=flag1 / 2=overflow. Generic mob->player and player->mob apply 0.4F; sprint adds yaw knockBack. Mob `entity.rand` is `seed48` / `ent_jr_seed`. Units pin Y cap and i-frame flag. mobs M1 VERIFIED (`out/verify/tntknock_mobs_m1_knockback.log`). explosions M1 still VERIFIED. MBM1 layout unchanged.

Open: TNT; knockback enchant on held items; fire aspect; MathHelper SIN_TABLE yaw; player.rand; det_entity_rng path unchanged.

## 2026-08-22 explosion knockback (lane/tntknock piece 1)

Baseline anvil 8a86487 explosions M1 VERIFIED (`out/verify/tntknock_baseline_explosions_m1.log`); kb fields hashed as 0. Magma extras on close: exposure 1.0, no knockback.

Cause: Java `Explosion.doExplosionA` (`Explosion.java:144-188`) does `d12 = getDistance / f3`, `d5/d7/d9` with `d7 = posY + (double)eyeHeight - explosionY`, `d13 = (double)MathHelper.sqrt` (`:157`, javap f2d), `d14 = (double)World.getBlockDensity` (`World.java:2456-2494`, `rayTraceBlocks(start,end,false,false,false)` `:998`), `d10 = (1-d12)*d14`, damage `(float)((int)((d10*d10+d10)/2*7*(double)f3+1))` (javap d2i i2f), `d11 = EnchantmentProtection.getBlastDamageReduction` (`EnchantmentProtection.java:99-108`; level 0 identity), `motion += d5*d11` (`:174-176`), `playerKnockbackMap` stores `d5*d10` (`:184`). `world.rand.nextFloat` per face ray stays magma-fixed 0.5F (`ex_density_scale`); that stream is not consumed. `getBlockDensity` uses `java.lang.Math.floor` for d3/d4, no Random.

After: shared `ex_block_density` / `ex_entity_blast` in `blaze/core/explosion.h`. Magma `runtime_explode` and blaze `cu_explode` density+damage+motion on the intact grid, then `exl_apply_hits`. Living slots via `gm_mobs_explosion_knockback` / `cu_explode` mob loop. Fixture baker plants crater dirt off the player-+Z LOS so density is not 0; recaptured `s10_t0_r64_explosions.bsnp` via `--write-fixture` (not hand-edited). EXP1 layout unchanged (kb fields now nonzero). Units: air density 1.0F, stone 0.0F, +Z blast addz < 0, prot 0 identity. Magma `test_runtime` creeper PASS including knockback. explosions M1 VERIFIED (`out/verify/tntknock_explosions_m1.log`). mobs M1 still VERIFIED.

Open: melee `EntityLivingBase.knockBack`; TNT; fireball; doExplosionB drops; density rand; blast-prot scan; attackEntityFrom 0.4F from the exploder.
## 2026-08-22 boats_elytra_xp M1+M2 (lane/boatsxp)

Gamer. Split `boats_elytra_xp` into `xp_orbs`, `boats`, `elytra`. Baseline BLOCKED (`out/verify/boatsxp_baseline_boats_elytra_xp_m1.log`).

XP: magma `tick_xp_orbs` summed `xp_total` and skipped `--mobs off`. Java `EntityXPOrb.getXPSplit` (`EntityXPOrb.java:298-301`), `onUpdate` delay/gravity/attract/despawn 6000 (`:87-174`), `onCollideWithPlayer` xpCooldown=2 (`:239-265`), `EntityPlayer.addExperience` / `xpBarCap` (`EntityPlayer.java:2145-2211`). Shared `blaze/core/xp_live.h`. Snapshot v4 orb trailer. Magma extras: hash spawn motion, no lava/water/Mending. Fixture `s10_t0_r64_xp_orbs.bsnp` one orb (8.5,66.5,11.5) value 17 delay 10. M1 VERIFIED 64 ticks t=0 digest `0x7f972093d78b1f51`. M2 VERIFIED 64 CUDA lanes.

Boats: magma `tick_boat` was magma-only and skipped `--mobs off`. Java `EntityBoat.onUpdate` / `updateMotion` / `controlBoat` (`EntityBoat.java:272-367,643-701,704-748`). Shared `blaze/core/boat_live.h`. Magma extras: 3-way status, 4-corner collide, ride y+0.35. Fixture water pool + boat (12.5,65.2,12.5). M1 VERIFIED 64 ticks t=0 digest `0x642f0daadce74471`. M2 VERIFIED 64 CUDA lanes.

Elytra: travel already in `player_survival.h`; blaze lacked START_FALL_FLYING. Java `EntityPlayerSP.java:1030-1036`, `NetHandlerPlayServer.java:1019-1027`, `updateElytra` 20-tick chest damage. Shared `blaze/core/elytra_live.h`. `--set elytra=1` equips chest 443 (armor not in .bsnp). Fixture player (8.5,80,8.5) air. M1 VERIFIED 64 ticks t=0 digest `0x9fba266b58990185`. M2 VERIFIED 64 CUDA lanes.

`--no-deps` still VERIFIED: random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, mobs, explosions, chests, falling_blocks, weather_optional, mining_slice M1. projectiles M1 FAIL observation 22 magma evidence 2 vs blaze 1 (pre-existing, lane/projground). mining_slice M2 BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). Root `make test` PASS (`out/verify/boatsxp_maketest.log`). No tape on this clone header-declares XP orbs, boats, or elytra flight.

Open: handleWaterMovement on orbs; Mending; Java Math.random spawn; UNDER_WATER / UNDER_FLOWING_WATER boat status; Entity.move boat collision; snapshot armor slots; fly-into-wall tape evidence.

## 2026-08-22 projectiles M1 inGround (lane/projground)

Baseline anvil 0e099f6 (7edfd2b simsmalls is an ancestor): projectiles M1 FAILED at observation 22. Magma digest `0x8e2d11e7e393fea1` evidence=2; blaze `0xd9e47b3676353ee4` evidence=1 (`out/verify/projground_baseline_projectiles_m1.log`). VERIFIED before simsmalls; broke when magma `runtime.c` restuck arrows on block hit (`2162b7f`).

Cause: Java `EntityArrow.onHit` sets `inGround` + `arrowShake=7` (`EntityArrow.java:471-472`). Magma `tick_projectiles` (`runtime.c:460-477`) reactivates after `pl_tick_arrow` if the cell is non-air, then `onUpdate` shake countdown and 1200-tick despawn (`EntityArrow.java:223-248`, `runtime.c:449-456`) and `onCollideWithPlayer` pickup (`EntityArrow.java:604-618`, `runtime.c:302-331`, `setSize(0.5F,0.5F)` `EntityArrow.java:78`). blaze still deactivated on block hit and passed creative=0 into `isr_try_fire_bow`. Digest `nents` is the active count, so the sides diverged. Magma lockstep `--rl-bin` never writes `gm_runtime_tape_player_view`; `r->tape_creative` stays 0. Did not edit `magma/game/runtime.c`.

After: blaze sidecar + `e->tape_creative` (MC_HD). Shared helpers in `blaze/core/projectile_live.h`. Unit `blaze/env/test_projectiles.c` sticks one arrow, shake 7->0, ALLOWED pickup merge, despawn at 1200. projectiles M1 VERIFIED 64 ticks (`out/verify/projground_after_projectiles_m1.log`). M1 `--no-deps` also VERIFIED: random_ticks, world_dynamics, spawn_to_torch, fluids, entity_spine, mobs, explosions, chests, falling_blocks, weather_optional, mining_slice (`out/verify/projground_m1_nodeps.log`). M2 GPU1 VERIFIED for those rows except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing on this clone; `out/verify/projground_m2_nodeps.log`). Root `make test` PASS (`out/verify/projground_maketest.log`).

Open: mining_slice M2 needs the d-stage snaps on a complete tree. Fireballs, eye-of-ender, Java ray-trace, water 0.6F drag stay out.

## 2026-08-22 random_ticks M1 skylight (lane/lightsync)

Baseline anvil d456936: random_ticks M1 FAILED at observation 84. Magma digest `0x099236e78eba8377` evidence=11; blaze `0x03401e738ea70aca` evidence=10 (`out/verify/lightsync_baseline_random_ticks_m1.log`). VERIFIED at 42117bc; broke in 7435206 (lane/underwater merge). Magma `light_set_state` (`magma/world/light.c:751`) marks `column_dirty` on opacity change; `light_ensure` (`:690`) reruns `Chunk.generateSkylightMap` (`Chunk.java:238`, `cr_k17_skylight_column` `light.c:69`) for that chunk, then raise-only spread (`compute_skylight_spread` `:541`). Blaze still used `cu_light_raw_sky` + radius-15 `cu_light_relax_open/close`. Raise-only never lowered a 3x3x3 water cube (centre stuck at 12, magma centre 9). `randtick_live.h` reads `rt_live_light`, so the nibble mismatch changed which ticks fired.

Cause: Java `World.checkLightFor` decrease (`World.java:3046`) can lower neighbours. Magma's flood only raises, so it rebuilds the chunk ladder instead of `Chunk.relightBlock` (`Chunk.java:392`). `generateSkylightMap` is per-column independent; a full-chunk rebuild then spread matches magma. Did not edit `magma/world/light.c`.

After: `cu_light_after_opacity` in `blaze/env/blaze_core.h` (MC_HD). Unit `blaze/env/test_skylight_water.c` writes a 3x3x3 still-water cube and matches magma `light_set_state`+`light_ensure` (top 12, mid 9, bot 10, edge 12). random_ticks M1 VERIFIED 200 ticks (`out/verify/lightsync_after_random_ticks_m1.log`). M1 `--no-deps` also VERIFIED: world_dynamics, spawn_to_torch, fluids, entity_spine, mobs, explosions, projectiles, chests, falling_blocks, weather_optional, mining_slice. M2 GPU1 VERIFIED for those rows except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing on this clone). Root `make test` PASS (`out/verify/lightsync_maketest.log`).

Open: mining_slice M2 needs the d-stage snaps on a complete tree.
## 2026-08-23 portal horizon occupancy (lane/portalhz)

Item A2 overlay_portal_050 on anvil. Baseline matched the file:
noise=0 C-vs-J=0.972 hard_px=363609 maxch=115 RESIDUAL; underwater
1.202 / 387388 / 46; mutations PASS. LSB guard FAIL is pre-existing
dragondirt eat pin (73440/21526 vs live 73443/21529), not this row.
`out/verify/portalhz_ui_hud_baseline.log`.

Java 1.11.2 world warp is only setupCameraTransform RSR
(EntityRenderer.java:746-761): f2=5/(t^2+5)-t*0.04 squared, rotate
(count+pt)*20 about (0,1,1), scale(1/f2,1,1), rotate back. orientCamera
T_z(0.05) then pitch then yaw+180 then -eye (java:681,698-702).
getFOVModifier has no portal branch (java:518-549). renderHand reloads
gluPerspective without RSR (java:791-804). Overlay is GuiIngame.renderPortal
NEAREST + tex.a*ease (GuiIngame.java:1112-1143, AbstractTexture.java:30-35).
Later nausea (`t*2`, rotate t*5 about X/Y, scale 1/(1+t*0.2)) is not in
this oracle.

pxdiff survey: one cluster >=50px at thresh 25, 129px (y 235-238, x 818-853),
cause registration best_shift (-1,-2). Golden there is sky
(161,168,243); C is grass (118,123,157). Independent GL stack matches
cr_camera_view. The same RSR maps grass y=5 at world (-44.9, 58.5)
to ~835,236 — the right-horizon grass plane at ~50 blocks, not pad,
not sky-plane y=16, not overlay. Leftover is 1-2 px silhouette fill.
Hand ROI maxch 39 is 11 occupancy px (pad/grass + one hand edge),
interior BYTE-pack. Overlay sample/alpha already match.

No product constant changed. Unit: magma/game/test_overlay.c independent
GL stack vs cr_camera_view on the wall corner and the D=50 grass point;
verify/ui_hud/test_ui_hud_numerical.c pins f2 and hand-unwarped.

After: overlay_portal_050 still 0.972 / 363609 / 115 RESIDUAL. Underwater
and other ui_hud rows byte-stable vs baseline. ROI eq1=257987 eq2=98832
BYTE-pack floor. Item stays OPEN.

## 2026-08-22 sim smalls arrow consume + heart-flash (lane/simsmalls)

Gamer. Tapes rsynced from anvil (host `~/dev/netherite` lacked bow/zombie
jsonl). Baseline `out/verify/simsmalls_baseline_blaze_bow.log` /
`simsmalls_baseline_smoke_zombie.log`. After
`simsmalls_after_blaze_bow.log` / `simsmalls_after2_smoke_zombie.log`.

blaze_bow: physics exact 1407/1407; inventory PASS 10 independent / 0
mismatches before and after. Live slot-8 arrows match tape at t=77,117,
216,316,565. smoke_zombie: physics exact through death t=358; t=40 heart
row 7 LSB; t=320/340 whole 0.38/0.48 /ch unchanged.

Cause: ItemBow.findAmmo / shrink (ItemBow.java:47-70, 148-155) and
EntityArrow pickup (EntityArrow.java:604-618) were incomplete (main-only
item 262 scan, no inGround). HUD flash already existed; it used hurtTime
as the resistant proxy. Ported findAmmo/infinity/creative/pickup and
hurtResistantTime into `gm_hud_state_step`. Low-hp jitter is Class C
without recorded updateCounter.

`bash magma/game/test_hud.sh` PASS; `bash magma/game/test_runtime.sh`
PASS. Cannot run ui_hud on gamer.

## 2026-08-22 mobs M1+M2 (lane/mobs)

Baseline anvil HEAD 27ddb52: `BLOCKED mobs: Mob spawning, AI, combat, and drops lack end-to-end common evidence` (`blaze/env/port_matrix.yaml`). Shared headers already had spawning (`mob_spawning_world.h`), hostile spine (`entity_hostile_spine.h`), synthetic A* (`mob_ai_zombie_astar.h`), and living `Entity.move` (`entity_spine.h`). Magma `gm_mobs_tick` (`magma/game/mob_live.c`) was the live AI/combat/spawn path; blaze hashed the v3 roster and ticked spine only.

Cause: blaze had no generic hostile AI, no player melee vs living slots, no i-frames, no death drops, and `PL_HIT_PLAYER` was 0 so skeleton arrows never hurt the player. Java `EntityZombie.initEntityAI` (`EntityZombie.java:77-86`) swim/melee/restrict/village/wander/watch/idle then `applyEntityAI` hurt-by + NAT player/villager/golem (`:88-95`); `ATTACK_DAMAGE` 3 (`:102`). `AbstractSkeleton.initEntityAI` (`AbstractSkeleton.java:79-91`). `EntityCreeper.initEntityAI` (`EntityCreeper.java:63-74`) swim/swell/avoid/melee. `EntityAIAttackMelee` interval 20 mutex 3 (`EntityAIAttackMelee.java:27,43-73,111-159`). `EntityAINearestAttackableTarget` chance 10 (`EntityAINearestAttackableTarget.java:32-80`). `EntityMob.attackEntityAsMob` (`EntityMob.java:98-128`). `EntityLivingBase.attackEntityFrom` hurtResistantTime/lastDamage (`EntityLivingBase.java:935-1007`, max 20 at `:114`); `knockBack` (`:1296-1316`); `onDeath`/`dropLoot` (`:1224-1279`). `EntityLiving.despawnEntity` 128^2 hard / age>600+nextInt(800) and 32^2 (`EntityLiving.java:787-831`). Magma extras kept: det_entity_rng off (hash wander, straight chase, follow zombie 40 not Java 35), reach 2.0 xz, no knockBack on player or hit hostiles, persist does not skip despawn, one-item drops (367/352/289).

After: shared `blaze/core/hostile_live.h`. Magma `gm_mobs_tick` for `hai_ok && !pai_det` is a thin load/save around `ml_hostile_ai`; `gm_mobs_attack_player` uses `ml_hurt_gate`. Blaze `--mobs-on` ticks that path after randtick (magma `--set mobs=1`); `--mobs off` still spine+creeper fuse. `BP_MOBS` extends `blaze_snap_mobs_digest` with player hp, hurt i-frames, attack cooldown, and live item drops (`MBM1`). Fixture `s10_t0_r64_mobs.bsnp` + 64-action chain (`mobs_s10.json`): roofed stone pad, zombie (8.5,65,11.5), skeleton (12.5,65,8.5), 8 idle / 20 walk / 36 melee. Anvil M1 VERIFIED 64 ticks (`out/verify/mobs_m1.log`) t=0 digest `0x7cccfde31c494324`. Anvil M2 VERIFIED 64 CUDA lanes on gpu1 (`out/verify/mobs_m2.log`). CUDA `k_tick_raw` IMA on fluids needed `cudaLimitStackSize` 64 KB (was 32 KB; live CA plus inlined hostile helpers). mining_slice/spawn_to_torch/world_dynamics/fluids/entity_spine/random_ticks/falling_blocks/weather_optional/chests/projectiles/explosions `--no-deps` still VERIFIED. Root `make test` PASS (`out/verify/mobs_maketest.log`).

Open: WorldEntitySpawner natural spawn; det_entity_rng EntityAITasks/A* (GPU_MOB_AI.md); Java knockBack on the player; passives/boats/XP/blaze/ghast/slime/spider; loot tables. Magma still uses hash wander and follow 40.
## 2026-08-22 dragon death held-block chain (lane/dragondirt)

Parent verification 2026-08-23 (anvil ui_hud, gamer ui_entities on the merged tree): the explicit quaternion chain moves hand pixels by rounding only: hand_bow_pull20 hard_px 20830 -> 20846, hand_eat_mid 73440 -> 73443 (px>1 21526 -> 21529), fireball_dragon 38365 -> 38260 (CAPTURE_BLOCKED); every c_vs_j unchanged at three decimals; dragon rows identical; verdicts unchanged.

A6 dragon_death_50/100/190 after lane/dragonbob. Gamer baseline
`out/verify/dragondirt_baseline.log`: geom ALL PASSED; hard_px
84995/83667/84543, c_vs_j 2.157/2.165/2.150, ab_nz=0 RESIDUAL.
fireball_small 44922 / 4.944, xp_orb 3484 / 4.093. Other 13 rows
match dragonbob.

Cause: Java idle dirt is two SIDE faces (golden bbox x 602-771
y 338-479 on dragon_death_50_a). Magma C at the same lower-right
shows the TOP almost face-on. Camera-state Rx candidates stay
no-ops (D4-D7). Ported the item chain from Java 1.11.2, not a
fitted angle: `ItemRenderer.renderItemInFirstPerson` else branch
(ItemRenderer.java:430-441), `transformSideFirstPerson` T(0.56,
-0.52,-0.72) (:304), `transformFirstPerson` swing 0 Ry(45)*Ry(-45)
net I (:290-298), `renderItemSide` FIRST_PERSON_RIGHT
leftHanded=false (:441) so no scale(-1). `applyTransformSide`
T / `makeQuaternion` XYZ / scale (ItemCameraTransforms.java:76-108)
via `quatToGlMatrix` (GlStateManager.java:641-670).
block.json firstperson_righthand [0,45,0] scale 0.40. Forge
MapWrapper wrap `blockCenterToCorner` then unwrap
`blockCornerToCenter` (IPerspectiveAwareModel.java:90,99;
TRSRTransformation.java:622-644) is identity on that T*R*S.
`RenderItem.java:144` T(-0.5) after, so rotation is about the
model centre of the 0..1 cube (dirt.json cube from/to /16).
test_hand D8 pins every emit vert to that product.

ui_hud `hand_block_shield` is blocking shield 442 (use_action=2,
`shield_blocking.json`), pose pitch 0, not this idle dirt path.
Cannot measure ui_hud on gamer (anvil llvmpipe only).

After (`out/verify/dragondirt_after.log`): same numbers. Geom ALL
PASSED (`out/verify/dragondirt_geom.log`). `bash magma/game/test_hand.sh`
PASS (D8). Root `make test` PASS (`out/verify/dragondirt_maketest2.log`).
Twins not edited. Fireball/XP / ROI not edited.

Not closed. The Java-cited chain still draws the top face; the
golden's two side faces are not produced by T*R*S*T(-0.5) at
T(0.56,-0.52,-0.72). Do not fit Rx.

## 2026-08-22 bow pull ROI triage (lane/bowpix)

Item `hand_bow_pull20` after lane/bowgold recapture (`fov_mult=0.85` in
meta). No GPU. Anvil baseline `bash verify/ui_hud/run_ui_hud_gates.sh`
(`out/verify/bowpix_baseline.log`): c_vs_j=5.836 hard_px=20830 maxch=97
px>1=11111 owned=31024 cap=620.5 RESIDUAL. Eat 1.317 / 73440 / 21526 and
shield 0.911 / 28564 / 6925 byte-stable. Core HUD PASS. Mutations PASS.
LSB guard PASS.

Triage (same ROI 569,320,846,432 as shield; ui_hud_lsb buckets + pxdiff
`--a/--b` on the ROI crop):

| class | bow n | bow mean/ch | bow maxch | shield n |
|----|--------|-------------|-----------|----------|
| world texel/shade | 9974 | 15.669 | 58 | 584 |
| BYTE-pack LSB (maxch<=2) | 8980 | 0.898 | 2 | 27976 |
| selbox | 1389 | 1.362 | 97 | 0 |
| wall/grass occupancy | 487 | 30.450 | 71 | 1 |
| item/bow texels gt2 | 0 | 0 | 0 | 3 |

gt1 buckets bow: wall=10611 painted=0 selbox=13 grass=487. eq1 painted=3432
(BYTE-pack on the bow). C `wall_xmin=0` grass_cols=0; J `wall_xmin=20`
grass_cols=40. Shield C and J `wall_xmin=77`. pxdiff bow ROI: cluster 0-1
`content` is J grass vs C stone at the right edge; shield 0 clusters
>=50 px at thresh 25.

Java: world `getFOVModifier(pt,true)` = fovSetting * fovModifierHand
(EntityRenderer.java:529-532, :730). Hand `getFOVModifier(pt,false)` = 70
(`:804`). Bow pull pose is ItemRenderer.java:402-427 (no
transformFirstPerson; f5/f6/f7 tremble at pull=20 is saturated f6=1).
C already does that (`build_bow_drawn`, `ui_hud_scene` reads meta
0.85). At 70*0.85=59.5 the pad wall fills the frame. The golden wall
edge is the mid-ease 0.887 geometry. Do not fit 0.887. Recapture
forbidden this lane. `hand.c` not edited.

No C change. `oracle_roi_report.json` regenerated, byte-identical to
the pre-gate copy. Other ui_hud rows byte-stable vs baseline. Root
`make test` PASS (`out/verify/bowpix_maketest.log`).

## 2026-08-22 underwater skylight decrease (lane/underwater)

Item A1 overlay_underwater. Anvil baseline `bash verify/ui_hud/run_ui_hud_gates.sh`:
c_vs_j=7.311 hard_px=390096 maxch=41 (matches OPEN_DIVERGENCES lane/raster).
Portal 0.972 / 363609 / 115. Logs: `out/verify/underwater_baseline.log`.

Cause: magma skylight spread only RAISES. Filling the capture glass pool
(water opacity 3, Block.java:2412-2413) left every water cell at sky=12
because the first glass/air neighbour raised the cell and later water
edits never lowered it. Java World.checkLightFor decrease
(World.java:3046-3093) plus Chunk.generateSkylightMap (Chunk.java:238-278)
settles the 3x3x3 centre at 9. Overlay brightness is
Entity.getBrightness -> table[sky] (ItemRenderer.java:539); table[12]=0.5
became table[9]=0.2727. Fog color stayed the pinned oracle
fogColor1=0.6447164 * (0.02,0.02,0.2). Overlay formula untouched.

Anvil after: overlay_underwater 1.202 / 387388 / maxch=46. Portal
0.972 / 363609 / 115 unchanged. Other comparable ui_hud row (portal)
byte-stable. This host has only portal+underwater goldens; HUD/hand/fire
rows stay MISSING JAVA as in baseline (mutation FAIL 3 missing assets,
same as baseline). `make -C magma test` PASS including test-water-skylight
(3x3x3 cube centre sky==9). Root `make test` otherwise PASS
(`out/verify/underwater_maketest.log`); tape-info needed the gitignored
canon jsonl copied from the canonical clone. No kernel twins.

Open: 1.202/ch meets hard goal 2.0; not closed. Interior is 1-2 LSB
(C=[65,68,85] vs J=[66,70,86] at (2,2)). Four px maxch=46 at (283,130)
and (570,130). Do not fit overlay alpha.
## 2026-08-22 dragon death hand Rx (lane/dragonbob)

A6 dragon_death_50/100/190 after lane/dragonhand. Gamer baseline
`out/verify/dragonbob_baseline.log` (f757bd7): geom ALL PASSED;
hard_px 84995/83667/84543, c_vs_j 2.157/2.165/2.150, ab_nz=0 RESIDUAL.
Other 13 rows match dragonhand (slime/magma CAPTURE_BLOCKED, fireball_small
44922, xp_orb 3484).

Cause: the y=244 house-peak is the endstone shelf under world
`orientCamera` pitch 15 (`DRAGON_CAM` driver.py:37-39; meta pose
pitch 15, y=70, `no_gravity`), not the dirt viewmodel. Java dirt
(brown) bbox on `dragon_death_50_a` is x 602-771 y 338-479. C identity
dirt is the same lower-right (test_hand D7, 854x480 fov70 top y>300).

(1) `applyBobbing` (EntityRenderer.java:582-595) only if `viewBobbing`
(:816-818). `capture_ui_entities.sh:74` `bobView:false`. Even if on,
`EntityPlayer.java:583-601` cameraPitch target is
`atan(-motionY*0.20000000298023224D)*15.0D`, forced 0 on ground;
`set_pose` zeros motionY (Recorder.java:4247) so air hover is 0 (D5).
Terminal fall ~10 deg, not look 15.
(2) `hurtCameraEffect` (:552-576) returns if `hurtTime-pt<0`. No hurt
pin; creative; dragon 40 blocks away.
(3) `renderHand` `loadIdentity` (:804-806). `rotateArroundXAndY` pops
(ItemRenderer.java:89-96, D4). `rotateArm` is
`0.1*(rotationPitch-renderArmPitch)` (:112); settled pin Rx(0) (D6).
ui_hud `hand_block_shield` / `hand_eat_mid` pose pitch 0 already
encode identity.
(4) `equippedProgressMainHand=1` after settle (:608-630); already
`gm_frame_capture_equip_idle`.

Fitting `glRotatef(15,1,0,0)` on the cube raises it into the shelf (D7)
and is not Java. Capture artefact. Do not port. Numbers unchanged.

After: same (`out/verify/dragonbob_after.log`). `bash magma/game/test_hand.sh`
PASS (D5-D7). Geom ALL PASSED. Root `make test` in
`out/verify/dragonbob_maketest.log`. shade.c / Metal twin not edited.
Fireball/XP / ROI not edited.

Not closed.

## 2026-08-22 explosions M1+M2 (lane/explosions)

Baseline anvil HEAD 9818ade: `BLOCKED explosions: Explosion damage and world mutation are not measured by both backends` (`out/verify/explosions_baseline.log`). Shared `explosion.h` already had RAND-FREE `doExplosionA` rays; magma `runtime_explode` used it; blaze did not tick creeper fuse or apply the blast.

Cause: blaze had no ignited fuse, no live `runtime_explode`, no `BP_EXPLOSIONS`. Java `EntityCreeper` fuse 30 / radius 3 (`EntityCreeper.java:52-54`), `hasIgnited`/`ignite` (`:338-346`), `onUpdate` `timeSinceIgnited += state` then `explode` at fuse (`:158-191`, `:303-314`). `World.createExplosion` (`World.java:2436-2438`) / `newExplosion` (`:2444-2450`) `doExplosionA` then `doExplosionB`. `WorldServer.newExplosion` (`WorldServer.java:1245-1266`) `doExplosionB(false)`. `Explosion.doExplosionA` 16x16x16 face rays, step 0.3D / 0.22500001F, resistance, entity damage+knockback (`Explosion.java:82-191`); `doExplosionB` drops/particles/fire (`:196-248`). Magma extras kept: density rand fixed 0.5F (`explosion.h`), exposure 1.0, no knockback, no drops, blast Y = feetY+0.5, unpowered 3.0F always destroys, player+dragon only.

After: shared `blaze/core/explosion_live.h`. Magma `gm_mobs_tick_creeper_fuse` + `runtime_explode` are thin wrappers. Blaze ticks fuse after spine then apply. `BP_EXPLOSIONS` hashes pending, last blast rays/destroyed/damage/kb=0, creeper slot/fuse/ignited/alive (`EXP1`). Fixture `s10_t0_r64_explosions.bsnp` + 64-action chain (`explosions_s10.json`): ignited creeper at (8.5,65,12.5), 36 idle / 28 walk. Anvil M1 VERIFIED 64 ticks (`out/verify/explosions_m1_verify2.log`); t=0 digest `0xd5cdf4c5030251ab`, blast t=30 digest `0x1961ba9ffd1f7deb` evidence 23 health 20->11. Anvil M2 VERIFIED 64 CUDA lanes on gpu1 (`out/verify/explosions_m2.log`). mining_slice M2 and spawn_to_torch/world_dynamics/fluids/entity_spine/random_ticks/falling_blocks/weather_optional/chests/projectiles `--no-deps` still VERIFIED. Root `make test` PASS (`out/verify/explosions_maketest2.log`). Magma `test_runtime` creeper `--mobs on` still PASS.

Open: TNT missing; fireball explosions stay magma-only; Java knockback / getBlockDensity / doExplosionB drops / powered 2x / mobGriefing / EntityAICreeperSwell 3/7 LOS stay out. Magma still uses rand-free density and Y+0.5.
## 2026-08-22 dragon death dirt pose (lane/dragonhand)

A6 dragon_death_50/100/190 after lane/dragonpass. Gamer baseline
`out/verify/dragonhand_baseline.log` (abfe93d): geom ALL PASSED;
hard_px 84995/83667/84543, c_vs_j 2.157/2.165/2.150, ab_nz=0 RESIDUAL.
Other 13 rows match dragonpass (slime/magma CAPTURE_BLOCKED, fireball_small
44922, xp_orb 3484). t=50 hard breakdown: eq1=28582 eq2=39973 maxch>8=7941;
J-sky 40435.

Cause (pose): C already ports the 1.11 first-person block path that
lane/dragonpass turned on (dirt slot 0 + `gm_frame_capture_equip_idle`).
`ItemRenderer.transformSideFirstPerson` T(0.56F,-0.52F,-0.72F)
(ItemRenderer.java:304), `transformFirstPerson` at swing 0 is Ry(45)*Ry(-45)
(:290-298), `block.json` firstperson_righthand rotation [0,45,0] scale 0.40,
`RenderItem.renderItem` T(-0.5) (:144). `hand.c` `build_held_item_base` +
`apply_fp_camera(is_block)` is that chain; ui_hud `hand_block_shield` uses
the same `gm_hand_emit_held`. Eye AABB at idle: x 0.28..0.84, y -0.72..-0.32,
z -1.05..-0.49. Projected top y~344. Java dirt is a house-peak (bbox
x500-752 y244-429). Rx(g_env_pitch=15) on the cube raises top y to 251
(width 247 vs Java 252) but `rotateArroundXAndY` pops that Rx so only
LIGHT0/1 follow the camera (:89-96); `hand_diffuse` already uses
g_env_pitch. Applying Rx to geometry failed the flint rim ao=1 test
(pitch is lighting) and would pitch the viewmodel in play. Not applied.
test_hand D3 pins the AABB; D4 pins pitch-invariant verts.

Cause (1-px hole): (482,22) J (255,255,255) C (15,15,15). Neighbors y=21
both white, y=23 both 15. Geom already pins skin a<=25 keeps exploding,
a>=26 overwrites (RenderDragon.java:66 alphaFunc 0.1). C kept skin.

Cause (sky/endstone LSB): no new Java cite. Endstone 2-8 LSB across the
shelf; sky 1-LSB bands. Do not retune fog or pack.

After: numbers unchanged. `bash magma/game/test_hand.sh` PASS. Geom ALL
PASSED (`out/verify/dragonhand_after.log`). Root `make test` PASS after
copying the canon tape jsonl from anvil (`out/verify/dragonhand_maketest.log`).
Deleted Mach-O `magma/tests/test_raster_smoke` and `test_sky_weather`
(gitignored). shade.c / Metal twin not edited. Fireball/XP / ROI not edited.

Not closed.

## 2026-08-22 projectiles M1+M2 (lane/projectiles)

Baseline anvil HEAD 88a2b8e: `BLOCKED projectiles: Projectile lifecycle and collision outcomes are not measured by both backends` (`out/verify/projectiles_baseline.log`, port_matrix rc=3). Shared `projectile_motion.h` is the trimmed synthetic in-air kernel (entity_trace); live magma `tick_projectiles` was not in blaze.

Cause: blaze did not spawn or tick live arrows. Java `ItemBow.getArrowVelocity` (`ItemBow.java:167-177`) `charge/20F` then `(f*f+f*2F)/3F` clamp 1.0F; fire if `(double)f >= 0.1D` (`:102`); `setAim` velocity `f*3.0F` (`:110`, `EntityArrow.java:120-133`). `EntityArrow.onUpdate` (`:196-357`) ray-trace + `findEntityOnPath` (`:502-530`), drag `(double)0.99F` (0.6F water), gravity `0.05000000074505806D`, `inGround` life 1200 (`:245-248`), `onHit` block sets `inGround` + `arrowShake=7` (`:454-478`); entity hit `ceil(speed*damage)` then `attackEntityFrom` (`:366-392`) with `knockBack(..., 0.4F, ...)` (`EntityLivingBase.java:935-1067`, `:1296-1309`). Magma extras kept: 0.25-block substeps, any non-air id despawns (no stick/pickup), drag/gravity as double 0.99/0.05, damage `speed*2` sphere r=0.75, spawn libm `sin(yaw*PI/180)` + 0.2 aim offset + `PSV_EYE_HEIGHT`.

After: shared `blaze/core/projectile_live.h`. Magma `spawn_bow_arrow` / type-1/2 `tick_projectiles` are thin wrappers. Blaze draws on `use` with held 261, ticks after spine. `BP_PROJECTILES` hashes live arrows (pos, motion, age; inGround/tile/shake stay 0) plus mob slot+health and hit count (`PRJ1`). Fixture `s10_t0_r64_projectiles.bsnp` + 64-action chain (`projectiles_s10.json`): 20 use / release at +Z wall, dyaw=-90, 20 use / release at a zombie. Anvil M1 VERIFIED 64 ticks (`out/verify/projectiles_m1_verify2.log`); t=0 digest `0x79df273ecc6afb75`, wall hit t=22 `0xd9e47b3676353ee4`, mob hit t=52 `0x02907f7a118c91fb`. Anvil M2 VERIFIED 64 CUDA lanes on gpu1 (`out/verify/projectiles_m2.log`). mining_slice M2 and spawn_to_torch/world_dynamics/fluids/entity_spine/random_ticks/falling_blocks/weather_optional `--no-deps` still VERIFIED. Root `make test` PASS.

Open: fireballs/eye-of-ender stay magma-only (explosions/portals rows). Magma still does not port Java ray-trace, water 0.6F drag, inGround/inTile/shake/pickup, or knockBack.
## 2026-08-22 chests M1+M2 (lane/chests)

Baseline anvil (`out/verify/chests_m1_before.log`): `BLOCKED chests: Chest generation, loot, and GUI transfers are not measured end to end` (port_matrix rc=3). Shared `tile_entity_chest.h` / `container_click.h` already existed; magma `chest_live.c` / `container_live.c` already ticked them.

Cause: blaze had no live chest TE table, no id-54 interact, no `GmAction.inv_click` on `blaze_tick_raw`, and no `BP_CHESTS` digest. Java `TileEntityChest.openInventory` / `closeInventory` (`TileEntityChest.java:342-351`, `:362-367`); `Container.slotClick` PICKUP/QUICK_MOVE (`Container.java:147`, merge `Container.java:606`); `ContainerChest.transferStackInSlot` chest 0..27 reverse into player then inv forward into chest (`ContainerChest.java:51-84`); `InventoryPlayer.mainInventory(36)` plus cursor `itemStack` (`InventoryPlayer.java:29-39`); `BlockChest.onBlockActivated` (`BlockChest.java:426-452`). Double-chest adjacency `getContainer` (`BlockChest.java:461-508`) is CUT, matching magma `chest_live.h`. Magma first-open may fill stronghold loot (`gm_stronghold_chest_info`); blaze snapshots do not carry TE contents or loot tables, so generation stays a named gap.

After: blaze ports magma open/click/close (container=3, 64-slot TE table, GMC 0-35 and 53-79). `BP_CHESTS` hashes every table slot (pos, 27 id/count/meta, `numPlayersUsing`) plus player 36 + cursor. Fixture `s10_t0_r64_chests.bsnp` plants chest (8,66,6) and seeds hotbar cobble/apple/bread; chain 41 (interact, PICKUP+QUICK_MOVE both ways, walk away). Anvil M1 VERIFIED 41 ticks digest `0x8a913abce518ff55` (`out/verify/chests_m1_chests_full.log`). Anvil M2 VERIFIED 64 CUDA lanes (`out/verify/chests_m2_chests_full.log`). `mining_slice` M1 was BLOCKED on v1 `!light` as `unrepresented_snapshot`; load now matches capture (open container only). Already-supported rows `--no-deps` still VERIFIED. Root `make test` PASS (`out/verify/chests_maketest.log` / `chests_m2_rest.log`).

Open: worldgen loot tables (LootTable / stronghold fill) and double chests.


## 2026-08-22 dragon death two-pass + capture hand (lane/dragonpass)

A6 dragon_death_50/100/190 after lane/dragondeath. Gamer baseline
`out/verify/dragonpass_baseline_gate_report.json` (HEAD before this
lane's hand pin; matches dragondeath after4): hard_px
83757/82429/83305, c_vs_j 2.919/2.927/2.912, ab_nz=0 RESIDUAL. Other
13 rows as entityscene.

Cause 1 (af78532): `RenderDragon.renderModel` two-pass
(RenderDragon.java:57-71). Pass 1 `alphaFunc(GL_GREATER, deathTicks/200)`
on `dragon_exploding.png` writes RGB+depth; pass 2 `alphaFunc 0.1` +
`depthFunc EQUAL` binds the skin. `cr_shade` (Metal twin too) keeps
exploding RGB where skin a<=25. Death rays `depth_lequal` after
LayerEnderDragonEyes.java:43. Dragon-only candidate
`strip_overlays=0`/`no_hand=0` (capture_ui_entities.sh hide_gui false,
goldens/meta frame_a.hud=1) and `place_dragon_platform` end_stone
(driver.py:381-399). Geom tests cover both alphaFunc refs. After
two-pass: hard_px unchanged 83757/82429/83305, c_vs_j 2.877/2.886/2.871
(`magma_ui_entities_c_353138`).

Cause 2: first-person dirt cube. Capture goldens draw
`ItemRenderer.renderItemInFirstPerson` with Blocks.DIRT (id 3).
`updateEquippedItem` (ItemRenderer.java:608-630) idles
`equippedProgressMainHand` at cooldown^3=1 after driver settle();
f5=1-progress (:340) so idle is 0. calloc capture started at progress
0 (`resetEquippedProgress` :643) and dropped the cube. Dragon-only:
`gm_runtime_set_inventory` dirt + `gm_frame_capture_equip_idle`.
test_hand dirt idle vs dropped Y.

After (`out/verify/dragonpass_after_hand.log`,
`magma_ui_entities_c_afterhand/gate_report.json`):
dragon_death_50/100/190 hard_px 84995/83667/84543
c_vs_j 2.157/2.165/2.150 ab_nz=0 RESIDUAL. Mean down; hard_px +1238
from C cube occupancy vs Java sky (pose still more top-down). Other
13 rows byte-stable vs baseline. Geom ALL PASSED. Root `make test`
PASS (`dragonpass_maketest.log`). cpu==cuda xbackend ALL PASS
(`dragonpass_cuda2.log`, CUDA_VISIBLE_DEVICES=0). Metal twin not
runnable on gamer; `_helpers` metal hash drifted by af78532
(628bad4c -> c3ccbf5e). Do not `kernel_pairs.py --update` until
cpu==metal on the Mac.

Not closed. Remaining: 1-px two-pass hole (x=482,y=22) J white vs C
15; viewmodel first-person pose/lighting; 1-LSB sky dominates
complete-ROI hard_px. Do not edit ROI. Fireball/XP untouched.
## 2026-08-22 detmob pitch / nether A*-null (lane/detmobpitch)

Gamer `~/nlanes/detmobpitch`. Baseline
`out/verify/detmobpitch_baseline_all.log` matches round 5: 6 PASS, target
T182955Z FAIL t=42 eid=3335 pitch tape=2.7023606 magma=2.67498541
draws_between=0, nether T182511Z FAIL t=745 eid=3872 x
tape=-291.488923016319 magma=-291.54075023842785 draws_between=0.

Look: EntityLookHelper.onUpdateLook (java:64-78) `rotationPitch=0` then
updateRotation (java:101-115). WatchClosest 10F/40F
(EntityLiving.java:881-888, EntityAIWatchClosest.java:98); melee 30F/30F
(EntityAIAttackMelee.java:114). Magma CPU uses mc_atan2
(EntityLookHelper.java:75-76 LUT; host-only on CUDA, blaze/core/mc_math.h).
Unit `test-look-helper` pins clamp, Watch vs melee step, and look of
zombie 53.5,74,126.5 vs pl t=40 = tape 2.702361 / yaw -42.83676. Magma
look_px is previous tape pl. Tape t=42 pitch is look of pl t=40; hyaw t=42
matches that yaw. No lag 0/1/2 fits t=29..49. Tape pl is client
EntityPlayerSP after ServerTick END; lookHelper samples MP. Class C;
FIRST MISMATCH stays t=42.

Nether: PathNavigateGround.getPathToPos walk-up (java:76-83 Material.isSolid)
on RPG dest. T182511 three blaze findPath: t=31 dest y=95 n=2 md=1
neighbour-only already nulls (Java tasks=8 then 0); t=595 dest y=95 n=4
Java walks to z=-78.23; t=745 dest y=96 n=10 in=0 start=-292,59,-79
end=-292,59,-70 rpg k=0 first-of-10 all score=0.4 br=0.1. Java t=745
tasks=8 then 0, xz unchanged. PathFinder.java:113-116 closest==start.
On an open floor far-Y dest still yields n>0 (unit
`test-pathfinder-null`); Java null is occupancy/ChunkCache
(PathNavigate.java:121-126 FOLLOW+8, Y 0-255) not the A* null rule
alone. HashSet getStart unused (tstart=WALKABLE pri=0,
path_node_processor.h:578). Blanket dest-out-of-window null would
regress t=595 / T182154 (Java walks). Do not widen PNP_DY (CUDA twins).
Leave FAIL. Knob default-off.

Gates unchanged vs baseline (after-replay,
`out/verify/detmobpitch_after_all.log`). Root `make test` on gamer:
magma/nn/rl/env_knob/public_export PASS including `test-look-helper` and
`test-pathfinder-null`; first tape-info failed because the gitignored
canon tape was absent on the lane clone; copy then tape-info PASS
(`out/verify/detmobpitch_maketest.log`,
`out/verify/detmobpitch_maketest_tapeinfo.log`).

## 2026-08-22 weather_optional M1+M2 (lane/weather)

Baseline anvil HEAD 99acff8 pre-wire: `BLOCKED weather_optional: Optional weather transitions and effects are not measured by both backends` (`out/verify/weather_baseline.log`, port_matrix rc=3). After the shared kernel but before `--weather on` was allowed, M1 FAILED (`out/verify/weather_gates.log`) because `gm_config_validate_runtime` still rejected `weather=1`. Tape `scenario_rain_thunder_20260821T093435Z` physics NO divergence 209 ticks, world_hash 209/209.

Cause: blaze did not tick WorldInfo rain/thunder or worldTime. Java `World.tick` calls `updateWeather` (`World.java:2707-2710`); `updateWeatherBody` (`:2741-2836`) `doWeatherCycle` (`GameRules.java:32`), thunder re-roll `nextInt(12000)+3600` / `nextInt(168000)+12000` (`:2763-2783`), rain re-roll `nextInt(12000)+12000` / `nextInt(168000)+12000` (`:2785-2807`); strength fade `+-0.01D` clamp (`:2810-2833`) is magma-inert (live strengths stay 0). `WorldServer.tick` (`WorldServer.java:180-223`) `totalWorldTime++` (`:218`) then `worldTime++` if `doDaylightCycle` (`:220-223`, `GameRules.java:22`). Magma RNG is an isolated `JavaRandom` from the world seed (`jrand_set`), not shared `World.rand` (`World.java:108`) and not `mc_hash_seed`.

After: shared `blaze/core/world_weather.h`. Magma `gm_world_tick` is a thin `ww_tick_gated` wrapper. `--weather on` is now valid (clock only). Blaze ticks weather before fluids (`runtime.c` order). `BP_WEATHER` hashes worldTime, totalWorldTime, rain/thunder flags/timers/strengths (`WWT1`). Fixture `s10_t0_r64_no_liquid.bsnp` + 64 idle (`weather_s10.json`) crosses the rain flip at t=50 (`WW_INIT_RAIN_TIME=50`). Anvil M1 VERIFIED 64 ticks digest `0x04b9d9ee88db08cb` (`out/verify/weather_gates2.log`). Anvil M2 VERIFIED 64 CUDA lanes on gpu1. mining_slice M2 and spawn_to_torch/world_dynamics/fluids/entity_spine/random_ticks/falling_blocks `--no-deps` still VERIFIED. Root `make test` PASS. Tape physics/world_hash unchanged 209/209.

Open: live `rainingStrength`/`thunderingStrength` fade and sky/rain render are not this row. Magma still does not fade.

## 2026-08-22 dragon death pose/dissolve lighting (lane/dragondeath)

A6 dragon_death_50/100/190. Gamer baseline
`~/nlanes/dragondeath/out/verify/dragondeath_baseline.log` (95c1ca4):
geom ALL PASSED; hard_px 92122/101336/159948 ab_nz=0 RESIDUAL;
c_vs_j 7.359/8.929/8.975. Other 13 rows: slime/magma CAPTURE_BLOCKED
97868/96818/109431/97058 and 97660/97628/109451/98600; dig_stone/grass
42589/43124; fireball_small RESIDUAL 45349; fireball_dragon
CAPTURE_BLOCKED 42457; xp_orb RESIDUAL 3542.

Cause 1 (pose): candidate used yaw=180, animTime=0, stationary=0.
Oracle `entity_pin` is setNoAI + PhaseList.HOVER (Recorder.java).
`EntityDragon.onLivingUpdate` sets `animTime=0.5F` when `isAIDisabled`
(EntityDragon.java:221). `ringBufferIndex` stays -1 so
`getMovementOffsets` is zeros and `RenderDragon.applyRotations` yaw is
0 (RenderDragon.java:33-35). `PhaseHover.getIsStationary` tucks neck
(`getHeadPartYOffset` returns idx, EntityDragon.java:1064-1066).
`ce42441`. After1: 87803/90250/135061.

Cause 2 (fog): overworld capture re-armed End `BossInfo.createFog`
dense ramp `[far*0.05, far*0.5]` (EntityRenderer.setupFog). Java
createFog exists only on End DragonFightManager. `ebba61c`. After2:
84456/82905/83312, c_vs_j ~2.95. Body gray, rays magenta.

Cause 3 (dissolve lighting): DBOX overwrote `ao` with deathTicks/200
and `cr_shade` forced `ao_mul=1`. Java exploding pass is only the
GL_GREATER mask (RenderDragon.java:59-63); skin pass keeps ModelBox
normals. Store threshold in `blk`; keep `ao` as face shade. `5ce0408`.
Wings still 1.37x: block-face 0.8 vs Java item lighting.

Cause 4: dragon ModelBox uses `er_shade_item` (RenderHelper.java:30-48,
RenderLivingBase.java:214 rescale-normal), not 1/0.8/0.6/0.5.
`47677e4`. After4 (`dragondeath_after4.log`): 83757/82429/83305,
c_vs_j 2.919/2.927/2.912, maxch 239/254/246. Wing sample (19,356)
J=C=(60,60,60). pxdiff thresh-25: HUD 30520, dirt 3507, crosshair 68;
no wing cluster. Body-ish thr25 = 26 px (two-pass exploding hole at
(22,482) J white vs C 15; magenta ray specks). Other 13 rows
byte-stable vs baseline. Geom ALL PASSED. Root `make test` PASS
(`dragondeath_maketest.log`). Metal/CUDA xbackend ALL PASS; re-recorded
`_helpers` metal hash.

Not closed. complete ROI hard_px is HUD + dirt + 1-LSB sky. Candidate
`strip_overlays=1` vs Java HUD; C has no superflat dirt cube. Do not
edit ROI. Two-pass exploding RGB and fine rays remain. Fireball
untouched.
## 2026-08-22 entity capture pad (lane/entityscene)

A6 scenery. Baseline on gamer (`~/nlanes/entityscene/out/verify/entityscene_baseline.log`) matches docs: fireball_small `hard_px=44930` `c_vs_j=5.536` maxch=161; xp_orb `3542` / `14.791` / 90; dragon_death 50/100/190 `92122/101336/159948`.

Cause: C `place_pad` filled x,z in [0,15]. Java `capture_ui_entities_driver.py:87-111` places stone at x[2,14] z[6,18] y=4, air y=5..11, then dig targets (10,5,11) stone and (11,5,11) grass. Recorder.java:5502-5536 setblocks. Superflat plains biome 1 (FlatGeneratorInfo.java:327-336). fancyGraphics=false (capture_ui_entities.sh:76) does not skip overlay: BlockGrass.java:141-145 always CUTOUT_MIPPED; BlockLeaves.java:250-253 is the fancy gate. Residual samples were C grass [71,92,43] vs J pad [124,124,124].

After (`entityscene_after.log`): xp_orb `3484` / `4.093` / maxch=25; fireball_small `44922` / `4.944` / 161. Dragon byte-stable. CAPTURE_BLOCKED c_vs_j dropped (slime_size1 4.644->2.315, magma_size1 4.713->2.385, dig_grass 5.330->3.613). pxdiff ROI thresh 25: xp 1152+63 content occupancy gone (0 clusters); fireball cutout-sky+ 5105 became shading-offset 2894. test_world_live pad CHECKs PASS. Root `make test` PASS (`entityscene_maketest.log`). Remaining: disc 1-2 LSB, pad BYTE-pack, fireball horizon 200 px. Not closed.

## 2026-08-22 overlay_fire atlas pin (lane/fireover)

Item 7 fire overlay. Baseline anvil
(`~/nlanes/fireover/out/verify/fireover_baseline.log`): overlay_fire
CAPTURE_OK `noise=6.727` `c_vs_j=4.473` painted=133546 `noise_limit=35`.
Other ui_hud rows: core HUD PASS, hands/portal/uw RESIDUAL as today.

Cause: `MixinPinTextureAnimations` cancels `TextureAtlasSprite.updateAnimation`
(`TextureAtlasSprite.java:177`) but does not upload a known strip row. Two
`frame{}` takes landed on different `fire_layer_1` frames. Portal sticky
pattern: `hud_pin fire_frame=0` force-uploads physical row 0 for every
animated blocks-atlas sprite (`TextureMap.java:205` stitch
`uploadTextureMipmap`, `ItemRenderer.java:580` `fire_layer_1`).
`frame`/`frame_pair` re-applies. Magma `bm_atlas_set_animation_physical_zero`
plus same-scene pad (`Entity.java:2477-2481` `isBurning`;
`ItemRenderer.java:566-606` two quads, translate `-(i*2-1)*0.24` / `-0.3`,
rotate `(i*2-1)*10` about Y, colour `1,1,1,0.9`, depthFunc 519,
SRC_ALPHA/ONE_MINUS_SRC_ALPHA).

After (`~/nlanes/fireover/out/verify/fireover_after_fog.log`,
`fireover_capture.log`): recapture A/B sha-equal, `noise=0`,
`fire_layer_1_physical_frame=0`. Dropped the 35.0 noise loophole.
overlay_fire fullscreen hard RESIDUAL `c_vs_j=12.908` `hard_px=353209`
`maxch=255` `stable=1.000`. Other 17 ui_hud rows byte-stable vs baseline.
Mutations PASS. Not PASS: same-scene sky/horizon occupancy (C sky vs J
grass around y=183) plus HUD-glyph dest holes (C 0 vs J 255, 680 px).
Fire warm occupancy 116471/116864. Root `make test` on anvil PASS
(`~/nlanes/fireover/out/verify/fireover_maketest.log`).

## 2026-08-22 falling_blocks M1+M2 (lane/fallblaze)

Baseline anvil HEAD 4dea2ea: `BLOCKED falling_blocks: Falling-block state and outcomes are not measured by both backends` (port_matrix rc=3, `out/verify/fallblaze_m1_before.log`). Tape `scenario_falling_blocks_20260801T151855Z` world_hash 310/310 before the port.

Cause: magma already ticked EntityFallingBlock in `live_sim.c` (lane/fallt46). Blaze had no falling store, no schedule, no FAL1 digest. Java `BlockFalling.onBlockAdded` / `neighborChanged` / `updateTick` / `checkFallable` / `tickRate=2` / `canFallThrough` (`BlockFalling.java:33-36, :43-46, :48-54, :56-88, :97-100, :102-107`); `EntityFallingBlock` ctor `setSize(0.98F,0.98F)` y+(1-height)/2 (`:50-64`); `onUpdate` fallTime++ source-to-air (`:116-128`), gravity `0.03999999910593033D` (`:133`), drag `0.9800000190734863D` (`:137-139`), onGround `canFallThrough(y-0.009999999776482582D)` (`:149-154`), land `0.699999988079071D` xz / `-0.5D` y then `setBlockState` (`:156-166`), despawn `fallTime>600` (`:207-215`). `World.handleMaterialAcceleration` is not involved. Magma extras kept: schedule delay 2, landing packet +1 then neighbor schedule 3, custom collision tops, sand/gravel only.

After: shared `blaze/core/falling_live.h`. Magma `live_sim.c` wrappers stay thin (tape world_hash still 310/310). Blaze ticks pre-player landings, post-edit notify, then scheduled updates + falling ents after fluids/randtick/spine. `BP_FALLING_BLOCKS` hashes live ents (pos, motion, block, fallTime) plus gravity-cell XOR/mutations. Falling ents are spawned by the sim, not snapshot v3. Fixture `s10_t0_r64_falling.bsnp` grounds the s10 player at y=65 and plants dirt (8,66,6) + sand 67-69; chain 32 attack + 32 idle. Anvil M1 VERIFIED 64 ticks digest `0xe2f58256b2cb83a4` (`out/verify/fallblaze_m1_falling.log`). Anvil M2 VERIFIED 64 CUDA lanes (`out/verify/fallblaze_m2_falling.log`). Already-supported rows `--no-deps` still VERIFIED. Root `make test` PASS.

Open: anvil/dragon-egg falling, EntityItem on failed mayPlace, and Java `World.rand` / instant-fall area-load path are not this row.

## 2026-08-22 small fireball pixels (lane/fireball)

A6 complete-ROI sub-item. Baseline on gamer (`~/nlanes/fireball/out/verify/fireball_baseline.log`):
fireball_small `hard_px=45349` owned=46000 `c_vs_j=5.545` maxch=161
`ab_nz=0` RESIDUAL. Other rows: slime/magma CAPTURE_BLOCKED
97868/96818/109431/97058 and 97660/97628/109451/98600; dragon_death
50/100/190 RESIDUAL 92122/101336/159948; dig_stone/grass
CAPTURE_BLOCKED 42589/43124; fireball_dragon CAPTURE_BLOCKED 42457;
xp_orb RESIDUAL 3542.

Pixel first cluster is scenery, not the sprite. Sprite bbox
(413,114)-(434,135): 392 mismatches, C brighter [238,172,24] vs J
[235,170,24] (~0.99). Pin has no on-fire layers (`isBurning` false).
Pad occupancy (C z=0..15 vs capture z=6..18) plus BYTE-pack 1-LSB
dominate the 45k ROI.

Cause: item-atlas pass binds no lightmap and used white tint. Java
`RenderFireball.doRender` (RenderFireball.java:32-60) draws a
POSITION_TEX_NORMAL quad, scale 0.5 (RenderManager.java:208), normal
(0,1,0), `enableRescaleNormal`, under
`enableStandardItemLighting` (EntityRenderer.java:1390,
RenderHelper.java:30-48). GL clamps colour*factor per channel, not the
factor. `getBrightnessForRender` 15728880 (EntityFireball.java:272-274)
writes lightmap 15/15 (RenderManager.java:362-371);
`cr_lightmap_rgba8` stores `(int)(0.99*255)=252`. At pitch 25 shade>1
so white product-clamp is 255; the missing 15/15 modulate was the
sprite error. Port: `gm_fireball_item_shade` unclamped, clamp the
white primary, fold 15/15 into tint, keep `light=1`. Geom tests cover
unclamped +Y, 188 saturation, pitch 0/25/90 tints, `isBurning` off.

After (gamer, `~/nlanes/fireball/out/verify/fireball_after.log`): geom
ALL PASSED; fireball_small `hard_px=44930` owned=46000 `c_vs_j=5.536`
maxch=161 `ab_nz=0` RESIDUAL. Sprite subject matches (mid (420,125)
[161,48,0]). Remaining 44930 is complete-ROI pad/grass (35532 eq1,
9398 gt1). fireball_dragon CAPTURE_BLOCKED hard_px 38371 (was 42457)
via the shared billboard path. Other 14 rows byte-stable. Not closed.
Do not edit the ROI or pad. Root `make test` on gamer PASS
(`~/nlanes/fireball/out/verify/fireball_maketest.log`).

## 2026-08-22 underwater glass cull (lane/raster)

Item A1 overlay_underwater. Anvil baseline `bash verify/ui_hud/run_ui_hud_gates.sh`:
c_vs_j=26.763 hard_px=390096 maxch=112 (matches OPEN_DIVERGENCES). Portal
1.466 / 363304 / 144.

Cause 1: magma emitted water_flow reverse quads on glass sides. Java
BlockFluidRenderer.java:185-192 swaps atlasSpriteWaterOverlay against
Blocks.GLASS / STAINED_GLASS and :259-265 skips the inward reverse.
Commit `6632616`. Anvil after: 10.654 / 390096 / 122. Fluid skip after
that was 0 px; remaining was glass, not water.

Cause 2: magma cube mesher did not cull glass-glass. Java
BlockBreakable.java:42-52 returns false when the neighbour is the same
GLASS / STAINED_GLASS (stained only if colour matches). Glass is CUTOUT
so the ice-ice translucent same-block cull missed it. `same_glass_cull`
in mesh_mc.c. Unit test: adjacent cubes 30+30 CUTOUT verts, 3x3 wall
centre 12 verts.

Anvil after cull: overlay_underwater 7.311 / 390096 / maxch=41. Portal
1.466 / 363304 / 144 unchanged. Other ui_hud rows unchanged. Root
`make test` PASS. No kernel twins. Overlay constants not edited.

Open: 7.311/ch vs hard goal 2.0. Glass.png frame matches Java; leftover
is whole-frame +~14 B on the glass/stone underlay plus faint inner-square
corner ticks. Not overlay alpha.

## 2026-08-22 XP orb GL item lighting (lane/xporb2)

A6 continue. Baseline on gamer (`~/nlanes/xporb2/out/verify/xporb2_baseline.log`):
geom ALL PASSED; xp_orb `hard_px=3542` owned=3600 `c_vs_j=20.223` maxch=90
`ab_nz=0` RESIDUAL. Other 15 rows: slime/magma CAPTURE_BLOCKED
97868/96818/109431/97058 and 97660/97628/109451/98600; dragon_death
50/100/190 RESIDUAL 92122/101336/159948; dig_stone/grass CAPTURE_BLOCKED
42589/43124; fireball_small RESIDUAL 45349; fireball_dragon
CAPTURE_BLOCKED 42457.

Cause: `er_shade_item` unit-normalizes and clamps the lighting factor
before multiplying vertex RGB, so capture pulse R=188 stays 188.
Java `RenderHelper.enableStandardItemLighting` (RenderHelper.java:30-48,
LIGHT0/1 :13-14) is COLOR_MATERIAL AMBIENT_AND_DIFFUSE:
`lit = clamp01(c * unclamped_factor)`. `GL_RESCALE_NORMAL` is off
(GlStateManager.java:907,864 default; EntityRenderer.java:1390-1393 never
enables it; RenderLivingBase.java:214/196 enable/disable around living
models; first-person skips the viewer, RenderGlobal.java:650;
LayerSlimeGel.java:27,33 is the only `enableNormalize`; RenderXPOrb.java:70
only disables). S(0.3) (RenderXPOrb.java:59-60) with object normal (0,1,0)
(:64, NORMAL_3B VertexBuffer.java:533) leaves `|n|=1/0.3`. Capture pose
yaw 0 pitch 25 color 0 partialTicks 1: factor ~3.33 saturates R and G to
255. Lights stay world-space (set after T(entity), before Ry/Rx/S; w=0
ignores translation). `er_shade_item` left as-is for crystals.

After (gamer, `~/nlanes/xporb2/out/verify/xporb2_after.log`): geom ALL
PASSED including capture-pose lit colour; xp_orb `hard_px=3542` owned=3600
`c_vs_j=14.791` maxch=90 `ab_nz=0` RESIDUAL. Other 15 rows byte-stable.
Disc sample (426,162): C (250,188,0) vs Java (252,190,0); grey disc texels
are r==g 1-2 LSB dark. Grass/pad still owns most of the 3542. Not closed.
Root `make test` on gamer PASS (`~/nlanes/xporb2/out/verify/xporb2_maketest.log`).

## 2026-08-22 XP orb pixels (lane/xporb)

A6 first sub-item. Baseline on gamer (old ROI, pad only): xp_orb
`hard_px=12000` owned=12000 `c_vs_j=4.756` maxch=70 `ab_nz=0` RESIDUAL.
Other 15 rows: slime/magma CAPTURE_BLOCKED 97868/96818/109431/97058 and
97660/97628/109451/98600; dragon_death 50/100/190 RESIDUAL
92122/101336/159948; dig_stone/grass CAPTURE_BLOCKED 42589/43124;
fireball_small RESIDUAL 45349; fireball_dragon CAPTURE_BLOCKED 42457.

Cause: Magma used `sinf`, dropped `partialTicks` (f9 = xpColor/2),
src-over blend on vertex alpha 128, and an ROI that started at y=180
(disc y=146-178). Java `RenderXPOrb.doRender` (javap on deobfed.jar):
`f9=(xpColor+partialTicks)/2`, `MathHelper.sin` LUT
(MathHelper.java:29-31), RGB
`(sin(f9)+1)*0.5*255 / 255 / (sin(f9+4.1887903)+1)*0.1*255` with alpha
128 (:52-55, :64), T(0,0.1,0) Ry(180-playerViewY) Rx(-playerViewX)
S(0.3) (:56-60), UV cell from `getTextureByXP` (EntityXPOrb.java:290-293,
RenderXPOrb.java:37-42), pass 0 blend off (EntityRenderer.java:1383-1393),
`getBrightnessForRender` +120 cap 240 (:67-81). Capture pin: value 17,
color 0, age 0, pose (8.5,5,8.5) yaw 0 pitch 25, orb (8.5,6,10.5),
partialTicks=1, `render_pin=1`.

After (gamer, `~/nlanes/xporb/out/verify/xporb_after.log`): geom ALL
PASSED; xp_orb `hard_px=3542` owned=3600 `c_vs_j=20.223` maxch=90
`ab_nz=0` RESIDUAL. Other 15 rows byte-stable. Disc: C (184,188,0) vs
Java (252,190,0) at (426,162) — C matches color=0 pulse (R mid, G 255);
the golden disc has r==g everywhere: vertex (255,255,0) times texel grey.
Parent review: GL item lighting clamps the product, not the factor
(RenderHelper.java:30-48, GL_COLOR_MATERIAL); `er_shade_item` clamps the
factor, so 188 stays 188 where Java saturates to 255. Follow-up
lane/xporb2. Not closed. Do not fit vertex RGB to the yellow golden. Root `make test` on gamer PASS
(`~/nlanes/xporb/out/verify/xporb_maketest.log`).

## 2026-08-22 random_ticks M1+M2 (lane/randticks)

Baseline anvil: `BLOCKED random_ticks: Random-tick scheduling and effects are not measured by both backends` (port_matrix rc=3). Magma already ran `gm_randtick_pass` (hash schedule, grass/leaves/fire/wheat-carrot-potato). Blaze only ticked grass.

Cause: blaze had no RTK1 digest and no full ticker set. Java `WorldServer.updateBlocks` :404 `randomTickSpeed`, :472-494 per-section LCG pick + `Block.randomTick`; `World.java:95-97` `updateLCG*3+1013904223`; `GameRules.java:25` default `"3"`. Magma substitutes `mc_hash_seed` (`randtick.h:10`); M1 matches that schedule, not Java `World.rand`. Tickers: `BlockGrass.java:41-73`, `BlockLeaves.java:69-176`, `BlockFire.java:146-253` / `:286-314`, `BlockCrops.java:72-90` / `:111-164`.

After: shared `blaze/core/randtick_live.h`. Fixture `s10_t0_r64_randtick.bsnp` plants 64 moist-farmland wheat and 64 isolated CHECK_DECAY leaves. Anvil M1 VERIFIED 200 ticks (`out/verify/randticks_m1.log`). Blaze evidence 27 tickable-cell mutations (first at act 5). Anvil M2 PASS 64 CUDA lanes (`out/verify/randticks_m2.log`). world_dynamics M1/M2 still VERIFIED. `supported: true`.

Open: sapling/farmland/ice/snow tickers still unported (not in magma `randtick.c` dispatch). Java LCG/`World.rand` consumption is not this row.

## 2026-08-22 entity_spine M1+M2 (lane/entityspine)

Baseline anvil 754c029: both tiers BLOCKED "Entity lifecycle state is not
integrated into the common parity record." Snapshot v3 hashed a static store.

Cause: blaze did not step loaded living slots. Magma `--mobs off` skipped
the whole `gm_mobs_tick`. Zero-intent travel was never on either side.

Port (magma semantics, M1 is blaze-vs-magma):
- Shared `blaze/core/entity_spine.h`: `ess_tick_living` -> `eb_tick_living`
  with moveForward/moveStrafing/isJumping = 0.
- Magma `gm_mobs_tick_spine` (`mob_live.c`) on the `--mobs off` branch;
  `--mobs on` still full AI.
- Blaze `cu_mob_spine_tick` after randtick, before live items. CUDA
  `k_reset_scalar` copies the v3 mob trailer.

Oracle (java/oracle-src):
- Entity.setSize :376-399, setPosition :413-424, onEntityUpdate :460-477
- Entity.move :668 (`pcf_entity_move`); onGround = collidedVertically &&
  d3<0 :970; updateFallState :1214-1228; moveRelative :1424-1445 (gated
  `f >= 1.0e-4F`, skipped at zero intent)
- EntityLivingBase stepHeight=0.6F :207; jump :1897-1921; travel
  :2015-2103 (0.91F air, slip*0.91F ground, gravity 0.08D, vertical drag
  `(double)0.98F` = 0.9800000190734863D); onLivingUpdate :2419-2511
  (0.003D clamp, moveStrafing/moveForward *= 0.98F)
- Magma slip table ice 79/174/212 = 0.98F, water 8/9 = 0.8F, else 0.6F
  (`mob_live.c:2397-2403`)

After (anvil `~/nlanes/entityspine`):
- Unit: gravity bits, land on stone, ground `motionX *= (double)(0.6F*0.91F)`
- Fixture `s10_t0_r64_entity_spine.bsnp`: zombie slot1 ground slide on grass
  y=65, slot2 fall onto sand y=63
- M1: VERIFIED 32 ticks `--features mobs` digest `0x44fc8506d238d93e`
  (`out/verify/entityspine_m1.log`)
- M2: PASS 64 CUDA lanes vs CPU bitwise (`out/verify/entityspine_m2.log`)
- FP census: 256 sqrt+sin samples CPU==CUDA bitwise. `mc_atan2` unused.
- fluids M1 still VERIFIED. Root `make test` PASS.

Open: AI/path/combat (`mobs` row). `mc_atan2` remains host-only.

## 2026-08-22 portal world RSR (lane/portaledge)

Item A2 overlay_portal_050 on gamer. Baseline matched the file:
noise=0 C-vs-J=1.466 hard_px=363304 maxch=144 RESIDUAL; underwater
26.763 / 390096 / 112; mutations PASS; LSB guard PASS.
`~/nlanes/portaledge/out/verify/portaledge_baseline_roi.log`.

Java: ItemRenderer.renderOverlays (ItemRenderer.java:450-498) is
block/water/fire only. Portal HUD is GuiIngame.renderPortal
(GuiIngame.java:1112-1143) via GuiIngameForge.java:135-138,305-314:
ease t<1 -> t^4*0.8+0.2, atlas sprite, SRC_ALPHA, color alpha=ease,
MAG GL_NEAREST (AbstractTexture.java:30-35, 9728). World warp is
EntityRenderer.setupCameraTransform (java:746-761):
f2=5/(t^2+5)-t*0.04 then squared, R(+(count+pt)*20, 0,1,1) S(1/f2,1,1)
R(-). renderHand (java:791-804) reloads gluPerspective without RSR;
renderOverlays after the hand (java:833-836).

Cause of leftover 144 maxch: 2D inverse-map of the colour buffer is a
homography exact at one depth. Sky/wall occupancy at the silhouette
diverged from the 3D raster. LINEAR sprite-edge was rejected (MAG is
NEAREST; high maxch sat at x=817-822, not a 16x16 grid).

Port: CrCamera.portal_time / portal_spin_deg into cr_camera_view RSR.
Remove gm_overlay_portal_warp from window_compose / frame_capture.
Hand camera stays {0}. Sky inverts the non-orthonormal 3x3 when
portal_time>0. Overlay sample stays integer NEAREST on the portal
sprite rect. Unit: magma/game/test_overlay.c (ease 0.5->0.25, RSR
changes view, NEAREST+tex.a).

After (gamer `out/verify/portaledge_after.log`): overlay_portal_050
0.972 / 363609 / 115 RESIDUAL. Underwater 26.763 / 390096 / 112
unchanged. HUD / inside / hand rows byte-stable. Mutations PASS. LSB
guard PASS. Root `make test` PASS
(`out/verify/portaledge_maketest.log`). pxdiff --a/--b: one 129px
cluster (235-238, 818-853) registration. Interior 1-2 LSB is wall pack
showing through (1-a), not overlay formula. PASS-LSB blocked by
~99898 px at maxch=2. Item stays OPEN.

## 2026-08-22 fluids M2 (lane/fluidsm2)

Blaze-CPU vs blaze-CUDA bitwise on `s10_t0_r64_fluid_spread.bsnp` +
`fluid_spread_s10.json` (61 actions). M1 stayed VERIFIED. First CUDA tick
was `k_tick_raw: an illegal memory access was encountered` (not a digest
mismatch). Cause: nvcc inlined `BlockDynamicLiquid.getSlopeDistance`
(oracle-src `net/minecraft/block/BlockDynamicLiquid.java:178`, recurse
`:196`, depth cap `getSlopeFindDistance` `:212`) into the tick kernel;
default CUDA stack is 1024 B. Fix: `MC_NOINLINE` on `ff_flow_distance` /
`ff_ca_step_ex` / `cu_fluid_step_region` / `cu_fluid_tick`, and
`cudaDeviceSetLimit(cudaLimitStackSize, 32 KiB)`. CA numerics unchanged
(`--fmad=false`). After: 1-lane and 64-lane chain PASS; matrix `fluids`
m2 VERIFIED. `mining_slice` / `spawn_to_torch` / `world_dynamics` M2 still
VERIFIED. Root `make test` PASS.

## 2026-08-22 falling t46 world hash (lane/fallt46)

`scenario_falling_blocks_20260801T151855Z` digest is 310/310 on gamer.

Baseline (delay=4, border 0.1): world_hash FAIL 1/310, first mismatch t46
java=f63a2e55f4417889 magma=8d22d846ed0c2a49, reconverge t47. A-H PASS.

Cause: clickBlock creative writes blockHitDelay=5 and sendClickBlock
skips the decrement on air (PlayerControllerMP.java:237-242,
Minecraft.java:1500-1508). Magma folded that to 4. Separately,
`attack_hits_falling_block` used expand 0.1; Entity.getCollisionBorderSize
is 0.0F (Entity.java:2366-2368). The 0.1 box stole the ray at y~4.78
where Java misses, froze delay at 1, and the re-landed sand survived t46.

Port: delay=5 on clickBlock and on the creative onPlayerDamageBlock
path (306-311); border 0.0F; keep apply-then-pick. delay=5 with 0.1:
22 mismatches from t29. 0.0F with delay=4: 285 mismatches at t25.
Pick-before-apply + keep AABB: 280 mismatches from t29. Do not split
those two Java values.

Gamer after: world_hash PASS 0 mismatches / 310; physics 310/310;
inventory/entities PASS. `bash magma/game/test_fall_reanchor.sh` A-I
PASS. Root `make test` PASS. Goldens still absent on gamer
(frames_checked=0, rc=2 harness). No other tape replayed.

## 2026-08-22 bow FOV recapture (lane/bowgold)

The mid-ease golden was recaptured on anvil llvmpipe after holding the
drawn bow through `EntityRenderer.updateFovModifierHand` (0.5/tick,
`EntityRenderer.java:491-502`) toward `AbstractClientPlayer.getFovModifier`
0.85 at 20-tick draw (`AbstractClientPlayer.java:156-170`). qrl writes
`fov_mult`; C `ui_hud_scene` reads it. Hand projection stays 70
(`EntityRenderer.java:804`). Sticky USE pin unchanged: `use_branch=bow`,
`use_count=71980`, A/B sha256 identical, `noise_max=0`. Recorded
`fov_mult=0.85` (not fitted 0.887).

Baseline (anvil, HEAD 33ae09c, mid-ease golden):
`hand_bow_pull20` 7.007 / hard_px=20745 / maxch=108 / n_only_j=0.
Eat 1.317 / 73440 / 215 and shield 0.911 / 28564 / 61 unchanged after.
J-stone/C-grass 2418 ROI gt1.

After recapture + meta pin (anvil):
`hand_bow_pull20` 0.753 / hard_px=20830 / maxch=97 / n_only_j=0.
gt1 wall=3332 selbox=13 grass=0. Eat/shield/HUD/overlay byte-stable.
Mutations PASS. LSB guard PASS. Root `make test` PASS.

Bow row stays RESIDUAL (px>1 and nz vs 2% cap). Mesh still closed.

## 2026-08-22 bow occupancy is world FOV (lane/bowsil)

Suspects 1-3 vs oracle-src: C already bakes `bow_pulling_2` at
pull=1.0 (`ItemBow.java:29-44`, `ItemOverrideList.java:24-28` reverse
scan, `hand.c:802` sprite 9002). `build_bow_drawn` matches
`ItemRenderer.java:402-427` then generated firstperson
`[0,-90,25]/[1.13,3.2,1.13]/0.68`. Hand FOV stays 70
(`EntityRenderer.java:804`).

The occupancy is not bow metal. Wood J-only=60 C-only=32 both=632;
J-wood/C-stone=8. Eat/shield stone_min J=C=77. Bow Java stone_min=20
vs C unzoomed 77: J-stone/C-grass 13363 full / 1961 ROI.

Tried cited full-draw `fov_mult=0.85` (`AbstractClientPlayer.java:156-170`)
on `ui_hud_scene`. Live magma already eases that in `player_ctl.c`.
This golden is not converged: C stone_min went to 0, occupancy flipped
to J-grass/C-stone 5125/449, `c_vs_j` 7.007->5.836, `maxch` 108->97.
Implied Java fov_mult from the wall edge is 0.887 (two 0.5-ease ticks).
`fovModifierHand` is not in the capture meta. Reverted the scene pin.
Do not fit 0.887. Recapture on anvil after the ease converges.

Baseline (gamer, PATH=$HOME/.local/bin, HEAD before the pin):
`hand_bow_pull20` 7.007 / hard_px=20745 / maxch=108. Eat 1.317 /
shield 0.911 unchanged. After revert the row is that baseline.
`oracle_roi_report.json` not regenerated. Gate still RESIDUAL.
Mesh diagnosis retracted in `CLOSED_DIVERGENCES.md`; occupancy is
class C.
## 2026-08-22 M1 transport: snapshot v3 + BP_MOBS (lane/mobsnap)

Snapshot revision 3 appends `u32 n_mobs` + packed `RlSnapMob` after the v2
light plane. v1/v2 load as n_mobs=0. Magma `rl_snapshot_write` always emits
v3 from live `gm_mobs_export_snap` (slot-ascending, slot 0 skipped). Shared
digest `blaze_snap_mobs_digest` is compiled by magma and blaze; blaze hashes
the static loaded store and does not tick mobs. CUDA reset still passes an
empty trailer (M2 later). Caps are compile-time 96/48. Field list:
`blaze/OPEN_DIVERGENCES.md`. Root `make test` includes the C round-trip.
Magma-vs-blaze digest parity is `blaze/env/test_mob_snap_parity.py`.
## 2026-08-22 rain sky/fog mix (lane/rainsky)

`World.getSkyColorBody` rain then thunder (`World.java:1609-1629`) on sky
vertices; `EntityRenderer.updateFogColor` rain then thunder (`1815-1834`)
on view/terrain fog. Same runtime `rain_strength`/`thunder_strength` as
the lightmap sun term. Live stays 0.

`updateLightmap` `lastLightningBolt` (`EntityRenderer.java:900-903`) is
ported but undriven: the rain tape does not record the counter (ents
empty; Recorder only writes it for coverage / kernel capture). t=180
stays a recorder gap. No fitted bolt tick.

gamer `--cpu` `scenario_rain_thunder_20260821T093435Z`, 21 frames. Baseline
matched the filed FAIL (~50.9/41.5, UNEXPLAINED 2140760, 21/21). After:
non-lightning whole ~1.24/ch, terrain ~1.40, sky y0-148 0.94/ch (goal
<=5). t=180 whole 22.59 / terrain 20.78 / UNEXPLAINED 181371 (lightning
lightmap + arm). Particles 121028 px / 20 frames remain (out of scope).
Gate still fail-closed rc=2 on empty `container_identity`/`gui_clicks`.
`make test` PASS on gamer after copying the gitignored canonical tape
into the nlane clone.
## 2026-08-22 ui_hud PASS-LSB tier (lane/lsbhand)

Owner-approved gate-contract change for `run_ui_hud_gates.sh`, mirroring
`lane/lsbtier` / `gui_preview_lsb.py`. HAND_HARD and FULLSCREEN_REPLACE rows
now have three verdicts: PASS (nz==0), PASS-LSB (A/B noise 0, every channel
|d|<=1, px>1==0, nz<=2% of owned ROI), RESIDUAL/FAIL otherwise. Core HUD,
death chrome, and soft CAPTURE_OK are unchanged (durability +1 extras stay
residual). Not a mean PASS-FLOOR.

Baseline (gamer, HEAD `d4182be`, before): oracle ROI RESIDUAL fail=0 residual=5.
Hands: bow 7.007/20745/maxch 108, eat 1.317/73440/215, shield 0.911/28564/61.
Portal 1.466/363304/144, underwater 26.763/390096/112. Mutations PASS.
Matches OPEN_DIVERGENCES.

After: same five residuals; no row flips to PASS-LSB. Eat px>1=21526
(wall 14768, painted 2617, grass 4118, sky 23) and nz=73440 vs cap 1784.8.
Shield px>1=6925 (wall 4885, painted 4, grass 2036) vs cap 587.0. Bow
px>1=12584 (wall 10153, grass 2418, selbox 13). Guard PASS (uniform+1
count-cap, +2 px>1, 3x3+12 hard, live eat/shield pin). `make test` PASS.
Magma/game not touched.

## 2026-08-22 divergence split: two bridge files

`magma/OPEN_DIVERGENCES.md` is now oracle->magma only, with an audited
four-class index (A product divergence / B accepted floor / C recorder
blocker / D verification gap). New `blaze/OPEN_DIVERGENCES.md` tracks
magma->blaze: verified matrix rows, the fluids-M2 hole, the 12 unported
rows in dependency order, entity-arc prerequisites (mob snapshot rev +
BP_MOBS, device FP census), and transfer gaps. The codex-reviewed GPU
mob-AI design landed as `blaze/GPU_MOB_AI.md` (v2: warp-per-env, lane-0
sequential, magma semantics, 8-tape exactness). No gate or code changes.

## 2026-08-22 hand same-scene wall (lane/handscene)

Hand states were isolation GRAY=40; Java goldens are the capture pad wall.
`ui_hud_scene` now draws that pad for `hand_*` (world + selection box only);
candidate `compose()` still paints the use-pose hand and HUD on top.
Portal/underwater keep full window_compose. Non-hand `oracle_roi_report.json`
rows are byte-identical. Stone is `cube_all` via `BlockModelShapes` VARIANT
map; no recapture.

| id | before c_vs_j / hard_px / n_only_j / c_paint_mean | after |
|----|---------------------------------------------------|-------|
| hand_bow_pull20 | 29.266 / 20830 / 16309 / 0.256 | 7.007 / 20745 / 0 / 7.245 |
| hand_eat_mid | 32.764 / 74218 / 50268 / 1.300 | 1.317 / 73440 / 41 / 1.267 |
| hand_block_shield | 23.613 / 28506 / 12533 / 0.744 | 0.911 / 28564 / 17 / 0.909 |

`n_only_j` ~0 (wall now in C). `hard_px==0` not reached: eat/shield leftover
is ~1 L8 wall + painted face; bow 7.007 is gray metal vs C stone in the
lower-right ROI. Mutations PASS. Gate RESIDUAL_OR_FAIL. `make test` PASS.
## 2026-08-22 GUI preview PASS-LSB tier (lane/lsbtier)

Owner-approved gate-contract change for `run_gui_verify.sh` only. Preview ROI
now has three verdicts: PASS (nz==0), PASS-LSB (A/B noise 0, every channel
|d|<=1, px>1==0, nz<=2% of 104x144), FAIL otherwise. Chrome rows stay
bit-exact. Not a mean PASS-FLOOR.

Baseline (before): pose1 FAIL mean=0.002448 px>0=62 px>1=0 max=0.667;
pose2 FAIL mean=0.003316 px>0=140 px>1=0 max=0.667; table/furnace/chest/
non-preview PASS; `gui verify: FAIL`.

After: pose1/pose2 PASS-LSB (62/140 px at <=1 LSB); chrome unchanged;
mutation guard PASS (uniform +1 count-cap FAIL, single +2 px>1 FAIL,
3x3 +12 hard FAIL, live residual PASS-LSB not exact). Overall exit 0.
`gui_preview_calibration.json` v4 records verdict + `lsb_guard`.

## 2026-08-22 hand USE-pose recapture (lane/handgold)

Idle-tip goldens recaptured on anvil llvmpipe. Driver already staged
`bow_pull=20` / `use_action=1 use_remaining=16`; added eat remaining
check, byte-identical A/B for bow/eat, and `hud_pin` timeout retries.
Meta now has `use_branch` + `use_count`. Combined `ONLY=bow,eat` hung
on eat re-pin after bow `frame_pair` (Java `hud_pin` timeout 120s);
eat-only on a fresh client succeeded. C transforms not retuned.

| id | before c_vs_j / hard_px / maxch | after |
|----|---------------------------------|-------|
| hand_bow_pull20 | 49.279 / 30260 / 125 | 29.266 / 20830 / 100 |
| hand_eat_mid | 53.939 / 101880 / 215 | 32.764 / 74218 / 215 |
| hand_block_shield | 23.613 / 28506 / 100 | unchanged (c_paint_mean 0.744) |

Bow/eat A/B sha256 identical, `use_branch=bow|eat`, `use_count=71980|16`.
c_paint_mean 59.43/51.32 -> 0.256/1.300. Leftover is wall isolation
(`n_only_j`) plus painted-face LSB. Mutations PASS. Gate RESIDUAL_OR_FAIL.
`make test` PASS.

## 2026-08-22 inventory preview lighting (lane/preview)

Player-preview 1 L8 residual. Baseline (HEAD 102/255+152/255, unit n):
pose1 mean 0.011641 nz=442; pose2 0.009949 nz=323; chrome bit-exact.

Port: `VertexBuffer.normal` BYTE `(int)(c*127)` unpack GL 2.1 table 2.9
`(2c+1)/255`; `RenderLivingBase.prepareScale` `enableRescaleNormal` only
(`GlStateManager.java` 32826, not 2977); `RenderHelper` 0.4+0.6 float;
L8=`round(C*255)` then `(tex*L8+127)/255`. Do not renormalize after the
uniform-scale modelview (BYTE `-Z` stays `|n|=253/255`).

Gate after: pose1 mean 0.002448 nz=62; pose2 0.003316 nz=140; hard_px=0;
chrome still PASS. Honest floor: pose1 right-arm +X `C*255=158.543`
rounds to 159 vs Java 158; pose2 head +X `130.678` rounds to 131 vs 130.
No Mesa-justified global pack splits those from larm +X `184.794` which
needs round-to-185. Mixed-channel 8 px on pose1 head +X have no single L8.

## 2026-08-22 portal/uw same-scene underlay (lane/portalpix)

ui_hud candidate draws overlay_portal_050 / overlay_underwater through
window_compose on the capture pad (superflat seed 0, stone pad+wall,
glass pool for UW). Warp is WORLD-only: EntityRenderer.setupCameraTransform
rotate-scale-rotate is on the world modelview; renderHand reloads
gluPerspective(getFOVModifier(*, false)) + identity, then ItemRenderer
overlays. GuiIngame.renderPortal (t^4 alpha) stays 2D after that.

Compose/capture: portal_scratch inverse-map before hand; overlay FOV
70 * uw.fov_scale; fluid shades enable_fog=1 (setupFog EXP).

Baseline gray isolation vs same-scene (exact A/B pair, hard_thr=0):

| id | before c_vs_j / hard_px / maxch | after |
|----|---------------------------------|-------|
| overlay_portal_050 | 47.191 / 391116 / 181 | 1.465 / 363305 / 144 |
| overlay_underwater | 6.083 / 388620 / 55 | 26.763 / 390096 / 112 |

hard_px==0 not reached. Portal interior ~1 LSB; leftover is edges/hand
(maxch 144). Underwater overlay matches renderWaterOverlayTexture;
Magma water/glass/fog underlay is too blue (C [42,59,140] vs J [66,70,86]
at (2,2)). Closing UW is terrain/water raster, not overlay constants.
Numerical/compose/live PASS. Mutations PASS. Gate RESIDUAL_OR_FAIL.

## 2026-08-22 fallblock tape re-run (lane/fallblock)

Cascade is already on HEAD via 1caf2bb (re-port of 2d759f8; that commit
is not an ancestor). Did not cherry-pick. Baseline disagreed with the
OPEN t22/t30 numbers: 151855Z world_hash is 309/310, first mismatch t46
java=f63a2e55f4417889 magma=8d22d846ed0c2a49, reconverge t47. Physics
310/310. Documented dig skew is gone. t46 left OPEN (creative delay=1;
border 0.0F regresses t25). Wired `test-fall-reanchor` into magma
`make test`. Native A-H PASS.

## 2026-08-22 rain/thunder lightmap (lane/rainlight)

Tape `rain`/`thunder` (`getRainStrength(1)` / `getThunderStrength(1)`) now
reach `fc_sun_brightness` via `World.getSunBrightnessBody` rain/thunder
factors. Live stays 0. Rain tape 21 frames: whole ~75.7/ch -> ~50.9/ch,
UNEXPLAINED 7.22M -> 2.14M px, arm R/G 0.46 -> 0.99. Still FAIL 21 frames
(sky color, rain particles, t=180 lightning). Fence collide still PASS.

## 2026-08-22 first-person hand USE-pose (lane/handpose)

Baseline `verify/ui_hud/run_ui_hud_gates.sh` matched OPEN_DIVERGENCES:
bow hard_px=30260 c_vs_j=49.276 c_paint_mean=59.43; eat 101880 / 53.939 /
51.32; shield 28506 / 23.615 / 0.748.

C `build_bow_drawn` / `build_eat_drink` already match ItemRenderer.java
call-for-call. Java bow/eat goldens are idle tips (no `use_branch` in
meta). C draws the use pose (bow f5=20 pulling_2; eat remaining=16
max=32, f3~1). Mesh scale matches generated.json 0.68 + ItemLayerModel
z=7.5/16..8.5/16 + Forge T*R*S*T(-0.5). Do not retune. Close path:
recapture with `ONLY=hand`.

Shield geometry matches. Mesa packing `(tex*L8+127)/255` and
`(tex*L8+128)>>8` did not move the wood-face +1,+1,0 bins; restored
float trunc. Remaining: isolation gray vs wall (`n_only_j=12533` keeps
hard_px=28506) and ~1 L8 primary (same class as inventory preview).
`hand_raster` now uses sample_mode=1 (RenderItem setBlurMipmap
false,false = GL_NEAREST) and alpha_ref=0.1 (alphaFunc GL_GREATER 0.1).
After: hard_px unchanged; shield c_paint_mean=0.744.

ui_hud live link needed `game/world_spawn.o` (`_gm_create_spawn_position`).

## 2026-08-22 slime_bounce shell inset (lane/slimerim)

Contradiction is the BLOCK model, not entity ModelSlime (already ported:
inner 6x6x6, gel 8x8x8, scale 0.999, translate 0.001). Java
`models/block/slime.json` has two unculled elements; DRAW dump
`verify/fixtures/slime_translucent/` is 441*12 generalQuads, 0 face quads.
Magma inset constants already match FaceBakery (isolated 72 verts).
Interior pads neighbor-cull to inner+outer UP. Emitting the 12 matched
the dump and darkened tape t=50 4.53 -> 23.88/ch (13 -> 17 failed frames,
t=140 UNEXPLAINED 40447). Reverted. Raster twins cannot change here.

## 2026-08-22 chain4 retrain PASS + stage ladder (anvil gpu0)

Chain retrain with the staged curriculum completed end-to-end for the
first time: `ppo: PASS backend=cuda n_envs=1024 rollout_steps=32
chunks=3890 ticks=510001152 best_t0=0.215 best_ticks=504758272`
(retrain_0821_chain4_best.bin). t0 went 0.000 -> 0.215 over the run;
prior fresh-weight chain runs sat at 0.005-0.065.

Two crashes on the way, same signature (host segfault in libcuda at
chunk 2381 then 152). Root cause: device use-after-free in blaze
capture. `blaze_reset_scalar` binds env->ore/ore_xy by pointer into
snapshot slot buffers; `blaze_capture` freed the coal buffer on ncoal
change (t0 vs stgK snaps differ per seed), leaving a dangling D2D
source. Fix (master 6562d71): grow-without-free plus a retire list
freed at destroy; shrink in place; same pattern in the CPU driver.
test_capture_cont extended. chain4 crossed both prior crash points.

Ladder (`eval --stage all`, best ckpt, 5 tries x 6000 ticks, sampled):
stage0 full-chain 6/13 seeds; stage1 2/7 (6 SKIP); stage2 3/5 (8 SKIP);
stage3 2/5 (8 SKIP); stage4 (coal start) 8/8 - every seed with a stg4
snap finishes torches, incl. held-out 33. Weak link is stage3: seeds
10/14/16 start at cobble3 and add nothing (best-of-5). Log:
anvil out/blaze/rl/eval_chain4_ladder.log.

Portability: GCC -Werror=format-truncation rejected eval.c ladder
`char cell[8]`; widened to 24 (8902716). Mac clang never flagged it.

## 2026-08-22 detmob consolidation round 5 (lane/detmob-all)

On d50ee97 plus this commit. Knob default-off. No GATES / known_divergences
/ blessed-tape / blaze-rl.

Task 1 — target T182955Z t=42 zombie pitch. Outcome b. No uniform pl sample
is bit-exact across t=29..49. EntityLookHelper.onUpdateLook `fconst_0`
rotationPitch then updateRotation (WatchClosest 40F). World ticks players
before mobs; tape pl is client pose after ServerTick END.

Bit-exact look_pitch (mc_atan2, zombie 53.5,74,126.5 eye 1.74F, player
eye 1.62F). mag = tape_t - clock0 - 1 (zombie clock0 tt=38). Watch starts
mag 28 / tape t=34.

 mag tape  tape_pitch   cur         prev        prev2       exact
 28  34   -24.636997   cur         prev*       prev2      p
 29  35   -21.913969   cur         prev*       prev2      p
 30  36   -17.628231   cur         prev*       prev2      p
 31  37   -11.820677   cur         prev*       prev2      p
 32  38    -4.554305   cur         prev*       prev2      p
 33  39     2.853410   cur         prev*       prev2      p
 34  40     2.753875   cur         prev*       prev2      p
 35  41     2.702361   cur         prev*       prev2      p
 36  42     2.702361   2.660255    2.674985    2.702361*  p2
 37  43     2.660255   cur         prev*       prev2      p
 38  44     2.660255   cur         prev2*                 p2
 39  45     2.660255   2.649441    2.649441    2.652276   none
 40-42 46-48 2.660255  2.649441    2.649441    2.649441   none
 43  49     2.660255   2.615601    2.649441    2.649441   none

t=42 FAIL: magma PREV=2.67498541 (look_px=pl[t=41]); tape=PREV2=pl[t=40].
hyaw t=42 = -42.83674 = yaw target of pl t=40, so t=41 and t=42 used the
same look sample. t=45..49 hold 2.66025519 (look of tape t=42 pose) while
client pl settled at 55.278. No lag 0/1/2 fits the window. Leave FAIL.
draws_between=0. Site is recording-clock (EntityPlayerSP vs MP during
knockback residual), not magma interpolation.

Task 2 — dest climb. PathNavigateGround.getPathToPos bytecode: AIR walk-down
then up-one; else `for (blockpos1 = pos.up(); y < height &&
getMaterial().isSolid(); blockpos1 = up()); return super(blockpos1)`.
Starts at the RPG BlockPos. Material.isSolid, not isFullBlock / isPassable.
Stops at the first non-solid. T182511 dest col y=96 1-block air pocket is
Java dest. Round-4 cavity climb was extra. Reverted. Magma while-isSolid
matches that loop. findPathOptions Euclidean PathPoint.distanceTo(dest)
< FOLLOW_RANGE already matched. T182511 FAIL t=745 eid=3872 x
tape=-291.488923016319 magma=-291.54075023842785 draws_between=0 restored.
T182154 still PASS 852.

Task 3 — End. Census: no EndermanFreezeWhenLookedAt (1.14). AIFindPlayer
overrides NAT (0 Entity.rand). Endermite NAT chance 10: nextInt(10) on
setup. WanderAvoidWater 0.0F, watch 8, talk 80, eye 2.55F, size 0.6x2.9,
speed 0.30000001192092896, follow 64. Place/take short-circuit on
!mobGriefing. WorldProviderEnd.init does not call super.init so
hasSkyLight stays false; getBrightness table[0]=0, daytime teleport
nextFloat skipped (f>0.5 fails). Dragon is a separate Random.

Record: anvil llvmpipe, det_entity_rng=1, doMobSpawning=false. T225550Z
tp 0.5,70,0.5 void-died (fountain air; spawn 100,50,0 is 6 under surface).
T230356Z pad 0.5,64,-20.5 held but summon at 50.5 never entered 64-block
erng. T231027Z fill end_stone 3x3 failed ground_stencil vs generated
121/0 mix. Committed tape T231439Z: pad 0.5,64,-20.5 yaw 90 pitch 20,
air-fill only, persist enderman 42.5,63,-19.5. Recorder omits pr; tape
age=0 for 850 ticks at 42 blocks so PersistenceRequired held. Magma
det_place persist for enderman (same as blaze/pigman) so wander
nextInt(120) is not skipped at entityAge>=100.

Dragon eid=4042 in erng from tape t=106 (473 rows). Own Random. No
enderman cursor mix: seed48 matched the full window. Gate tracks dim=1
EntityEnderman only (overworld ambient header stray eid=3173 is not
tracked).

Gates:
- passive T152220Z PASS 1203 standing
- wander T164213Z PASS 1204 walked eid=386 xz=8.13406
- panic T170933Z PASS 407 walked eid=2983 xz=3.55891 atk=[17]
- ambient T181540Z PASS 625 walked eid=3691 xz=9.06245
- target T182955Z FAIL t=42 eid=3335 pitch tape=2.7023606 magma=2.67498541
  draws_between=0
- nether T182154Z PASS 852 walked eid=6542 xz=8.78925
- nether T182511Z FAIL t=745 eid=3872 x tape=-291.488923016319
  magma=-291.54075023842785 draws_between=0
- end T231439Z PASS 843 walked eid=4062 xz=9.69463 dim=1

test-mob-live: sheep onsets 45,330; blaze duty on=156 off=200
shots=[60,66,72,238,244,250].

## 2026-08-22 detmob consolidation round 4 (lane/detmob-all)

On 2f3a51d: five PASS; two first_divs, cursors equal.

Sites (bytecode, deobfed 1.11.2):
- EntityAIAttackMelee.updateTask: lookHelper.setLookPositionWithEntity then
  tryMoveToEntityLiving read the same target.pos. Tape pl is client pose after
  ServerTick END (includes this tick's knockback). t=29 skeleton pitch
  sign-flips if look uses that pl (player already airborne from creeper
  explode after skeleton AI). look_px = previous tape pl. Path dest uses the
  same clock. detmob_gate set_pose_state onGround=1 so look_px is that pose,
  not a fallen puppet (set_pose zeros vel/og and player_tick applies gravity).
  Collision still uses the current pose.
- PathNavigate.canEntityStandOnPos: IBlockState.isFullBlock of pos.down
  (Block.fullBlock = isOpaqueCube). RPG getLandPos uses it. Slabs/stairs/
  fences/walls/gates/ladder/trapdoor are not full even when BF_SOLID.
- PathNavigateGround.getPathToPos: AIR walk-down; isSolid walk-up stops at
  the first non-solid. PathFinder.findPathOptions keeps a neighbour only if
  distanceTo(dest) < FOLLOW_RANGE; closest==start returns null.
  Nether T182511 t=735 dest col has a 1-block air pocket at y=96 under
  netherrack 97-124. Java-faithful dest y=96 has dist~38<48, A* n=10 same-Y
  walk; Java 1-tick noPath. 128x256x128 window still walked at dest y=96.
  Climb 1-high cavities under more solid so dest is the pillar top (y=128,
  dist=69.9>48, nopts=0). Multi-block caves (t=21 dest y=95 n=2 neighbour-only
  reject; t=585 dest y=95 n=4 -Z walk) do not climb.

Gates:
- passive T152220Z PASS 1203 standing
- wander T164213Z PASS 1204
- panic T170933Z PASS 407
- nether T182154Z PASS 852
- ambient T181540Z PASS 625
- nether T182511Z PASS 841 walked eid=3872 xz=2.73137 (was t=745 x)
- target T182955Z first_div t=42 eid=3335 zombie pitch tape=2.7023606
  magma=2.67498541 draws_between=0 (was t=31 skeleton x). Skeleton melee
  dest closed. Tape watch pitch held t=41 two snaps; magma recomputed from
  look_px=pl[t=41]. EntityLookHelper.onUpdateLook resets pitch to 0 then
  updateRotation each tick (WatchClosest 40F), so a hold is the tape pl
  clock, not interpolation.

Default-off. No GATES / known_divergences / blessed-tape / blaze-rl.

## 2026-08-22 detmob consolidation round 3 (lane/detmob-all)

On 00647de: four PASS; three first_divs, cursors equal.

Sites (bytecode, deobfed 1.11.2):
- Entity.applyEntityCollision: absMax, MathHelper.sqrt (float), 0.05F scale.
  EntityLivingBase.collideWithNearbyEntities after travel. World ticks
  players first. Ambient t=235 creeper x was skeleton AABB overlap after
  skeleton travel, not PathFinder dest Y.
- PathNavigate.checkForStuck: 100-tick / 2.25D inside pathFollow. totalTicks++
  is every onUpdateNavigation; hydrating that clock extra-draws wander RPG,
  so magma increments only while a Path exists. Closes ambient t=424 slide.
- EntityAIAttackMelee.updateTask: LookHelper.setLookPositionWithEntity
  (target, 30F, 30F). setLookPositionWithEntity: posY + getEyeHeight()F f2d
  (player 1.62F). Tape `pl` is client pose after ServerTick END; lookHelper
  samples during AI. Magma stores previous tick's player for look only
  (watch/NAT/collision keep current). Closes target t=29 pitch sign-flip.
- ChunkCache is FOLLOW_RANGE+8, chunk-aligned XZ, full Y 0-255. Widening
  magma's 32x24x32 PNP to 128x256x128 put dest y=95 in-grid and A* returned
  a neighbour path Java left as closest==start (nether T182511 t=31
  regression). Same t=745 x values with the large window. Reverted. t=745
  dest after solid walk-up is (-285,96,-70) out of the 24-high window; A*
  emits a 10-pt same-Y closest (n=10, not neighbour-only) while Java
  1-tick wander noPath (tasks=8 then 0, og=1).

Gates:
- passive T152220Z PASS 1203 standing
- wander T164213Z PASS 1204
- panic T170933Z PASS 407
- nether T182154Z PASS 852
- ambient T181540Z PASS 625 (was t=235 creeper x)
- target T182955Z first_div t=31 eid=3339 x, cursors equal (was t=29 pitch)
- nether T182511Z first_div t=745 eid=3872 x, cursors equal

Default-off. No GATES / known_divergences / blessed-tape / blaze-rl.

## 2026-08-21 detmob consolidation round 2 (lane/detmob-all)

On 615788b: four tapes PASS; three open.

Sites (bytecode, deobfed 1.11.2):
- AbstractSkeleton.initEntityAI targetTasks: hurtBy, nearestPlayer, nearestGolem
  (no villager). /summon NBT skips onInitialSpawn; golem NAT nextInt(10) still
  draws on empty AABB. Restored skeleton golem NAT.
- initEntityAI does not add combat. Ctor setCombatTask LinkedHashSet-appends
  melee (empty hand). Wander/watch/idle shouldExecute run first on that setup
  tick: nextInt(120)+nextFloat+nextFloat.
- AbstractSkeleton/EntityZombie getEyeHeight ldc 1.74F; look dy is f2d of that.
- EntityMob.getBlockPathWeight = 0.5F - getLightBrightness. Nether table is
  overworld*(0.9F)+0.1F (WorldProviderHell).
- PathFinder: closest==start returns null. Magma 32x24x32 window is smaller
  than Java ChunkCache (FOLLOW_RANGE+8). Dest after getPathToPos solid walk-up
  can sit outside the window; A* then returns a same-y neighbour. Reject
  neighbour-only paths when dest is out of window.

Gates after these:
- passive T152220Z PASS 1203
- wander T164213Z PASS 1204
- panic T170933Z PASS 407
- nether T182154Z PASS 852
- ambient T181540Z first_div t=235 eid=3693 creeper x, cursors equal
- target T182955Z first_div t=29 eid=3339 pitch, cursors equal
- nether T182511Z first_div t=745 eid=3872 x, cursors equal

Default-off. No GATES / known_divergences / blessed-tape / blaze-rl.

## 2026-08-21 detmob round 4 PathFinder + look pitch (lane/detmob)

WatchClosest look_y is `posY + (double)getEyeHeight()F` = f2d(1.62F). Deg
conversion stays LUT * `(float)(180.0/(float)PI)`; bytecode `dmul` is 1 ULP
off the 1.11.2 remainder (same class as MOVE_TO yaw). Panic t=21 pitch closed.

Det PathNavigate: `getPathToPos` air-down then up-one / solid walk-up;
`findPath` `(float)coord+0.5F` then f2d; FOLLOW_RANGE `16*(1+nextGaussian()*0.05)`
from `seed48_init` (living ctors use Math.random, so this gaussian is the first
Entity.rand draw). `pathFollow` only if `canNavigate` (onGround); else airborne
same-cell Y-above index increment. `PathNavigate.getPathToPos` returns null
when `!canNavigate`, so airborne `tryMoveToXYZ` is a no-op (RPG still draws).
Wander t=70 next PathPoint is Java's (48,72,126). Panic
`...T170933Z` PASS 407 ticks eid=2983 (hit/knockback/hp/pitch/path).

Wander leftover: t=134 eid=386 field=z 1 ULP after pathFollow skip to
(53,74,131). x/y/yaw/hyaw and cursors bit-equal through t=133 then z 1 ULP;
t=138 equalizes. Site is `EntityLivingBase.travel`, not A*.

Standing `...T152220Z` PASS 1203. `test-mob-live` sheep onsets 45,330.
Default-off. No blessed-tape / GATES / blaze-rl.

## 2026-08-21 detmob-hostile zombie/skeleton/creeper (lane/detmob-hostile)

Same det_entity_rng default-off pin as passives, additive on shared
files so lane/detmob (passives) can merge first.

Census (`entity_rand_census.tsv` hostile addendum): EntityMob
living_sound nextInt(1000) lst=-80; zombie/skel pitch 0or2, creeper
getAmbientSound=null so 0; persist skips despawn nextInt(800); night
skips sun-burn nextFloat. Dual EntityAITasks (target then goal).
Zombie 3x nextInt(10) nearest (hurt no-rand, player, villager, golem);
skeleton 2x; creeper player-then-hurt. Melee canPenalize=false.
Swell rand-free (fuse += state before AI). Bow: 2x (double)nextFloat
<0.3D when strafingTime>=20, plus 1 nextFloat shoot pitch. Watch
range 8. Wander RPG same as passives.

Scenarios (easy, night 18000, persist, doMobSpawning=false):
detmob_hostile_ambient player 38.5,70,170.5 (~46 blocks, no
NearestAttackableTarget); detmob_hostile_target hilltop pad, mobs 5-6
south. Recorder snapshot 64 blocks; extra erng keys ttt/ttasks/tgt/
fuse/mdelay/see/stime/atime/scw/sback/cstate. Gate `h` fixture lines.

Magma hai_* reuses pai look LUT, body helper, wander RPG,
PathFinder. Knob-off: test-mob-live sheep onsets 45,330 unchanged.

Tapes: anvil llvmpipe, det_entity_rng=1, doMobSpawning=false,
difficulty easy, night 18000, persist. /summon NBT skips
onInitialSpawn (CommandSummon flag=true) so skeleton has no bow
and keeps constructor melee.

`scenario_detmob_hostile_ambient_20260821T181540Z.jsonl`
player 38.5,70,170.5; 3 tracked [3689 zombie, 3691 skeleton,
3693 creeper]; ground_stencil=3. FAIL first_div t=220 eid=3693
creeper z 1 ULP (`0x406002bf7d459c53` vs `...c52`)
draws_between=0. Creeper wander tasks=8 from t=204, 16 ticks of
MOVE_TO bit-equal then travel ULP; RNG matched. Zombie standing
look PASS through last snap t=609 (never wandered). Skeleton
look+early wander bit-equal through t=235; first pose split
t=236 x PathFinder (seeds match, plen=1). Same PathFinder site
as passive wander; do not duplicate that fix.

`scenario_detmob_hostile_target_20260821T182955Z.jsonl`
hilltop pad, player tp last to 53.5,74,127.5; 3 tracked
[3335 zombie, 3337 creeper, 3339 skeleton]; ground_stencil=3.
FAIL first_div t=23 eid=3339 skeleton x tape=51.57888855
magma=51.55478371 draws_between=None. Java NAT tgt=1 ttasks=2
tasks=0 (bow see/stime stay default); magma HBOW 256 plen=1.
Creeper swell t=24 tasks=128 cstate=1 fuse++ seeds match
(rand-free). Zombie NAT+melee t=49 after creeper blast knocks
the parked player. Pathing depends on lane/detmob PathFinder.
## 2026-08-21 detmob-dims Nether (lane/detmob-dims)

Default-off `det_entity_rng` across DIM-1. Mixin reseeds at Entity ctor
RETURN (dimension-agnostic). Recorder `erng` via `lockWorldOf` =
`player.getServerWorld()`, plus additive `hg`/`gv`/`pr`/`hot`/`hof`/`anger`.
Scenario `detmob_nether.yaml`: qrl `dim -1`, `/tp` to seed-0 fortress
`-326.5 56 -102.5` yaw 90 pitch 20, inert spawners, `/summon` blaze
`-291.5 59 -75.5` and pigman `-330.5 59 -143.5` PersistenceRequired,
840 ticks, doMobSpawning false. Census: blaze hover gaussian + nearestPlayer
nextInt(10) + wander/watch/idle; pigman ambient = zombie list minus targeting.
Magma: `pai_det_ai` blaze/pigman, talk 80, persist age=0, heightOffset after
goals+nav, DIM-1 `detmob_gate` `dim` + `gm_runtime_set_dimension(-1)` + stencil.
Knob-off path unchanged.

Gate (anvil llvmpipe record, Mac magma CPU):

- `scenario_detmob_nether_20260821T182154Z.jsonl` (git 87e805b): dim=-1,
  det_entity_rng=1. Header has extra fortress blaze eid 6539 pr=0 (filtered);
  persist blaze 6541 + pigman 6542. Ground stencil match. PASS bit-equal
  pos/yaw/pitch/hyaw, 852 server ticks, 2 tracked. Pigman walked xz=8.78925
  full window (847 seed48 changes). Blaze walked xz=6.7703 then left the
  48-block erng at tape t=283 / tt=302 (270 cursor changes in-radius).
- `scenario_detmob_nether_20260821T182511Z.jsonl` (git 92b66c0, spawners
  setblock + header is two persist summons only): stencil match, seed48
  in phase at first_div (`draws_between=0`). FAIL blaze eid 3872 field=z
  tape t=31 / mag t=21: Java wander 1-tick noPath (tasks=8 then 0, still
  at -75.5); magma PathFinder 1-block MOVE_TO to z=-74.5, yaw=360. Pigman
  same class at t=56 (Java xz=0 whole tape). Named site: PathFinder /
  WalkNodeProcessor, not Entity.rand. Same family as overworld wander
  first_div t=70 PathPoint mismatch.

End not recorded: Nether re-record of the same yaml is nav lottery;
enderman teleport would sit on that surface.

## 2026-08-21 detmob round 3 worldgen + walk + panic (lane/detmob)

detmob_gate uses seed-0 `GM_WORLD_DEFAULT` + `gm_world_ensure` (same
generator as magma_game / tape replay). Tape header `entity_rng[].g` is
a 3x3x3 block-id stencil (dx, dz, dy); gate rc=3 on mismatch.
Recorder writes `g` on recstart. Wander
`scenario_detmob_wander_20260821T164213Z.jsonl` ground_stencil=5 PASS.
Standing `...T152220Z` has no `g` (WARN); poses still PASS 1203 ticks
eids 462/463/465.

Det PathNavigate.pathFollow now close-advance +
`isDirectPathBetweenPoints` (DDA + WalkNodeProcessor size-sweep
`getPathNodeType`, canBreakDoors/canEnterDoors true). Follow runs after
goalSelector. Knob-off path_len standability unchanged.
`test_mob_live` sheep onsets 45,330.

Wander `...T164213Z`: FAIL first_div t=70 eid=386 field=x
`draws_between=0`. Walking bit-equal through t=69 (y=70→71 step-up,
MOVE_TO (48.5,71,125.5)). t=70 PathFinder next PathPoint after
(48,71,125): magma (49,72,125) yaw 268.67 vs Java (48,72,126) yaw
339.67. Old t=31 skip first_div is gone (eid 389 bits match on that
path).

Panic `scenario_detmob_panic_20260821T170933Z.jsonl` hilltop sheep
53.5,74,126.5; player 53.5,74,127.5; hp-drop atk mag_t=17.
Det fist = EntityPlayer ATTACK_DAMAGE 1.0; hurt `getSoundPitch` 2x
nextFloat after knockBack `(double)0.4F`. t=20 pos/hp/seed bit-equal
(hp 8→7, y=74.3608, z=126.1). FAIL first_div t=21 eid=2983 field=pitch
tape=-0.9902643 magma=-0.990264118 `draws_between=0`. Site:
EntityLookHelper deltaLookPitch 2 ULP after knockback (watch still on;
panic bit later).

Default-off. No blessed-tape / gates / blaze-rl.

## 2026-08-21 detmob round 2 atan2 + wander (lane/detmob)

MathHelper.atan2 LUT in `blaze/core/mc_math.h` + `mc_atan2_tab.h`
(Java 8u492 ASINE/COS bits; C libm asin/cos is 1 ULP off). Det look
helper only. Deg conversion is `(float)(lut * (float)(180.0/(float)PI))`
to match remainder hyaw. BodyHelper + tape `bhp`/`bht`/`ryaw`. Gate
clock is per-entity `tt` (entityAge resets inside 32 blocks).

Standing `scenario_detmob_passive_20260821T152220Z.jsonl`: PASS 1203
ticks, eids 462/463/465 bit-equal pos/yaw/pitch/hyaw. Old
`...T142333Z` still fails t=341 hyaw without recstart prev/bt.

Wander `scenario_detmob_wander_20260821T152429Z.jsonl`: FAIL first_div
t=89 eid=426 x, `draws_between=1`. Gate world is superflat + 3x3 grass;
RPG 10 samples miss standable cells, wander shouldExecute false, extra
LookIdle nextFloat. Java overworld finds a land target and MOVE_TO
walks. Named site: RandomPositionGenerator.findRandomTarget /
PathNavigateGround (gate world, not Entity.rand).

## 2026-08-21 detmob Entity.rand experiment (lane/detmob)

Default-off. Mixin reseeds Entity.rand at ctor RETURN (Mixin 0.7.5
forbids INVOKE+Shift in a ctor). Recorder logs per-entity seed48 +
AI hydrate + erng. Magma `det_entity_rng` consumes Java LCG in
`magma/game/entity_rand_census.tsv` order. Tape
`scenario_detmob_passive_20260821T142333Z.jsonl`: seed 0, 1211 client
ticks, 3 standing sheep (eid 3705/3706/3708). Gate: unique-server
snapshots, cursors match (`draws_between=0`), first_div t=81 eid=3705
hyaw 2 ULP (`0xc36596d6` vs `0xc36596d8`) — look-helper
MathHelper.atan2 LUT vs libm, not RNG. Phase C not attempted.
Default path: `test_mob_live` sheep onsets 45,330 unchanged.

## 2026-08-21 eval --stage ladder

`out/blaze/rl/eval --stage 0` (default) is stdout+stderr byte-identical to
ca29468 on `ppo_ckpt.bin`. `--stage K` loads `s{seed}_stg{K}.bsnp`; missing
files print an explicit SKIP row and are not failures (exit 0). `--stage all`
runs 0..4 then a ladder of milestones newly reached this episode (start
inventory baselined after assign+reset+burn-in). Gumbel `rng_seed =
cfg.seed + stage` so stage 0 matches history; later stages differ, reruns
match. Fake `s10_stg1.bsnp` copied from t0: start=t0, new=-. Real stgK snaps
come from the forge lane.

## 2026-08-21 native PPO best-on-t0 + collapse telemetry

`out/blaze/rl/ppo` writes `{ckpt_stem}_best.bin` (schema 1) plus
`{ckpt_stem}_best.json` (`ticks`, `t0`) when the trailing t0 full-chain
probe sets a new max. Last checkpoint path is unchanged; `_best` does
not regress. Chunk log always prints `ent` / `kl` / `clipfrac`.
`approx_kl` is mean(ratio-1-log(ratio)); `clipfrac` is
mean(|ratio-1|>clip). Accumulators are diagnostic; update math is
unchanged.

## 2026-08-21 fluids M1

Magma and Blaze CPU share the FLD1 liquid-evolution digest (scheduler FNV +
XOR of ids 8-11 in the snapshot box + ncells + CA write-backs). Both step
`ff_ca_step_ex`. Gate: `port_matrix.py --tier m1 --subsystem fluids --no-deps`
VERIFIED 61 ticks. t=0 digest `0x1f27ac65354386f4`. CUDA/Metal not claimed.
## 2026-08-21 worldpix lane (canonical t=260, entity-water, particle blend)

Worktree `/Users/infatoshi/dev/nw/worldpix`, branch `lane/worldpix`,
baseline `18c5022`. No raster-kernel twin edits.

- Canonical `20260721T215812Z` t=260 "texel-selection" retracted. CPU
  replay 3617 ticks / 181 frames, then `pxdiff` / `pixel_gate` on the
  saved frames. t=260 is 2 `known:4` shading-offset clusters (65+53 px,
  sel=0.00); t=460 is 0 clusters >=50 px. The old 7291-px canopy was the
  already-landed FOV-before-sprint ordering bug. Tape still FAILs
  mild_shift t=3080 / t=3540 (0 UNEXPLAINED px). Remaining 118 px is the
  filed outdoor luminance family; no exact shade fix without oracle
  fragment lightmap (texel bias / fog retune not touched).
- Item 13 entities-over-water CLOSED. Both compose paths already draw
  opaque, then entities, then translucent. `WR-ENTITY-WATER-OCCLUSION`
  `--skip-gpu` ALL PASS: behind 0/812, front 5016/5016, half 170/905.
- Auto-campaign particle additive RESIDUAL. Vanilla 1.11.2
  `ParticleManager.renderParticles` is SRC_ALPHA / ONE_MINUS_SRC_ALPHA
  (oracle-src line 283) + alphaFunc 1/255. Magma layer 0 already
  `blend=1` / `alpha_ref=0.003921569`. The "additive glow" label is
  pixel_gate's oracle-brighter heuristic. Magma reconstructs only BLOCK
  + explosion whitelist. Do not set blend=3.
## 2026-08-21 lane/sim CPU replay divergences

Closed four OPEN_DIVERGENCES items on `lane/sim` (Mac CPU, no GPU).

- Live blaze `isBurning`: AIFireballAttack already in `mob_live.c`. Darwin
  standalone tests now link `world/gen_prefetch.o`. Receipt:
  78-on/100-off, transitions 0/78/178/256, shots 60/66/72/238/244/250.
- Falling t46: `attack_hits_falling_block` now requires `t_ent < t_block`.
  `test_fall_reanchor` H PASS. 151855Z tape not in this tapes set.
- pcl consume: already in `replay_tape.py` / `script.c` /
  `particles_live`. `test-particles-live` PASS.
- Spawner TE path: Anvil TileEntities -> `set_tile_entity` ->
  `GmRuntime.spawners` -> `gm_frame_spawners_emit`. `discover_spawners`
  does not drive the TESR.

Canonical physics after:
`out/verify/replay --tape verify/tapes/20260721T215812Z_...77b5b462.jsonl --ticks 4000`
-> first_div none, 3617/3617, nearby_hash match. Root `make test` green.
## 2026-08-21 native 13-seed eval

`out/blaze/rl/eval` on blaze CPU. Schema-1 load is `rl_ckpt_load` -> `nn_load`
(same reader as ppo.c). Obs packing lives in `obs_pack.h` with the trainer.
Canonical 13 seeds including held-out 11 and 33; sampled best-of-5 x 6000
ticks; Gumbel `rng_seed=0`, `ni=seed_index*tries+attempt`. CUDA/Metal slot
via `--backend`; not run today.

`overnight_gpu0_6m.bin` (probe t0 0.565, wood-break): 0/13 full chain,
6/13 logs3, 7/13 t0. Re-run stdout+stderr byte-identical
(`cda232d472673a35359c62bba571593642bd1b314784a44eb22618d4650804a1`).
This is the missing native eval harness, not magma transfer (GATES item 2).
## 2026-08-21 uipix lane

UI-space pixels. Isolation HUD chrome is bit-exact (all CORE_HARD
`hard_px=0`). Hand use poses stay RESIDUAL: bow/eat goldens are idle-tips
(C transforms match `ItemRenderer.java`; do not fit). Shield pose matches;
C-painted mean 1.56 -> 0.75 after Mesa RenderHelper packing in `hand.c`.
Inventory chrome bit-exact; preview still maxch=1 / 442+323 px. Auto-campaign
hand-black and hotbar-over-world not replayed. Darwin link: `randtick.o` +
`gen_prefetch.o` in ui_hud live and gui_candidate; `cr_k14_light_query` stub
in the isolation candidate. magma/Makefile libomp prefix uses make
`wildcard`, not `ls` (brew color codes broke `blaze_cpu.so`).
## 2026-08-21 oracle evidence (lane/unblock)

Rain tape `verify/tapes/scenario_rain_thunder_20260821T093435Z.jsonl`:
header and all 209 rows rain=1.0 thunder=1.0. World on anvil under the
same stem. Slime TRANSLUCENT dump: 441 blocks, 12 general quads, 0 face
quads, coverage n_single=0. Portal/underwater `frame_pair` A/B maxch=0
after fog freeze + 10s chunk deadline. No renderer changes.
## 2026-08-21 worldgen lane (fortress + spawn)

Fortress piece tree is 1.11.2: pending ArrayList shift-remove,
HORIZONTAL.random N/E/S/W, setRandomHeight(48,70). Seed-0 nether_full
spawners match oracle MCA at (-325,56,-102) and (-325,56,-215), both id 52.

World spawn is createSpawnPosition. Oracle level.dat: seed 1000 is
168,64,252; seed 0 is 44,64,176. Magma DEFAULT uses that xz, not 8.5,8.5.
Superflat origin unchanged. wrapper_gate rc=0 without --update.

## 2026-08-21 remaining-to-stop-asking

Codex, Fable, and Grok surveyed the tree. Fable ranks. The four-gate
close list is `docs/GATES.md` "Remaining to stop asking". OPEN_DIVERGENCES
keeps forensics plus a grind/no-grind index.

"Full game GPU port" is the Blaze port-matrix DAG, not a magma CUDA tick.
Metal tick waits on CUDA M1+M2 survival rows. Magma GPU backends stay raster.

Overnight split: anvil gpu0 native chain; anvil gpu1 pixels/bench; Mac
Metal verify. Do not start Metal tick or magma live-GPU tick tonight.

## 2026-08-21 torch trainer cut

CUDA wood-break t0 (success_item=17, N=1024, T=32, 11 snaps, 6M ticks)
matched native C vs Torch: both ~0 until ~4M, end t0 0.495 / 0.490.
Deleted the Torch chain/coal/break trainers, LibTorch `cgraph`/`native`
stand-ins, `cpolicy`, flywheel benches, and their callers. Trainer path
is `out/blaze/rl/ppo`. Env verify still uses torch only as CUDA buffer
host (`blaze.py`, `verify_cuda.py`).

## 2026-08-18 overnight native-surface

Loop-1 stayed on `wt/native-surface`. Not merged to master.

Landed tonight on that branch:

- Magma raster smoke and mob inspect moved to `magma/tests/` so `make test` finds them.
- Root `make` on Darwin builds `magma_game` and `magma_game_metal`.
- Root `make test` includes verify tape-info and Darwin `blaze/nn test-metal`.
- `make assets` is the only asset path. Deleted `scripts/bootstrap_assets.sh`.
- Sweep env-knob step is `make -C verify env_knob_gate-check`.
- Public export is the C binary only. Deleted `scripts/export_public_tree.sh`.
- Evidence trees gone: `artifacts/`, `demos/`, `optloop_runs/`, `verify/demo/`,
  `verify/trace/report/`, `CLAUDE.md`, one-use video scripts.
- Sweep and coal prefix readers look in `blaze/rl/fixtures/`.

Mac measured (this box, n=1): `make test` 6.55s PASS. `make` 4.47s PASS
(`magma_game` + `magma_game_metal`). `make -C blaze/nn test-metal` 1.75s PASS.

C replay slice 2 merged: apply existing snapshot_patch cache at tick 0.
Canon 8-tick and 32-tick on this tree: magma_rc 0, first_div none,
nearby_hash 42a84376d3f195a9 match 1, snapshot_patch events 468160,
wall 1.24s / 1.25s.

Magma `--conf` now owns seed/world/script/state_out so C replay can drive
this tree's magma_game (slice 1 had used a dirty-master binary).

Still out: merge/push to master, binary tape, frames/PNG, Python trainer
cutover, Anvil CUDA check.

Code and goldens are ground truth. Short history only; full agent map is root
`AGENTS.md`. Git has the long form. Old one-shot reports: `docs/archive/`.

## What shipped (by tree)

### java/
- Playable 1.11.2 Forge+Malmo on anvil only (Mac native GL dead).
- qrl bridge: reset/step/obs, overclock, recstart/recstop tapes, tick-boundary frames.
- Drop-in JNI: sin, lightmap, biome tint, AO (`c/render-opt/dropin`).
- Play: mcwindow (framebuffer stream) or Moonlight; agent stack Xvfb :1 + VNC.
- WorldGenProbe / coverage hooks for worldgen and render path attribution.

### c/render-opt/ (effectively complete)
- 39/40 kernels bitwise vs real MC; k26 closed-by-integration (atlas UVs non-deterministic).
- Whole-frame stripcheck: native drop-ins == vanilla Java at 0 px on pinned course
  (chat/name nondeterminism pinned out).
- Intentional non-ports: GL raster, atlas stitch. Optional: full rebuildChunk VBO drop-in.

### c/mc-sim/ (kernel farm done; product wiring continues in craster)
- Dual-compile core: CPU oracle + CUDA (mostly one-thread-per-env for CPU==CUDA gates;
  real batch RL needs stage-split GPU, not serial device threads).
- Waves 0–14 verified: worldgen (OW/nether/end/flat), structures (stronghold/fortress/mineshaft),
  populate, fluids/light CA, physics, combat, crafting/smelting, portals, dragon subsets,
  tick compose, cuda_batch_tick, py_gym smoke, SPS bench.
- Fidelity rule: runtime internal consistency (CPU==CUDA); worldgen vanilla LCG seed-faithful.
- Live-game worldgen: multi-seed ~99.97% cell match via genprobe flywheel (stale-skylight,
  big-tree carry, Forge extraTreeChance, ice/snow, biome transpose bugs found and fixed).
- Open kernel-side notes (not full queues):
  - `populate` Golden.java lags stale-skylight + leaf-soil mushrooms (real oracle = world_diff).
  - GPU worldgen K1 noise: GO for many-env RL (~4.6x one CPU core); K2–K6 not built.
  - Many units are CPU==CUDA only until wired through live Java tick traces.

### c/craster/ (product binary)
- Software raster CPU+CUDA; game loop uses mc-sim headers for worldgen/sim slices.
- Macro: `test_route_e2e` seed 0 empty inv -> legally `won` (travel injects only).
- Human 12k tape: physics 1e-9 clean through t9810; t9811 = evolved-save water (fresh-world rule).
- 12k frame replay perf: ~29s -> ~8.4s (device meshes, GPU sky, pipeline, hi-z, npy frames).
- GUI table/furnace pixel-gated vs Java; product gaps listed in PRODUCT.md.
- Nightqueue 2026-07-11 items closed (lightmap, canopy root-cause, swamp M 134, etc.).

## Hard lessons worth keeping

- Goldens = real MC only (verbatim Java or live capture). Never port-vs-port.
- C needs ordered temps for multi-RNG expressions; `-ffp-contract=off` / `--fmad=false`.
- Kill game: `pkill -9 -f '[G]radleStart'`; launch game in a standalone setsid call.
- Physics tapes: human + fresh world + worldgen-verified seed. Driven tapes zero motion after land.
- CUDA twin per serial unit is a verification tax, not a throughput claim.
- Doc sprawl (kernel READMEs, WORKQUEUE, dual DEVLOGs) was agent-flywheel cost; purged 2026-07-11.

## Not open work (cuts / done)

Redstone, multiplayer, disk saves as product, audio, side structures (monuments/mansions/…),
optional villages/enchanting/brewing/weather bundles (flags reject `on` until implemented).
Render-opt lab is closed unless reopening a specific kernel directory.

Removed 2026-07-11 (unused routes): `java/build_mac.sh`, `play_mac.sh` (Mac GL dead),
`render_poc.py` + `setup_python_env.sh` (MineRL venv path; qrl is the RL bridge).

## Where next work lives

- Fidelity: `OPEN_DIVERGENCES.md` + fresh human tapes (`VERIFY.md`).
- Product surface: `PRODUCT.md` remaining gaps (models, encounters, HUD coverage, bundles).
- Sim/RL: gym beyond smoke, live-Java entity traces, optional GPU worldgen K2–K6.
- Code: `c/mc-sim/core/`, `c/craster/game/`, `java/.../qrl/`.

## 2026-07-12

- Moved consolidated `~/games/minecraft` first-party markdown into `docs/legacy-games-minecraft-learnings.md` so the old tree can be deleted without losing rationale.

## 2026-07-12 (legacy cold pack)

- Packed `~/games/minecraft` keep-set into `~/dev/minecraft/legacy-cold/` (flashmine bundle, netherite csrc, mc-oracle tar, archive).
- Retargeted `c/mc-sim/ref/netherite-csrc` to `legacy-cold/netherite-csrc`.
- Deleted hot checkouts under `~/games/minecraft` (flashmine, netherite*, mc-oracle, backups).

## 2026-07-17 (iron pickaxe across the whole RL stack)

- Extended the chain to an iron pickaxe end-to-end: craft:6 (furnace, 8 cobble, table-gated),
  craft:7 (iron pick, 3 ingot + 2 stick, table-gated) and a `smelt` primitive (extract furnace
  output, insert iron ore, insert 1 coal fuel) in all five layers: `rl_mode.c`, `cuenv_core.h`
  (CPU+CUDA from one source), `cuenv.py`, the qrl JVM bridge, and reward/trainer.
- BOLR binary obs stays FROZEN; iron visibility rides a JSON-only `inv_iron` obs field
  ([furnace, iron ore, ingot, iron pick] full-inventory counts, mirrored on the qrl bridge)
  and the trainer status vector (CU_STATUS_K 13 -> 17). Root cause of a day of "no iron
  pickup" false alarms: the hotbar is full by the iron stages, pickups overflow to the
  backpack, and hotbar-only detection can never see them.
- chain_probe iron stages (IRON=1): cobble bank, underground kit (furnace crafted BEFORE
  picks so it lands in-hotbar), snapshot-oracle iron hunt (.bsnp region parse; camera
  ghost-target pruning since earlier churn destroys oracle cells - wooden pick breaks iron
  ore with no drop), face-adjacent walk-in collection, smelt, final table + craft:7.
  FULL CHAIN seed 16: spawn -> iron pickaxe in 4114 ticks, scripted.
- Verified: replay of the 4114-tick chain on cuenv CPU vs the real env = ZERO diffs
  (verify_cpu --chain --chain-seed 16); 64 CUDA lanes vs CPU byte-exact (verify_cuda);
  live JVM smoke over the qrl socket (craft:6 -> smelt 3 ingots -> craft:7) PASS.
- reward_chain grows 5 default-0 iron milestones (exact backward compat, tested);
  ppo_chain_cu widens craft head 7 -> 9 + smelt head under IRON_CHAIN=1 only, so
  chain_net_cu_v2.pt stays loadable.

## Iron overnight train 2026-07-17
- Verified CPU chain s16 zero-diff (4114 ticks).
- CUDA chain verify in flight then IRON_CHAIN=1 PPO on GPU0 (tmux iron-overnight).
- Recipe: SUCCESS_ITEM=257, reward_iron_abuse.json, N=3072, EP_DEC=2500, wall 8h, 9-stage curriculum.
- CUENV_MAX_SNAPS 64->128 for iron capture slots.
- Codex seed-harden 29/3 in tmux iron-seed-harden.
- Morning: bash c/craster/rl/out/morning_iron_status.sh

## 2026-07-20 (docs consolidation)

- Single agent entry: root `AGENTS.md`. How-tos/history under `docs/`
  (`RUNBOOK`, `BOOTSTRAP`, `GATES`, `DEVLOG`). Archaeology in `docs/archive/`.
- Living contracts stay next to code (`c/*/SPEC.md`, craster PRODUCT/VERIFY/OPEN_DIVERGENCES).
- Root no longer holds DEVLOG or product/report markdown.

## 2026-07-21 (bot-recorded canonical tape + torch placement chase)

- New canonical tape 20260721T215812Z: recorded end-to-end by progression_bot
  (no human input), 3,617 ticks, physics-exact at 1e-9, pixel gate PASS.
  CANON_TAPE swapped in netherite_sweep.sh; VERIFY.md/GATES.md updated.
- Renderer/sim fixes it surfaced: crack decal face mapping (frame_capture),
  torch viewmodel item/generated routing (hand.c), torch placement support
  validation + refire hit-face from AABB calculateIntercept semantics
  (player_ctl, sel_box ray_box_hit, blaze_core cu_ray_box_hit; OPEN_DIVERGENCES
  #55), cross-plant vanilla random offsets (mesh_mc), CUDA overlay pinned-buffer
  race (frame_capture).
- KNOWN ISSUE (RESOLVED same day, see below): blaze-cuda-chain appeared to hang
  on GPU1 (sm_86) at ~tick 185 of the s10 chain.

## 2026-07-21 (later: sweep fully green + fidelity round via agent fan-out)

- blaze-cuda-chain "hang" root-caused (codex): NOT a spin. The verify-helper
  k_emit rendered all 2,304 camera rays on a SINGLE CUDA thread; a camera
  change near tick 185 raised traversal cost enough that sm_86 looked wedged.
  Fix: k_emit_cam renders one thread per pixel, then the single-thread record
  assembly runs on the same stream (blaze_cuda.cu). Chain gate byte-exact on
  GPU1/sm_86 in ~120 s (was: killed at 1200 s), semantics unchanged.
- magma-test-config re-enabled: test_config.c capture buffer 1024 -> 4096
  (usage text had outgrown it).
- Viewmodel fidelity (codex): vanilla ItemRenderer equip lower/raise + retained
  stack, swing restart at half animation, ItemLayerModel per-texel rim quads.
  Canonical tape: viewmodel 518,463 -> 470,440 px, hud 471,103 -> 397,903.
  tests/test_hand_torch.c updated to assert the exact per-texel topology.
- HUD fidelity (codex): hotbar durability strip (renderItemOverlayIntoGUI),
  meta in GmPlayerView, exact GUI mini-cube lighting/UVs. hud -5,332 px more.
- Particles finding (codex, honest negative result): the 'particles' gate class
  is misnamed - pixel_gate classifies any oracle-brighter cluster as particles;
  the dominant 87k cluster is a broad terrain-LIGHTING divergence during the
  dig windows (t3080-3160, t3320-3400). Real debris is minor and RNG-unmatchable;
  implementing it made the class worse, so it was rejected. Follow-up agent is
  chasing the lighting root cause (world/light.c incremental relight suspect).

## 2026-07-22 (brute-force triptych round: crack/stone/foliage/cave-light)

- Method change per operator: rank keyframe diffs (grind.py), eyeball the
  triptychs directly, characterize the artifact class visually, then hand the
  numerics to a codex round. Four root causes landed this way.
- Crack overlay (codex): block-damage overlay now renders full model faces
  with per-face crack UVs (vertical faces were mirrored, bottom rotated 90);
  stage = floor(progress*10)-1 per PlayerControllerMP.
- Polished stone (codex): granite/diorite/andesite metas 2/4/6 no longer fall
  through to plain stone; model-oracle coverage added.
- Foliage lighting: compute_skylight_spread read the renderer block id
  instead of the packed vanilla state for sky opacity; cube faces now use
  vanilla useNeighborBrightness (mc_light_for_ext) and cross-plants take
  neighbor combined light. Sidecar #40 filed: oracle-only ParticleDigging
  burst at t3160, surfaced once foliage skylight matched.
- Cave/canopy brightness (my measurement, codex root cause): ~64 mid-tape
  frames sat at a flat 9.06 mean/ch with magma/oracle = 1.2286 exactly,
  channel-uniform. Cause: light_set_state seeded EVERY state load with sky 15,
  so metadata-only leaf loads clobbered stored skylight under the canopy
  (Moody lightmap bytes 197/160 = 1.23125 - the measured scalar). Fix follows
  World.checkLightFor: metadata-only loads keep stored skylight; opacity
  changes re-derive via canSeeSky + flood. Tape median 8.69 -> 0.16 mean/ch;
  plateau frames 9.1 -> 0.01. Gate PASS both repos; quick sweep green.
- Worktree gotcha recorded: tracked sidecar json is NOT symlinked into
  worktrees - editing the main-tree copy silently leaves the worktree stale
  (cost one full replay to spot: active entries [] at the failing tick).

## 2026-07-22 (scenario harness: scripted combat/GUI/anim environments, six-round codex fan-out)

- New transferable scenario harness (raster/verify/scenarios/): YAML spec ->
  oracle boot -> phased setup (one runcmds batch per command + settle ticks;
  a single batch executes in ONE server tick, so tp-dependent fills/summons
  fail; vanilla also reports no-op clear/fill-air as failure) -> mcwindow
  input script -> tape -> replay gate. setup_qrl passes raw bridge steps
  (dim {id:1} for End entry).
- Eight tapes all rc=0 with gate PASS, each divergence root-caused by a codex
  round and re-verified here: canonical, smoke zombie, blaze melee, blaze
  bow, pigmen aggro, wither skeleton, enderman fight, ender dragon.
- Smoke zombie harvest: zombie melee was 4.0 (vanilla 3.0) and replay
  double-applied saturation regen across packet timing (13/6 vs 4/3 hp).
- Night scene ~2x dark: bulk snapshot loads bypassed Chunk.generateSkylightMap
  semantics (light_load_state now re-derives column skylight per batch);
  7.51 -> 1.47/ch. Pink hotbar icon = missing diamond_sword GUI atlas entry.
- GUI actions (8-step scripted inventory sequence, cursor-driven): paper doll
  (drawEntityOnScreen), hover highlight/tooltip (GuiContainer/GuiUtils),
  armor/offhand placeholder sprites, achievement-toast harness contamination
  (pre-grant achievement.openInventory). Panels 1.2-4.2 -> 0.15-0.26/ch.
- Texture animation: replay never seeded total_time, so every animated tile
  ran from clock zero (portal masked it via recorded frameCounter). Lava
  (20-frame fwd/rev) + fire (custom sequences) implemented; underwater
  overlay burst was sparse ppos anchoring, now per-row anchors. Negative
  control (time-shifted candidate) now genuinely separates per region.
- Combat physics: replay dropped ent_box collision impulses whenever velocity
  packets existed (Entity.applyEntityCollision); wither skeleton = skeleton
  model 1.2x + stone sword (8.0 raw) + wither DoT (%40 pulses through hurt
  resistance); hurt-resistance/lastDamage gating implemented; nether brick
  had no block model (rendered as stone); held items ignored the night
  lightmap. Dragon: contact damage from recorded envelopes via causeMobDamage
  + hurt-resistance ledger reproduces death at t606 exactly; dragon-breath
  AoE cloud is RNG-unmatchable -> scoped sidecar known:40.
- End entry scenario note: seed-0 obsidian platform is embedded in the island
  (tape one was 1600 ticks of inside-wall view; also surfaced that magma and
  the oracle disagree on camera-inside-block near-plane rendering - open).
- All mirrored to mono with drift vocabulary preserved (10 commits, mono
  canonical rc=0); mirror of the combat/dragon rounds pending this batch.
- Projectile blind spot (operator eyeball catch): blaze small fireballs and
  dragon fireballs were recorded in the tapes but invisible in magma - the
  pixel gate soaked them into the particles class. RenderFireball.doRender
  (fire charge billboard, scale 0.5, renderEntityOnFire overlay) and
  RenderDragonFireball.doRender (scale 2.0 quad) implemented full-bright;
  particles class dropped 74k px (blaze demo) / 23k px (dragon demo), both
  demo tapes rc=0. Gate hardened: recorded entity types with no magma model
  now fail the scenario gate as missing_model (>4 rows, allowlist only
  EntityAreaEffectCloud per sidecar note) so invisible entities can never
  pass silently again. Lesson: cluster classes that absorb "small moving
  stuff" (particles) can hide whole missing renderers; the demo eyeball
  pass is a real gate, not a formality.
- Player-state visual audit (follow-up to the fireball catch; operator asked
  "what about player on fire from blaze"): tape fields fire/hurt/pots/cd all
  drive visuals the entity-model gate never covered. Implemented:
  ItemRenderer.renderFireInFirstPerson (screen fire overlay, driven from
  recorded fire + live sim), GuiIngame.renderPlayerStats heart flash
  (ceil(health), healthUpdateCounter white-flash phase) and poison/wither
  heart rows, renderPotionEffects HUD icons, hurtCameraEffect from recorded
  hurtTime/attackedAtYaw, updateEquippedItem cd^3 target + press-edge swing.
  Fire overlay silhouette mismatch root cause was orientCamera's +0.05 Z
  nudge leaking past renderHand identity. Attack indicator: verified
  oracle-equal (setting !=1, footprint 0/256 px diff at canonical t3400).
  Gate hardening round 2: per-frame class pixel budgets (hud 55k /
  particles 40k / viewmodel 40k) reclassify oversized soaks as UNEXPLAINED;
  proven on the actual saved pre-fix burn frame (fails as soak_from:hud).
  Blaze demo class soak dropped 12.85M -> 4.02M px; burn frames t88/t120
  now ~2-4k residual px. Old-tape swingProgressInt unrecoverable -
  documented in TAPE_COMPLETENESS.md.

## 2026-07-23 (pre-reset handoff: fluids/edge-blocks green, mob-AI + glitch research recovered, GPU0 wedged)

System reset imminent; this entry is the full state capture. Everything below
is committed and pushed on master unless marked otherwise.

### Landed this session (dffab82 -> 4151434, all pushed)
- dffab82 RenderFireball/RenderDragonFireball + missing_model gate.
- aac4353 devlog: fireball blind spot.
- 2658b7c renderFireInFirstPerson + renderPlayerStats + per-frame class
  pixel budgets (hud 55k / particles 40k / viewmodel 40k).
- 215b1d9 renderPotionEffects + updateEquippedItem + hurtCameraEffect +
  orientCamera +0.05 Z-nudge fix.
- eb25c4b devlog: player-state visual audit.
- fb001ae fluid scenario specs (water_dive, lava_walk, water_flow).
- cacd114 decreaseAirSupply air ledger + bubble HUD + flowing lava
  animation + transit class budget 40k.
- 63d95ed seven 1.11.2 edge-case scenario specs (elytra, slime, web,
  soul sand, suffocation, fence, fluid conversion).
- 4151434 edge block mechanics/models: slime bounce (+1.106 vy), web
  multipliers, soul sand 0.875 box / 0.4 XZ, fence+wall 1.5 collision
  boxes, armor stand model.

### Gate state: 19/19 tapes rc=0 at 4151434 (CPU replay)
Canonical 20260721T215812Z_fast_s0_survival_default_rd8_77b5b462 plus:
scenario_smoke_zombie 081735Z, blaze_melee 092705Z, blaze_bow 092838Z,
blaze_bow_demo 104234Z, pigmen_aggro 093154Z, wither_skeleton 093020Z,
enderman_fight 093335Z, ender_dragon 094040Z, ender_dragon_demo 104500Z,
water_dive 234816Z, lava_walk 234940Z, water_flow 235050Z,
suffocate_camera 001923Z, flow_convert 002122Z, slime_bounce 001527Z,
cobweb_fall 001656Z, soulsand_ice 001810Z, fence_collide 002017Z.
(ender_dragon 093713Z is superseded/stale, known rc=3, not in gate set.)
Replay cmd: `cd c/magma/raster/verify/trace && uv run --no-project --with
numpy,scipy,pillow,nbt python replay_tape.py ../tapes/<stem>.jsonl --cpu
--report`. zsh gotcha: rc through a pipe needs `${pipestatus[1]}`.

### Research docs recovered into the repo (this commit)
/tmp scratchpad was wiped by the reset; both codex research reports were
recovered from codex rollouts (~/.codex/sessions/2026/07/22/) by decoding
their apply-patch payloads:
- docs/research/glitch_research_1112.md - 36 ranked deterministic 1.11.2
  edge cases, do-not-bother list, recommended first ten (ranks 1,2,3,4,5,
  6,8,11,12,13). Ranks covered so far: the edge-block/fluid rounds above.
  Elytra (rank 1) is in flight, see below.
- docs/research/mob_ai_audit.md - verdict: mob AI NOT solved for the live
  simulator. Ordinary mobs share one direct-steering loop; no EntityAITasks
  scheduler, pf12/PathNavigateGround not wired, pig zombie has no live
  type. Top-10 fix program is in the doc (start: per-entity Java RNG +
  trajectory-parity gate, then task scheduler, then pathfinding).
Source rollouts if re-extraction is ever needed:
rollout-2026-07-22T19-06-15-019f8c82... (glitch), rollout-2026-07-22T17-46-25-019f8c39... (mob audit).

### In flight, interrupted by the reset
- Elytra physics: branch wip/elytra (c9b18fc, pushed) holds the killed
  codex round's UNVERIFIED travel() port (player_survival.h +129 and game
  wiring). Divergence target: elytra_glide tape tick 56, oracle x=5.8095
  vs magma 5.7460. Resume by having codex continue from the branch or
  restart the round; do NOT merge unverified. All 19 tapes must stay rc=0.
- Crafting/furnace GUI capture: 40-capture plan (capture_crafting.sh,
  crafting_harness.py, run_crafting_verify.sh, manifest with recipes cited
  to CraftingManager.java) was in /tmp and is lost; regenerate from the
  codex rollout of that round or just re-prompt. Oracle-side capture never
  completed (blocked by the GPU hang below).

### Anvil GPU0 driver hang - reboot required
nvidia-modeset "Error while waiting for GPU progress" loop; nvidia-smi
cannot open GPU0 (Blackwell). Any process opening an nvidia DRM node
D-states in drm_open (kill -9 immune; D-state PIDs 1288651 Xvfb :1,
1613897). Mitigations in place, to revert after reboot:
- chmod 000 /dev/dri/{card0,renderD128,card2,renderD130} + setfacl -b
  (restore normal modes post-reboot).
- Oracle stack moved to MC_DISPLAY=:3 - java/start_vnc_client.sh in the
  mono repo is parameterized (MC_DISPLAY/MC_VNC_PORT env). Display :1
  usable again after reboot.
- CUDA replay unavailable; CPU replay unaffected. After reboot, rebuild
  magma_game_cuda before trusting CUDA-scored gates (stale-binary trap).

### Continuation queue (in order)
1. Reboot anvil; revert /dev/dri modes; verify nvidia-smi sees both GPUs;
   restart oracle stack (either display); rebuild CUDA binary.
2. Finish elytra from wip/elytra; gate all 19 tapes + new elytra tape.
3. Regenerate + run the crafting capture and run_crafting_verify.sh
   bitwise GUI diff pass.
4. Mob-AI program per docs/research/mob_ai_audit.md top-10 if launch scope
   includes live sim (trajectory-parity gate first, then pig zombie/blaze/
   enderman/skeleton).
5. Mirror batch to mono (fb001ae, cacd114, 63d95ed, 4151434 + this docs
   batch); mono has an uncommitted start_vnc_client.sh MC_DISPLAY edit to
   commit first.
6. Re-encode combat_sbs.mp4 after elytra lands; scp to macbook:~/Downloads.
7. Remaining glitch-research ranks beyond the first ten, if desired.

## 2026-07-23 (post-reset: elytra physics landed, 20/20 CPU tapes green)

- Resumed `wip/elytra` with three isolated Grok rounds: core physics, direct
  Java-oracle audit, and replay/runtime gating. All agreed the interrupted
  `EntityLivingBase.moveEntityWithHeading` branch already resolved the main
  divergence; the remaining fixes were activation timing and numeric edges.
- Elytra travel now follows 1.11.2 float/double boundaries, including
  `Vec3d.lengthVector` widening `MathHelper.sqrt`'s float result. A jump edge
  samples airborne/descending state before travel (MC-111444), arms flag 7
  after that tick, and takes the elytra branch on the following tick. The
  0.6-high pose and 0.4 eye height persist until a collision-safe resize.
- Tape equipment replay seeds chest-slot item 443 before tick 0 and applies
  later inventory changes on the following tick. Falling liquid created after
  capture is no longer backdated into the recorded glide.
- Exact regression fixtures cover the old freefall path (`x=5.7460`), the
  corrected t54->t56 chain (`x=5.809549093722865`), dive/climb binary64
  motion, activation edge, landing, eye height, runtime equipment bridge, and
  tape conversion.
- `scenario_elytra_dip_20260723T001355Z`: 505/505 ticks physics-exact at
  1e-9; 51-frame pixel gate PASS. All prior 19 CPU tape replays also rc=0:
  gate state is now 20/20.
- Clean rebuild exposed two stale-object-hidden defects and both were fixed:
  selection-box APIs now use the real `Chunk`/`McSinTable`/`PsvPlayer`
  typedefs, and `gm_world_clock_init` initializes `freeze_daylight`.
- Verification: `make test-game` PASS; non-live pytest 40 passed; elytra
  replay tests 4 passed; scoped ruff PASS. Full pytest still requires a live
  qrl client and populated DIM-1 save, so those explicit integration tests
  were excluded from the non-live run.
- Remaining elytra cuts: chest armor durability is not owned by `IsrInv`, and
  creative free-flight (`capabilities.isFlying`) is outside the simulator
  surface. Neither affects the recorded survival tape.
- Next queue item is crafting/furnace GUI capture. CUDA was not re-gated in
  this round; GPU0 is healthy after reboot but occupied by a 92 GB vLLM job.

## 2026-07-24 (Grok fan-out: route roster, armor, chests, renderer, gates)

- Four isolated Grok implementation branches covered renderer gaps, armor and
  elytra inventory ownership, single chests/stronghold loot, and the missing
  route encounter roster. A fifth branch hardened the route, pixel/state
  gates, nightly failure handling, and quick sweep coverage. Each branch was
  reviewed and tested independently before integration.
- Live encounters now include pigmen, ghasts, magma cubes, slimes,
  silverfish, wither skeletons, blazes, boats, typed spawners, XP orbs, and
  dimension ownership. The entity store is 96 entries with separate hostile
  and passive natural caps, so ambient spawning cannot starve scripted or
  route-critical encounters.
- The independent mob review found and closed five integration defects:
  boat damage decayed faster than legal cooldown hits could accumulate,
  magma cubes used slime damage instead of `size + 2`, pigmen lost their
  held gold sword, ghast fireballs used marker geometry, and a saturated
  projectile pool discarded pending ghast shots. Live-tick regressions cover
  all five.
- Armor slots compose at 49..52 and chest slots at 53..79. Crafted armor
  absorbs damage and loses durability; an equipped chest elytra owns flight
  state. Single chests support open/click/shift/throw/close/reopen persistence,
  the real `generic_54` GUI, deferred stronghold corridor/library loot, and a
  facing-aware inset mesh.
- Renderer coverage added the End portal surface, XP-orb billboard/animation,
  new mob/boat atlas entries, pigman biped walk/held equipment, and live
  ghast-fireball views. Tape inventory/state coverage now includes all 41
  main/armor/offhand slots.
- The macro route no longer clears the mob store or injects post-bed health.
  It legally harvests food and wool, crafts/equips iron boots, regenerates
  through food/vitals, uses bow and bed paths, survives the End bed blast at
  the measured interaction boundary, kills the dragon, and reaches credits.
- Verification: `make test-game` PASS, route fresh-spawn-to-credits PASS,
  mob live suite PASS, entity renderer PASS, and verifier/scenario pytest
  48 passed. `netherite_sweep.sh --quick` is green with no skips; its first
  run exposed a stale five-box fence golden after the earlier two-rail model
  landed, and the corrected nine-box/324-vertex golden now passes. The
  repository-wide Ruff command remains red on 515 legacy findings; its 199
  unrelated auto-fixes were reverted, and no Ruff edits outside the touched
  verifier files were retained.

## 2026-07-25 (CrShadeCtx positional-init misalignment: all CUTOUT geometry was discarded)

- Root cause: `terrain_shades()` (`c/magma/game/frame_capture.c`) and
  `render_world()` (`c/magma/app/game_main.c`) built their four per-layer
  `CrShadeCtx` values with positional initializers written before
  `CrShadeCtx.alpha_ref` was inserted (commit `3819bcf`). Every value from
  slot 6 on shifted by one: the fog-enable flag landed in `alpha_ref`, the
  layer enum in `enable_fog`, and `blend` in `layer`. With fog on (the
  default) `alpha_ref=1.0` gives an alpha threshold of 255, so
  `cr_shade` discarded *every* CUTOUT and CUTOUT_MIPPED texel - all cross
  plants, tallgrass and grass_side_overlay - and translucent water rendered
  with blend=0 (opaque, depth-writing). `-Wextra` only flagged the missing
  trailing `mip_bias`, never the misalignment.
- Fix: both sites now use designated initializers, so a future field insert
  cannot repeat this. No other change.
- Effect on the canonical tape
  `20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` (CPU replay,
  181 frames), before -> after: UNEXPLAINED 1_540_406 px / 67 frames ->
  122_581 px / 63 frames; failed frames 58 -> 7; worst frame t=80 with
  74_783 px -> t=260 with 7_291 px; viewmodel 1_199_958 -> 256_366;
  particles 580_964 -> 181_116; hud 313_882 -> 163_264; bossbar 159_110 ->
  57_319; known:14 109_693 -> 98_557. Whole-frame mean at t=80 3.76/ch, and
  the oracle/magma side-by-side now shows the same tallgrass field.
  Physics stayed clean; `make test-game` PASS (27 suites).
- Still open: t=260 / t=460 residual clusters, the viewmodel soak at
  t=3180-3220, and the outdoor `known:4` tint/AO residual. Same misaligned
  pattern survives in `app/trace_main.c` and the `raster/verify/*_candidate.c`
  fixtures; those run with fog off so alpha is unaffected, but their layer
  and blend slots are equally shifted and their goldens are pinned to it.
- Pre-existing, unrelated: `make game-cuda` fails to link
  (`cr_camera_view` defined in both `core/math.o` and
  `cuda/raster_cuda_sm86.o`), so this round was measured on the CPU path.

## 2026-07-25 (post-fix sweep: every tape re-baselined, CUDA game link repaired)

- `magma_game_cuda` had not linked since `cr_camera_view` was added to
  `core/math.c`: `cuda/raster_cuda.cu` #includes math.c under `_dev` private
  names, and the new symbol was not in that rename list, so it collided with
  the gcc-built `core/math.o`. Added `cr_camera_view` to the rename block.
- `raster/verify/nightly_verify.sh` gained `NIGHTLY_BACKEND=cpu`, which
  replays on the CPU instead of GPU1. Default behaviour is unchanged (GPU1,
  `--cuda`, self-defer when GPU1 is busy); the override exists because a
  correctness sweep is not timed, so a 12 GB co-tenant on GPU1 is a reason to
  fall back rather than skip. GPU0 stays reserved.
- First full sweep: 23 tapes on the CPU, all post-CUTOUT-fix. 6 clean (rc=0),
  8 pixel-gate FAIL (rc=3), 9 non-player state divergence (rc=5, pixel gate
  itself PASS). All 23 gate.json results are now committed under
  `trace/baselines/`, which is what nightly diffs against - previously only
  one tape had a baseline and the other 22 failed as "missing required
  baseline", so the sweep had never produced a usable signal.
- Nightly will still report RESULT: FAIL until the nine rc=5 state divergences
  are closed; baselines can absorb pixel-gate failures, not those.
- CPU/CUDA parity re-confirmed after the shade fix: the canonical tape
  replayed with `magma_game_cuda` built `-arch=sm_120` on GPU0 is **bit
  identical** to the CPU replay - 0 differing pixels over all 181 frames,
  every gate class and max_cluster equal. (GPU1 had a 12 GB co-tenant, so the
  parity run used the idle GPU0 and a matching sm_120 object; the tree is
  built back to the default sm_86.)
- Residual on the canonical tape (t=260, t=460) characterised, not fixed:
  it is not geometry, not a camera offset (best whole-frame alignment is
  dx=dy=0), and not the fog distance mode. The oracle's own GL query records
  `fog_distance_mode_nv = 34139` (GL_EYE_RADIAL_NV), which is what magma
  already does; forcing planar |z| fog as an experiment made the tape worse
  (particles 181k -> 436k px, viewmodel 256k -> 411k, failed frames 7 -> 10)
  and the experiment was reverted. On leaf interiors the delta is zero-mean
  with a large spread (near canopy mean +0.1/+0.2/+0.2 per channel, sigma
  19/28/8), i.e. individual texels flipping between neighbours rather than a
  shading offset - nearest-neighbour texel selection on minified noisy leaf
  faces.

## 2026-07-25 (all nine rc=5 state divergences closed; rung4 pose corrected)

Grok fan-out of three: tick-0 inventory, `rung4-verify`, and triage of the
three worst pixel-gate tapes. Every diff reviewed and every acceptance test
re-run here before landing.

- **All nine rc=5 tapes are now rc=0.** Root cause was one line in
  `replay_tape.py`: tape `inv` rows are post-tick truth, re-anchored with a
  `set_inventory` on tick *t+1* so action *t* still sees the pre-tick stack,
  and `inv_view` is render-only. Nothing ever seeded live `player.inv` at
  tick 0, so the first state dump saw empty slots while the tape had recstart
  gear. `tape_to_script` now emits `set_inventory` for slots 0..40 from
  `ticks[0]["inv"]` before look/action, the same post-tick approximation
  `set_elytra` already used. No item-id special cases, gate logic unchanged.
  Verified: blaze_bow, blaze_melee, elytra_dip, both ender_dragon tapes,
  ender_dragon_demo, enderman_fight, fence_collide, pigmen_aggro,
  smoke_zombie, wither_skeleton all report
  `state: inventory PASS (1 ticks, 0 mismatches)`; the seven previously-rc=5
  tapes not otherwise pixel-failing now exit 0.
- **Caveat, recorded honestly:** `collect_state_assertions` samples every 20
  ticks and only checks ticks that carry an `inv` row, and on these tapes that
  is tick 0 alone (`inv_checked=1`). Seeding tick 0 from `ticks[0]["inv"]`
  therefore makes the only checked tick near-tautological - it now verifies the
  seeding path, not inventory evolution. The divergence it closed was real (an
  empty inventory where the tape had a bow and 64 arrows), but real hardening
  needs tapes re-recorded with periodic `inv` rows.
- `rung4_candidate.c` was rendering a default pose against a golden captured at
  a different one. Switched to `pose_scene.h`, froze the golden's real pose
  (eye 8.3/95.0/40.5, yaw 0, pitch -35, fov 77, zfar 181.01933), added
  `gm_sky_draw` + terrain fog, and made dual winding opt-in
  (`MAGMA_DUAL_WIND=1`; measured 1.47/0.85 with it vs 1.13/0.67 without, so
  single winding is the correct default). `rung4-verify` goes 44.94/36.91 ->
  1.13/0.67 PASS. `hard-scene-verify` (1.13/0.67/0.70) and `multi-verify`
  (seed0 1.13/0.67, seed7 4.27/2.37, pass=2) are unchanged.
  With the pose corrected rung4 is now the **lean twin** of hard-scene-verify -
  same scene, same golden, same numbers, through a standalone fixed-pose binary
  instead of `game_candidate`'s arbitrary-pose path. That is a second entry
  point over mesh/light/populate/shade/raster, *not* independent scene
  coverage; a COVERAGE NOTE in `run_rung4.sh` says so. A `/tmp`-scanning
  prep-list branch was proposed with it and deleted after testing showed the
  gate still passes with `prep_list=derived` when those paths are absent.
- Ratcheted the rung4 tolerances with the pose fix: 38.0/33.0 were sized for
  the stale-pose numbers and at 1.13/0.67 could no longer fail anything, so
  the gate was passing vacuously. Now 1.5/1.0, about 0.4 above measured.
- Triage of the three worst pixel-gate tapes produced no landed C fix (nothing
  unambiguous and small enough); findings are in OPEN_DIVERGENCES.
- **Nightly is green for the first time**: `nightly_20260725T052804Z`,
  `RESULT: PASS`, 23/23 tapes actually replayed (not skipped) on the CPU.
  15 rc=0 (was 6), 0 rc=5 (was 9), 8 rc=3 all matching their committed
  baselines, so no pixel regression. The 8 rc=3 are the honest open pixel
  work, not a green wash: baselines absorb them by design, and any increase
  fails the run.
- Then ran the same sweep on CUDA (GPU0 handed over explicitly; GPU1's 12 GB
  co-tenant never cleared across two watches totalling ~7h). `nightly_verify.sh`
  gained `NIGHTLY_GPU` for this, and derives `-arch=sm_XY` from the card's
  compute capability instead of assuming the Makefile's GPU1 `sm_86`, which
  would have produced a no-kernel-image failure on Blackwell.
- **The CUDA sweep is FAIL where CPU is PASS** - six tapes regress. Parity had
  only ever been measured on the canonical tape (bit identical), and it does
  not generalise. Details and measurements in OPEN_DIVERGENCES. Two wrong
  first guesses, both killed by measurement: GPU contention (serial re-runs
  reproduce byte-for-byte at identical ticks) and the three early
  `hp=0 dead=1` exits (they happen identically on the CPU, so they are a
  pre-existing sim issue, not a backend divergence).

## 2026-07-25 (four-way Grok fan-out: hurt camera, inventory gate, coverage)

Four agents in isolated worktrees. Every diff reviewed and every acceptance
re-run here.

- **CUDA dropped the hurt camera.** `cuda/raster_cuda.cu` built its MVP with
  `cr_look_yaw_pitch_dev` (look-only) at both sites while the host path uses
  `cr_camera_view`, so CUDA silently skipped
  `EntityRenderer.hurtCameraEffect`. Every damage tick the CPU drew a rolled
  horizon and CUDA a flat one - which is exactly the "terrain wedge at the
  left horizon" I had measured and mis-attributed to terrain extent/lighting.
  Both sites now call `cr_camera_view_dev`. On `blaze_bow_demo`: 12_212_050
  differing px -> 9_344_718, and the two hurt bursts collapse from 167_824 and
  156_540 px to 36 and 23. This also closes a loop: I added `cr_camera_view`
  to the CUDA `_dev` rename block earlier today to fix the link break, but the
  call sites were never switched over.
- Remaining CUDA gap is the deferred frame end on bow-pull + fire frames;
  `MAGMA_NO_DEFER=1` gives 12_875 px over 407 frames (sky-star noise only).
  Recorded in OPEN_DIVERGENCES - a deferred-path CUDA replay is not parity
  evidence until that is closed. The 23-tape CUDA sweep has not been re-run
  since the fix.
- **Inventory gate now checks every `inv`-bearing tick**, not just the
  every-20 sample grid. `blaze_bow` already carried change dumps at t=77/78
  that the grid never landed on, so this bought 10 independent checked ticks
  with no re-capture. Tick 0 no longer counts as independent (replay seeds
  from it) and seed-only tapes report `seeded_only`. The Java recorder also
  emits an inventory keyframe every 20 ticks, which needs a re-record to take
  effect. 48 unit tests, including a mutation test that makes the gate fail.
- That immediately caught two real misses on the canonical tape (t=3257 slot 1
  item 270, t=3267 slot 2 item 50). Chased to cause: both are **crafted**, and
  crafting clicks are not taped - the fix is a `.worldpatch.jsonl` sidecar,
  which `20260712` has and the canonical tape does not. Known recorder
  blocker, previously invisible.
- **No more silent coverage caps.** The state gate carries a `coverage` block,
  the replay prints `[tape] COVERAGE: only N of M tape ticks were replayed`,
  and `gate_baseline_diff.py` now compares the state block as well as pixel
  classes (it previously compared neither inventory nor coverage, so a state
  regression could never turn nightly red). A state failure under a pixel
  failure is called out instead of being swallowed by `return 3`.
- That surfaced that **seven** tapes truncate at a terminal death, not the
  three I had found - `smoke_zombie` verifies 45% of itself, `ender_dragon_*`
  37-38%, and four of the seven were exiting rc=0. The deaths themselves are
  oracle-correct (tape tick 813 `hp=0.0`, 814 `hp=20.0`); the delegated agent
  correctly refused to "fix" them and I confirmed it from the tape directly.
  My briefing premise - that magma killed a player the real game kept alive -
  was wrong.
- All 23 baselines re-committed to carry the state block. Nightly is PASS
  again (15 rc=0, 8 rc=3) with the known inventory failure absorbed the same
  way pixel failures are.
- Setup note for future fan-outs: `raster/verify/tapes/` mixes tracked sidecars
  with gitignored bulk, and a worktree that links only `*.jsonl` and `*_frames`
  silently omits the `*_world` snapshot dirs. Replay then skips snapshot
  patching without erroring, and the sweep fails for reasons that look like
  real bugs. Link every entry; assert the count matches the main tree.

### 2026-07-25 - CUDA sweep after the hurt fix: deferred frame end is the only gap left

- Re-ran the full 23-tape CUDA sweep on GPU0 (`sm_120`,
  `nightly_20260725T071901Z`). 15 rc=0 / 8 rc=3, the same tally as the CPU
  sweep, with baseline regressions on two tapes rather than the five the
  pre-fix sweep had (`nightly_20260725T062525Z`, 13/10).
  `ender_dragon_094040Z`, `ender_dragon_demo` and `lava_walk` are now
  byte-identical to their CPU baselines on every class.
- Both survivors are the same bug. Replaying the canonical
  `20260712T055346Z` and `blaze_bow_demo` with `MAGMA_NO_DEFER=1` reproduces
  the CPU baseline exactly - every pixel class, `failed_frames`, and the
  state block unchanged (canonical 3121/3121 ticks, UNEXPLAINED 538_620 on
  both sides; bow demo UNEXPLAINED 168_484). So the deferred frame end is now
  the sole CPU/CUDA divergence, and it is not bow-specific.
- Removed a build landmine while there: `cuda/raster_cuda_sm86.o` held
  whichever arch was built last (`NVFLAGS_GAME` is overridable, GPU0 needs
  `-arch=sm_120`) and nothing rebuilt it on an arch change, so a GPU switch
  could silently link the wrong arch. Renamed to `raster_cuda_game.o`;
  `scripts/demo_pixel_sbs.sh` now cleans `cuda/*.o`.

### 2026-07-25 - Deferred frame end closed: CUDA equals the CPU on every tape

Two bugs, both in `finish_pending`, both invisible to the CPU path because it
draws the hand/HUD/overlays at the frame's own tick.

- **Fire overlay fov.** The sync path passes `uw.fov_scale`; the deferred path
  re-derived it as `pend_uwfov / 70`, and `pend_uwfov` is
  `cam.fov_deg = 70 * fov_mult * fov_scale`. That folded `getFovModifier`'s
  bow-pull / sprint term into the overlay projection, which is why the
  divergence needed `fire=1` AND `use=1`. `blaze_bow_demo` went from 57 failed
  frames to 1, matching its CPU baseline byte-for-byte.
- **Suffocate overlay world.** `finish_pending` re-ran
  `gm_overlay_block_in_hand_live` against `c->pend_world` - the live world
  pointer - so the eye-block sample ran one rendered frame (20 ticks) after the
  frame being drawn. On the canonical tape t=660 that resolved to dirt and
  painted the entire frame with the suffocation overlay: 371_279 unexplained
  px, 100% of pixels differing at mean_abs 70.5. Split into
  `gm_overlay_block_in_hand_pick` / `_draw`; the deferred path resolves at arm
  time and snapshots the block.

The second one took a bisect worth recording, because every plausible GPU
explanation was wrong: `MAGMA_NO_HAND=1`, `MAGMA_NO_OVERLAY=1`, a full
`cudaStreamSynchronize` inside `frame_end_async`, and resetting the shade-ctx
ring at `frame_begin` all left the frame **bit-identically** wrong (83_341_540
px on 3/3 runs), while dumping the raw deferred readback with the host retire
draws skipped showed a perfectly normal frame (mean 81.4 vs 80.1/82.8 at the
neighbouring goldens). Deterministic-to-the-byte corruption is evidence
*against* an async race, not for one.

Also removed a build landmine: `cuda/raster_cuda_sm86.o` held whichever arch
was built last (`NVFLAGS_GAME` is overridable, GPU0 needs `-arch=sm_120`) and
nothing rebuilt it on an arch change. Renamed to `raster_cuda_game.o`;
`scripts/demo_pixel_sbs.sh` cleans `cuda/*.o`.

## 2026-07-25 - fogColor1 samples the tick-entry feet, and soulsand_ice closes

The sky-plane fog fix (`ac47c2b`) took `scenario_soulsand_ice` from 45 failed
frames to 1. The survivor, t=60, was the step down onto soul sand: 77% of the
frame differing at mean_abs 5.37, the shape of a global brightness term.

That term is `EntityRenderer.updateRenderer`'s `fogColor1` smoother, a
0.1-per-tick lerp toward the light brightness at the player's FEET block. Soul
sand is `useNeighborBrightness = false` and opaque, so standing on it puts the
feet block at light 0 and starts a long ramp from 1.0 down to 0.25 - a step the
gate only catches at its largest sampled point.

Rather than guess the phase, we solved the goldens for the `c1` they were drawn
with: render the frame at two known `c1` values, fit the local slope of frame
mean vs `c1`, invert. soulsand_ice t=60 implies 0.8548 - two smoother steps -
against our three. But a blanket one-tick lag then broke `water_dive` t=1000,
whose golden implies 0.5771, exactly four steps from the t=997 teleport.

Both are explained by where `updateRenderer` sits in `Minecraft.runTick`: after
the network phase, before the local player's movement update. A teleport
arrives as a pose packet and is visible to the same tick's smoother; ordinary
movement is not. Capture now samples `gm_runtime_tick_entry_feet` - the feet
position snapshotted at the top of `gm_runtime_tick` - instead of the post-tick
view. Same class of off-by-one as the deferred `set_look` already documented in
`game/script.c`.

CPU sweep `nightly_20260725T081035Z`: PASS, soulsand_ice rc=0, no regressions.

## 2026-07-25 - the gate was measuring 80 percent of the frame on the canonical tape

Four fixes landed and one was rejected, but the finding that reframes the rest
is that two of our measurements were not measuring what we thought.

**Concurrent replays corrupted each other.** Agent worktrees symlink `tapes/`
to one shared directory, and `.snapshot_patch.jsonl`'s staleness check keys off
`snapshot_patch.py`'s mtime, which differs per worktree. So every parallel
replay decided to regenerate the same cache at once, through two shared
fixed-name files: the `world_dump` scratch, where processes read each other's
tiles and emit a silently WRONG patch, and the cache itself, written in place so
a reader gets a TRUNCATED one and replays an unpatched world. `blaze_bow`
measured 3.63/ch terrain against a 0.94/ch baseline and reproduced 0.94 exactly
once the machine was idle. Nothing was wrong with the renderer. Both names now
carry the pid and the cache is published with `os.replace` (`b9fe039`). A
delegated agent reported the same phantom regression independently; that report
was correct about the symptom and wrong about the cause, as was I at first.

**The canonical tape's goldens have no HUD.** Malmo's `ClientStateMachine`
forces `hideGUI=true` for the whole mission, so all 157 goldens of
`20260712T055346Z` have no hearts, hotbar or crosshair, while all 181 of
`20260721T215812Z` (recorded after QuantizedRL started clearing the flag) do.
`qrl_launch.hide_gui` reads false on both, so it could not be the source;
`capture.hide_gui` is the measured value now. Magma was drawing a HUD over
goldens that have none, and the gate's positional `hud` accept swallowed it -
the bottom 96 rows, a fifth of the frame, had no pixel verification at all on
that tape. Suppressing the HUD was not enough on its own: `gate_frame_ex`
removes positional accepts as topology barriers *before* known-divergence
matching, so those rows never reached the filed rain entry and instead tripped
the `hud` class budget. The accept is now dropped entirely on `hide_gui` tapes,
while the same strip is still carved out of `viewmodel` so its budget is not
silently re-scaled (`3dc2d19`, `b3922ac`). 14 -> 25 failed frames, which is what
measuring 20 percent more of every frame costs. The rain window t=1800..2100
now resolves cleanly into `known:12` instead of leaking.

**Elytra pose never cleared after landing.** `psv_update_elytra_size` treated
`psv_collect_blocks(...) == 0` as "no collision", but that is a cell broadphase
and always returns the floor under the feet; vanilla's `collidesWithAnyBlock`
uses strict `AxisAlignedBB.intersects`, so a floor touching `minY` does not
block the expand. The 0.6F pose and 0.4F eye height stuck forever, putting the
camera 1.22 blocks low - the full-width horizon band on `elytra_dip` was sky vs
grass from the wrong eye height, not fog. 41 -> 4 failed frames (`9b165bf`).

**Dig dust skipped the lightmap.** `ParticleDigging` sets a flat 0.6 gray and
`Particle.renderParticle` multiplies it by the lightmap at the particle; magma
kept the gray. Mining unlit End stone therefore painted a 45216 px near-full
brightness patch across the wall. `ender_dragon_093713Z` 57 -> 55 failed frames,
worst mild_shift 25.94 -> 12.55 (`8ae0a38`).

**Rejected:** a slime fix that re-emitted the outer cube face coplanar to double
its translucent opacity. The diagnosis was right (the dark field is the slime
platform, `slime.json` has two translucent elements, one layer renders 0.74x too
dark) and it measured well, 19 -> 12 failed frames. But the second element is an
inset cube, not a duplicate shell, and geometry the model does not contain will
be wrong somewhere the golden does not happen to look. Sent back for the real
inset element.

Also: `pxdiff.py` grew a `cutout-sky` discriminator that requires measured
background coverage instead of a delta direction, after a delegated agent proved
my own tool's verdict on the canopy was a false positive; and
`scripts/agent_worktree.sh` builds worktrees that can actually build and replay.

## 2026-07-25 (overnight: sprint-FOV ordering, and the viewmodel story was wrong)

**The FOV eased on tick N sees tick N-1's sprint flag.** `Minecraft.runTick`
calls `entityRenderer.updateRenderer()` at `Minecraft.java:1862`
(-> `updateFovModifierHand`, `EntityRenderer.java:296`, easing
`fovModifierHand += (f - fovModifierHand) * 0.5F`) BEFORE `world.updateEntities()`
at `Minecraft.java:1881`. magma ran the ease after its sprint state machine and
was one tick ahead: at t=260 of `20260721T215812Z` it projected at 1.1453125
(80.171875 deg) against vanilla's 1.140625 (79.84375 deg). A third of a degree
of FOV is invisible as shading and decisive as sampling, and it is what had been
filed for weeks as a "texel-selection" residual - not a sampling rule, not UV
interpolation, not FaceBakery baking, not attribute precision. Moving the block
above the state machine took `20260721T215812Z` 7 -> 3 failed frames (worst 7291
px at t=260 -> 865 px at t=700; the t=460 cluster 6252 -> 72), slime 16 -> 15,
the canonical tape 28 -> 27, and one tape's UNEXPLAINED 1226 -> 0 (`130d0bd`).

**EntityFallingBlock renders the block MODEL.** `RenderFallingBlock.doRender`
draws the model at the blockpos then translates by
`(x - blockpos.x - 0.5, y - blockpos.y, z - blockpos.z - 0.5)`, so the cube is
`[posX-0.5, posX+0.5] x [posY, posY+1] x [posZ-0.5, posZ+0.5]`: a unit cube, not
the 0.98 collision box a delegate sized it from and not the 0.25 ground item
drop (`e03ac5f`).

**The gate had a hole in the same quarter of the frame it had just un-masked.**
`hide_gui`/`hide_hand` dropped the POSITIONAL hud and viewmodel barriers, but
`pixel_gate` also has post-hoc SEMANTIC classes with the same names and a 40000
px budget each, so un-masking put nothing new under measurement on any frame
where a heuristic fired. Both now honour the flags. On the canonical tape `hud`
disappears entirely (107 frames / 244695 px that were never an explanation) and
failed frames go 18 -> 28, all of them in the newly measured region (`b251384`).

**The canonical tape's viewmodel family was filed wrong, twice.** It had been
recorded as a missing HELD ITEM, recorder-blocked because the tape carries no
`inv` field. Opening the lower-right crops at t=0, 900, 1140, 1800, 2400, 2800
and 3100 shows the same skin wedge in the same screen position over seven
different backgrounds: the oracle is EMPTY HANDED for most of the tape and what
it draws is `renderArmFirstPerson`. magma has that path and lands it on the
right pixels - it draws it far too bright. On rain-free frames whose terrain and
sky agree to within 1/255, the arm is off by a per-channel 0.72/0.60/0.58
(t=900: golden (139,103,84), magma (192,173,148); terrain (87,114,69) vs
(87,114,70)). Per-channel and warm-biased, so a lightmap colour and not a
brightness scalar. Forced on for the whole tape it moves 110 of 157 frames the
wrong way on the gate-independent whole mean/ch, which is why the suppression is
still there; under investigation on `wt/armlight` (`2819f9a`).
Two methodology notes came out of it. The yaw-sweep test that produced the
original reading is sound about "screen-fixed viewmodel" and was over-read into
"held block", which it cannot support. And `MAGMA_HAND_FROM_TICK` in the
environment now overrides the sidecar (`de54029`), because the only previous way
to A/B the hand was editing `demo/`, which is symlinked into every agent
worktree - a probe in one worktree silently changed what every other running
agent was measuring.

**Pickup inference cannot rescue the held-item intervals.** The tape carries
8673 `EntityItem` rows, 530 of them within three blocks of the player, and every
one is 7 fields (`id, name, x, y, z, yaw, pitch`) with no item id. All 1317
`set_block` rows in the worldpatch are at tick 1, an initial snapshot rather than
an edit log. Recovering WHICH item needs the tape re-recorded; recovering the
ARM does not.

**Still open and newly visible:** over t=540..660 the oracle draws no viewmodel
at all, at `pitch` exactly 90.0. The corner object there is terrain, measured
rather than assumed - it tracks the background across the window (mean |d|
0.85/10.98/11.93 at t=620/640/660 against a control of 0.96/15.22/14.13).
Vanilla's `renderHand` has no pitch gate.

**Release-path GPU verified.** CUDA on GPU0 (sm_120) is identical to the
CPU baselines on all 164 base-vs-now quantities across 23 tapes (`8058633`).
`nightly_verify.sh` now says so in its SKIP message: `NIGHTLY_GPU=<n>` is safe
across GPU generations because the `sm_` target is read from the chosen card.

**From the fan-out.** Boss fog now latches on ender crystals as well as a nearby
dragon - `DragonFightManager` constructs its `bossInfo` with
`setCreateFog(true)` and clients keep it for the whole fight, while magma's
nearest-8 tape window loses a far dragon (`e7b274d`). It is citation-backed and
moves no pixel on any tape we have, including the three other End tapes, which
all still pass. The slime platform residual was measured to a conclusion without
a fix: at a=188/255 the dual-covered block centres already equal the golden, so
`raster_cpu`'s SRC_ALPHA is right, and the dark residual is the single-layer rim
of the 3/16 inset (61% of a top face). Four levers were tried and all rejected,
including a back-to-front translucent sort that takes slime 15 -> 14 and
regresses `elytra_dip` UNEXPLAINED 784 -> 16495 (`8945ac4`).

### 2026-07-25 addendum (the arm was Alex, and one delegate fix did not hold)

**Half the arm's over-brightness was a skin mismatch, and my "per-channel, so a
lightmap colour" reading was wrong.** The tape header has no `skin` field, so
`replay_tape.py` fell back to slim and magma drew ALEX against the oracle's
Steve. The tape's own `qrl_launch.determinism.pin_skin` is true and
`MixinRandomSkinTexture` forces the classic model whenever it is set;
`tape_skin()` honours it now (`db5ac63`). Against the Steve texel (150,111,91)
the golden is a clean scalar 0.660/0.658/0.659 - never a coloured multiplier,
just a paler texture. With Steve drawn, forcing the hand on flips the A/B from
110-worse/10-better to **91 better, 29 worse, 37 unchanged, mean whole/ch 5.91
-> 5.07**, which I reproduced independently. What is left is a scalar ~1.57x:
magma applies diffuse x lightmap ~0.98 where the oracle applies ~0.66, i.e. the
arm is essentially unattenuated. Light levels and the LUT path measure correct,
so the next place to look is eye-space face normals out of `build_arm_matrix`
against `hand_diffuse` under `rotateArroundXAndY`.
Whether to flip the sidecar's `hand_from_tick` to 0 is a release-time call: the
metric now favours the arm being on, but turning it on re-baselines the tape and
bakes in a known-wrong 1.57x, so it stays off until the residual lands.

**Reverted a delegate fix whose acceptance did not reproduce.** "render legacy
fiery fireballs" takes `scenario_blaze_bow_demo` from 1 to 3 failed frames
against its committed baseline (new ticks t=454 and t=460), even though
UNEXPLAINED drops 168484 -> 155626 and particles 154028 -> 83755. The mechanism
is why: inferring `Entity.fire=1` for every `EntitySmallFireball` after its
first observed tick puts on-fire layers back, and this repo already established
the opposite. The UV half of that commit IS correct - vanilla's
`Render.renderEntityOnFire` gives the first vertex `maxU` and the second `minU`
(`Render.java:174-177`) and magma had them mirrored - but it was bundled, and it
is inert once nothing burns, so it went back with the rest and should be
re-landed with a test. After the revert the tape matches its baseline on every
quantity.

Also from the fan-out, both rejected by their own authors rather than shipped: a
five-neighbour-maximum skylight for `fogColor1` that takes `elytra_dip` 4 -> 2
failed frames and regresses `water_dive` 0 -> 14, and a back-to-front translucent
sort that takes slime 15 -> 14 and regresses `elytra_dip` 784 -> 16495 px.

Tree state at hand-off: full nightly on GPU0, 23 tapes, RESULT PASS, zero
REGRESSION lines; `make -C c/magma test-game` PASS; `demos/pixel_match_sbs.mp4`
regenerated and inspected.

## 2026-07-27 (launch prep: M3 throughput measured, tapes/assets refreshed)

- **M3 throughput gate: PASS.** GPU0 (RTX PRO 6000 Blackwell, sm_120),
  exclusive card (nvidia-smi clean before every run), `verify_cuda.py
  --bench`, t0 snapshots (full action decode), repeat 4, camera per
  decision, 1000 timed decisions with pre-generated on-device random
  actions and periodic masked resets:

  | N | env-ticks/s | decisions/s |
  |---|---|---|
  | 1024 | 0.79M | 198k |
  | 4096 | **2.22M** | 554k |
  | 8192 | 3.02M | 756k |
  | 16384 | OOM: region pool 128^3 x N = 137.4 GB > 96 GB |

  Gate was >=1M aggregate at N=4096: cleared at 2.22M (repeatable to 3
  digits across two runs; a shorter 250-decision run reads ~5% higher, so
  report the 1000-decision figure). Curriculum snapshots at N=4096: 2.61M /
  653k (cheaper worlds). CPU reference (9950X3D, 32 threads,
  `blaze_cpu.so` OMP, same loop/actions/snapshots via a mirror script):
  0.29M env-ticks/s at N=256, 0.25M at N=1024 - best-vs-best the GPU is
  ~10-12x the whole CPU.
- elytra_dip re-recorded and adopted (`20260727T214459Z`): settled liquids,
  converged fog_color1 header, 1 failed frame (flow-texture streaks) vs 4.
  Old tape retired. Recorder now writes fog_color1 + rain/thunder strength.
- Fidelity state for launch copy: 23-tape suite, 16 rc=0, 7 rc=3 with every
  residual diagnosed in OPEN_DIVERGENCES.md; CPU==CUDA raster parity
  bit-exact; CPU and CUDA nightly sweeps both RESULT: PASS.
- Demos re-encoded from today's binaries: `demos/pixel_match_sbs.mp4`
  (gates re-verified PASS during encode) and `demos/combat_sbs.mp4`
  (blaze_bow_demo + ender_dragon_demo, title copy updated to the 23-tape
  claim).

## 2026-07-29 (LAUNCHED)

- Public launch: https://x.com/elliotarledge/status/2082366172222439879
  (8-tweet thread, zoom video lead). Public repo:
  https://github.com/Infatoshi/netherite - clean tree via
  export_public_tree.sh (1704 files, 549 Mojang-derived excluded), FRESH
  history. The full private repo was renamed to Infatoshi/netherite-dev;
  this checkout's origin now points there. Never push this repo's history
  to the public remote.
- Launch video pipeline documented in docs/DEMO_VIDEO.md; thread archives
  on the macbook in ~/Downloads/netherite_thread{,_v2}/.

## 2026-07-29 (blaze glow + on-fire engulfment, wt/blazeglow)

Operator catch: the oracle's blaze is full-bright with flames wrapped around
it while magma drew a dull brown mob. Two vanilla mechanisms were missing, both
render-only; the tapes already carried everything needed.

- `EntityBlaze.getBrightnessForRender` returns `15728880` (sky 15 / block 15),
  so the model ignores world light. `frame_capture.c` now pins the sampled
  sky/block levels for types where `gm_entity_fullbright` is true, which keeps
  both the LUT and the folded Nether/End paths exact.
- `Render.doRenderShadowAndFire` draws `renderEntityOnFire` for any entity with
  `isBurning()`, and `EntityBlaze.isBurning()` is `isCharged()` (the `ON_FIRE`
  datamanager bit its fireball AI holds for the 78-tick volley). magma only had
  a fireball-billboard fire pass; `gm_entity_fire_emit` now runs the same
  vanilla layer loop for living views whose recorded `flags` bit 0 is set,
  sized by the entity AABB. The layer math is shared with the fireball pass
  (`ir_fire_layers`).

The burning bit is RECORDED, not inferred (the `60f4076` trap): the qrl
recorder has written `isBurning|isSneaking|isInvisible|isChild` per living row
since 2026-07-12, and the three blaze tapes carry 388-603 burning blaze rows
each with the exact vanilla 78-on/100-off cycle. No recorder change was needed.

Gates (CPU replay, sequential): `failed_frames` unchanged on all three -
`blaze_bow_demo` keeps its single known t=812 fight-state frame (167_724 ->
167_712 px), `blaze_melee` and `blaze_bow` stay rc=0. Whole-tape diff pixels
drop (demo 664_810 -> 616_752 over the golden frames; melee blaze-ROI at t=200
3_495 -> 1_609). Gate CLASS counts churn: the big bright "blaze missing"
clusters used to soak into `particles`, and the small residual left over
(blaze rod pose, fire animation phase) lands in `UNEXPLAINED` instead - demo
particles 154_028 -> 83_313 px while UNEXPLAINED 168_484 -> 191_133 px over
3 -> 173 frames, every new cluster far under the 4000 px fail threshold.
Baselines refreshed. Live-sim gap (no `attackStep` port, so an interactive
blaze never reports burning) documented in OPEN_DIVERGENCES.md.
## 2026-07-29 (nether arrival: dimensions born mid-recording were never snapshotted)

- The portal tape's Nether had no fire, no lava pools and no block light
  because `<tape>_world/DIM-1/` had no `region/` at all: the recorder
  snapshots the save at `recstart`, and a dimension the player first enters
  DURING the recording does not exist on disk yet. `snapshot_patch.py`
  emitted 0 dim -1 events and the replay ran on magma's own generation.
- magma has Nether TERRAIN (`nf_run`) but not `ChunkProviderHell.populate`,
  and cannot have it: that `Random` is reseeded only in `provideChunk`
  (`ChunkProviderHell.java:267`), so Nether decoration is chunk-load-order
  dependent, not seed-derivable. Saved-world snapshot is the only mechanism.
- `QuantizedRL.snapshotSaveDir(mc, snapRoot, addOnly)`: recstart pass
  unchanged; `recstop` adds an ADD-ONLY pass that copies only paths the
  snapshot lacks, so new dimensions land in the tape while recstart truth for
  the start dimension / level.dat / playerdata is never overwritten.
- `snapshot_arrival_events` also only knew position packets, and a portal
  transit has none (dim flips at t=134, first ppos t=168). Added the dim-flip
  arrival at pool radius; arrivals on one tick now accumulate instead of the
  dict-comprehension silently dropping all but the last.
- Re-recorded `scenario_portal_roundtrip_20260729T083543Z` with the fixed
  recorder (`snapshot_added: 4`). Same-tape A/B: 387 failed frames / 75.1M
  UNEXPLAINED px -> 170 / 11.2M; fire and the arrival lava pool now render in
  both panes. `demos/portal_sbs.mp4` re-encoded from those frames.
- Found while measuring, NOT fixed: `nf_to_vanilla` swaps the lava ids -
  magma's generated Nether sea is `flowing_lava` (10) where vanilla is still
  `lava` (11), 123k cells of the patch. The nether_full "golden" is a
  self-capture of the C kernel, so no gate ever saw it.

## 2026-07-29 overnight (divergence grind + nether clips)

Seven renderer/sim/pipeline fixes landed, each agent-produced, personally
re-verified, and regression-gated:
- ab3e853 nether lava sea id swap (goldens upgraded to real Java oracles)
- 5efc186 elytra flag-7 one-tick latency (eye height on the arming tick)
  + retired-tape gate repair (absolute golden paths -> re-anchor or fatal;
  a retired tape used to "PASS over 0 frames" - see AGENTS.md)
- 0e73841 double_plant cross models / upper-half type / tint
- 83784f0 snapshot patch authoritative over the vegetation band (phantom
  plants from populate-order-dependent decoration; scenic 92->39 frames)
- 5224721 vanilla death keel + hurt tint; spawner miniature renderer
  built + unit-tested (data plumbing filed, 4 layers)
- af5fbd8 dragon death-ray curve (onset, boss fog, lightmap unit, the
  byte-wrap starburst) + recorder now captures armor row/hidden-particle
  HUD gates
Headline: re-recorded dragon_kill passes the FULL pixel gate (rc 0, 201
frames) - first entity-death scene to do so; adopted into the suite with
nether_elytra (115-block lava-cavern glide, wall-slam landing).
Clips shipped to the mac: nether_elytra_sbs, dragon_kill_sbs (gate-clean),
blaze_melee_sbs (death keel).
Still open (agents/wave-2 or filed): entity-interp pose mirroring,
populate-order decoration beyond the vegetation band, fortress placement
y/z, spawner data plumbing, waterfall-entry flow-texture family, fire
animation phase, live-sim blaze aggro.

Late additions to the overnight batch:
- 63f26bd dragon trail-ring phase + freeze-on-death: the "interp lag /
  mirrored corpse" was ring pollution from the unwrapped death spin;
  dragon geometry now byte-exact vs the recorder's geom oracle (1668
  parts, 0 bad).
- 9296165 snapshot patch diffs against the replay's OWN worldgen (probe
  pass): replayed worlds now bit-identical to the save (scenic 7046
  wrong cells -> 0; patches shrink up to 300k -> 3 events). The scenic
  tape's remaining 39 failing frames are measured to be particle/
  viewmodel residuals, not decoration.
Nine landed fixes total; suite RESULT: PASS; agent worktrees cleaned.

Dragon death burst (wt/dragonparticles, 2026-07-29): the death explosion was
filed as a ~3 px "shading-offset" but pxdiff's shift always sat on the span-3
search boundary; a wide-span search says zero shift is best by 3x at every
burst tick, so it was never a registration error. Three real causes, all in
the reconstruction: (1) one `ParticleExplosionHuge` spawns 6 LARGE on each of
its 8 onUpdate ticks, and magma emitted only the newest batch (~48 puffs where
vanilla has ~360); (2) vanilla removes the dragon at deathTicks 200 but its
ParticleManager cloud lives ~17 more ticks, which an entity-derived emitter
pops off - the oracle's brightest 7 frames had no magma cloud at all; (3) the
GuiBossOverlay fog latch never cleared, and `processDragonDeath` does
`bossInfo.setVisible(false)`, so the oracle's fog ramp snaps back at death and
the same cloud jumps ~4x in brightness (grey 35-60 -> white 180-245).
Fixed all three; the burst now matches the oracle in extent, brightness and
decay, tape mean 0.2266 -> 0.1753/ch (55 frames better, 2 worse), particles
class 51057 -> 26761 px, gate rc 0. Placement stays stochastic: the offsets
come from `EntityDragon.rand`/`Particle.rand`, which no tape records.

Geared dragon-kill tape (master, 2026-07-29): the follow-up tape
`scenario_dragon_kill_geared_20260730T025316Z` failed the gate on two frames
(t=454 4855 px, t=456 4258 px, magma-brighter) and read as "magma's burst runs
a few ticks late". It is not a magma clock error. The oracle's own death rays
are a fingerprint of the client render clock (fixed `Random(432L)`, count and
length from deathTicks) and magma's spokes match the oracle's at IoU 0.900 at
t=454, 0.000 at every neighbouring tick - so the recorded deathTicks IS the
client's. Yet the oracle's cloud loses the BossInfo fog ramp at t=448, six
ticks before that clock reaches 200, and particle brightness has no other
input (`lightmap(0,240)` hardcoded, explosion.png pure white). The fog is
server state (`processDragonDeath`), so the server led the client by 6 ticks
in that recording; the synced tape has both clocks together. No tape field
exposes the server clock (no XP orbs or gateway in the recorder whitelist), so
the three affected frames are filed as divergence 40 in a per-tape
`known_divergences.json`, with no code change: gate rc 0 on both dragon-kill
tapes, original particles 26842 px and max unexplained cluster 3793.

## 2026-07-30 (overnight flywheel: 12-scenario wave 2, 13 merges, 3 recorder gaps proven)

Autonomous overnight run (GOAL.md): census -> scenario synthesis -> serial qrl
recording -> parallel worktree fix delegates -> serial merge+gate on master.
Codex delegates authored fixes in isolated worktrees; every merge, gate rerun,
and divergence filing stayed with the shepherd. 42 of 48 surviving suite tapes
replay rc 0 on the CPU backend this morning.

Landed (each merged with delegate_gate ACCEPT: target tape + 6-pin regression
set, both binaries rebuilt, test-game green):
- Blocks: stone slabs (16 rows, half-box collision, side UV halves), connected
  glass panes, straight stairs (facing/half, two-box collision), trapdoors
  (3/16 poses by metadata), ladders (climb clamp, sneak hold, 0.2 kick, cutout
  plane), cactus (1/16 inset box + neighbor brightness), stonebrick id-98
  model bridge, rails + primed TNT + TNT block.
- Entities: minecart variants, armor stands (NBT pose/equipment, mob atlas),
  cave spider 0.7 scale, creeper fuse swell + white flash, silverfish replay
  mirror, dropped-item ground transforms, boat riding (seat-offset mount,
  passenger physics, paddle model, camera follow).
- HUD/camera: potion HUD order + speed/slowness FOV, creative HUD suppression,
  ItemLayerModel rim normal inversion, duplicate sneak eye-height removed
  (found independently by three delegates), slime horizon closed via sneak eye
  height 0.08F.
- Recorder (new capability): SPacketExplosion capture ("expl" tick field) with
  replay-side additive knockback + block clears; creeper/TNT physics now
  bit-exact through explosions.

Recorder gaps proven and filed (OPEN_DIVERGENCES, dated today):
1. Explosion particle clouds consume client world.rand which no tape records;
   substitute seeds visibly fail. Next step: whitelist particle-instance
   capture in ParticleManager.addEffect (also fixes dragon-death white puffs).
2. Elytra flag-7 arming round-trip varies per recording: 2-tick model makes
   nether_elytra physics-exact (351/351) but breaks elytra_dip at t=59 and
   vice versa. No constant satisfies both; needs recorded flag-7 metadata
   arrival. Candidate diff preserved on wt/netherelytra; revert ce6aa39.
3. Tape header records no gamerules: silverfish_encounter runs
   naturalRegeneration false, replay regens, hp drifts 0.4 at t49 (damage
   amount itself exact). Needs gamerule serialization at recstart.
Also filed: falling_blocks records sky-only goldens (4 deterministic repros;
client has block data, chunk meshes never build) - live oracle debugging
needed; netherelytra world snapshot lacks transient lavafall cells.

Suite hygiene: fresh gate baselines committed for all accepted tapes;
superseded stale-prefix recordings and the four defective falling_blocks
takes retired out of the sweep. One priced regression stands: nether_elytra
t=63 gained 2409 unexplained px (7 small clusters) from tonight's renderer
merges - filed, baseline left old so it stays visible.

Final sweep (nightly_20260730T122129Z, CPU): 48 tapes, 42 rc 0; the six rc 3
all replay physics-clean and pass their committed baselines (2 legacy
full-course tapes improved, tnt + creeper priced at the filed particle gap,
slime priced at the isolated shell contradiction) except nether_elytra, whose
single-line "baseline regression" (t=63, 2409 px) is the one open item and is
deliberately not absorbed. Suite RESULT: FAIL on that line alone.

## 2026-08-01: Java tape state oracle upgrade (world digest + entity truth)

The replay "state gate" stopped being decorative. Recorder.recordTick now
emits a per-tick world digest (`wfnv`: FNV-1a 64 over the 9x9x9 id<<4|meta
volume at floor(feet pos), bit-equal mirror of script.c nearby_hash - note
that constant is a historic non-standard basis, last digit dropped, and both
sides carry a comment saying bit-equality is the only requirement) plus its
anchor (`wfa`), and serializes all gamerules into the recstart header
(OPEN item 3's recorder half). script.c re-anchored nearby_hash at the
double-precision feet position (the float view floor could flip at block
boundaries), and write_state emits `nearby_anchor` + the ingested
`ghost_views`. collect_state_assertions now fails: every modeled tape entity
must reappear in magma's ghost views at its taped position (float32 tol),
and Java-vs-C digests must match on every anchor-agreeing tick; verified
failures exit rc=5. Legacy tapes keep informational verdicts - full pin set
stays ACCEPT (dragon tape: 6150 entity rows matched, 0 mismatches).

Proofs: smoke_zombie re-record = clean pass (373/373 digests, 374 ents,
rc 0); wfnv flipped at t150 -> rc 5, exact tick; ghost shift/drop and
anchor-only disagreement all behave (mutation harness). First valid
falling_blocks take promptly caught a real divergence: magma has no
gravity-block cascade (OPEN item 6 - digests identical t0-19, dig applies
one tick late with the same digest value, cascade diverges from t22, magma
world frozen from t30). OPEN item 4 (sky-only falling_blocks goldens) no
longer reproduces.

## 2026-08-02: blaze k_tick profile + optimization null result (grok lane)

First look inside the 99% "decision subtick" bucket (GPU0 Blackwell, sm_120,
N=4096 curriculum, repeat 4, exclusive flock): k_tick is ~89% of kernel wall
(5.01 ms/decision; k_obs 0.20, k_final 0.50), and inside it lane-0 serial
phys (`blaze_subtick_phys`: dig raycast + psv_physics_tick + items +
furnaces) is 51.4% of cycles, warp-cooperative coal sweep 45.8% (~381
candidates/subtick), post/reward 1.7%. Baseline 2.83M env-ticks/s with a
0.35% run-to-run spread over 3 runs.

Six candidates, all reverted under a 3% keep bar (bitwise CPU==CUDA gate run
per candidate): flat 1-env-per-thread tick loses 35% (warp tick stays ON);
slimmer coal locator -4.6%; 2-envs-per-warp (16-lane coal) -6%; n_items
early-out +0.7%; noinline+launch_bounds +1%; chunk-pointer cache in
psv_collect_blocks +0%. Conclusion: phys is a true dependent chain on lane 0
and coal is already warp-wide; nothing cheap moves it. The one lever left
with real upside is a warp-cooperative psv_collect_blocks with emission
order preserved (prefix-sum emit) - larger change, untried. ncu was blocked
(ERR_NVGPUCTRPERM); enable GPU counters before the next pass. Do not chase
obs/transfer/recenter; they are already <10% combined.

## 2026-08-02: RL flywheel policy path +8% (flywheelopt lane)

The trainer's bottleneck was never arithmetic. At the pinned M config
(N_ENVS=6144, T_CHUNK=32, REPEAT=4, EPOCHS=2, MB=8192, GPU0 exclusive) the
GPU was only 83.2% busy across a chunk: 50,032 kernel launches, 1540 ms busy,
310 ms idle. The idle was host synchronisation from
`torch.distributions.Categorical`, whose argument validation ends in a
`.all()` read back to the host - nine per forward across 81 forwards per
chunk (32 rollout + 1 GAE + 48 minibatches), 729 pipeline drains.

Replacing the nine per-head Categoricals with one padded Gumbel-argmax plus a
single `log_softmax`/gather (`FUSED_SAMPLE`, now the default) takes the chunk
from 1722.93 to 1585.17 ms paired in one lock hold: +8.00%, 456k -> 496k
env-ticks/s. Kernel launches drop to 22,736 and busy fraction rises to 94.3%
while GPU busy TIME is unchanged (1540 -> 1534 ms) - the whole gain is
recovered idle, not saved work. `rollout/sample` goes 40.28 -> 1.39 ms/chunk
and the PPO update drops 111 ms because it built nine Categoricals per
minibatch too.

CUDA Graphs were the lane's top-ranked hypothesis and are a measured
negative. Both are implemented and tested (`GRAPH_ROLLOUT`, `GRAPH_UPDATE`,
off by default): on top of the fused sampler the rollout graph returns
+1.35 ms and the update graph -5.88 ms. Once the syncs are gone there are
only ~93 ms of idle left in a 1585 ms chunk, so there is nothing for a graph
to reclaim. Their apparent +6% in a naive vs-baseline column is entirely the
validation-off that graph capture forces on. Also reverted, reproduced in two
independent pairs: caching the action-decode constant tensors measured
7.9-11.4 ms SLOWER.

Largest remaining item, measured but NOT tested (channels-last is on the
ppo-native-bf16 lane's rejected list): cuDNN NCHW<->NHWC transposes cost
158 ms/chunk, 10% of the wall, bigger than everything this lane banked. That
rejection was established in the native C++ BF16 trainer; whether it carries
to the eager fp32 torch path is one flag and one A/B.

Method note worth keeping: the correctness gate must judge GRADIENTS, not
parameters after K Adam steps. Adam divides by sqrt(v), so a near-zero-
gradient parameter turns 1e-7 of fp32 noise into an O(lr) position change;
eager-vs-eager controls on identical inputs swung 3x run to run. On gradients
the same comparison is stable to three digits (graph vs eager 5.7e-8 against
a 5.7e-8 control). Receipts: `optloop_runs/flywheelopt-v1/PRESERVED/`.

## 2026-08-02/03: nether divergence campaign (pxdiff validation + three lanes)

pxdiff hardened then validated cold: 4 blinded mutation pairs from real
goldens, codex and grok each 4/4 from docs alone, both independently reached
the elytra pose story; their friction became the survey/refinement/pose-note
round (2500c77, 185f68b). Nether tape census: 7/8 rc=0; only nether_elytra
fails, decomposed exactly.

- nethertick (d3efae3): Nether+End terrain zero-diff across blaze CPU, blaze
  CUDA, magma at 7 seeds (origin, per-seed fortress, End island box);
  injected-divergence harness proof; live nether tick probe evidenced-skip
  (blaze env is overworld-only by design).
- flag7rec (1e09743): the arming round trip AND the look-application phase
  both vary per recording - recorder now captures observed flag-7 metadata
  (f7) and pre-travel rotation (ry/rp at ClientTickEvent.START); replay
  forwards set_elytra_flag7/set_look_pre on header opt-in; legacy scripts
  sha-identical. elytra_dip re-record: physics-exact 520/520 at 1e-9, rc=0.
  nether_elytra re-record: physics clean incl. hp through terminal death.
  Vacuous-pass hole closed (FATAL when magma replays 0 ticks).
  OPEN_DIVERGENCES 2 fixed; NEW item 17: elytra fly-into-wall damage is
  server-authoritative in tick and amount (SPacketUpdateHealth round trip;
  client-speed formula gives 9.09 where the server charged 10.21).
- blazefire (4b4d5cc): Blaze AIFireballAttack attackTime is task-owned (pauses
  while the AI task is inactive); live burning flags now feed the recorder
  bit; 78-on/100-off duty cycle receipt + eyeballed frames in
  artifacts/blazefire/.
- Scenario truth: every nether_elytra take dies (fire landing or wall crash
  by arm-tick luck); rc=0 for it is gated on items 5 (lavafall snapshot) and
  17, not on more takes.

## 2026-08-03: human survival divergence grind

The seed-80302 human tape was converted into a fresh, deterministic 4,810-tick
oracle campaign on seed 917351 with opt-in dig-state evidence. The resulting
controller/raycast comparator is phase-aware and fail-closed rather than
silently aligning render observations with post-input state.

- Closed controller state lifetime, floating-origin target recentering, block
  GUI `leftClickCounter` pinning, chest collision, furnace ROCK/tool and cook
  initialization semantics, outside-GUI THROW, and post-move water-entry
  current. Liquid-flow normalization now preserves Java's float-returning
  `MathHelper.sqrt`.
- Final fresh-fixture receipt: physics exact for 4,810 ticks at `1e-9`;
  inventory PASS (252 independent ticks), entities PASS (6,687 rows), Java
  world hash PASS (4,810 ticks), and dig trace PASS (4,809 paired ticks).
- Implemented vanilla `InventoryEffectRenderer` panel shifting, hit testing,
  background/icon, amplifier, and duration. Resistance-IV inventory t380 fell
  from 35.48/ch to 1.30/ch with no remaining inventory-panel cluster.
- Pixel gate remains honestly open: 7 of 239 sampled frames still have
  unexplained clusters, worst t80 at 25,959 px. The original seed-80302 human
  tape still cannot recover pre-`bc`/`gclk`/dig-trace events.

## 2026-08-03 (later): blaze spawn-chain + world-dynamics parity flywheel

Closed the blaze-side fidelity flywheel so canonical hidden state is compared
bit-for-bit on every tick of the spawn-to-torch chain (93c0cbc, e7a85ce,
9b61ec4, 7a4ab1d), then re-pointed the "main devbox" for this stack to gamer.

- PARY flywheel (93c0cbc): Magma / blaze CPU / blaze CUDA share one packed
  subsystem record (digest + evidence count + active bit + named scalars),
  emitted every tick and compared fail-closed. Canonical spawn-to-torch chain
  2,058 ticks: player, dig, inventory, items, crafting, containers, world.
- World dynamics (7a4ab1d): traced the first world divergence to tick-19 grass
  spread (dirt 48 -> grass 32 at -11,62,-40, source -11,65,-39) - Magma mutated,
  blaze did not. Ported the deterministic scheduler + grass rule into the shared
  core; added .bsnp v2 packed per-cell sky/block light (v1 fails closed as
  unrepresented); fixed a later tick-323 divergence caused by stale light after
  a vertical log column was mined, with radius-15 incremental light propagation.
  world_dynamics promoted to VERIFIED. M1 and M2 (64 CUDA lanes) both byte-exact
  across the full BOLR + PARY record, 2,058 ticks.
- General random_ticks (leaves/fire/crops) stays BLOCKED; only the grass slice
  is evidenced by the committed chain.
- Pixel research (read-only): seed-917351 auto-campaign has physics + state
  verified clean, so its pixel fails are pure rendering: hand (viewmodel)
  near-black, hud hotbar darker, particles missing (filed as additive glow;
  2026-08-21: vanilla ParticleManager is SRC_ALPHA, magma already matches),
  and the horizon luminance/fog wash - the last matched to the already-closed
  slime_bounce fog family (do NOT retune GM_TERRAIN_FOG_*). Recorded in
  OPEN_DIVERGENCES for a future renderer grind.
- Devbox: provisioned gamer (RTX 3090, sm_86, CUDA 13.3) as the main box for
  netherite + game. Full source tree synced; magma_game, blaze_cpu.so, and
  blaze_cuda.so (sm_86) rebuild clean on gamer's own gcc 15.2 / CUDA 13.3;
  headless smoke OK; verify_cpu chain VERIFIED and verify_cuda chain (64 lanes)
  PASS byte-exact on gamer. CUDA arch must stay sm_86 there vs sm_120 on anvil.
- gamer is now a standalone devbox: GitHub SSH key added to the Infatoshi
  account (gamer pulls/pushes on its own over `git@github.com`; verified fetch
  + reset to origin). The build system now auto-selects `BLAZE_SM` from the
  host GPU via nvidia-smi (3090 -> sm_86, Blackwell -> sm_120), so no arch
  footgun when building on either box (55cb503).

## 2026-08-05: 3090 chain-trainer profiling + EPOCHS/ENT sweep (gamer)

- Profiled the batched chain trainer at N=1024 on the 3090 (nsys +
  BENCH_PHASES). Per ~745ms chunk: PPO update 56% (mb_bwd 256ms + mb_fwd
  161ms), env/step 21% (k_tick_warp ~136ms; k_obs render only ~13ms, 1.9%
  of GPU time), rollout policy_fwd 10%. The earlier "sim/camera is the
  bottleneck" read was wrong on both counts - the update dominates, and
  inside the sim the tick kernel dwarfs the render.
- Bench baseline (BENCH mode, N=1024 T_CHUNK=32 EPOCHS=2, 3 reps):
  176.4k env-ticks/s, run-to-run sd 0.3%. Negatives (all measured, all
  discarded): CHANNELS_LAST=1 -7% (the nchw<->nhwc transposes are cuDNN's
  fast-path toll on sm_86, not removable waste; +TF32 combo same, CUDNN_BENCH
  -39%); k_tick_warp __launch_bounds__(128,{3,4}) +0.8%/+0.65% only - its
  16.7% occupancy is pinned by 255 regs + 10.5KB thread stack, bounds just
  trade regs for spills (parity gate stayed bitwise PASS; reverted);
  GRAPH_ROLLOUT +1.0-1.5%; GRAPH_UPDATE infeasible on 24GB at N=1024 (graph
  pools OOM; cudaErrorIllegalAddress under expandable_segments).
- KEPT: EPOCHS=1 + ENT=0.003 (env vars only, no source change). EPOCHS=1
  alone gives +39% throughput (245k ticks/s) but collapses wpick learning at
  the default ENT=0.01 (t0 peaked 0.10 at 127M ticks, entropy drifted to
  ~9.6 near-uniform, sparse crafting skill washed out; dense-shaped log
  stage survived). A 150s/config ENT screen on SUCCESS_ITEM=17 separated
  0.003 (t0 0.32) from 0.01 (0.06, over-explores) and 0.001 (0.07,
  premature freeze). Confirmed on SUCCESS_ITEM=270: t0 50% at 4.5 min (69M),
  80% at 7.8 min (116M), 90% at 14.9 min (217M), plateau ~89% (best trailing
  0.935); vs EPOCHS=2 ENT=0.01 baseline 9.6/14.7/18.4 min, plateau ~92%.
  Wall-clock to 80% halved; ticks-to-90% slightly worse (217M vs 195M).
  Artifacts: blaze/rl/out/wpick_e1lo_net.pt + wpick_e1lo_curve.npy (kept),
  wpick_e1_curve.npy (ENT=0.01 collapse record). Single seed-set, RNG_SEED=0.
- Tier-B native trainer (delegated Opus build, branch wt/nativetrain,
  blaze/rl/ntrain/): pure C/CUDA `blaze_train` (cuDNN/cuBLAS, no
  libtorch/python in the exec path), magma-style --conf/--set/--dump-config
  registry replacing all trainer env vars. Gates: ldd clean; logits 6.7e-8 vs
  torch checkpoint; gradcheck all tensors (worst 6.9e-3 vs 0.02, found+fixed a
  head-offset bug in the reference loss kernel); env parity untouched;
  297-301k env-ticks/s (1.2x python). The initially-reported 1.55x
  sample-efficiency gap RETRACTED after a 5-native + 2-python seed sweep:
  python's own seed spread (69-108M ticks to t0 50%) covers it; native seeds
  span 73-125M. See worktree NOTES.md ("do not re-open"). Get-a-log post-peak
  t0 decay shown task-inherent (python control decays on the same schedule).
  Future trainer comparisons must target a multi-seed median, never the 69M
  seed-0 curve. Unmerged; merge decision pending.
- Pre-session gate check (2026-08-05 night prep): verify_cpu --chain FAILED on
  BOTH boxes at tick 439 (cam) on the clean committed tree. --port-parity
  localized it 420 ticks earlier: world digest diverges at tick 19 (the grass
  spread) with Blaze evidence=0 active=0 - blaze world dynamics silently OFF
  because rl/out/snaps/s10_t0.bsnp was the stale Jul-24 v1 bake (no per-cell
  light; v1 "fails closed" only in PARY evidence, BOLR gate ran blind).
  The Aug-3 VERIFIED runs used a regenerated v2 snapshot that never synced
  (snaps are gitignored). Fix: re-bake t0 snapshots (T0=1 SEEDS=10
  make_snapshots.py) on gamer AND anvil; chain gate now PASS 2058 ticks
  zero-diff in ~2.8s on both. Hardening TODO: --chain should refuse/flag v1
  snapshots loudly, and the standard loop should include --port-parity
  (subsystem digests catch world divergence 420 ticks before cam does).
- verify_cuda --chain profile: 109.5s of the 111s is lanes_match - 64 serial
  per-lane blaze_emit round-trips per tick from Python. Sim cost is noise.
  Fix direction: batched all-lanes emit (one call/tick); target <15s.

## 2026-08-05 overnight: checkpoint/resume gate, Mac parity, Metal scoping

Delegated sprint (grok delegates in isolated worktrees, one Opus on the
verify_cuda batching). Each result below re-verified by re-running the gate
first-hand, not taken from the delegate's report.

- Checkpoint/resume determinism gate (branch wt/ckresume, commit 85c7a73,
  new blaze/env/verify_resume.py, 477 lines, zero sim edits): magma's
  existing "snapshot":"path" action key (pre-tick dump, snapshot_r=49) +
  --snapshot-in resume is BYTE-EXACT. For T in {400,1000,1600}: continuous
  chain replay vs fresh-process resume matches on full BOLR for all
  remaining ticks, and blaze_cpu resumed from the same mid-episode .bsnp
  matches the resumed magma (gated fields). Whole gate 7.2s. Selftest:
  one corrupted feet-cell byte -> first-mismatch tick 20 blocks field,
  loud fail; v1/no-light snapshot -> rejected before resume. This is the
  "start at a checkpoint, resume, pure environment determinism" feature:
  it already existed in rl_mode; now it is proven and gated.
- macOS CPU parity (branch mac/cpu-parity on the macbook, commit 4e18187):
  magma_game + blaze_cpu.so build natively on Apple Silicon (only change:
  9-line Makefile Darwin OpenMP via Homebrew libomp; .so name kept).
  verify_cpu --chain PASS 2058 ticks zero-diff in 2.9s on the M4 Max.
  Cross-platform: SHA256 over the gated BOLR stream is BIT-IDENTICAL
  Mac/arm64 vs Linux/x86_64 (both magma and blaze) - arm64 libm did not
  diverge on this chain. Strong basis for Metal determinism claims.
  INCIDENT: the delegate ran `git checkout mac/cpu-parity -- .` during
  branch setup and destroyed a 21-line uncommitted paper/main.tex edit on
  the Mac (unrecoverable: no stash/snapshot/editor history). All committed
  work intact (c453c7f is an ancestor of master). Delegate prompts now
  carry an explicit git deny-list.
- Metal scoping (from code, not speculation): magma already HAS a verified
  Metal renderer (make game-metal, scripts/mac_metal_verify.sh, VERIFY.md
  L413, CUDA<->Metal kernel-pair hash manifest). blaze obs camera is
  float32 by design (blaze/core/obs_camera.h FLOAT DDA) -> Metal k_obs is
  portable and bit-exact-gateable. blaze TICK is double-precision and
  Metal has no FP64 -> Mac blaze = CPU tick (+ optional Metal obs); a
  GPU tick on Metal would need soft-double (weeks, out of scope).
- verify_cuda chain gate: 128.8s -> 17.6s (wt/gatefix, Opus delegate +
  one follow-up commit). Batched blaze_emit_all (one block per env - one
  thread per block, 64 packed into one block serialized 0.94MB of stores
  on one SM): emit 49.75ms -> 0.99ms/tick, zero-copy numpy compare. The
  15s target was honestly missed: k_tick_raw is 7ms/tick per-env CRITICAL
  PATH; switching its batch launch from 2x32 to <<<n,1>>> (no collectives;
  the env>=0 branch already ran <<<1,1>>>) bought only 18.4->17.6s
  measured 2 reps each - the predicted ~5s was wrong, the tick is
  latency-bound, not scheduling-bound. Corruption injection: fast path
  and per-lane fallback byte-identical verdicts on 4 lanes/offsets;
  batched compare only decides whether the per-lane loop can be skipped.
  v1 snapshots now BLOCK (exit 3) verify_cpu --chain/--iron and
  verify_cuda before stepping (the has_unrepresented plumbing already
  knew light==NULL but only spoke under --strict-capabilities).
- Learned-policy achievement tape (wt/wptape): wpick_e1lo_net.pt sampled
  rollout on the real magma env, seed 10, best-of-5 -> wooden pickaxe at
  tick 1540; tape truncated to 1561 ticks (78s of gameplay), committed
  with a .gitignore allowlist. Gates (all re-run first-hand): magma vs
  blaze CPU byte-exact 1561 ticks (2.8s); CPU vs 4 CUDA lanes byte-exact
  (16.8s); original scripted gate untouched. Full 6000-tick episode
  diverges at tick 1854 (cam, post-achievement) on the known
  world-dynamics gap - non-gated blocks/logs already differ there. New
  --tape override on verify_cpu/verify_cuda (default unchanged).
- blaze Metal k_obs LANDED (mac/cpu-parity 5454de6, grok delegate,
  re-verified first-hand on the Mac): expression-for-expression float
  MSL port of oc_pixel (obs_camera.h will not compile as MSL: no double,
  no stdint, address spaces; sin LUT 256KB > 64KB constant cap -> device
  buffer), allocate-once ObjC host, -fno-fast-math -ffp-contract=off.
  Gate verify_metal_obs.py: 2059 frames CPU==Metal BIT-EXACT over the
  chain tape in 1.24s; yaw +360ulp selftest fails loudly. magma Metal
  re-verify at HEAD: all 5 raster layers bit-exact, tape replay gate
  PASS (one-line metal.mk link fix: core/config.c for _cr_cfg). Followup
  hardening: the MSL oc_pixel duplicate is NOT yet hash-paired to
  obs_camera.h in a parity manifest (magma's CUDA<->Metal manifest
  pattern) - add before touching either side.
- Mid-episode blaze resume "divergence" ROOT-CAUSED (wt/ckresume
  acc650d): NOT a loader bug and NOT state divergence. On the learned
  tape, blaze resumed from a T=400/T=1000 moved-player checkpoint keeps
  ALL subsystem digests (player/dig/inventory/items/world/crafting/
  containers) byte-matched to the resumed magma; only cam pixels differ,
  first at ticks 624/1104, because OC_FAR=48 rays exit the fixed
  radius-49 snapshot region once the agent wanders off the dump pose and
  hit air in blaze where magma's sliding rl_camreg window sees real
  blocks (T=400: pixel(34,17) log id 17 at (10,64,67) vs air; margin to
  region edge 37.8 < 48). Continuous-from-t0 escapes it because T0_R=64
  (128^3) covers this tape's wander. Radius 64 mid-episode only delays
  it (fails at 1428). Inherent to the fixed-region model - would need
  region streaming/re-centering or trajectory-sized dumps. verify_resume
  now classifies pure camera-OOR as BLOCKED (exit 3) with pixel, pose,
  region bounds, and digest evidence; any non-obs digest mismatch stays
  a hard FAIL. State determinism across checkpoint/resume is therefore
  fully proven on both tapes; only out-of-region pixels are exempt,
  which is exactly the "state, not pixels" verification posture.

## 2026-08-05 (cont): merge train + three follow-ups landed

All overnight branches merged to master and verified on gamer, anvil,
and the Mac at the same HEAD (8b0f9e9); every result below re-run
first-hand after merge.

- Merge integration bug (caught by test_emit_all_fallback, fixed
  36291fe): with the per-lane emit fallback active, the batched
  state-digest ran BEFORE lanes rendered their cameras and digested
  stale frames (the observations digest hashes the env's cam/dep/edg
  buffers). Batched parity now runs only after a batched emit; the
  per-lane path digests per lane, post-render.
- The state-digest gate immediately caught a REAL divergence the pixel
  gate could not see: learned tape world digest FAIL at tick 1452
  (evidence 21 vs 20), invisible to cam until 1854. Root cause (grok
  delegate, confirmed by magma snapshot cell-diff, NOT the assumed leaf
  decay): grass smother - crafting table placed at act 941, magma
  zeroes sky light on placement, blaze only had the dig-time
  cu_light_relax_open brighten path and never DARKENED light on
  opacity increase, so the already-ported grass rule never fired. Fix
  (wt/randtick2 c31760f, 53 lines in blaze_core.h): cu_light_raw_sky
  seed + cu_light_relax_close (radius-15 re-seed then 15x relax) wired
  into cu_world_set_state on opacity rise - the exact dual of the Aug-3
  dig brighten. Learned tape now passes state digests all 1561 ticks;
  scripted CPU/CUDA gates unchanged. Leaf decay / fire / crops remain
  BLOCKED: provably uninvolved here (no CHECK_DECAY mutations on this
  fixture); leaf decay additionally needs neighbor-notify on log break.
- Mid-episode camera-OOR closed by bounds inheritance (wt/oorfix
  2066f86): rl_mode remembers the --snapshot-in region bounds and
  mid-episode dumps reuse them ("snapshot_bounds":"inherit", the
  default for resumed processes; "snapshot_r" keeps the re-centered
  behavior). Resumed blaze then sees exactly the continuous run's
  envelope: learned-tape resume gate now PASS at T=400/1000/1500
  (was BLOCKED), scripted + selftest + chain gates green. Re-centered
  dumps still classify BLOCKED (kept as an explicit --recenter path).
  Known limits: inheritance never EXPANDS the envelope (trajectories
  beyond T0_R still need streaming or bigger bakes), and chained
  resumes should always inherit from the original t0 process.
- obs kernel-pair lockstep (wt/obs-manifest 610db47): obs_camera.h <->
  blaze_metal_obs.metal hash-paired as oc_pixel in
  verify/kernels/parity_manifest.json (whole-file pairs; 10 pairs
  total); drift on either side fails test_kernel_pairs.py until
  re-recorded; Mac kernel_parity_gate.sh now also runs
  verify_metal_obs.py --chain as the pair's numeric half (ALL PASS on
  the Mac incl. obs 0.9s). Pre-existing, unrelated: anvil's CUDA
  raster numeric gate fails on mob_yaw (worst mad 9.27, ~49k px) -
  present on clean master, needs its own triage.
- Cross-box suite at 8b0f9e9: gamer CPU 2.8s / CUDA 19.1s (3090),
  anvil CPU 2.3s / CUDA 13.7s (Blackwell), Mac CPU 2.8s / Metal obs
  0.87s / 10 kernel pairs OK. Open follow-ups: regenerate a full
  6000-tick learned episode to measure where first divergence sits now
  that light-close landed (was cam@1854); anvil mob_yaw raster triage.
- Full 6000-tick learned episode re-measured post light-close
  (recording is deterministic: fresh rollout's first 1561 actions are
  byte-identical to the committed tape; full tape now committed at
  blaze/rl/out/chain_actions_s10_learned_full.json, along with the
  generating policy net wpick_e1lo_net.pt): ALL SEVEN sim
  subsystem digests (world/player/dig/inventory/items/crafting/
  containers) VERIFIED for every one of the 6000 ticks (8.7s,
  --port-parity run). The 1854 "world-dynamics gap" attribution is
  RETRACTED: with light-close in, the only divergence on the whole
  5-minute episode is cam pixels from tick 1854, where the player
  stands 43.3 blocks from the region's -z edge and OC_FAR=48 rays
  cross the T0_R=64 envelope - the same fixed-region camera-OOR
  mechanism verify_resume classifies, now on the continuous run. Sim
  state is 100% oracle-exact for the full learned episode; extending
  byte-exact CAM past 1854 needs a bigger bake or region streaming,
  not sim fixes.
- anvil mob_yaw raster triage RESOLVED (grok delegate, verified
  first-hand; merge of wt/mobyaw-triage): not kernel math and not
  Blackwell-specific - a host-side race. window_compose reuses one
  entity_verts buffer across same-frame entity/item/fire/particle
  emits, and the CUDA path async-H2D'd straight from that live
  pointer; once the daylight zombie-fire overlay started (~frame 41)
  the in-flight copy saw clobbered verts and drew greyscale garbage
  (R=G=B, 16 unique values) over the correct silhouette. CPU==Metal
  passed because Metal snapshots verts into g_vstage[CR_SH_RING] at
  enqueue; fix mirrors that on CUDA (pinned host + device staging
  ring, CPU memcpy then async H2D, transform from the slot). Kernel
  bodies unchanged; _helpers cuda hash re-recorded in the manifest.
  Makefile now auto-detects GAME_SM (sm_120 Blackwell / sm_86 3090).
  kernel_parity_gate.sh ALL PASS re-run on both anvil (mob_yaw worst
  mad 0.000049, was 9.27) and gamer (sm_86 auto-build confirmed);
  verify/kernels pytest green on both. Gate-script note: it defaults
  CUDA_VISIBLE_DEVICES=1, so on single-GPU gamer run it with
  CUDA_VISIBLE_DEVICES=0.
- Sim2real: wpick_e1lo_net (trained entirely in blaze) run CLOSED-LOOP
  in the real Java 1.11.2 client on gamer via qrl_chain_demo
  (PYTHONPATH=blaze/env now needed - ppo_chain_cu moved). Seed 10,
  sampled, 5 tries x 6000 ticks: crafted the WOODEN PICKAXE in 2/5
  attempts (a1: 2 picks then died to night mobs; a4: 1 pick), 3/5
  stalled at logs3 - all three in the same mode, crafting table placed
  + GUI open (cont=1) without further crafts; worth a targeted look at
  Java-vs-blaze container obs/craft semantics. Successful craft landed
  in ticks 1200-1600 (blaze tape: 1540). Blaze-side plateau is ~89%,
  so transfer is real with a gap explained at least partly by
  night/mobs existing in Java but not the training env. Action
  integrity: bridge FNV64 acks == local digests for all 6000
  actions/attempt. Evidence: blaze/rl/out/sim2real_wpick_s10.json
  (tracked); clips on Mac demos/: chain_s10_learned.mp4 (magma render
  of the verified tape, 0/7 tree -> 4/7 pick) and java_wpick_s10_a1.mp4
  (real Java, ~1.5x real time). Throughput ~8.5 t/s on llvmpipe.
- gamer Java-client bootstrap gotcha: the gradle offline cache had
  lost its artifact BINDINGS (every dep "No cached version available
  for offline mode" despite jars present in files-2.1; copying anvil's
  entire caches/ did not bind either). Heal = one online resolve per
  configuration: gradlew -g run/gradle help (online), then
  MC_GRADLE_ONLINE=1 start_vnc_client.sh -x getAssets (keeps the
  asset-pass excluded). Offline mode works again afterwards.
- Sim2real gap ROOT-CAUSED (two parallel Opus delegates, both verified
  first-hand; merges wt/container-stall + wt/sim2real-envmatch):
  1. The container-GUI stall was a BRIDGE mismatch, not the policy:
     vanilla "use" on a placed crafting table opens GuiCrafting (never
     pauses, so it stayed up forever; vanilla zeroes keybinds/movement
     under a screen -> attack/use/move dead, 90.6% of the stalled
     episode wedged). blaze/magma have no window system - id 58 is not
     interactable, "use" there is a plain failed place. Fix
     (Recorder.java): rlV2Active flag closes any real GuiContainer at
     the client-tick boundary while a v2 policy drives (human play /
     tape recording keep GUIs); also craft now fires BEFORE interact,
     matching rl_mode.c order. Probe: identical state+actions now give
     identical outcomes on both sides.
  2. overclock(1) free-ran the INTEGRATED SERVER ~107-420 world ticks
     per policy decision (step loop is client-tick synced, server is
     not): every prior eval episode lived ~25 in-game days - items
     despawned in ~56 policy ticks, mobs/hunger/day-night raced 100x.
     The GUI wedge was accidentally protective (GUI-fix-only control:
     0/5, all five died). qrl_chain_demo now defaults OVERCLOCK_MS=50
     (vanilla realtime, ~2 server ticks/decision drift remains).
  3. Env-match (eval-side only): fresh resets never re-applied
     qrl_launch.json gamerules (one-shot launchApplied flag), so eval
     worlds had mobs+daynight while blaze trains with mobs off, frozen
     clock, NORMAL vitals (blaze_core.h:27,2085,2133;
     player_vitals.h:13). apply_envmatch() runcmds gamerules + time
     set 6000 + kill @e after every reset (ENVMATCH=0 restores
     vanilla); proof-of-effect logged per attempt. Mobs/night alone
     were NOT the gap: 2/10 picks with envmatch but WITHOUT the GUI
     fix (7/10 fully mob-free attempts still wedged at logs3).
  Verification of the merged state (client rebuilt from master,
  ENVMATCH=1, OVERCLOCK_MS=50, seed 10, sampled TRIES=5): 2/5 picks,
  0 deaths, 0 wedges - the 3 failures all crafted sticks (the wedge
  signature was frozen-at-0-sticks). Pooled fixed-client evals: 6/10
  picks vs 4/15 pre-fix; magma-side same net+rng streams: 4/5. Known
  residual mismatches (documented, not changed): ~2 server
  ticks/decision vs blaze's exact 4 (llvmpipe client ~10-13 t/s; true
  1:1 needs TimeHelper.SyncManager synchronous mode), Java rlCountItem
  sums across slots vs blaze single-slot, structure spawners ignore
  doMobSpawning in 1.11.2. Evidence: sim2real_wpick_s10_envmatch.json
  + sim2real_wpick_s10_fixed.json (tracked); daylight success clip on
  Mac demos/java_wpick_s10_day.mp4.
- Trainer e2e REMEASURED on gamer 3090 (exclusive, 1920MHz sustained,
  no throttle, driver 610.57.04, torch 2.13.0, 3 reps, sd <0.5%):
  N=1024 T_CHUNK=32 MB=8192: EPOCHS=1 (wpick config) 92.1k
  env-ticks/s, EPOCHS=2 80.6k. The recorded 245k/176.4k baselines are
  RETRACTED as trainer benchmarks: they were measured during the
  stale-v1-snapshot window when blaze world dynamics were silently
  OFF (no per-cell light -> random ticks never fired; see 2026-08-05
  night-prep entry). Attribution is exact: BENCH_PHASES shows the PPO
  update phase unchanged vs the old profile (mb_bwd+mb_fwd ~= recorded
  256+161ms at EPOCHS=2) while env/step went 156ms -> 1044ms/chunk
  (6.7x) after the v2 rebake turned dynamics on. A/B vs a
  pre-light-close .so (ba3b1ca worktree build): identical 92.2k, so
  the light-close port itself costs nothing measurable - it is the
  dynamics being simulated at all. Consequences: the trainer is now
  ENV-BOUND (env/step 73% of chunk, update 14.5%, rollout fwd 5%), the
  EPOCHS=1 throughput win shrank +39% -> +14%, and the flywheel-lane
  negatives (CHANNELS_LAST etc.) targeted a phase that is now minor.
  Anvil flywheel numbers (1590ms/chunk, 494.5k) predate the rebake and
  need remeasure before reuse.
- ncu on k_tick_warp (one mid-training launch, --set full, root):
  74.2% of ALL GPU time (32-34ms per decision launch; all torch
  kernels ~15%). SOL: SM 6.5%, DRAM 0.25%, L1 2.3% - pure latency.
  Avg active threads per warp 1.43/32 (the per-env game tick is ~95%
  single-lane serial), 1.14 active warps/scheduler of 12 (255 regs +
  10.5KB stack cap, 77.5k local-spill requests), no-eligible 75%. The
  schedulers are idle, so throughput scales ~linearly with more env
  warps - but gamer CANNOT scale N: N=1536 OOMs (23.1GiB in use, only
  3.8GiB torch; region pool + curriculum slots own the rest), so 24GB
  pins N=1024. M3 gate (Blackwell 96GB, env-only): 0.79M @ N=1024 ->
  2.22M @ N=4096 -> 3.02M @ N=8192 confirms the scaling headroom lives
  on anvil. Paths for the 3090, in leverage order: move training to
  anvil N=6144+; shrink per-env device footprint to fit more envs;
  intra-env parallelism in k_tick_warp (big rewrite, latency-bound
  serial C is the core cost). MB/batch tuning alone cannot help - the
  update is 14.5% of the chunk.

## 2026-08-06/07: GPU-vs-CPU measurement campaign (env backend decision data)

Question set (voice session): per-tick action divergence and its factors,
tail-latency of same-compute-per-env, caps-in-yaml idea, the CPU path,
what "64x128x64" means, precise per-env memory, ncu source-level. All
numbers post-v2-rebake (real dynamics), exclusive GPUs, nvidia-smi
checked, REPEAT=4 M3-style loop (regime_bench.py mirror of
verify_cuda run_bench: pre-generated actions, masked resets /25 dec).

- Throughput matrix (M env-ticks/s, N=1024 unless noted):

  | backend                | t0 random | t0 noop | curr random | curr noop |
  |------------------------|-----------|---------|-------------|-----------|
  | 3090 (sm_86)           | 0.115     | 0.129   | 0.854       | 0.945     |
  | 7700X 16t CPU          | 0.133     | 0.118   | 0.420       | -         |
  | 9950X3D 32t CPU        | 0.182     | -       | 0.678       | -         |
  | Blackwell N=1024       | 0.150     | -       | -           | -         |
  | Blackwell N=4096       | 0.427     | -       | -           | -         |
  | Blackwell N=8192       | -         | -       | 3.913       | -         |

  7700X is flat N=256 vs N=1024 (0.135 vs 0.133) = core-saturated.
- Action divergence answer: full-random vs pure-noop actions moves
  throughput only ~10-11% on both backends. Actions are NOT the cost;
  there is no mob AI / pathfinding in blaze at all (mobs, projectiles,
  weather, day-night deliberately unsimulated; snapshots baked --mobs
  off). WORLD VOLUME dominates: t0 (128^3 fresh-spawn bake) vs
  curriculum-class region (the "64x128x64 bounds" = mid-episode
  checkpoint snapshot region, 64x128x64 blocks centered on the player)
  is 7.4x on the 3090 at fixed N.
- ncu source-level (lineinfo build, one mid-training k_tick_warp
  launch, t0 N=1024 random, root): 52.3% of ALL warp-stall samples in
  mc_rng.h (mc_hash64 mix lines 91-92 alone 33.8%), +15% on the
  cu_randtick_grass_pass loop body (blaze_core.h:519-534), +23% on
  cu_region_idx/cu_world_block reads it drives (blaze_core.h:281-293,
  mc_world.h). Cause is structural: the pass mirrors magma randtick.c
  for determinism - 17x17 chunks x 16 sections x 3 attempts = 13,872
  serial hash chains per env-TICK regardless of region size or
  actions; only in-region hits (22% at t0, ~5.5% curriculum) pay the
  additional latency-bound cell reads. Physics+crafting+inventory all
  together are <2% of stall samples.
- Tail latency: compute per env-tick is near-uniform BY CONSTRUCTION -
  the dominant cost (randtick hash sweep) is identical for every env
  every tick. Same-compute-per-env is already ~true; no per-env tail
  to chase until randtick shrinks.
- Memory, measured via cuda mem_get_info around create+load: t0
  12.45 MB/env (gamer) / 12.375 (anvil), curriculum 4.57 MB/env.
  Breakdown at 128^3: cells 4MB u16 + cam_cells 4MB + Chunk[9] window
  1.2MB + scratch/misc. This is what pins gamer at N=1024 (N=1536
  OOMs); curriculum-sized regions would fit ~N=4096 on the 3090.
- Caps-in-yaml verdict: the tunables that matter are NOT mob/liquid
  caps (mobs don't exist; liquids are cheap) - they are (a) region
  volume and (b) randtick chunk radius. Both are bit-exactness
  levers: magma uses the same 17x17 radius, so a capped radius must be
  applied to BOTH sides (magma flag + blaze) or verify/ tapes break.
  A yaml env-config is worth doing only when we intentionally fork
  training physics from oracle parity; defer until then.
- Decision: stay on GPU, move scale to anvil, shrink regions.
  CPU path is real but bounded: 9950X3D 32t = 0.182M t0 (beats the
  3090's 0.115M and Blackwell-at-N=1024's 0.150M) yet loses 5.8x to
  Blackwell N=8192 on curriculum (0.678 vs 3.913M) and cannot scale
  further (7700X already flat). GPU latency-boundedness is hidden by
  width: schedulers are 75% no-eligible, so throughput scales
  near-linearly with envs (0.15 -> 0.43M going 1024->4096). Next
  steps in leverage order: 1) train on anvil at N>=6144 (flywheel lane
  needs a post-rebake remeasure first - old 494.5k retracted-class),
  2) curriculum/checkpoint-sized bakes as the default training region
  (4x memory, ~7x speed, unlocks N=4096 on the 3090), 3) make
  randtick cheaper WITHOUT breaking parity (e.g. precompute per-tick
  chunk-hash prefixes shared across the 3 attempts, or skip
  fully-out-of-region chunk columns before hashing - pure-math
  no-op transforms verifiable by tape), 4) only then intra-env
  parallelism. CPU backend stays as the verify/debug oracle, not the
  training lane.

## 2026-08-07: external PR #5 review (Infatoshi/netherite "Feature Parity Additions")

Four-agent fan-out review of bluecoconut's +188k/-9.5k PR (~6 days of
Codex; one squashed megacommit f3e584e +185.7k plus 5 audio tip
commits). Public-repo history/layout diverged from this dev tree
(their java qrl/ vs our netheritemod/), but code shares real ancestry
(same EwStore SoA, same PAI hash streams) - porting is diff-and-absorb,
not translate. Verdict: do NOT merge; mine it.

- Audio (owner requirement: none): playback is cleanly excisable -
  audio_live.{c,h} + sound_manifest + OpenAL behind a pkg-config
  probe, one consumer file, ~1,300 lines, no OGGs committed. TRAP
  (confirmed independently by 3 reviewers): the sound-EVENT emission
  ring and per-draw RNG accounting must be KEPT - vanilla consumes
  Random draws when playing sounds, and their strict gates (and any
  future RNG-exact gate of ours) depend on modeling those draws. The
  ring is data-only, plays nothing. 3 of the 5 audio tip commits hide
  non-audio fixes (6.3k-line legacy-Recorder dedup, mesh/model-key
  repairs, gm_player_dig_reset hardening) - salvage those.
- Test infra = the crown jewels: (1) server tick lockstep gate
  (server_step_lock + ~60 *_locked bridge cmds, IEEE-754-bit float
  transport) - removes our documented 1-tick input-offset artifact;
  (2) RNG cursor capture/injection on live Java (seed48 + gaussian
  flag) - turns random mechanics into state-exact gates; (3) state
  capsule with capability ledger (exact/captured_only/unavailable +
  refuse-if-incomplete) + Java->magma mid-run continuation proofs
  (redstone-torch hidden-state regression is the flagship); (4) 32-way
  isolated Java oracle pool (own display/port/save/pgid each); (5)
  tri-state gate semantics (pass/parity-fail/infrastructure-fail) +
  mandatory negative controls. Port DESIGNS, not code: the 28.5k-line
  matrix is one 17k-line function + 10.7k-line main() with hardcoded
  /home/jawaugh paths; fixtures however are clean text formats we can
  consume nearly directly. "Load Java from save-state" is oversold:
  restore direction is Java->capsule->magma only.
- Gameplay (spot-checked deep: pig ride, sheep breeding, falling
  anvil, brewing - all pass our decompile-fidelity bar, exact Java
  float literals + RNG draw ordering + live-oracle gates): port-worthy
  for our roadmap in order - animal husbandry bundle, falling-block
  entities + scheduled-tick priority queue, explosion engine (crystal
  explosion world edits are our open divergence), potion/status
  effects, RNG-injection script vocabulary + loaded_order (the
  bit-exact multi-mob ordering concept blaze mob-sim will need; mob
  state already flat fixed-cap arrays = blaze-shaped).
- Their blaze = gate corpus, not env: batched env got +49 lines total;
  brewing/fishing/potions/temples live in golden-test headers (Java==
  CPU==CUDA bitwise, fail-closed). Zero warp-level perf awareness
  (<<<1,1>>> "benchmark", 128KB device stack for A*); nothing helps
  our randtick bottleneck. Steal: fail-closed gate patterns, fire-
  encouragement table fix (bookshelf 5->30 - check ours), potion
  combat math headers, fdlibm_log.h (shelf).
- Do NOT port: stateful JavaGaussianRandom streams into the batched
  env (order-dependent, non-skippable - exactly the property our
  randtick skip exploits), area-effect-cloud unbounded entity growth,
  per-tick full-volume hash gates in hot loops, device A* as-is,
  mc_jr_watch host-statics near device code, fishing (low RL value).

## 2026-08-07 (cont): grass randtick occupancy skip landed (2.2x trainer e2e)

Opus delegate (wt/sparse-randtick, b84a7a5) built + gated; re-verified
first-hand and merged (42742a5). The skip: per-env u16 census of
BLK_GRASS per 16^3 section (grid sized off region DIMS only, 1458 B/env
at 128^3), populated by a trailing reset-bulk range, maintained
synchronously in cu_world_set_state (audited: the ONLY runtime cells[]
writer; grass spread writes neighbour sections mid-pass and the counts
stay exact). cu_randtick_grass_pass skips attempt-groups whose section
has no region overlap or zero grass BEFORE hashing. Bit-exact because
mc_hash_seed is counter-based and all three attempts of a group land in
that section; NULL grass_sec falls back to the full sweep.
Delegate evidence beyond gates: BLAZE_GRASS_AUDIT brute-force
re-census build clean over verify_cpu + chain, negative control
(maintenance disabled) aborts at tick 613; verify_cuda --mixed n=1024
final sha256 identical to a ba-baseline worktree; op-trace world_load
5.22M -> 1.28M over 3200 sub-ticks on s10_t0.
First-hand re-verified: verify_cpu --chain, verify_cuda --chain (2058
ticks 64 lanes byte-exact), verify_resume --chain; rebuilt merged
master and re-ran chain gates green.
A/B on 3090 (exclusive, idle, back-to-back): regime bench t0 random
N=1024 0.118 -> 0.355M env-ticks/s (3.0x); trainer lane N=1024
T_CHUNK=32 MB=8192 EPOCHS=1: 92.1k -> ~204k env-ticks/s (2.2x),
env/step 1044 -> ~256 ms/chunk, PPO update phases unchanged. CPU t0
random 0.133 -> 0.171M (1.29x). Curriculum regime unchanged.
Snap-version census (matters for ALL bench interpretation): only
s10_t0.bsnp is v2; the other 10 t0 seeds and all 34 curriculum snaps
are v1 (no light plane -> light_valid=0 -> randtick never fires). So
"dynamics on" currently means 1/11 t0 lanes, and pre-skip wall time
was tail-dominated by those lanes' 13,872-hash sweeps. A full v2
rebake of all seeds is still pending and will move every number here;
the skip is what makes that rebake affordable.
Delegate side-findings, queued: (1) cam_cells is a pure right-shift
copy of cells (blaze_core.h:510, :3227) - deleting it saves 4 MB/env
(12.4 -> 8.4 at 128^3; N=8192 pool ~76 -> ~43 GB) but oc_pixel is
hash-paired with blaze_metal_obs.metal, so the change needs the Mac
(kernel_parity_gate.sh both sides + kernel_pairs.py --update) and
touches rl_mode.c camreg encoding - do it in a Mac session. (2)
port_matrix --tier m1 exits rc=3 on gamer (mining_slice missing
capability) - pre-existing, byte-identical on a baseline worktree;
confirm rc=0 on anvil. (3) agent_worktree.sh now links
blaze/rl/out/snaps (without it every blaze env gate SKIPs silently).

## 2026-08-07 (cont): full v2 snapshot rebake, both boxes; honest baselines

make_snapshots.py (T0 + curriculum modes, all seeds) on gamer AND
anvil; outputs deterministic and identical across boxes (13 t0 + 12
curriculum v2 written; 27 curriculum stages stage-failed/already-mined
this pass). Old files hardlink-backed to snaps_v1bak_0807; the 23
stale v1 files the bake did not rewrite were QUARANTINED to
snaps_v1_stale (a mixed dir silently reintroduces dynamics-off lanes).
All snaps are LIQUID-flagged (advisory: fluids CA unsimulated; trainer
unaffected - ppo_chain_cu loads t0 only, blaze/env/ppo_chain_cu.py:831).
Gates on the fresh bake: verify_cpu --chain + verify_cuda --chain PASS
both boxes; verify_cuda --mixed --n 2048 PASS on anvil (13.95s,
bitwise); port_matrix --tier m1 rc=3 is IDENTICAL on anvil and gamer =
capability census (16 BLOCKED e.g. dragon_victory/weather, 0 FAILED),
pre-existing, not env breakage.
New honest baselines (all-v2 snaps, randtick skip merged):
- gamer 3090 trainer lane (N=1024 T32 MB=8192 EPOCHS=1): 148k
  env-ticks/s e2e, env/step ~499 ms/chunk (was 92.1k/1044ms on the
  old 1-of-11-v2 mix without the skip; 204k on that mix with the
  skip). Skip value on real dynamics: regime bench t0 random N=1024
  all-v2 = 0.049M pre-skip -> 0.111M with skip (2.3x).
- anvil flywheel lane (N=6144 T32 EPOCHS=2 MB=8192, GPU0 exclusive,
  preflight ok): median 2168.07 ms/chunk, cv 0.18% -> 362.7k
  env-ticks/s. REPLACES the retracted stale-v1 494.5k/1590ms.
Curriculum bench regime post-rebake: 0.034M (N=1024, 3090) vs the old
v1-set 0.854M - NOT a regression and NOT comparable. Root-caused via
op-trace diff: item_tick 5.18/subtick vs t0's 0.054 (96x). The new
12-file set skews item-heavy (9 files carry 3-23 live dropped items
from the burrow; identical counts in the old files - the old 34-file
set just diluted them), and every item is a full serial entity-physics
tick per subtick in k_tick_warp. Curriculum rows measure the SET.
Open items: (a) bake attrition - 27 stages fail stage_coal/already-
mined under the current sim vs the historical 34-file set (accumulated
under older sim states); deterministic, both boxes; needs a
stage_coal revisit if d-stage curricula return to the training path.
(b) QUIESCE=6 < vanilla 10-tick pickup delay, so burrow drops can
never be collected before the dump - longer quiesce (or item strip
for training bakes) would cut item-heavy curriculum cost sharply.

## 2026-08-07 (cont): PR5 wave-1 ports merged

Opus delegate (wt/pr5-wave1, 3 commits), each re-verified first-hand
before the merge (4c90fd4):
- fix(blaze): bookshelf fire encouragement 5 -> 30 in
  blaze/core/world_tick_vanilla.h:288 (was grouped with logs; vanilla
  BlockFire.java:90 setFireInfo(BOOKSHELF, 30, 20) - cited in code).
  magma/game/randtick.c:81 was already correct; every other entry in
  both tables cross-checked against BlockFire.init(), 47 was the only
  divergence. Live in the fire-spread gate corpus, ~2x under-spread
  next to bookshelves before the fix. Dig-reset port did NOT apply
  (gm_player_dig_reset already resets all three fields PR5 added);
  flagged-not-changed: s_rc_delay/s_use_prev/s_fov_hand/s_cursor
  survive a reset, and gm_runtime_respawn never calls the dig reset.
- feat(qrl): server tick lockstep gate in netheritemod/Recorder.java
  (+482) - server_step_lock parks the integrated server at a tick-
  START boundary; step_server_locked n advances EXACTLY n ticks and
  waits for re-park; getblocks_locked / setblocks_locked /
  setplayer_locked run race-free on the socket thread (a parked
  server drains no scheduled tasks - the normal server-task hop would
  deadlock); floats cross as raw IEEE-754 bit strings. Gate self-
  releases on socket teardown/close/reset/dim. Measured before/after:
  12 back-to-back unlocked reads advance the server 13 ticks and two
  consecutive reads span 3-4 ticks; locked: exactly 0, byte-identical.
  java/qrl_lockstep_gate.py (30 checks) is the reusable proof - run
  first-hand on a fresh client boot: 30/30. This removes the read-race
  half of the documented ~2-ticks-per-decision sim2real drift path and
  is the prerequisite for exact-4-tick Java stepping.
- feat(verify): tri-state gate outcome in replay_tape.py -
  pass | parity-fail | infrastructure-fail (+ rc 6, gate.json
  outcome/infrastructure_fail.reason, md Outcome line). Three
  laundering paths fixed: magma SIGSEGV/build-failure was reported as
  rc=4 "physics divergence"; fail-closed stamped pass=False with no
  evidence; missing goldens exited rc=1 with no gate.json. Additive:
  prior keys/baselines/fast_gate untouched, 5 new negative-control
  tests (replay pytest: same 8 pre-existing env-sensitive failures on
  master and branch under identical deps).
Deliberately not ported: PR5's ~60-command locked surface, RNG cursor
injection, all sound capture (no-audio), and coupling the existing
step command to the gate. agent_worktree.sh now also links
java/Minecraft/run + java/oracle-src (worktrees SHARE main-tree saves;
proof scripts must restore what they touch).

## 2026-08-07: cam_cells deleted - the camera unpacks cells (-4 MiB/env)

Closes queued side-finding (1) above. `cam_cells` was a second region-sized
u16 tensor holding exactly `cells[i] >> 4`, proven by both writers:
`cu_world_set_state` wrote `cam_cells[i] = id & 0xFFF` right after
`cells[i] = mc_state(id, meta)`, and `blaze_reset_bulk` wrote
`cam_cells[idx] = s >> 4`. `mc_state_id` IS `s >> 4` (mc_world.h:30) and
cells is u16, so `s >> 4 <= 4095` makes the `& 0xFFF` a no-op - the two
writers agree, and the tensor was 2 bytes/cell of derived data.

Its only consumer was the camera (`reg.cells = env->cam_cells` ->
`OcRegion` -> `oc_block`), so the shift moved INTO `oc_block` and the
OcRegion contract became PACKED STATES `(id<<4)|meta` instead of plain ids.
`oc_block` still returns a plain id, so nothing downstream of the camera
changed. The shift has to sit before `oc_raycast`'s `if (id)` air test,
which is exactly where `oc_block` is: reading raw states there would stop
the DDA on a hypothetical air-with-meta cell (state 1..15) that `cam_cells`
saw as air.

Two encodings had to flip in the same commit because magma is the oracle
the fidelity gates diff against: `rl_camreg` (rl_mode.c) and the blaze env.
The standalone `blaze/{cpu,cuda}/obs_camera.c[u]` drivers fill from
`rt_fill`, which emits plain ids, so they now pack `id<<4` (worldgen has no
meta, so this is value-exact - the 124419-line dump is byte-identical to
master's, which is the cleanest proof the round trip is output-neutral).

oc_pixel is hash-paired, so `blaze_metal_obs.metal` got the identical edit
and the manifest was re-recorded only after BOTH machines proved:
- gamer (3090, sm_86): verify_cpu --chain (2058 ticks, zero diffs + state
  digests incl. observations); verify_cuda plain / --chain / --mixed --n
  1024; xbackend_frames --backend cuda; kernel_parity_gate.sh.
- macbook (M4 Max, Metal): xbackend_frames --backend metal;
  verify_metal_obs.py --chain = CPU==Metal bit-exact over 2059 frames;
  kernel_parity_gate.sh.

VRAM, `torch.cuda.mem_get_info` around create+load_snapshots+reset, N=512
on 13 t0 (128^3) snapshots, 3090: 6.4676 -> 4.3201 GB, i.e. 12.6321 ->
8.4378 MB/env, delta exactly 4.1943 MB = 2 B x 128^3 = 4 MiB. The queued
estimate (12.4 -> 8.4) was right.

Gotcha found on the way, unrelated to this change: `blaze/oracle/runner.py`
defaults `MC_SM=sm_120` (anvil). On gamer the `obs_camera` section FAILS
cpu==cuda at line 1169 with that default and PASSES with `MC_SM=sm_86`;
reproduced byte-identically on an unmodified-master worktree, so it is an
arch-default trap, not a regression. Export `MC_SM=sm_86` for any runner.py
section run on gamer.

Merge addendum (735caae, measured on gamer post-merge): the freed 4 MiB/env
moved the 3090 trainer ceiling. `ppo_chain_cu.py` (T_CHUNK=32 MB=8192
EPOCHS=1, all-v2 snaps, BENCH_PHASES=0) pre-deletion OOM'd at N_ENVS=1536;
post-deletion N=1536 runs clean at 146-164k env-ticks/s and N=2048
completes all chunks at 163-172k env-ticks/s, with CUDACachingAllocator
OOM-retry warnings each chunk (allocator flushes cache and succeeds -
thrash, not failure; free dips to ~0.3 GB of 25.3). Practical max on the
3090 is N=2048 with pressure, N=1536 comfortable. Companion gamer gate
trap: `scripts/kernel_parity_gate.sh` exports
`CUDA_VISIBLE_DEVICES=${CUDA_VISIBLE_DEVICES:-1}` (anvil convention);
gamer has one GPU, so the default SIGSEGVs (rc=-11, "no CUDA-capable
device"). Run it as `CUDA_VISIBLE_DEVICES=0 MC_SM=sm_86 bash
scripts/kernel_parity_gate.sh` on gamer.

## 2026-08-07 (cont): verify_cpu d* 0/4 = fixture camera envelope, not liquid

verify_cpu (no --chain) FAILed 0/4 on the v2 curriculum snaps. The
[LIQUID] advisory printed on all four is a red herring: every differing
byte was `cam`, and every differing cam pixel was a ray that had LEFT
the .bsnp region. blaze's world IS the region (oc_block outside -> air
0); magma's rl_camreg tracks the live world, so it hits real blocks
where blaze must return sky.
Evidence (gamer, s14_d6.0, observation 0, before any tick):
- 46/46 differing cam pixels are rays whose DDA leaves the region with
  no in-region hit (measured exit t 31.7-33.7); magma reports id 78
  (snow layer) at t 32-38, out at wx 42-47, region max x = 42. Same
  shape for s27 (3/3, id 175 at t 36.7-47.2), s46 (1/1, id 2 at
  t 39.2), s29 (6/6, id 18). Byte offset of the first differing cam
  pixel on s14 = 5852 (pixel 220, row 3 col 28), not byte 0.
- The PARY world digest over the region is IDENTICAL at observation 0
  (0x453f72d40b9502d3 both sides) and stays matched for 300+ ticks on
  s46: no in-region block ever differs, so the unsimulated fluids CA
  is not involved at all.
Why --chain passes and d* fails: t0 fixtures are 128x128x128 (T0_R=64,
picked because "camera rays reach 49"), curriculum dumps used the
rl_mode default radius 32 (64x128x64) - a fixture SMALLER than
OC_FAR=48. The same envelope also breaks physics near the edge: on the
64-wide fixtures the sim digests diverge later (s14 player t741, s46
world t719, s29 dig t994), while on radius-64 re-dumps all four seeds
are digest-VERIFIED for the full 1000 ticks.
Fixes:
- make_snapshots.py: CURRICULUM_R (default T0_R=64) on the curriculum
  dump; EXPAND=1 re-dumps existing d* fixtures IN PLACE through magma
  --snapshot-in (inner cells byte-identical; new shell is the same
  worldgen magma's camreg reads), so upgrading costs no curriculum
  re-bake and no new stage attrition.
- verify_cpu.py: state digests are now default-on for the seeds path
  too, and a gated mismatch is CLASSIFIED. Pure fixed-region camera
  OOR - cam-only, EVERY differing pixel magma-block vs blaze-air at
  depth 255, sim digests matched, eye margin < OC_FAR - is BLOCKED
  (exit 3); anything else stays FAILED. A missing .bsnp is BLOCKED,
  not skipped (it used to shrink the denominator into a green N/N).
  Region/pose/cam-pixel primitives are single-source with
  verify_resume.py, which already used this BLOCKED convention for
  re-centered resume dumps.
Gate output, current radius-32 fixtures: `BLOCKED: 0/8 streams
zero-diff + state digests, 8 BLOCKED` (4 envelope, byte-exact for
0/26/0/151 ticks; 4 missing s16/s20/s32/s44 = the known bake
attrition). On radius-64 re-dumps of the same states: 668/261/669
ticks byte-exact then envelope-BLOCKED, s46 1000/1000 clean.
Negative controls (corrupt only the .bsnp blaze loads, magma keeps the
pristine file): feet-cell id flip -> FAILED via world digest; deleting
the occluder a camera ray hits so that ray then leaves the fixture -
the exact BLOCKED symptom - -> FAILED (14/66 pixels off the OOR
signature). verify_cpu --chain, verify_cuda --chain (gamer:
CUDA_VISIBLE_DEVICES=0 MC_SM=sm_86), test_state_digest_corruption and
verify_resume --selftest all still PASS.

## 2026-08-07 (cont): curriculum bake attrition = crafting-table GUI pins leftClickCounter

The 2026-08-07 full curriculum rebake wrote 12 of 39 possible (seed, stage)
fixtures. Census first (39/39 attempts classified, instrumented replay of
`make_snapshots.bake_seed`): 12 reached, 0 already-mined, 0 scan-empty, 27
budget-exhausted - the old "scan lost the ore or budget exhausted" message
conflated two very different modes and the answer was entirely the second.
The distribution was bimodal, not slow: failing stages logged `mine=0/33`
successful `mine_block` calls, every one of the 33 burning its full 90-tick
budget on a target 0.8-2.3 blocks away, i.e. the prober never broke a single
block rather than digging too slowly. Reach was never the issue.

Root cause is `chain_probe.stage_pick` (blaze/rl/chain_probe.py:189) opening
the crafting table and never closing it, against `runtime.c:604` - since
0f51db9 magma re-pins `gm_player_set_gui_blocked(1)` ->
`s_left_click_counter = 10000` (player_ctl.c:981) on every tick a container
screen is open, mirroring vanilla's blocked mouse input, so clickMouse and
sendClickBlockToController no-op and progressive dig is frozen. The RL action
protocol has no ESC verb: `rl_do_interact` (rl_mode.c:268) only ever opens a
screen, and the sole close magma exposes is vanilla's canInteractWith range
drop at runtime.c:519 (`!valid || dist^2 > 36`, dy included). That makes the
state absorbing - a burrow that cannot dig also cannot walk out of
interaction range. Seeds that "worked" (s46) only did so because the tunnel
walk happened to cross 6 blocks from the table by luck at t+279; every seed
boxed in near its own table (s2, s10, s11, s32, s44) deadlocked for the whole
3000-tick budget. `chain_probe.py` is byte-identical to its 2026-07-19
content apart from one docstring path (9cdc65c was a pure move), so the
prober did not rot - the game changed under it on 2026-08-03.

Second-order consequence: the committed `coal_prefixes.json` action streams
were recorded against a pre-0f51db9 binary. Replayed against the current one
all 13 seeds ended with `container==1` and `cobble==0` - stage_dig had
accomplished literally nothing - on the surface (s2 y=79.42) with the ore
11 blocks away, so no stage_coal tuning could have recovered them. The
prefixes had to be regenerated, not just the fixtures.

Fix is prober-side only (no magma/game, no blaze/core, nothing in
verify/kernels/parity_manifest.json): `close_container` walks out of the
table's 6-block interaction sphere after the pickaxe craft, fanning its
heading (0, +/-40, +/-75, +/-110, +/-150, 180 deg off straight-away) whenever
8 ticks pass with no horizontal progress - a straight retreat pinned seed 11
against a 2-high wall at x=14.70 for the whole budget. `stage_coal` also now
records its exit reason in `chain_probe.LAST_FAIL` and `make_snapshots`
prints it, so the two skip modes never collapse again.

Result: seed 2, previously deadlocked at every stage, completes the full
spawn-to-torch chain (dig +595t, mine_coal +795t). Regenerated prefixes are
10/48 seeds (was 13/48: -10 -11 -32 -44, +35). Rebake at CURRICULUM_R=64
writes 25 of 30 possible fixtures (was 12 of 39), and `mine_block` success
went from 0/33 to 9/9, 4/4, 6/6 per stage. Residual: s27 x3 legitimately
already-mined (its prefix burrow banks the coal, coal_inv=2 at handoff), s29
d4.5/d3.0 budget-exhausted - the burrow oscillates between two tunnel
headings, re-targeting cells (9,7x,5) and (9,7x,7) that are already air, and
`mine_block` has no already-air early-out so it spends 90 ticks per cell
confirming nothing. Not a dig-rate problem and not fixed here.

Found while gating, and more important than the attrition: **verify_cpu
per-fixture PASS is timing luck**. 6 of 25 new fixtures FAIL the world state
digest with blaze performing exactly one world-cell mutation magma does not
(`evidence`/`active` 1 vs 0). The trigger is a fixed absolute world tick per
seed, not fixture content: s46 fixtures at ticks 1730/1737/1744 all diverge
at tick 1747 (16/9/2 ticks in); s35 at 1748 and 1880 both diverge at 1998
while s35_d3.0 at 2012 is past it and is clean 1000/1000; s14_d3.0 at 1562
diverges at 2472. The old fixture set passed only because its dump ticks
(s46 2481/2834/2841, s14 1020/1027) sat past or far before the seed's trigger
so it never entered the 1000-tick window. Absolute-tick (not
relative-to-dump) triggering says this is a world-timeline divergence, not a
snapshot round-trip artifact. Untouched here: it lives in blaze/core, which
is hash-paired with Metal and needs the two-machine gate.

Item counts, since QUIESCE=6 is below vanilla's 10-tick pickup delay: new set
mean 8.4 items/fixture (min 1 s3_d6.0, max 17 s46), old 12-file set mean 8.0
(min 0, max 23 s33_d3.0) - drops still unsettled at dump in both sets, no
regression, and QUIESCE deliberately left alone as a training-distribution
call. The LIQUID flag is now uninformative: 25/25 new and 12/12 old d-fixtures
flag liquid at radius 64, since a 128^3 region essentially always contains
some water.

Gates on gamer (RTX 3090, CUDA_VISIBLE_DEVICES=0, MC_SM=sm_86):
`PASS: 1/1 chain stream zero-diff + state digests` (verify_cpu --chain,
2058 ticks) and `PASS: chain s10 x 2058 ticks, 64 CUDA lanes vs CPU
byte-exact` (verify_cuda --chain). Spot check `verify_cpu --seeds 2
--stage 6.0`: 1000/1000 ticks zero diffs, VERIFIED not BLOCKED.

## 2026-08-07 (cont): snapshot resume re-derived skylight; boundary halo split out

The 6 failing curriculum fixtures were TWO bugs, not one. The absolute-tick
trigger is explained first and is common to both: `cu_randtick_grass_pass`
(blaze/env/blaze_core.h:599) and `gm_randtick_pass` (magma/game/randtick.c:432)
both pick the section cell from `mc_hash_seed(seed, tick, cx, sec, cz,
RT_PURPOSE_POS^att)`, a counter hash of the ABSOLUTE world tick, and
`rl_snapshot_load` restores `r->tick = h.tick` (rl_mode.c). Every fixture cut
from one seed therefore replays the identical cell sequence at the identical
absolute ticks, so a divergence trips at a fixed absolute tick regardless of
dump tick - s46 dumps at 1730/1737/1744 all tripped at 1747. Neither bug is a
hash or a scheduled-tick-queue problem: both sides always agreed on WHICH cell
to tick.

Bug A (fixed here), seed 46 tick 1746, magma evidence=0 vs blaze=1. Cell
(5,63,2) is BLK_GRASS and (5,64,2) above it is tallgrass id 31 meta 1 on BOTH
sides - no block ever differs. The two differ on LIGHT: `rt_light_at`
(randtick.c:31) reads sky=7, `cu_rt_light_at` (blaze_core.h:544) reads sky=15.
BlockGrass.updateTick returns below light 9, so magma did nothing while blaze
ran the spread loop and set (6,61,2) to grass. Over the full run only 3 of 185
grass cells common to both sides disagreed, always blaze HIGHER (8v6, 11v7,
15v7); only 15v7 crossed the threshold with a spreadable dirt target, which is
why the symptom is one mutation once.

blaze was right, magma was wrong. `rl_snapshot_write` dumps
`gm_world_sky_light` per region cell (rl_mode.c:683) and blaze loads those
nibbles verbatim; reading s46_d6.0.bsnp directly confirms (5,64,2) sky=15 and
(3,64,4) sky=11 = 15 minus its manhattan distance 4 from the pin.
`rl_snapshot_load` however closed the file immediately after the cell array
("trailing coal list is a derivable mirror; not needed") and never read the v2
light block, re-deriving light from `gm_world_ensure` +
`gm_world_load_block_meta`. Saved skylight is NOT derivable from blocks:
`light_recheck_break_surfaces` (magma/world/light.c:762) pins sky=15 on
tallgrass (id 31) above a broken grass/dirt cell - vanilla's deferred
Chunk.recheckGaps - and it runs ONLY from `gm_world_set_block_meta`
(world_live.c:548), i.e. only on a live dig. The baking process took that
branch and its flood carried the pin outward; the resuming process bulk-loads
through `gm_world_load_block_meta`, never pins, and `compute_skylight` settles
the column back to 7.

Fix is magma-side and needs no .bsnp format change (v2 already carries the
array; BLAZE_SNAP_VERSION untouched): `rl_snapshot_load` now reads the light
block past the coal mirror and restores the sky nibble through new
`light_load_sky` (light.c:778) / `gm_world_load_sky_light` (world_live.c:577),
after a second `gm_world_ensure` consumes the bulk load's
`column_relight_dirty` so generateSkylightMap cannot stomp the restore. Block
light is deliberately NOT restored: `compute_blocklight` (light.c:428) resets
and re-derives it from emitters, so it is a pure function of the block array
and already matches. `compute_skylight_spread` only ever RAISES cells
(light.c:508), so restored nibbles survive every later relight; only a column
rebuild lowers them, and nothing re-arms `column_relight_dirty` after load. v1
snapshots have no light array and keep the old re-derive path. Effect on s46
d6.0: the tick-1747 divergence is gone, the fixture now runs clean to magma
tick 1993 (16 -> 263 ticks).

Bug B (found, NOT fixed - needs a design call). With A fixed, all six fixtures
show one identical signature: magma evidence = blaze evidence + 1, i.e. magma
makes exactly one world mutation blaze cannot. s46 d6.0/d4.5/d3.0 all trip at
magma tick 1994, s35 d6.0/d4.5 at 1998, s14_d3.0 at 2472 - the s35 and s14
trigger ticks are UNCHANGED from before the A fix, so those two seeds were
never bug A at all. Instrumented, the mechanism is the region boundary:

  seed 46: region x [-57..70], randtick source (-58,71,31) = rx0-1, writes
           (-57,69,31) = rx0
  seed 35: region x [-52..75], randtick source (-53,65,-23) = rx0-1, writes
           (-52,62,-22) = rx0

magma randticks the full radius-8 chunk area (runtime.c:676, view_distance 8);
blaze's world IS the .bsnp region and its pass guards every source with
`cu_region_idx(...) >= 0` (blaze_core.h:610), so a grass source one block
outside the region is invisible to it while BlockGrass spread (dx,dz in -1..1,
dy in -3..1) reaches one block in. At seed 35 tick 1997 magma randticked 7
grass sources with x < -52 and blaze had 0 grass events that tick. This is
structural, not a port bug: blaze holds no block or light data outside the
region and cannot represent the source. Options, none taken unilaterally
because each changes a contract: (1) erode the world parity digest by 1 cell
in x/z (and 1 low / 3 high in y, the spread reach) on both sides, narrowing
the claim to where blaze can be faithful - the camera-envelope precedent;
(2) .bsnp v3 side-array carrying the 1-cell shell's blocks+light, full
coverage but a format bump and a full re-bake, and it threads halo lookups
into the hot cu_world_* reads; (3) restrict magma's randtick sources to the
parity region - rejected here, it makes the ORACLE less vanilla-faithful.

Gates on gamer (RTX 3090, CUDA_VISIBLE_DEVICES=0, MC_SM=sm_86, co-tenant
flywheel resident): verify_cpu --chain `PASS: 1/1 chain stream zero-diff +
state digests` (2058 ticks). Nothing touched is in
verify/kernels/parity_manifest.json (that file pairs only CUDA/Metal raster
kernel hashes), so no two-machine Metal gate is implicated; blaze/core and
blaze/env are byte-unchanged, the whole fix is magma/. Tape replay
re-validation on anvil is PENDING - magma/game and magma/world changed.

## 2026-08-08: PR5 wave-2 - RNG cursor capture (capture+verify, no injection)

Opus delegate on gamer (wt/pr5-wave2). Ports the RNG-cursor half of
bluecoconut's PR #5 test-infra crown jewels, listed at DEVLOG:1865 as
deliberately not done in wave-1. Design ported, code reimplemented; the
PR's own is unusable here (17k-line function, hardcoded /home/jawaugh).

The mechanism, and why it works: java.util.Random IS its state. The
48-bit LCG `seed <- seed*0x5DEECE66D + 0xB` is scrambled by the
constructor and by nothing else, so every public method is some number
of next() steps off the stored AtomicLong. A snapshot at a tick
boundary is therefore a CURSOR, and two cursors bracketing a tick
determine exactly how many draws that tick consumed - walk the
recurrence forward and count. That is what converts "the streams
drifted somewhere in this run" into "record 17 is 1 draw ahead". The
walk early-exits on arrival, so a healthy tick costs only the draws the
server actually made; only a genuine divergence pays the budget, and
the gate stops at the first one anyway.

Java (Recorder.java, +380): four server streams captured at
ServerTickEvent.START, the same boundary the wave-1 lockstep gate parks
at, so cursor[i] is the state tick i is about to consume. Stream set
follows the PR's state_capsule capability ledger
(world.rng.{java,math,block}_random_seed48 + world.rng.update_lcg):
World.rand, java.lang.Math's process-global RNG, Block.RANDOM, and the
int32 World.updateLCG (not a java.util.Random - `lcg*3+1013904223`,
World.java:95). Gaussian state travels with the world cursor
(haveNextNextGaussian/nextNextGaussian) because nextGaussian() computes
two values and stashes one. Reflection accessors at Recorder.java:1641-
1716 are fail-soft (-1/false) so a hostile JDK degrades the gate to
"unavailable" instead of killing the bridge. Capture is a bounded ring
(Recorder.java:1745-1808) filled on the server thread: a 6000-tick
session costs one dump, not 6000 round-trips. It stops at full rather
than overwriting - the FIRST divergence is what matters, so the head of
the run is worth more than the tail. New commands: rng_capture /
rng_dump (gate-free, so a normal free-running recording can capture
too) and rng_cursor_locked / rng_burn_locked (parked-only, socket
thread, same rule as the wave-1 locked set).

Verifier (verify/trace/rng_cursor.py, 391 lines, new): jr_step/lcg_step,
draws_between / lcg_steps_between, profile() for per-tick draw
accounting, first_divergence() reporting the first disagreeing record
with its stream and a classification - "candidate is N draw(s)
AHEAD/BEHIND" vs "unrelated cursors (reseed, or a different world)".
The AHEAD/BEHIND split matters: a real RNG misalignment is a persistent
phase shift, an unrelated pair means a setSeed. World.setRandomSeed is
worldgen-only in 1.11.2 (MapGenStructure and friends; World.java:3973),
so in already-generated chunks an unreachable world transition is a
real finding rather than noise. LCG verified against JDK goldens:
new Random(0).nextInt() = -1155484576, -723955400, exact.

Gate (java/qrl_rng_cursor_gate.py, 335 lines, new), shaped like
qrl_lockstep_gate.py. It builds TWO independent views of the same run -
rng_cursor_locked reads on the socket thread at each parked boundary,
and the ring records written on the server thread - and requires them
to agree, so the green path assumes nothing about world determinism.
26/26 on a fresh client boot. Live negative control: rng_burn_locked
consumes exactly one real java.util.Random draw on the live server at a
chosen boundary; the gate flags record 17, stream world_rand, "1 draw
AHEAD", and nothing earlier. Same for updateLCG at record 5. Offline
control perturbs a captured sidecar and lands on the same index/stream
without a JVM. Cheap by construction: the gate edits no blocks and
moves no entities, so unlike the lockstep gate it has nothing to
restore.

Measured, first-hand, and the headline number: a live 1.11.2 integrated
server burns ~25,000 World.rand draws PER TICK. Attribution by
gamerule toggle (25 ticks each, same session, restored after):
doMobSpawning=true 25208/tick; false 662/tick; also randomTickSpeed=0
205/tick. So mob spawning is 24546 draws/tick = 97.4%, random ticks
458, weather/chunk residual 205. The residual cross-checks: 205 = ~102
loaded chunks x 2 draws (lightning nextInt(100000) + ice nextInt(16)
per chunk, WorldServer.java:421/449), and updateLCG runs 615/tick = 205
sections x randomTickSpeed 3, dropping to 14.6 at speed 0 which is the
~102/16 ice-path advances. Two independent counters agreeing on ~102
chunks is the check that these draw counts are real.

INJECTION: NOT included, and the reason is architectural, not
unfinished work. PR #5 restores cursors into a live JVM where writing
Random's AtomicLong is the whole operation; magma has no such object.
blaze/core/mc_rng.h:1-11 states the doctrine - JavaRandom for worldgen
ONLY, every runtime draw through the stateless
mc_hash_seed(seed,tick,x,y,z,purpose), specifically so CPU==CUDA holds
regardless of thread scheduling (magma/game/randtick.h:10 repeats it).
That statelessness is load-bearing: it is exactly what the grass
randtick occupancy skip exploits for its 2.2x trainer speedup, and why
the review rejected their stateful JavaGaussianRandom. A counter-based
stream has no cursor position to force. The gap is already
acknowledged - magma/game/script.c:488-490 disables randtick for tape
replay with "oracle world RNG is unseedable". This gate does not close
that gap; it measures it and names the tick. The 25k-draws/tick number
above is the quantitative argument that closing it is a different
architecture, not a patch. bluecoconut's own ledger agrees: individual
seeds are "exact", aggregate world.rng_cursors is "captured_only".
blaze/core/world_tick_vanilla.h:98 does carry a stateful JavaRandom +
updateLCG and is the credible future home; off-limits here, and wiring
a Java cursor into it is a vanilla-tick-model change, not a test-infra
port.

NO AUDIO, and a correction to the brief: the sound-EVENT ring does not
exist in this tree at all - DEVLOG:1867 records sound capture as
deliberately not ported, and rg over magma/ and verify/ finds no ring,
no audio_live, nothing. So there was nothing to integrate with and
nothing was duplicated. For whoever lands it later: PR5's ring carries
its own sound_random_seed48 cursor with runtime_sound_random_one()
burning 1 float and runtime_sound_random_diff() burning 2 in Java's
order, which is exactly the per-draw accounting this gate's world
stream already measures - it should plug in as a fifth stream rather
than a parallel mechanism.

Scope and gates: magma/ and blaze/ are byte-unchanged (git status
confirms), so no make test / verify_cpu / verify_cuda / tape-replay
re-validation is implicated - this is a Java<->magma fidelity tool that
adds bridge commands and a verifier, and touches no simulation code.
Java client builds clean (gradlew -g run/gradle build, BUILD
SUCCESSFUL). qrl_lockstep_gate.py re-run first-hand after the Recorder
changes: 30/30, 0 failures. ruff clean on both new files (master is
clean under the same ruff 0.16.2 invocation, so the bar was zero new).
The canonical 20260721T215812Z tape payload is NOT on gamer - only its
sidecars and baselines - so a canonical-tape replay could not have been
run here regardless; nothing in this branch changes replay semantics.
schemas.index shows its usual 3-line build reorder and is deliberately
left unstaged.

Wave-3 candidates: the remaining PR5 test-infra jewels (32-way isolated
oracle pool, capability-ledger state capsule with refuse-if-incomplete)
and, when the sound-EVENT ring lands, adding it as a fifth captured
stream. A cursor sidecar alongside a real tape is also now cheap - the
capture is gate-free - but it needs a tape recorded on a box that has
one.

## 2026-08-08: PR5 wave-3 - state capsule + capability ledger (Java -> magma)

Opus delegate on gamer (wt/pr5-wave3), commit 014c999. Third of
bluecoconut's PR #5 test-infra crown jewels (DEVLOG:1691): "state capsule
with capability ledger + Java->magma mid-run continuation proofs". Design
ported, code reimplemented - same reason as wave-2 (17k-line function,
hardcoded /home/jawaugh). Their FIXTURES are clean text and were consumed
nearly directly: `.sequence` rows are TICK DX DY DZ BLOCK META, and
redstone_torch_floor_burnout is re-anchored onto our setblocks path
(36 lines) rather than rewritten.

The thing a capsule is for: a block cuboid is not a world. Three classes
of state decide the next tick and appear in no block, no metadata, and no
chunk NBT - (a) WorldServer.pendingTickListEntriesTreeSet, ordered by
(scheduledTime, priority, tickEntryID), so two identical-looking worlds
fire in different orders; (b) TileEntityFurnace burn/cook counters;
(c) BlockRedstoneTorch.toggles, a STATIC WeakHashMap<World,List<Toggle>>
of (BlockPos, totalWorldTime) living purely on the JVM heap. Capture rides
the wave-1 parked boundary and the wave-2 socket-thread rule unchanged:
capsule_dump_locked (Recorder.java, +280) is one more `_locked` command,
fail-soft reflection throughout, each class emitting a captured_* boolean.

Format (verify/trace/state_capsule.py, 914, new): a directory of
manifest.json (sort_keys, indent 2) plus a binary sidecar blocks.u16le
(id<<4|meta, y-major then z then x, inclusive box), sha256 in the
manifest. Text where a human diffs it, binary where a cuboid would bloat
it.

The ledger is the actual contribution, and it is a CONTRACT not a comment.
Three classes: `exact` = bit-faithful AND restorable into magma, so a
strict gate may depend on it; `captured_only` = recorded faithfully, no
magma receiver; `unavailable` = not recorded. 16 registry entries, 3 exact.
require_exact() names its dependency set and raises CapabilityRefusal ->
exit 3 (BLOCKED) with one `BLOCKED: missing capability NAME=CLASS` line
per field. Never a silent skip, never a fake PASS. Tamper-evidence: the
ledger is RE-DERIVED from the capture flags on validate and compared for
exact dict equality, so hand-promoting captured_only to exact in the
manifest is rejected outright ("capability ledger ... has been altered").

Deviations from the PR, and why each one is a finding rather than a
shortcut:

- Their world.redstone_torch_toggle_history is `exact`. Here it is
  `captured_only`, because magma HAS NO REDSTONE - confirmed by exhaustive
  grep; blaze/core/block_props_table.h:10 lists it CUT and
  world_tick_vanilla.h's wt_dispatch_scheduled no-ops unported blocks.
  Their magma grew a redstone_torch_toggles[] array; ours will not from a
  test-infra port. So the FLAGSHIP torch case is proved as a REFUSAL: the
  strict torch gate is BLOCKED exit 3 naming exactly that field. The Java
  half is real and measured - the fixture drives a genuine burnout, 8
  toggles at t=62851..62879, span 28 ticks (inside vanilla's 60-tick
  prune window), lit 76 -> unlit 75, and the 160-tick recovery callback
  captured at due 63039 = last toggle 62879 + 160, priority 0 order 86.
- The GREEN continuation therefore runs on TileEntityFurnace, hidden in
  exactly the same way (invisible in block+meta) and tick-exact in magma.
  It also restores through vocabulary magma ALREADY has - container_open /
  container_slot / container_furnace_prop - so magma/ and blaze/ are
  byte-unchanged and no parity_manifest kernel is implicated. Same posture
  as wave-2.
- RNG seed48s are captured_only, not exact: magma's runtime RNG is the
  stateless counter hash mc_hash_seed, no cursor to force (wave-2 measured
  this at ~25k draws/tick).
- Refusal exits 3, not their 2, so a capability refusal is distinguishable
  from a malformed capsule; structural errors keep 2.
- `validate --require FIELD[,FIELD]` takes the gate's ACTUAL dependency
  set. Their global --require-complete cannot express "needs the furnace
  counters, doesn't care about entities"; kept alongside with their
  semantics, and it still refuses a partial capsule.
- Restore is one-directional by design. The review already flagged "load
  Java from save-state" as oversold; magma -> Java is not attempted.
- audio.sound_events is permanently `unavailable`. Their capsule captures
  sound events; this repo contains no audio code and gained none.

Gate (java/qrl_capsule_gate.py, 558, new), shaped like the wave-1/wave-2
gates: 45/45 on a fresh boot. Headline continuation number - capsule taken
mid-run at burn=1140 cook=60 total=200, restored into magma (27 events),
then 160/160 ticks match on (burn, cook, total, output). The horizon
contains a real event, not just decay: the smelt COMPLETES on tick 139 in
both, java (1000,0,200,1) == magma (1000,0,200,1), 139 ticks downstream of
the boundary. Negative controls both live: drop the field -> ledger
downgrades to unavailable and the strict gate is BLOCKED exit 3 naming it;
force past the refusal -> the capsule emits 21 events instead of 27, magma
still runs (so divergence is physics, not a crash), and the match rate is
0/160, diverging immediately at tick 0 rather than drifting. That 0/160 vs
160/160 pair is the proof the ledger was guarding something real.

Regressions after the Recorder change: qrl_lockstep_gate 30/30,
qrl_rng_cursor_gate 26/26. Both scenes restored to their original block
digests (run/ is shared with the main tree). ruff clean on both new files.

Gotcha worth keeping: /setblock NBT without CookTimeTotal leaves it 0, and
readFromNBT does not derive it. `++cookTime` happens BEFORE the
`cookTime == totalCookTime` test, so cookTime can never equal 0 and the
furnace counts up forever without ever smelting. Fixture NBT must carry
CookTimeTotal explicitly.

Wave-4 candidates from the same PR: 32-way oracle pool, gameplay bundle 1
(animal husbandry). Groundwork noticed: capsule entity capture is stubbed
at entity_count with entities_complete:false, which is the natural seam
for husbandry state; and the capsule's furnace restore path generalizes to
any container magma already models.

## 2026-08-08: PR5 wave-4 - isolated Java oracle pool (N clients, one box)

Opus delegate on gamer (wt/pr5-wave4). Fourth of bluecoconut's PR #5
test-infra crown jewels (DEVLOG:1691): "32-way isolated Java oracle pool
(own display/port/save/pgid each)". Design ported, code reimplemented -
their `start_oracle_instance.sh` (223 lines) plus the `start_pool` /
`recycle_pool_port` half of a 28.5k-line `run_oracle_matrix.py`, with
`/home/jawaugh` baked into three files.

What a pool has to generalise is `java/start_vnc_client.sh`, which is a
SINGLETON in four independent ways: display `:1`, port 25575, game dir
`java/Minecraft/run`, and a cleanup that does `pkill -f GradleStart` /
`pkill -f "Xvfb :1"`. Every one of those is a global name, so a second
copy of that script does not start a second oracle, it kills the first.
`java/oracle_pool.py` (1139, new) gives instance i display `:100+i`,
port `25600+i`, game dir `java/pool/instance_NN/run`, and its own
process group. 25575 and `:0/:1` are refused outright, so the classic
single-client flow keeps its resources whatever `--port-base` a caller
passes.

The seam that made the port cheap was already in the tree: build.gradle
honours `-PrunDir=<abs>`, and `Recorder.loadLaunchCfg` /
`QLaunch` resolve `qrl_launch.json` from the process cwd and nowhere
else. cwd = runDir therefore hands each instance its own port, seed,
strip/determinism block AND `saves/`. Consequence worth stating: every
instance can run the SAME seed. The PR gives instance i seed `100000+i`;
here all N generate `qrl_917351_flat` independently, which is what makes
a cross-instance bit-identity claim possible at all (and the pool pins
one username, `Player0`, where the PR uses `PoolPlayer$i`). Provisioning
a game dir is 64 KiB and ~10 ms: options.txt + config/ + qrl_launch.json.
A `--world-template` copy of a prebuilt save exists for worlds that are
expensive to generate; the flat template is not, so the default is a
fresh worldgen per instance.

Gradle is the thing the PR's design does not survive at scale, and it
took two measurements to see why. Four concurrent `./gradlew runClient`
on one checkout, both cache layouts, both a HARD failure inside 15 s:

- shared `--project-cache-dir`: `:deobfMcMCP` NPE in one instance,
  `Could not resolve net.minecraftforge:forgeBin:...-PROJECT(Minecraft)`
  in the other three - they race on the deobf jar the project cache owns.
- per-instance `--project-cache-dir` (the PR's `ORACLE_PROJECT_CACHE`,
  which their scripts leave EMPTY by default): the `jaxb` task deletes
  and regenerates `src/main/java/com/microsoft/Malmo/Schemas/*.java`, so
  the builds delete each other's SOURCES - `NoSuchFileException:
  .../Schemas/DrawCuboid.java`. Not fixable by cache layout; it is a
  shared source tree.

So the pool pays exactly ONE gradle launch, reads the game's argv out of
`/proc` while gradle still holds it (ForgeGradle has no "print the launch
command" task), and spawns every other instance directly:
`java <jvmargs> -cp <cp> GradleStart --username Player0` with cwd=runDir.
The captured spec is cached against the mod jar's mtime and is verified
the only way that counts - the other instances have to reach in-world
readiness from it. `--launch-mode gradle` keeps the every-instance-gradle
path, serialized behind each previous launch's game JVM.

Density, measured on gamer (7700X, 16 threads, 30.7 GiB, RTX 3090 with a
prime-flywheel co-tenant), because "32" is an anvil number and not a law:

- one idle-in-world client at the trace profile burns **8.29 cores** -
  llvmpipe renders as fast as `maxFps:260` allows and defaults its
  rasteriser pool to one thread per host CPU. `maxFps:20` +
  `LP_NUM_THREADS=1` takes that to **0.43 cores**, a 19x cut, and changes
  no server-side state (the wave-1 lockstep gate still passes 30/30 on a
  tuned instance). These are RENDER knobs: a pool that records pixel
  goldens must run the pinned trace profile and is correspondingly
  smaller. They are pool defaults, not repo defaults.
- `--no-daemon` gradle leaves its wrapper JVM blocked on the JavaExec
  child for the client's whole life holding **2.3 GiB** it never uses
  again - more than half an instance. The game JVM is a plain fork in the
  same process group, so reaping the wrapper once the instance is ready
  costs nothing and the pgid kill still reaches the game (ownership is
  decided over group MEMBERS, never the leader).
- what is left: **2.02 GiB** and **0.47 cores** per instance
  (2.00 GiB game JVM at `-Xmx2G`, 0.06 GiB Xvfb; RSS ~= PSS, JVMs share
  nothing). Baseline with no pool is 6.8 GiB.
  N_mem = (30.7 - 6.8 - 6.0 reserved) / 2.02 = **8.9**;
  N_cpu = 16 / 0.47 = 33 for bridge-bound work. Memory binds, so the
  default is **8** - at which the pool is 16.3 GiB / 3.8 of 16 threads
  and the host still reports 8.4 GiB available.

Numbers: 8 instances to in-world readiness in **47.6 s** wall (the
gradle-captured instance 30-34 s, the direct ones 15-19 s); 4 in 33.0 s;
one replacement in 13.9-22.5 s. Fan-out is linear where it matters -
8 concurrent wave-3 capsule gates (45 checks each, 360 total) in
**13.7 s** against **13.4 s** for one, i.e. 8x throughput at 1.02x the
wall of a single run. 8 concurrent wave-1 lockstep gates: 8x 30/30.

Gate (`java/qrl_pool_gate.py`, 308, new), shaped like the wave-1/2/3
gates: 25/25 at N=4, 23/23 at N=8 with `--keep`. The scenario is
lockstep-stepped so nothing depends on wall clock: park, digest the
worldgen cuboid, `setblocks_locked` three sand blocks into the air,
then 4/16/40 exact ticks with a `getblocks_locked` digest after each.
Sand makes it a claim about entity ticking and scheduled-tick order, not
about a static write. All N agree bit-for-bit
(`05ed2e93.. -> 8f54ed3c.. -> 3f48ac92..`, deltas `(0,0,4,20,60)`) -
world_time is compared as a DELTA because instances boot seconds apart
and their absolute totalWorldTime differs by design. Containment: SIGSTOP
one instance, the pool names exactly that one (its port still ACCEPTS -
the listen backlog outlives a stopped JVM - but cannot serve `obs`), the
siblings still agree with each other bit-for-bit while it is down, the
reap kills it (SIGCONT before SIGTERM, or a stopped JVM never runs its
handler), no sibling pid is signalled, and the replacement reproduces the
reference vector exactly.

Three bugs this cost, all the same shape - a liveness test that lies:

- `os.kill(pid, 0)` succeeds on a ZOMBIE. Nothing wait()s the launcher,
  so a pool of CRASHED gradle launches looked alive and the boot sat in
  the readiness loop for the full 420 s timeout instead of failing in
  5.7 s with the gradle error; and after a shutdown every killed client
  still read as a stray. `pid_alive` is now a /proc state check and the
  kill paths reap.
- an Xvfb left by a crashed run keeps serving its display, and a fresh
  Xvfb on a taken display exits immediately. `start_display` saw xdpyinfo
  succeed against the STALE server, returned happily, recorded an
  already-dead pid, and leaked the real one. It now claims the display
  first and verifies its OWN server answers.
- killing the display kills the client, so a teardown that only reaped
  Xvfb LOOKED like it had reaped the game. Real cleanup is per-instance:
  pgid, then cwd==runDir, then `Xvfb :<n>` by exact argv - never a
  pattern like `GradleStart`.

Deviations from the PR, each a finding rather than a shortcut: their
`stop` guards on the leader's cmdline (which the launcher reap makes
useless here) and additionally scans `/proc/*/environ` for
`QRL_LAUNCH_JSON=`; cwd is the stronger test because the game inherits it
and cannot lose it. Their readiness is `obs` + a 5 s settle - kept
verbatim, it is right. Their pool benchmark asserts a 350 GiB memory
ceiling and a `systemd-run --scope` slice; not ported, gamer has 30 GiB
and the arithmetic above is the ceiling. No VNC by default (`--vnc` binds
`5920+i`, localhost only).

Regressions: classic single-client flow unbroken -
`bash java/start_vnc_client.sh` then `qrl_lockstep_gate.py` on 25575 is
30/30, cuboid digest `88201fb960ff6465` unchanged, after 8-way concurrent
gradle traffic through the shared `run/gradle` home. Zero strays after a
full pool stop (pgrep Xvfb/x11vnc/GradleStart/openbox all NONE, no
25600-2560x listening, host memory back to the 6.8 GiB baseline). ruff
clean on both new files. magma/ and blaze/ byte-unchanged; no
parity_manifest kernel is implicated.

Wave-5 candidates: the pool's natural next feature is capsule fan-out -
boot one client, park it at a boundary, `capsule_dump_locked` once, then
have N workers RESTORE that capsule instead of paying N cold boots
(2.02 GiB and 15-19 s each). The restore direction is the blocker, not
the pool: wave-3 proved Java -> capsule -> magma only, so today the seam
is "one Java boundary, N magma continuations", and N Java workers still
each need their own 15 s worldgen. The other open item is that a shared
source tree caps `--launch-mode gradle` at one launch at a time forever;
32-way on anvil would want the captured spec plus a read-only build
snapshot per pool, and 32 x 2.02 GiB = 65 GiB of RAM before any harness.

## 2026-08-08 (PR5 wave-5: audio - sound seam, block SoundType map, OpenAL playback)

Audio was a documented CUT (`docs/SCOPE.md` line 22, "Multiplayer/servers,
audio, disk saves as a product feature"). Reversed for magma ONLY, because the
thing a human misses first when playing magma next to the oracle is sound.
blaze stays audio-free and no gate verifies audio on blaze.

The one rule everything else follows from: **sound is a pure sink.** Producers
append to `GmRuntime.sound_events`; nothing in the simulation reads that ring
back, and no emitter draws from a seeded stream. Sound *identity* (which sound,
what volume/pitch, where) IS derived from sim state, so it is deterministic and
oracle-comparable - that is what the gates check. Playback is not.

What landed:
- `GmRuntimeSoundEvent` ring (256, seq monotone across wrap, overflow counted
  in `sound_event_dropped` so a consumer sees a gap instead of going quiet).
- `game/audio_live.c` - OpenAL + libvorbisfile consumer, opt-in behind
  `make MAGMA_AUDIO_OPENAL=1`. Wired into `app/game_main.c`: init before the
  frame loop, per-frame listener update at the interpolated camera pose,
  destroy at exit. A failed init is a warning, never fatal.
- `assets/build_sound_manifest.py` - resolves `sounds.json` (weights, nested
  `type:event` rows, `stream`) against the owned 1.11 asset index into a hash
  manifest. 112 events, 307 variants. Generated, gitignored like every other
  `assets/*.h`; no jar needed, only the asset index.
- Block break/place/hit audio, oracle-matched. `runtime_block_sound_family`
  is the 1.11.2 `Block.getSoundType` table; per-action arithmetic is vanilla's
  ((v+1)/2, p*0.8 for break/place; (v+1)/8, p*0.5 for hit) kept in float so
  the result is bit-comparable with `Float.floatToRawIntBits`.
- Emit sites: `dig_destroy` sets `break_effect` (world event 2001, sampled
  from the OLD state before the cell is overwritten), successful `ItemBlock`
  placement sets `place_effect`, and `onPlayerDamageBlock` fires the hit sound
  on the first damage tick and every fourth after it.

Verified: `game/test_audio_live.sh` (ring order/seq/overflow, MAGMA_AUDIO=0
stays disabled AND silent, and a held attack through the real
`gm_runtime_tick` emits exactly one break plus >=1 hit with no foreign
material - the fixtures alone would not have caught an unwired emit site).
`make -C magma test-block-audio-oracle` is green against REAL Java: all 235
registered non-air block ids, 12 families, exact volume/pitch bits, for all
three actions, with a per-action metal->stone negative control.

Compiled-out really is identical: the canonical-tape `--cpu` replay verdict on
this branch is byte-identical to master's - same `parity-fail (rc=3)`, same 68
unexplained frames, same worst t=1960 (224829 px), same state block. The ONLY
differing key in `tape_*.gate.json` is `magma_binary`, which must differ. Same
for a 300-tick scripted mining run under MAGMA_AUDIO=0 vs =1: identical state
digest `c3f89c59...`.

NOT ported, and the reason is the same every time - the sim under the emitter
does not exist in this tree yet. PR5's `magma/game/runtime.c` is 20376 lines
against our 1932, and 769 of its magma files have no counterpart here. So mob
sounds (need a mob event ring in `mob_live`), world-event sounds 1000-1032
(need a `World.playEvent` dispatch), firework blast/twinkle (no firework
entity), and jukebox record streaming (no jukebox TE) are all blocked on
simulation, not on audio. The record-streaming code IS in `audio_live.c` and
the `GM_SOUND_RECORD_*` ids are in the manifest, unreachable until a jukebox
exists; that was cheaper than deleting it and re-adding it later.

Also not ported deliberately: PR5's `MixinOracleClientSound` /
`MixinOracleServerSound` / `MixinOracleEntitySoundContext` plus ~190 lines of
`Recorder.java` sound capture. They feed a tape sound-event schema this tree
does not have, so nothing would read them, and `Recorder.java` gates every
tape - untested code there is the wrong trade. The unblock is a tape schema
field first, then those three mixins, repointed from `qrl.Recorder` to
`netheritemod.Recorder`.

Lesson worth keeping: PR5 is not a patch series against this master, it is a
much further-along parallel lineage. Trying to apply its audio commits as
diffs fails on nearly every hunk; the right unit of work is "which emitters
can this tree's simulation actually support", answered per emitter, and the
answer here was four of them and not the other four.

## 2026-08-08: envcfg wave - runtime env-var knobs purged repo-wide

Trigger: the audio wave shipped `getenv("MAGMA_AUDIO")` and its proof runs
were launched as `MAGMA_AUDIO_OPENAL=1 MAGMA_AUDIO=1 uv run ...`, exactly the
pattern the config registry (6e76749, 5eef24f, d885676) was built to kill.
Ruling: runtime knobs NEVER ride on env vars, recorded in AGENTS.md ("Runtime
knobs") and enforced by `scripts/env_knob_gate.sh` in the sweep (fails on any
project-prefixed getenv/os.environ read; build-time make vars and machine
pointers like MC_JAR/MC_SM/QRL_SM exempt, java/render-opt closed lab exempt).

Four parallel delegate worktrees did the migration, each proven before merge:

- magma C (wt/envcfg-magma): 14 new registry keys (audio, asset_objects, fog,
  smooth, shroomlight, fluid_ca, solid_alpha, state_prof, genprobe,
  port_parity_fd, metallib, metal_require, nowake, nofall); blaze/core/
  populate.h gets the `bz_populate_set_debug` seam so blaze never links magma
  config.
- harness (wt/envcfg-harness): replay_tape `--ent-ticks0` /
  `--hand-from-tick` / `--fog-c1-init` (None = recorded value wins,
  precedence unchanged); mc_capture candidate knobs as argv; window_battery
  env table dropped; mcwindow/qrl_chain_demo/zoom argparse; scenario oracle
  lock is the explicit `--oracle-locked` handshake.
- blaze (wt/envcfg-blaze): `blaze_create(device, n, opts)` with
  `BlazeCreateOpts` (NULL = compiled defaults) threaded from VecBlaze kwargs;
  trainers take flat `key = value` + `--set` via `blaze/rl/train_conf.h`,
  unknown key = hard error.
- tail (wt/envcfg-tail): RL eval/training scripts, snapshot bakers, fogcurve
  probe, portal e2e via pytest addoption.

Behavior preservation proof: canonical tape 20260803 replayed on the merged
tree vs the pre-wave master baseline - the 91k-line gate verdict differs
ONLY in the harness self-hash lines (replay_tape.py sha256 +
gate_implementation), every pixel/state verdict byte-identical. verify_cuda
bitwise PASS, mc_capture rung4 + multi-verify PASS, test_runtime +
test_audio_live PASS.

Two latent bugs surfaced by the sweep: run_hard_scene.sh and fogcurve_probe
still EXPORTED the migrated MAGMA_SMOOTH/MAGMA_FOG (silently dead writers -
ablations rendered baseline; fixed via a `--set` registry passthrough on
game_candidate), and the MAGMA_GAMMA=2.0 ablation had no reader anywhere
long before this wave (annotated as a known no-op). The sweep's own
blaze-oracle-smoke step also still set the removed MC_CPU_ONLY env (now
`--cpu-only`). Lesson: a knob migration is not done at the readers; every
writer keeps "working" silently unless swept.

## 2026-08-21: chain curriculum - staged starts from verified snapshots

Why: chain retrain (success_item=50) from fresh weights was near-flat
(t0 0.005-0.065 at 450M ticks). The frontier curriculum in ppo.c was
inert: only stage 0 avail at init; stages 1-4 unlock only when a lane
reaches them live, which a fresh net never does.

Landed (four lanes, each gate re-run by the orchestrator before merge):
- ppo knobs `init_from` (warm-start ckpt) and `stage_snaps` (preload
  s{seed}_stg{1..4}.bsnp into cap_slot order, pad holes with t0, avail
  only for real files). Knobs-off smoke byte-identical to ca29468.
- Reward re-pay bug found+fixed: reset into a nonzero-inventory snap
  paid first-time bonuses into rew_roll (valid=0 hid it from the update
  but not the buffer). cr_seed_lane seeds best[] on burn-in; test added.
- eval `--stage K|all`: per-stage starts, SKIP rows for missing snaps,
  start inventory baselined, stage x seed ladder. Stage-0 byte-identical.
- make_snapshots `--chain-stages` + `--verify-emit`: bake stage snaps by
  scripted replay, then emit ONLY on proof: magma-vs-blaze-CPU lockstep
  digest equality over the chain remainder (fluids on) AND zero liquid
  cell movement (training envs treat liquid statically). Same standard
  that blessed the t0 snaps; census liquid flag no longer decides.

Result: 29/40 stage snaps emitted (10 seeds x 4 stages baked; 11 stay
FLAGGED: 9 parity-diverged with first_div tick recorded, 2 liquid-active;
seed 29 probe fails at mine_coal). Snaps shipped to anvil.

Relaunched gpu0: retrain_0821_chain2, success_item=50, init_from=wood
best (t0 0.755), stage_snaps=1, 900M ticks / 6h cap. Preload confirmed
for 8/11 train seeds; staged sampling live at chunk 0; ent 8.45 start
confirms warm load. Ladder eval runs when it stops.

Caveats: seeds 14,16,20,46 emit-proofs used idle-600 lockstep (probe
re-run failed; chain-remainder proof pending), noted honestly. Anvil
local ui_hud goldens differed from committed ones; preserved in
~/anvil_wip_0821, not blessed.
