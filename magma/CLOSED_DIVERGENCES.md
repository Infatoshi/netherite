# Closed divergences

Resolved, retracted, or superseded entries moved out of OPEN_DIVERGENCES.md
so the open file stays an actionable list. Entries are preserved verbatim
(full forensics) because they document why a question is settled; read them
before re-investigating anything that smells similar. Newest at top.

### XP orb lava/water/pushOut: CLOSED 2026-08-23 (lane/xplava)

Anvil. Sweep 2026-08-23 magma row 7 leftover. Magma extras in `xp_live.h`
were no lava hop, no `pushOutOfBlocks`, no `handleWaterMovement`.

Java `EntityXPOrb.onUpdate` (`EntityXPOrb.java:87-174`):
`super.onUpdate` runs `Entity.onEntityUpdate` (`Entity.java:460-535`)
which calls the override `handleWaterMovement` (`EntityXPOrb.java:179-182`)
`World.handleMaterialAcceleration(getEntityBoundingBox(), WATER)`
(`World.java:2333-2398`). Unexpanded box, no `Entity.java:1311`
`expand(0,-0.4,0).contract(0.001)`. Liquid-height
`BlockLiquid.getLiquidHeightPercent` (`BlockLiquid.java:60-68`).
`BlockLiquid.modifyAcceleration` adds `getFlow` (`:196-198`, `:139-194`).
`isPushedByWater` default true (`Entity.java:3057-3059`); 0.014 * unit
(`World.java:2391-2394`). Then delay (`:91-94`), gravity
`(double)0.03F = 0.029999999329447746` (`:100-103`), lava at
`BlockPos(this)` (`:105`, `BlockPos.java:42-44` / `Vec3i.java:25-27`
`MathHelper.floor`): `Material.LAVA` is still 11 or flowing 10,
`motionY = 0.20000000298023224D` (`:107`). xz
`(rand.nextFloat()-rand.nextFloat())*0.2F` (`:108-109`) and burn
`nextFloat` (`:110`) are `Entity.rand = new Random()` unseeded
(`Entity.java:238`) CLASS C; skip both sides like `item_live.h`
EntityItem lava hop. `pushOutOfBlocks`
(`EntityXPOrb.java:113` / `Entity.java:2651-2720`):
`collidesWithAnyBlock` false returns at `:2658` with no rand (dry
arena rests ON a cube; `AxisAlignedBB.intersectsWith` strict `<`).
True would draw `rand.nextFloat()*0.2F+0.1F` (`:2697`) CLASS C;
skip the magnitude.

C: shared `eo_tick` / `xl_tick_orb` (`entity_xp_orb.h`,
`xp_world_tick.h`). Magma `tick_xp_orbs` and blaze `cu_xp_tick` wrap
the same kernel. Dry arena bit-exact. Units:
`magma/tests/test_xp_orbs.c`, `blaze/env/test_xp.c`.

Stay out: Mending (`EntityXPOrb.java:248-255`); spawn xz
`Math.random` (`:40-43`); item overflow; chest realloc; block light.

Gate: `make -C magma test-xp-orbs` and `make -C blaze/rl` test_xp PASS.
xp_orbs M1+M2 VERIFIED (`out/verify/xplava_m1_xp_orbs.log`,
`out/verify/xplava_m2_xp_orbs.log`). Listed `--no-deps` M1 VERIFIED
all named rows (`out/verify/xplava_m1_all.log`). M2 VERIFIED
raw/warp/scalar for those including mining_slice on this clone
(`out/verify/xplava_m2_all.log`; snaps present under
`blaze/rl/out/snaps/*_d*.bsnp`). A clone without those snaps would
BLOCK mining_slice M2. Root `make test` PASS
(`out/verify/xplava_maketest.log`). Tapes: bow physics NO divergence
1407 / entities 5525; creeper FIRST DIVERGENCE t=76 y 2.1e-09; smoke
zombie 358/373 through death; TNT physics NO divergence, inventory
t=28 slot 0 flint-and-steel 259 tape meta 0 vs magma meta 1;
canon 3617 / entities 16526.

### Beds: CLOSED 2026-08-23 (lane/beds)

Anvil. Sweep 2026-08-23 magma row 9.

Java: `ItemBed.onItemUse` (`ItemBed.java:28-83`) requires click facing UP
(`:34-37`), yaw `MathHelper.floor((double)(rotationYaw * 4.0F / 360.0F) +
0.5D) & 3` then `EnumFacing.getHorizontal` (`:49-50`), both cells
replaceable-or-air, `isFullyOpaque` below both (`:61`). FOOT then HEAD
meta (`:63-65`). Not `World.mayPlace`. `BlockBed.onBlockActivated`
(`BlockBed.java:47-121`) maps FOOT to HEAD along FACING (`:54-64`);
occupied message (`:68-75`); `trySleep` then OCCUPIED (`:82-87`).
Nether/End explode (`:108-119`) is `!canRespawnHere`; magma keeps that
path for `dimension != 0`. `EntityPlayer.trySleep` (`EntityPlayer.java:1637-1707`)
checks in order: OTHER_PROBLEM, NOT_POSSIBLE_HERE, NOT_POSSIBLE_NOW,
TOO_FAR_AWAY, NOT_SAFE, then pose `0.5F + facingOffset * 0.4F` and Y
`(float)y + 0.6875F` (`:1685-1688`). Monster AABB 8.0/5.0/8.0 (`:1665-1667`).
`bedInRange` 3/2/3 (`:1710-1720`). `onUpdate` sleep branch (`:228-257`)
is World.updateEntities after WorldServer.tick. `isPlayerFullyAsleep`
at `sleepTimer >= 100` (`:1841-1843`). `WorldServer.tick` (`:191-200`)
`areAllPlayersAsleep` then `setWorldTime((time+24000) - (time+24000)%24000)`
then `wakeAllPlayers` (`:287-302`) `resetRainAndThunder` (`WorldProvider.java:584-589`).
`isDaytime` is `getSkylightSubtracted() < 4` (`WorldProvider.java:450-453`).
`getBedSpawnLocation` (`EntityPlayer.java:1779-1800`) /
`getSafeExitLocation` (`BlockBed.java:195-228`) / `hasRoomForPlayer`
(`:231-234`). Respawn (`PlayerList.java:538-580`) uses that, else world
spawn; obstructed bed sends `SPacketChangeGameState(0,0)`.

C: shared `blaze/core/player_bed.h`. Magma `player_ctl.c` ItemBed place;
`runtime.c` trySleep / sleep onUpdate after `gm_world_tick` / spawn.
`ww_tick_gated_sleep` is skip=0 on every RL tick so `BP_WEATHER` stays
equal. RL has no sleep action. `ibp_may_place` does not fit ItemBed.

Stay out: nether explode as an RL path; potion/shield; snapshot sleep
fields (resumegate).

Gate: unit tests `make -C magma test-beds` and `make -C blaze/rl test-beds`.
No bed/sleep tape in `verify/tapes` (only dropped item 355 on detmob
wander). Tape physics unchanged vs parent. M1 `--no-deps` VERIFIED all
listed rows including `weather_optional`. M2 VERIFIED raw/warp/scalar
for those except mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp`
missing). Root `make test` PASS.

### Explosion residuals: CLOSED 2026-08-23 (lane/expresid)

Anvil. Sweep 2026-08-23 magma row 4.

Java: `EntityCreeper.explode` (`EntityCreeper.java:303-314`) calls
`world.createExplosion(this, posX, posY, posZ, (float)explosionRadius * f, flag)`
with `f = getPowered() ? 2.0F : 1.0F` (`:308`) and `flag = mobGriefing` (`:307`).
Lightning sets POWERED (`:274-277`). `World.createExplosion` (`World.java:2436-2438`)
passes `isFlaming=false`, `isSmoking=flag`. TNT `createExplosion(..., true)`
(`EntityTNTPrimed.java:114`) always smokes. `getBlockDensity` (`World.java:2456-2494`)
samples `1/((len*2)+1)` then `rayTraceBlocks(start, end)` (`:998-1000`,
stopOnLiquid false) against collision AABBs. Blast knockback is
`EnchantmentProtection.getBlastDamageReduction` (`:99-108`); damage magic
absorb is `applyPotionDamageCalculations` (`EntityLivingBase.java:1483-1487`)
`CombatRules.getDamageAfterMagicAbsorb` (`CombatRules.java:14-18`).
`doExplosionB` sound is two `world.rand.nextFloat` (`Explosion.java:198`);
server `WorldServer.newExplosion` (`WorldServer.java:1250`) skips particle
draws. Flaming fire is `explosionRNG.nextInt(3)==0` on air above
`isFullBlock` (`Explosion.java:249-257`); `explosionRNG` is `new Random()`
(`:65`). Live flaming sources: `EntityLargeFireball.java:47` both flags
= griefing. Creeper/TNT are not flaming. ItemFireball / EntitySmallFireball
place fire without an explosion. `EntityCreeper.explode` has no rand draws.

C: shared `explosion.h` / `explosion_live.h`. Creeper origin is posY.
Powered aliases `RlSnapMob.screaming` on `EW_TYPE_CREEPER` (zero default,
no snapshot version bump). `isSmoking` gates destroy; TNT hard 1.
Density uses movement collision AABBs. Snapshot inv has no enchant
payload so live magic absorb is identity; units plant `IcEnch`.
EXP digest stays EXP4 (no new hashed fields).

Stay out: weather lightning strike as a live powered source; fireball
`explosionRNG` tape-exact (unseeded `new Random()`); armor enchants in
`.bsnp`; witch/item/M2-harness.

Gate: explosions M1+M2 VERIFIED 64 ticks t=0 digest `0xcc693e0d9377745c`
(`out/verify/expresid_explosions_m1_detail.log`). Fixture
`s10_t0_r64_explosions.bsnp` baked by `test_explosions --write-fixture`
(charged creeper + bottom slab). mining_slice M2 BLOCKED
(`blaze/rl/out/snaps/*_d*.bsnp` missing). TNT inventory mismatch
unchanged: t=28 slot 0 item 259 tape_meta 0 magma_meta 1. creeper_encounter
FIRST DIVERGENCE still t=76 y 2.1e-09.

### mayPlace / TNT flint / boats / container leftovers: CLOSED 2026-08-23 (lane/tntsupport)

Anvil. Sweep 2026-08-23 magma row 6 + silent mayPlace; blaze rows 10, 12
and do_place reachability on row 2.

Java: `ItemBlock.onItemUse` `world.mayPlace(block, pos, false, facing, null)`
(`ItemBlock.java:49`). `World.mayPlace` (`World.java:3363-3368`) is dest
`isReplaceable` plus `canPlaceBlockOnSide` (= `canPlaceBlockAt`) plus
collision AABB unless `NULL_AABB`. Torch `canPlaceBlockAt` /
`canPlaceAt` (`BlockTorch.java:98-116`). Bush soil grass/dirt/farmland
(`BlockBush.java:39-51`, `Block.java:1890`). Cactus sand + open sides
(`BlockCactus.java:96-128`). Ladder any horizontal `isSideSolid`
(`BlockLadder.java:65-71`). Door `canPlaceBlockAt` (`BlockDoor.java:240-242`);
`ItemDoor.onItemUse` stays out. TNT flint is `BlockTNT.onBlockActivated`
(`BlockTNT.java:105-119`) then `explode` (`:85-96`) fuse 80
(`EntityTNTPrimed.java:25,38`). No world.rand on that spawn (`Math.random`
xz CUT). Flint `damageItem(1)` (`ItemStack.java:351-370`) maxDamage 64
(`ItemFlintAndSteel.java:20`). Boats `CraftingManager.java:140-145`. Cake
`:115`. Remaining items `ShapedRecipes.java:42-52` /
`ForgeHooks.getContainerItem` (`ForgeHooks.java:957-969`) /
`Item.java:1568-1577,1680` / `SlotCrafting.onTake` (`:132-166`).

C: shared `blaze/core/block_may_place.h` + `crafting_remaining.h`. Magma
`player_ctl.c` and blaze `blaze_core.h` both call `ibp_may_place` /
`ibp_tnt_flint_activate`. Magma runtime `gm_mobs_spawn_tnt_primed`; blaze
`cu_spawn_tnt_primed`. RL place is `act.use` `a[8]` mapped to `do_place`
like magma `player_ctl.c:687`.

Stay out: ItemDoor, snow-layer side rewrite, anvil-on-circuits, mushroom
light / deadbush sand / reeds water, thin ladder AABB vs full cube,
fire-charge TNT, Unbreaking flint, `Math.random` xz kick.

Gate: `placement` M1+M2 VERIFIED 96 ticks t=0 player digest
`0x9f0939bbcbfb06b2` (`out/verify/tntsupport_placement_m1_detail.log`).
Fixture `s10_t0_r64_placement.bsnp` baked by `test_placement --write-fixture`.
mining_slice M2 BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). TNT tape
inventory mismatch unchanged: t=28 slot 0 item 259 tape_meta 0 magma_meta 1.

### Furnace registry / buckets / food / hotbar: CLOSED 2026-08-23 (lane/furnaceids)

Anvil. Sweep 2026-08-23 magma rows 2, 3, 12 + silent hotbar; blaze row 11.

Java: `FurnaceRecipes.java:31-91` (51 addSmelting* rows, fish COD/SALMON),
`TileEntityFurnace.update:200-272` cook 200, lava `getContainerItem` ->
bucket (`Item.java:1569`), wet sponge + bucket (`smeltItem:327-330`).
`getItemBurnTime:340-355`. Item ids from `Item.registerItems` /
`Block.registerBlocks`. Empty bucket `Item.java:1566` setMaxStackSize(16);
filled `ItemBucket.java:32` / milk `ItemBucketMilk.java:17` = 1.
`ItemBucket.fillBucket:117-140`. `ItemFood.onItemUseFinish:55` burp
`nextFloat` then `:66` potion draw if potionId set. `InventoryPlayer.getBestHotbarSlot:162-185`. Subset has no `ench` flag so the unenchanted loop returns current after the empty search.

C: `smelting_recipes.h` vanilla ids (lava 327, fish 349, beef 363), XP on
each row, full fuel ternary + Material.WOOD block ids. Table derived by
`verify/furnace_registry.py` from oracle-src. `fft_tick` / `furnace_live.c`
/ blaze `cu_furnace_*`. `isr_max_stack_size` / `cc_max_stack_size`.
`ic_fill_bucket` + player_ctl / blaze_core twins. `ic_food_info` +
`jrand_float(&world_rand)` on eat finish. Snapshot furnace TE still not
in `.bsnp` (no version bump).

Gate: `furnaces` port_matrix M1+M2 VERIFIED 223 ticks (interact +
shift-click beef/coal + 220 idle). Fixture
`s10_t0_r64_furnaces.bsnp` baked by `test_furnaces --write-fixture`.
`make -C magma test-furnace-registry` 51 recipes PASS. mining_slice M2
BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). Tapes unchanged vs
baseline.
### Environmental damage on the player: CLOSED 2026-08-23 (lane/hazards)

Gamer. Magma sweep 2026-08-23 row 5 + blaze rows 6 (terminal death, documented)
and 7 (player fire). Shared rules in `blaze/core/player_survival.h`
(`psv_env_pre_move`, cactus in `psv_do_block_collisions`, magma
`onEntityWalk`). Magma `runtime.c` and blaze `blaze_runtime_tick_nr` apply
pending `hz_*` through `gm_mobs_attack_player` / `cu_hurt_player`.

Java 1.11.2 (server EntityPlayerMP.onUpdate decrements hurtResistantTime
then onUpdateEntity -> EntityPlayer.onUpdate -> Entity.onUpdate):
- AIR default 300 `Entity.java:256`. Drown `EntityLivingBase.java:297-320`:
  `decreaseAirSupply` (`:460-464`, respiration 0 => air-1); at air==-20
  reset 0 and `DROWN` 2.0F unblockable (`DamageSource.java:24`). Eyes in
  water: `ForgeHooks.isInsideOfMaterial` `:972-997` with
  `BlockLiquid.getLiquidHeightPercent` `:60-68`.
- IN_WALL 1.0F unblockable `EntityLivingBase.java:268-271` via
  `isEntityInsideOpaqueBlock` `Entity.java:2156-2186` (8 samples, 0.1F y
  and width*0.8F xz). `causesSuffocation` `Block.java:353-355`.
- LAVA 4.0F armor-applies + `setFire(15)` `Entity.java:563-567,605-629`.
  `isInLava` box expand -0.1/-0.4/-0.1 `:1416-1418`.
- ON_FIRE 1.0F unblockable when `fire%20==0` then `--fire` `:541-560`.
  `isWet` extinguish `EntityLivingBase.java:341-344` (`inWater` or caller
  `wet_rain`).
- CACTUS 1.0F `BlockCactus.java:133-136` from `doBlockCollisions`.
- HOT_FLOOR 1.0F `BlockMagma.java:45-50` unless sneak (Entity.java:1010
  walking trigger) or frost walker.
- OUT_OF_WORLD 4.0F `Entity.java:569-572` / `EntityLivingBase.kill :1647-1649`.

Snapshot v9 trailer after biome: `i32 fire, i32 air`. v8 loads fire=0
air=300. `BP_PLAYER` tag PLY1 hashes fire+air. Magma respawn now also
resets food/air/fire (`FoodStats` ctor + Entity AIR). Blaze stays
terminal: magma GUI respawn is a death_click; without it both freeze at
the death tick, so M1 stays equal.

Fixture `s10_t0_r64_hazards.bsnp` baked by `test_hazards --write-fixture`
from `s10_t0_r64_no_liquid.bsnp` (not hand-edited): +Z water tank, lava,
cactus, magma. Chain `hazards_s10.json` 448 actions. M1 VERIFIED 448
ticks t=0 player digest `0x0ac36057b116e2d3`
(`out/verify/hazards_hazards_m1_detail.log`). M2 VERIFIED. `--no-deps` M1
VERIFIED for spawn_to_torch world_dynamics fluids entity_spine
random_ticks random_ticks_bodies falling_blocks weather_optional
projectiles explosions chests mobs mobs_ss mobs_end passives xp_orbs
boats elytra biome_plane mining_slice. M2 VERIFIED for those except
mining_slice BLOCKED (`blaze/rl/out/snaps/*_d*.bsnp` missing). Magma
`test_hazards` PASS. `make -C blaze/rl test-hazards` PASS. Root `make
test` PASS.

Tapes (`replay_tape.py --cpu --no-gate --report`): bow physics NO
divergence 1407, entities PASS 5525. smoke_zombie x2 physics NO
divergence through death (358), entities PASS. Canon INFRASTRUCTURE
FAILURE (golden frames missing on this host): harness, not a verdict.
First-divergence ticks did not move earlier.

Stay out: potion fireResistance, respiration RNG, frost-walker enchant
NBT (test hook `pl->frost_walker`), blaze auto-respawn.

### Sim smalls arrow consume + HUD heart-flash: CLOSED 2026-08-22 (lane/simsmalls)

Tapes (gamer, client inventory at ClientTick END, Recorder.java:8387-8678):
`scenario_blaze_bow_20260722T092838Z` (5 shots, survival) and
`scenario_smoke_zombie_20260722T081735Z` (hp drops t=31, 358/803 death
prefix). `--cpu --no-gate --report` (pixel_gate skipped: frames_checked=0
FATAL is the `--no-gate` harness, not a parity verdict).

**(a) Hotbar arrow count.** Tape `use` 1->0 at t=77/117/216/316/565; slot 8
arrows 64->63->62->61->60->59 on those ticks. Magma live dump matches
each consume tick. Inventory gate 10 independent / 11 compared / 0
mismatches (after same as baseline). Hotbar slot-8 pixels at t=80 are
1 LSB. Recorder dumps EntityPlayerSP, not the server player.

Port: `isr_find_ammo` / `isr_try_fire_bow` from ItemBow.java:47-70,
:86-87, :104, :138-155 (off-hand then main hand then 0..40; Infinity
enchant 51; ItemArrow.isInfinite.java:23-27 plain 262 only; shrink via
isr_decr). EntityArrow pickup EntityArrow.java:94, :471-472, :604-618
(ALLOWED vs CREATIVE_ONLY; inGround + arrowShake<=0 after decrement;
isr_add_item_stack_to_inventory merge). Magma-only inGround sidecar so
PlProj layout stays blaze-identical. This bow tape never picks up
(counts only fall). Bow `damageItem` (ItemBow.java:136) is `!isRemote`
so the client tape lags one tick on bow meta; replay re-anchors it.

**(b) HUD heart-flash.** smoke_zombie t=31 hp 20->17 hurt=9; t=40 frame
in the 20-tick window. Heart row x[240,410] y[399,423]: 7 LSB,
structure_corr=1, same after the port. Flash was already stepped every
capture tick; the port uses hurtResistantTime (GuiIngame.java:769-779)
with tape hurtTime as the damage-tick proxy (resistant is unrecorded).
`lastSystemTime>1000L` (:782) is 21 ticks at 20 Hz. Low-hp
`rand.setSeed(updateCounter*312871)` (:791) is tested
(`gm_hud_lowhp_jitter` pin 1000 -> 0000110011) and not drawn on live
tapes: updateCounter is unrecorded (TAPE_COMPLETENESS). Applying the
replay tick as the seed moved t=320 hearts (464 px); reverted the draw.
Regen potion wobble (:808-811, :869) is in the draw path; these tapes
have no potion 10.

ui_hud `hud_hurt_flash_on/off` stay behind `hud_flash` /
`hud_update_counter=1000` (health 14, no jitter). Cannot run
`run_ui_hud_gates.sh` on gamer (anvil llvmpipe only).

### Bow FOV recapture occupancy-gone 0.753: superseded 2026-08-22 (lane/bowpix)

The bowgold close below claimed `c_vs_j` 7.007 -> 0.753 and J-stone/C-grass
gone after recapture at recorded `fov_mult=0.85`. Anvil lane/bowpix
re-measured that same golden: 5.836 / hard_px=20830 / maxch=97 /
px>1=11111, C `wall_xmin=0` vs J 20, occupancy C-stone/J-grass 487 plus
wall texel-selection. Item gt2=0; mesh stays closed. Image FOV vs the
0.85 field is Class C; do not fit 0.887. Forensics in OPEN first-person
hand entry.

### Falling blocks t46 world hash: CLOSED 2026-08-22 (lane/fallt46)

Tape `scenario_falling_blocks_20260801T151855Z`. Baseline on gamer:
world_hash 309/310, first mismatch t46 java=f63a2e55f4417889
magma=8d22d846ed0c2a49, reconverge t47.

clickBlock creative writes `blockHitDelay=5` then sendClickBlock sees
air and skips onPlayerDamageBlock (PlayerControllerMP.java:237-242,
Minecraft.java:1500-1508). Magma had folded that to 4. Entity.java:2366
returns `getCollisionBorderSize` 0.0F; magma expand 0.1 stole the held
ray at falling y~4.78 and left delay=1 on the re-land
(PlayerControllerMP.java:301-305). delay=5 alone: 22 mismatches from
t29. 0.0F with delay=4: 285 mismatches at t25. Both Java values
together: 310/310. Test I covers the delay=5 countdown.
`t_ent < t_block` (test H) stays. See recorder-gaps item 6.

### Bow viewmodel silhouette as mesh/transform: retracted 2026-08-22

Filed as gray bow metal vs C stone (~1977 ROI px, `c_vs_j=7.007`,
`maxch=108`) after the same-scene wall (`lane/handscene`). Suspects
were pull-stage model, first-person transform chain, and
`magma/game/hand.c` `build_bow_drawn`.

None of those. C bakes `bow_pulling_2` (ItemBow pull>=0.9,
ItemOverrideList reverse match, `hand.c:802` sprite 9002).
`build_bow_drawn` matches `ItemRenderer.renderItemInFirstPerson` BOW
branch (ItemRenderer.java:402-427) then generated
`firstperson_righthand`. Hand FOV stays 70
(`getFOVModifier(pt, false)`, EntityRenderer.java:804). Wood-class
pixels: J-only 60, C-only 32, both 632; J-wood vs C-stone = 8.

The occupancy is the pad wall under world FOV. Eat/shield stone_min
x=77 on both sides. Bow Java stone_min=20 vs C unzoomed 77.
J-stone/C-grass 13363 full / 1961 ROI. Live 20-tick draw is
`fov_mult=0.85` (AbstractClientPlayer.java:156-170) eased 0.5/tick
(EntityRenderer.java:491-502); `player_ctl.c` already does that.
Pinning 0.85 on `ui_hud_scene` over-zooms this golden (C stone_min=0,
`c_vs_j=5.836`, `maxch=97`). Implied Java fov_mult from the wall edge
is 0.887 (two ease ticks). Capture meta has no `fovModifierHand`.
Do not fit 0.887. Recapture is anvil-only.

Occupancy leftover was class C until lane/bowgold 2026-08-22 recaptured
the golden after `fovModifierHand` converged (`fov_mult=0.85` in meta).
`c_vs_j` 7.007 -> 0.753; J-stone/C-grass gone. Remaining 1-LSB wall is
the eat/shield pack family. Do not retune `hand.c`.

### Canonical t=260 "texel-selection" unexplained clusters: retracted 2026-08-21

The 7291-px t=260 / 6252-px t=460 clusters on
`20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` were filed as
nearest-neighbour texel selection on minified leaves (2026-07-25 pxdiff:
canopy y[83,178] x[526,611] 2989 px, sel 0.55 / tol4 0.83 at shift (1,-1)).
That diagnosis was the FOV-before-sprint ordering bug, already fixed
2026-07-25 (`player_ctl.c`: `updateFovModifierHand` before the sprint
state machine; DEVLOG 2026-07-25, `130d0bd`). Magma had eased FOV after
the sprint flag and was one tick ahead (t=260: 1.1453125 vs vanilla
1.140625). A third of a degree of FOV shuffled every minified texel.

Re-measured 2026-08-21 on `lane/worldpix` at `18c5022`, CPU replay to
`$HOME/dev/nw/.tmp/worldpix_canon_tape`, then `pixel_gate` / `pxdiff`
against the tape goldens (181 frames, hide_gui=false):

```
[gate] class UNEXPLAINED  frames    22 px     11441 max_cluster 1435
[gate] class known:4      frames    10 px     18888 max_cluster 5618
[gate] FAIL: 2 frames with unexplained clusters; worst t=3080 (0 px)
  fail t=3080 unex=0 mild_shift mean=10.666666666666666
  fail t=3540 unex=0 mild_shift mean=6.96213919301738
```

t=260 whole-frame 0.216/ch. Two clusters >=50 px, both `known:4`
`shading-offset`, sel=0.00, shift (0,0) on the oak log:

| # | px | bbox | cause | sel | mean_delta |
|---|----|------|-------|-----|------------|
| 0 | 65 | y[188,203] x[328,333] | shading-offset | 0.00 | [-22.77, -33.37, -38.02] |
| 1 | 53 | y[149,158] x[0,7] | shading-offset | 0.00 | [-19.02, -22.92, -29.43] |

Cluster 0 RGB (one texel): golden (90,89,84) vs magma (59,44,34). Same
bark, magma darker. Probe: texel_selection_frac 0.0, structure_corr 0.82,
sky_hole_frac 0.0. Cluster 1 is a left-edge clipped strip (clip_frac 1.0).
Neither is UNEXPLAINED; both sit in the tape sidecar ticks [220,320]
`texture_luminance_modulation` (open_divergence 4). t=260 mild_shift
mean_abs=0.070 (under FAIL_MEAN_ABS 3.32). t=460: 0 clusters >=50 px,
mean 0.239/ch.

The remaining 118 px is the filed outdoor luminance family (AO/lightmap
face shade), not a sampling rule. Tape options are `ao=0`,
`fancyGraphics=false`, `gamma=0.0`; magma default `ao=0` matches. No
exact one-line shade fix without an oracle fragment lightmap capture
(texel bias / fog retune stay forbidden). The tape still FAILs mild_shift
at t=3080 / t=3540 (0 UNEXPLAINED px each); those ticks are the dig
window, not this item.

Historical 2026-07-25 canopy characterization (kept so it is not
re-filed as cutout-sky+): the tape ran `fancyGraphics=false,
mipmapLevels=0`, so `BlockLeaves.getBlockLayer` is SOLID and magma
already meshes leaves as `CR_LAYER_SOLID` with alpha forced opaque. A
global sub-pixel camera offset was ruled out (best whole-frame alignment
dx=dy=0). Forcing planar |z| fog made the tape worse.

Repro:

```bash
cd verify/trace
uv run --no-project --with numpy,scipy,pillow python pxdiff.py selftest
uv run --no-project --with numpy,scipy,pillow python pxdiff.py clusters \
  --tape 20260721T215812Z_fast_s0_survival_default_rd8_77b5b462 --tick 260
```

### Item 13 entities x-ray through water: CLOSED 2026-08-21

Interactive-play sweep 2026-08-01 filed entities painting over
translucent water because window compose was believed to draw all four
terrain layers then entities. Vanilla `EntityRenderer.renderWorldPass`
draws opaque terrain, then entities / overlays / particles, then
translucent.

Both paths already match that order on this tree:

- `magma/game/window_compose.c`: `render_world_layers(..., SOLID,
  TRANSLUCENT)` (exclusive end), then entities/particles, then
  `TRANSLUCENT .. TRANSLUCENT+1`.
- `magma/game/frame_capture.c`: same split; comment at the translucent
  pass states the vanilla order.

`WR-ENTITY-WATER-OCCLUSION` (`scripts/window_battery.py`, CPU,
`--skip-gpu`, magma_game sha eef71dbc89c1):

```
== WR-ENTITY-WATER-OCCLUSION ==
  PASS  WR-ENTITY-WATER-OCCLUSION  wall=0.01s
ALL PASS
```

Metrics: behind 0/812=0.000 (occluded), front 5016/5016=1.000 (not
attenuated by water behind), half_submerged 170/905=0.188 (split).
water_dive pin remaining rc=0 was already noted; that tape never put an
entity behind water.

Repro:

```bash
uv run --no-project --with numpy,pillow python scripts/window_battery.py \
  --game magma/magma_game --skip-gpu --only WR-ENTITY-WATER-OCCLUSION
```
### Spawner miniature data path: CLOSED (lane/sim, 2026-08-21)

Was OPEN as a data gap, not a renderer gap. `gm_spawner_miniatures_emit`
already matched `TileEntityMobSpawnerRenderer`. Nothing called it.

Closed path, in order:

1. `verify/trace/snapshot_patch.py` reads `chunk["Level"]["TileEntities"]`
   and emits tick-0 `set_tile_entity` for `minecraft:mob_spawner` with
   `SpawnData.id` (1.11.2 string form).
2. `script.c` handles `set_tile_entity`. `gm_entity_type_for_spawn_id`
   maps `minecraft:blaze` -> EW_TYPE 7. The mapper lives in
   `entity_render.c`, not `runtime.o`.
3. `GmRuntime.spawners[64]` is a position-keyed store.
   `gm_runtime_spawner_views` copies the current dimension, and only
   while the cell is still block 52. `discover_spawners` does not
   construct these views.
4. `gm_frame_spawners_emit` runs in `frame_capture.c` and
   `window_compose.c` after `gm_entities_emit`.

Native `verify/tape/replay.c` copies `set_tile_entity` lines through the
same START-arrival radius filter as `snapshot_block`. Fortress
`RequiredPlayerRange=0` stays rotation 0.

Gates: `bash magma/game/test_runtime.sh` PASS (store/views);
`bash magma/game/test_entity_render.sh` ALL TESTS PASSED
(`minecraft:blaze -> EntityBlaze 7`);
`bash magma/game/test_script.sh` `set_tile_entity blaze spawner: ok`.

### Falling-block t46 intercept: LANDED (lane/sim, 2026-08-21); digest residual OPEN

`attack_hits_falling_block` used to return 1 as soon as any falling AABB
crossed the look ray. Vanilla `EntityRenderer.getMouseOver` lets the
entity win only when intercept `d1 < d0` (block hit). A leftover falling
AABB further along the same ray stole the held creative click from the
closer re-landed cell.

Fix that landed: compare parametric t against the selection-box block
hit. Entity wins only if `t_ent < t_block`. Gate H in
`bash magma/game/test_fall_reanchor.sh`.

Tape re-run 2026-08-22 on lane/fallblock: 151855Z world_hash still
309/310, first mismatch t46 (java=f63a2e55f4417889,
magma=8d22d846ed0c2a49), reconverge t47. The intercept fix is necessary
and present; delay=1 on the re-landed cell is a separate OPEN residual
(see OPEN_DIVERGENCES item 6).
Canonical physics after the change:
`out/verify/replay --tape verify/tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl --ticks 4000`
-> `first_div none`, 3617/3617, `nearby_hash` match.

### pcl consume: CLOSED (already landed; verified lane/sim 2026-08-21)

Tape `pcl` rows become `spawn_particle` in `replay_tape.py`.
`script.c` calls `gm_particles_live_spawn_recorded`. Capture and window
compose emit them.

Gates: `make -C magma test-particles-live` -> `particles_live: PASS`.
`replay_tape.tape_to_script` maps three `pcl` rows to three
`spawn_particle` events (ids 0,1,2). Dragon-death puff placement from
unrecorded `Particle.rand` stays a separate OPEN item.

### Blaze on-fire flag in the LIVE simulator: CLOSED (already landed; verified lane/sim 2026-08-21)

Replay was already fixed (`ent_view.flags` bit 0). Live
`AIFireballAttack` is in `gm_mobs_tick`: `charge[]` = attackStep,
`blaze_on_fire` set at step 1 and cleared at step 5 / `resetTask`,
`gm_mobs_fill_views` ORs flags bit 0.

Vanilla 78-on/100-off. Darwin standalone tests now link
`world/gen_prefetch.o` so Mach-O resolves the `genpf_*` refs.

Gate: `bash magma/game/test_mob_live.sh` -> `mob_live: PASS`.
`./magma/game/test_mob_live --blaze-receipt /tmp/blaze_live.json`:
`charged_on=156 charged_off=200` (two cycles),
transitions `0:on,78:off,178:on,256:off`,
fireballs `60,66,72,238,244,250`.
### Fortress placement vs oracle MCA seed 0 (GATES item 11)

CLOSED 2026-08-21. Magma/blaze used to emit seed-0 blaze spawners at
(-325, 72, -151). Oracle DIM-1 MCA
(`verify/tapes/retired/scenario_portal_fortress_blaze_20260729T090129Z_world/DIM-1/region`)
has spawners at (-325, 56, -215) and (-325, 56, -102).

Root cause in `blaze/core/map_gen_fortress.h`: the piece tree was 1.7.10-style
DFS with `nextInt(4)` as coord_base, swap-remove on `pendingChildren`, and no
`StructureStart.setRandomHeight(48, 70)`. Vanilla 1.11.2
`MapGenNetherBridge.Start` continues the canSpawn RNG into
`EnumFacing.Plane.HORIZONTAL.random` (N,E,S,W -> coord_base 2,3,0,1),
dequeues `pendingChildren` with `ArrayList.remove(i)` (shift, not
swap-with-last), then offsets the union BB to a random height in [48, 70]
(when ysize > 23 this is always minY=48, which drops the throne spawner
local y=5 from 72 to 56).

After the port, `nether_full` on seed 0 writes id 52 at both oracle cells.
Oracle MCA TileEntities are `minecraft:mob_spawner` with
`SpawnData.id=minecraft:blaze` at the same cells.

Repro:

```
cc -O2 -ffp-contract=off -Iblaze/core verify/worldgen/dim_region_dump.c -o /tmp/dim_region_dump -lm
/tmp/dim_region_dump nether 0 -21 -7 1 1 | awk -F, '$4==52'
# -325,56,-102,52
/tmp/dim_region_dump nether 0 -21 -14 1 1 | awk -F, '$4==52'
# -325,56,-215,52
```

Spawner-cage TileEntity data path remains OPEN (renderer exists; NBT type
does not reach it).

### World spawn selection (GATES item 12 / ledger item 16)

CLOSED 2026-08-21. Magma interactive default started at (8.5, surface, 8.5)
on chunk 0. Vanilla `WorldServer.createSpawnPosition` uses
`BiomeProvider.findBiomePosition(0,0,256)` on `genBiomes` (1/4-res,
biomesToSpawnIn = forest/plains/taiga/taiga_hills/forest_hills/jungle/
jungle_hills) then a grass-block walk of up to 1000 steps, and stores
SpawnY = averageGroundLevel (64).

Seed 1000: findBiomePosition -> (168, 64, 252), equal to oracle
`java/Minecraft/run/saves/qrl_1000/level.dat` SpawnX/Y/Z.
Seed 0: (44, 64, 176), equal to `qrl_0/level.dat`. Magma DEFAULT places the
player at xz+0.5 / `getTopSolidOrLiquidBlock`. Superflat keeps the origin pin
(`test-launch` `pos 8.5,5.0,8.5`).

Oracle player feet at ~ (159.5, 56, 242.5) is Forge spawnRadius fuzz
(`WorldProvider.getRandomizedSpawnPoint`, gamerule spawnRadius=10) with
Malmo `MixinWorldProviderSpawn` swapping `world.rand` for
`SeedHelper.getRandom("playerSpawn")`. Magma does not port that fuzz; tapes
use `set_pose`.

Pin: `make -C magma test-world-spawn`
(seed 1000 -> 168,64,252; seed 0 -> 44,64,176).
`findBiomePosition` 129x129 needs `GL_ARENA_INTS >= 180582`; the spawn TU
uses 262144.

### The oracle's fogColor1 had not converged when recording started

Every scenario tape is worse at t=0 than at t=10, by 2-6x, on the whole-frame
mean. It is the same shape on all of them and it had never been filed because
each tape's t=0 sat under its own gate class. It is the whole reason
`suffocate_camera` and half the reason `elytra_dip` fail their gate: both have
t=0 failures with **zero** unexplained pixels, i.e. the frame is uniformly off
rather than structurally wrong.

The direction settles it: **magma is flat from t=0 and the ORACLE ramps.**
On `suffocate_camera`, golden sky goes 135.1 -> 140.9 and golden grass
117.9 -> 124.9 over the first 40 ticks while magma sits at 141.1 / 125.0 the
whole time. The error decays by 0.35 per 10 ticks, and 0.9^10 = 0.3487 - that
is exactly `EntityRenderer.updateRenderer`'s
`this.fogColor1 += (f2 - this.fogColor1) * 0.1F` (`EntityRenderer.java:327`),
which starts at 0 on a fresh EntityRenderer and had not finished converging by
recstart. magma implements the smoother correctly but seeds it converged
(`gm_uw_fog_c1_seed`, and `underwater.h` states the assumption out loud: "the
oracle client has been running long before recstart, so its c1 has converged").

Mechanism check, on `suffocate_camera` (whole mean/ch, tape floor 0.75):

| c1 seed | t=0 | t=10 | t=20 | t=30 | t=40 |
|---|---|---|---|---|---|
| steady (current) | 7.69 | 2.41 | 1.16 | 0.85 | 0.75 |
| 0.88 | 3.70 | 1.19 | 0.89 | 0.77 | 0.76 |
| **0.90** | **2.44** | **0.82** | **0.80** | **0.73** | **0.72** |
| 0.93 | 3.17 | 0.98 | 0.77 | 0.74 | 0.72 |

One parameter, a single clean optimum, and fitting it on t=0 alone drags t=10,
t=20 and t=30 to the tape's floor as a side effect - it is the mechanism, not a
per-frame fit. With the seed supplied, the tape's gate goes **FAIL -> PASS**.

**Do not hardcode 0.90.** The starting value is a property of the recording
session and is not derivable from the tape: `cobweb_fall` and `water_dive` have
near-identical `total_time` (112 and 113) yet start at 0.9946 and 0.9612,
because what matters is the light the client saw while the world loaded. The
per-tape t=0 ratios measured on the sky band are suffocate 0.9632, water_dive
0.9612, lava_walk 0.9846, elytra_dip 0.9903, soulsand_ice 0.9941, cobweb_fall
0.9946, fence_collide 0.9996, flow_convert 1.0068 (the last two have no ramp).

Fixed the only honest way: the recorder now writes `fog_color1` into the tape
header (`QuantizedRL.recFogColor1`, reflected off `EntityRenderer`, -1 when
unreadable), and `replay_tape.py` seeds magma from it when present via
`MAGMA_FOG_C1_INIT`. Tapes recorded before the field existed return None and
keep the steady-state seed, so nothing re-baselines. **This is inert until the
tapes are re-recorded** - the same re-record that would close the inventory
keyframe and rain gaps.

`MAGMA_FOG_C1_INIT` also works standalone, for sweeping the value on tapes that
predate the header field.

### slime_bounce horizon band: fog-blend decomposition (wt/horizonfog, 2026-07-27)

Measured on t=80 of `scenario_slime_bounce_20260723T001527Z` (flat world, pose
feet `(0.5,4,0.5)` yaw/pitch 0, RD8, fog linear 96→128, GL_EYE_RADIAL_NV).
Replay via `replay_tape.py --cpu`; goldens from the tape frames dir. Silhouette
= first row from y=235 with blue < 200. Baseline gate: 15 failed frames,
UNEXPLAINED 6709.

**Blend model.** Horizon sky / fog colour F = (179, 207, 255) (matches
`updateFogColor` clear at this noon flat take). Unfogged terrain T recovered
from a `MAGMA_FOG=0` replay of the same tape (same geometry, no fog lerp). Then
for each edge pixel:

```
c = (1 - t) * T + t * F    →    t = fog factor in [0,1]
```

G−M colour delta at the first gold-visible row is **parallel to (F−T) with
cos ≈ −0.999** (orth residual ~2/ch): same albedo T, almost pure fog-factor
difference. Not a texel flip, not a sky-gradient bug (rows above the band are
bit-identical).

**Per-run numbers (t=80, mean over run columns at first gold-visible row):**

| run cols | mid-angle | gold sil y | magma sil y | t_gold | t_magma | Δt (m−g) | r_eff gold | r_eff magma |
|----------|-----------|------------|-------------|--------|---------|----------|------------|-------------|
| 58–90 (33) | −45.8° | 246 | 247 | 0.647 | 0.831 | **+0.185** | ~117 m | ~123 m |
| 174–210 (37) | −34.4° | 245 | 246 | 0.626 | 0.825 | **+0.199** | ~116 m | ~122 m |
| 644–676 (33) | +34.3° | 245 | 246 | 0.626 | 0.820 | **+0.194** | ~116 m | ~122 m |
| 762–796 (35) | +45.8° | 246 | 247 | 0.639 | 0.831 | **+0.192** | ~116 m | ~123 m |

Full-width mean Δt at `min(sil_g, sil_m)` is **+0.178** (std 0.057) — the
same ~0.18 over-fog is present on the 711 agreeing columns; the 143 flips are
only where gold's t pushes blue under 200 one row earlier than magma.

Re-fogging magma's nofog T with `t_magma − 0.18` cuts edge-row error from
~25/ch to ~4/ch on every run (residual then matches gold_as_fog(T) ~2/ch). So
the band **is** the fog-factor gap, not a separate coverage hole.

**Geometric coverage (`MAGMA_FOG=0`).** Magma has terrain (blue≪200) from y=244
on the run columns; with fog on, y=244–245 are sky-exact (t=1, fully fogged to
F). Gold's visible sil is y=245/246. Magma is not missing far geometry — it
draws it, fully fogged, then shows a more-fogged transition row.

**Analytic flat-plane check** (ground y=4, eye y=5.62, vFOV 70, pitch 0):

- Magma's measured t matches radial fog on the plane hit: mean
  `|t_magma − t_radial(r_hit)| ≈ 0.002` for |angle| > 10°.
- Gold is systematically under-fogged vs the same hit:
  `|t_gold − t_radial| ≈ 0.19`. Planar |z| is worse for gold
  (`|t_gold − t_planar| ≈ 0.30`).
- Magma vs documented ramp 96/128: RMSE **0.0015**. Gold vs 96/128: RMSE
  **0.15**. Best unconstrained fit for gold is roughly start≈102 end≈134
  (not a constant present in oracle-src).

**Hypothesis results:**

1. **Per-vertex vs per-pixel fog — REFUTED as the 0.18 gap.** Magma already
   does perspective-correct per-fragment radial fog (`raster_cpu.c` interpolates
   `eye_dist_w`, `shade.c` applies the linear ramp). Vanilla 1.11.2 sets no
   `glHint(GL_FOG_HINT, …)` (default DONT_CARE). On 1×1 block faces (both
   mesher and vanilla FaceBakery), max |t_true − t_affine_vertex| and
   |t_true − t_persp_lerp_r| are **~7e−5** at the horizon — three orders below
   0.18. The sky-plane Gouraud fix (`ac47c2b`, 64×64 tiles) does not transfer:
   terrain quads are 1 m, not 64 m.

2. **Planar |z| vs radial — REFUTED as the fix direction.** Oracle capture
   queries `fog_distance_mode_nv = 34139` (GL_EYE_RADIAL_NV); magma matches that
   and the analytic plane. Forcing planar was already shown to regress the
   canonical tape (OPEN_DIVERGENCES "Canonical tape residual"). Gold is closer
   to radial than planar but still Δt≈−0.18 vs true radial.

3. **Projected far-edge / half-pixel — open but not sufficient alone.** Far
   ground tops foreshorten to ~0.04 px of height; the visible rim is extremely
   pitch-sensitive (Δr ≈ 6 m for ~0.04° ≈ 0.3 px). A pure integer row-shift of
   magma vs gold is a *worse* match than same-row fog adjust (best dy=0). The
   data prefer "same pixel, same T, different t" over "magma is one row late."
   A sub-pixel registration gap could still contribute at the threshold, but it
   does not explain the global Δt≈0.18 on agreeing columns.

**What magma implements (oracle-aligned):**
`EntityRenderer.setupFog(0)` linear start=`far*0.75` end=`far` with
`far=RD*16=128` (`EntityRenderer.java:2025–2036`), plus
`glFogi(34138, 34139)` when NV_fog_distance is present (`:2039–2041`). Magma:
`GM_TERRAIN_FOG_START/END` in `sky.h`, radial `eye_dist` in `transform.c` /
`shade.c`. No `glHint` for fog in oracle-src.

**Not a safe code fix yet.** Dropping magma fog by ~0.18 (or widening fog end /
narrowing start toward the empirical 102/134 fit) would paper over gold and
break the documented vanilla ramp that magma already matches to 0.0015 RMSE on
this geometry. Next leads if revisited: (a) capture-side fog evaluation on the
recording GL stack (does the golden's driver honour EYE_RADIAL the same way the
seed7 probe claims?), (b) any remaining view/projection registration at the
0.3 px level that would put gold on a nearer isosurface while sharing T, (c)
confirm with a depth/eye_dist dump from the live Java capture at these columns.

Do **not** widen RD cull, fudge `GM_TERRAIN_FOG_*`, or retune CLASS_PIXEL_BUDGETS
for this band.

**Addendum (2026-07-27, independent re-measure): the fit degeneracy is
resolved - same fog color, real fog-factor gap - and lead (b) is the live
one.** On t=80 agreeing columns, both sides converge to the identical
sky/fog color (179,207,255) in the row above the silhouette and to the same
grass color a few rows below; magma's last terrain rows are consistently
~+37 blue foggier (x=100: gold (128,154,152) vs magma (149,176,191); x=220
and x=530 alike, and magma's sil+1 row is still fog-tinted where gold's is
already clean grass). So it is genuinely Δt with shared F, not a fog-color
difference. Converting: Δt 0.18 x 32-block ramp ≈ 5.8 blocks of effective
distance, and at the horizon's ~6 m per pixel row that is ~0.3 px of
vertical registration - exactly the sensitivity the H3 note computed, and
invisible to the integer-shift test that "refuted" it. A single sub-pixel
vertical projection offset (eye height, pitch, gluPerspective cotangent, or
viewport pixel-center convention) explains a global horizon-only Δt with
zero near-field effect. Discriminating probe: measure the sub-pixel screen
position of a tall NEAR vertical edge (slime block silhouette) golden vs
magma on the same frame - a registration offset shows there too; a pure fog
difference does not.

Probe results (same day): the near-edge measurement over 122 high-contrast
edge pairs at t=80 gives magma-minus-gold dy median 0.000 px (mean 0.21,
std 0.41 - outlier-driven), and a lower-frame band agrees (median 0.000).
A uniform screen shift, eye-height offset, or FOV-scale error would all
have moved those near edges by the same ~0.3 px, so every screen-space form
of H3 is now refuted alongside H1/H2. Separately, the oracle's LIVE GL fog
state is on record: `mc_capture/camera_seed7.json` captures
`fog_start 96.0, fog_end 128.0, fog_mode 9729 (LINEAR),
fog_distance_mode_nv 34139` from the running client, so the empirical
"102/134" fit is NOT the oracle's fog config either. What survives: either
the capture GL stack's fog EVALUATION deviates from t=(d-96)/32 at large d,
or golden's row-to-distance mapping at grazing incidence differs in a way
near edges cannot see. Next probe that separates them: compute golden's
empirical t(d) across the whole 96..128 band on the mc_capture pose/seed7
scenes (exact camera + fog state recorded per capture) against analytic
ground distances - a fog-curve deviation shows as t(d) bending off the
ramp everywhere; a mapping difference shows t(d) on-ramp but with d
shifted only on grazing ground, not on vertical faces at the same
distance.

**Addendum (2026-07-27, wt/fogcurve): t(d) probe — hypothesis A survives,
B refuted.** Repro:
`cd verify/trace && uv run --no-project --with numpy,scipy,pillow
python fogcurve_probe.py --scene all --out ~/dev/nw/.tmp/fogcurve`
(uses existing slime_bounce fog/nofog magma frames under `.tmp/hfog_{out,nofog}`
if present; seed7 re-rendered via `game_candidate --seed 7 --fov 77` with
`--depth` dump).

Method: recover `t = median_ch (P − T)/(F − T)` with `T` from `MAGMA_FOG=0`
and recorded `F`. Analytic eye-radial `d` on slime flat ground via ray/plane
at y=4; seed7 `d` from magma depth buffer. Magma's own `t_magma` tracks
`t_ramp = clamp((d−96)/32)` to RMSE 0.002 on clean grass (control).

*slime_bounce t=80, clean grass tops, plane d (n=991 HC; bulk 100..122 n=599):*

| d     | n   | t_gold | t_magma | t_ramp | t_gold−ramp | t_magma−ramp |
|-------|-----|--------|---------|--------|-------------|--------------|
| 100–102 | 90 | 0.005 | 0.162 | 0.161 | **−0.155** | +0.002 |
| 104–106 | 51 | 0.120 | 0.281 | 0.279 | **−0.159** | +0.001 |
| 108–110 | 52 | 0.241 | 0.408 | 0.406 | **−0.165** | +0.002 |
| 112–114 | 48 | 0.344 | 0.539 | 0.537 | **−0.193** | +0.001 |
| 116–118 | 58 | 0.474 | 0.656 | 0.655 | **−0.181** | +0.001 |
| 120–122 | 39 | 0.600 | 0.783 | 0.781 | **−0.181** | +0.002 |

Bulk mean `t_gold − t_ramp` = **−0.169** (flat across the band, not a
growing bend). Implied constant distance shift
`δ = d − (96 + 32·t_gold)`: median **5.26 blocks** (mean 5.33, std 1.52);
`0.18 × 32 = 5.76` matches the horizon-band Δt. Free linear-ramp fit for
gold: start≈**101**, end≈**134.5** (RMSE 0.045 vs 0.173 on vanilla 96/128).
Magma vs vanilla ramp RMSE **0.0023**. Flat world has no far vertical faces
in the fog band (entities empty at t=80), so orientation needs seed7.

*seed7 (camera_seed7.json: eye (16.5, 89, 268.5), pitch −40°, FOV 77,
F=(179,206,255), fog 96/128 LINEAR EYE_RADIAL), mid-band d∈[104,120],
material classes from nofog albedo + orth-to-fog filter:*

| class | n   | t_gold−ramp | t_magma−ramp | t_gold−t_magma |
|-------|-----|-------------|--------------|----------------|
| trunk (vertical) | 666 | +0.212 | +0.002 | +0.210 |
| grass (ground)   | 975 | +0.174 | +0.002 | +0.172 |

`grass − trunk` residual = **−0.038** (B predicted **−0.18** if only
grazing ground were distance-shifted; A predicted ~0). Absolute seed7
t_gold is *positive* (gold looks more fogged) because magma nofog `T` is not
bit-aligned to the golden's albedo (lighting/smooth residuals on the
mc_capture path); that biases both classes equally and is why the
**relative** residual is the discriminator, not the absolute sign.

**Verdict: A (orientation-independent fog-curve gap).** Gold is under-fogged
by ~0.17 vs the documented linear EYE_RADIAL ramp on clean same-geometry
ground; vertical faces do **not** sit on the ramp while ground is offset, so
B (grazing-only distance mapping) is out. Equivalent descriptions of A: a
constant Δt ≈ −0.17, a constant δd ≈ 5.3 blocks, or an effective
start/end ≈ 101/134.5 — all the same linear warp. Live GL state still
reports 96/128, so this is evaluation / post-fog, not the configured
params.

**No magma code change.** Magma already matches the oracle formula
(`EntityRenderer.setupFog(0)` start=`far*0.75` end=`far`,
`glFogi(34138, 34139)` EYE_RADIAL) to 0.002 RMSE; fudging
`GM_TERRAIN_FOG_*` toward 101/134 would paper over the golden and break the
documented ramp. Next leads: (1) llvmpipe / capture GL fog evaluation vs
spec at large eye-radial d (does the driver honour LINEAR EYE_RADIAL as
`(d−start)/(end−start)`?), (2) any post-fog colour path on the recording
client that pulls toward terrain, (3) a live depth/fog-factor dump from the
Java capture at the same columns. Do not retune CLASS_PIXEL_BUDGETS for the
band.

Two sharpening facts (2026-07-27 review): the fitted endpoints are BOTH the
configured ones scaled by the same factor - 101/96 = 1.052 and
134.5/128 = 1.051 - so the warp is exactly "the capture stack's fog
distance reads as d/1.05", a multiplicative radial-distance underestimate,
not an additive offset or a start/end reconfiguration. And the recording
renderer is on record as llvmpipe (Mesa 26.0.3, the `glxinfo` preamble in
every `start_vnc_client.sh` log), so lead (1) concretely means: how does
Mesa/llvmpipe evaluate GL_NV_fog_distance EYE_RADIAL - per-vertex fog
coord with screen-linear interpolation across the quad would systematically
underestimate the radial distance of interior pixels on large ground quads
(chord-vs-arc), which has the right sign and is orientation-independent at
these view angles. Reproducing THAT (vertex-evaluated radial fog,
interpolated) in magma would be a mechanism port, not a fudge - but measure
it against a llvmpipe minimal repro first.

### slime_bounce: horizon band CLOSED, shell contradiction isolated (2026-07-30)

The horizon-band family is SOLVED: the camera sat 0.08F too high because
magma skipped EntityPlayerSP's sneaking eye height (1.62 -> 1.54); fix merged
65ea82a, and three delegates independently removed a second, duplicate
application of the same offset in frame_capture. Re-recorded
scenario_slime_bounce_20260730T095754Z: t=0 is clean (the fogColor1 recorder
fix confirmed), and EVERY remaining failed frame has 0 unexplained px,
failing only the global check. That residual is the slime-shell
inset-vs-full-element contradiction documented below: honest geometry
attempts render worse (fix_slimebounce findings); a fake double-shell was
rejected. Old tape 20260723T001527Z is superseded and retired. The 2026-07-27
fog-blend decomposition below remains for reference; its baseline numbers
predate the eye-height fix.

### slime_bounce horizon band: NOT a render-distance cull mismatch

All 15 of `slime_bounce`'s failed frames are the same static artifact: a band at
the horizon (y 244..253) spanning the full width, identical from t=60 on.
Per-column silhouette at t=80 (first row from y=235 with blue < 200): **711 of
854 columns agree exactly, 143 have magma's edge exactly one row lower**, in 4
runs of 33/37/33/35 cols (plus 1-2px stragglers).

**Hypothesis tested and refuted (wt/chunkcull, 2026-07-26):** magma's
render-distance cull does **not** use a different metric or off-by-one vs
vanilla.

| Side | Cull test | Metric |
|------|-----------|--------|
| magma | `game/world_live.c:381-388` (`gm_world_mesh_view`; twin at 459-466) | Chebyshev square `cx,cz ∈ [ccx-R, ccx+R]` with R=8, then `cr_aabb_in_frustum` |
| vanilla | `RenderGlobal.getRenderChunkOffset` (oracle-src ~1027) + `ViewFrustum` `(2*RD+1)^2` | `abs(playerChunkOrigin - neighborOrigin) > RD*16` → reject (keeps `\|d\| ≤ RD`); same inclusive Chebyshev |

No Euclidean chunk test in either path. Magma's `<= R` matches vanilla's `>`
(equality kept). Frustum port is the verified ClippingHelper path
(`core/frustum.h`); full-column AABBs for outer-ring ground sections at this
pose are **kept** for the front diagonal chunks `(±8,8)`.

**Diagonals check (tape yaw=0, vFOV 70 → hFOV ~102.5°):** run mid-angles are
**-45.8°, -34.4°, +34.3°, +45.8°**. Only two of four sit on the square diagonals;
a pure cheby-vs-euclid mismatch would be two large side sectors (~400 cols
each), not four ~35-col runs. So the angular pattern does **not** diagnose a
distance-metric bug.

**Vanilla ViewFrustum centering note (not the fix direction):** at player
(0.5,0.5), `updateChunkPositions` uses `floor(x)-8` and covers chunk origins
**-9..7**, then the BFS distance filter keeps **-8..7**. Magma's symmetric
**-8..8** is one chunk *longer* on the + side, so matching that quirk would not
raise magma's horizon.

**Cause remains open** (elsewhere than the RD cull): on the 143 run columns the
sky rows above the band are bit-identical, but the first non-sky row is a
fog/edge blend where gold crosses blue<200 one row earlier; terrain rows below
the band also still differ. `sky.h` `GM_TERRAIN_ZFAR = RD*16*sqrt2` already
matches `EntityRenderer.setupCameraTransform`. Do not widen R or fudge fog end
to paper over this.

### CPU/CUDA replay parity: closed, keep sweeping

Parity had only ever been measured on one tape. The canonical
`20260721T215812Z` replay is bit identical CPU vs CUDA, but a full 23-tape
CUDA sweep on GPU0 (`sm_120`, `nightly_20260725T062525Z`) was **FAIL** with
baseline regressions on six tapes where the CPU sweep was PASS.

The terrain half of that is **fixed**: `cuda/raster_cuda.cu` built its MVP with
`cr_look_yaw_pitch_dev`, which is look-only, while the host path uses
`cr_camera_view` - so CUDA silently dropped
`EntityRenderer.hurtCameraEffect` (hurt roll/yaw). Every tick the player took
damage, the CPU rendered a rolled horizon and CUDA a flat one. Both MVP sites
now call `cr_camera_view_dev`.

On `scenario_blaze_bow_demo_20260722T104234Z` (407 frames, serial runs):
whole-tape diff 12_212_050 px before, 9_344_718 after the hurt fix, and the two
hurt bursts collapse (fi=43: 167_824 px -> 36; fi=231: 156_540 -> 23). With
`MAGMA_NO_DEFER=1` on top, 12_875 px total, 0 frames over 1000, max 46 - sky
stars only. The remainder is the deferred-frame-end issue above.

Ruled out along the way: GPU contention (serial re-runs reproduce
byte-for-byte); a chunk/mesh upload budget (`wl_ensure_mesh` is dirty-driven,
there is no per-frame budget); and the early player deaths, which happen
identically on the CPU and are a separate matter.

Re-run of the 23-tape CUDA sweep on GPU0 after the fix
(`nightly_20260725T071901Z`): 15 rc=0 / 8 rc=3, the same tally as the CPU
sweep, with baseline regressions on **two** tapes instead of five.
`scenario_ender_dragon_20260722T094040Z`,
`scenario_ender_dragon_demo_20260722T104500Z` and
`scenario_lava_walk_20260722T234940Z` are now byte-identical to their CPU
baselines on every class.

The two that still regressed were both the deferred frame end, and both are
now **fixed** - the DEFERRED path reproduces the CPU baseline byte-for-byte on
every class, `failed_frames` and the state block:

- `finish_pending` re-derived the fire overlay's fov scale as
  `cam.fov_deg / 70`, which folds in `getFovModifier`'s bow-pull / sprint
  term; the sync path passes `uw.fov_scale`. Divergence on exactly the
  fire+bow ticks (`blaze_bow_demo`: 57 failed frames -> 1).
- `finish_pending` also re-ran `gm_overlay_block_in_hand_live` against
  `c->pend_world`, which is just the live world pointer, so the eye-block
  sample happened one rendered frame (20 ticks) after the frame it drew. On
  the canonical tape t=660 that resolved to dirt and painted the whole frame
  with the suffocation overlay. The overlay is now split into pick/draw and
  the deferred path resolves at arm time.

The bisect that found the second one: `MAGMA_NO_HAND=1`, `MAGMA_NO_OVERLAY=1`,
a full `cudaStreamSynchronize` inside `frame_end_async`, and resetting the
shade-ctx ring at `frame_begin` each left the frame bit-identically wrong
(83_341_540 px, 3/3 runs), while the raw deferred readback with all host
retire draws skipped was normal (mean 81.4). That ruled out GPU asynchrony
entirely and pointed at the host draws in `finish_pending`.

A deferred-path CUDA replay is parity evidence again. Baselines remain
CPU-authoritative.

- `scenario_ender_dragon_20260722T093713Z` (stale, superseded by `094040Z`):
  magma draws large extra bright geometry the oracle does not have (45216 px
  cluster at t=420, magma mean `[118,124,89]` where the oracle is
  `[34,45,30]`), so this is added content rather than a gate misclass. One
  contributing cause is confirmed in code: `gm_runtime_set_dimension`
  (`game/runtime.c:1022`) never calls `gm_dragon_init`, which only the portal
  path (`game/runtime.c:609`) does, so an authoritative tape dimension switch
  arrives in the End without the fight initialised. Prefer `094040Z` as the
  dragon gate tape.
  Do **not** "fix" this by calling `gm_dragon_init` from `set_dimension`:
  `replay_tape.py` already turns every recorded entity into a render-only
  `ent_view` ghost, and `frame_capture.c:712` fills live-dragon views before
  appending ghost views, so a live dragon would be drawn *on top of* the tape
  one. The symptom here is too much bright geometry, not too little, so the
  likelier cause is End island worldgen / snapshot coverage at x~100. The
  portal path additionally carves a platform and sets the pose, neither of
  which an authoritative tape transfer should do.

- `scenario_elytra_dip`: **re-recorded 2026-07-27 as `20260727T214459Z`**
  (old `20260723T001355Z` moved to `tapes/retired/`, baseline swapped). The
  new tape has settled liquids (200 settle ticks per setup command), a
  converged recorded `fog_color1` (0.99999976 in the header), and the lava
  sea trimmed to x<=36 - the first settled recording (`213715Z`, also in
  retired/) landed at x=40.7 in the last lava column, burned to death
  standing there, and respawned at world spawn, which replay cannot follow.
  Current state: **1 failed frame, t=60, 3010 px** - narrow ~12px vertical
  strips inside the curtain where the golden renders darker falling-water
  streaks and magma is flat brighter blue (cluster means g [45,65,160] vs
  m [48,69,182]). Texture animations ARE pinned on this tape, so it is not
  animation phase; it is the flow-texture selection/orientation on falling
  cells viewed from inside the curtain, the same family as the rejected
  native `water_flow` quadrant experiment. Whole-frame at t=60 is
  mean_abs 3.57 (threshold 3.32), ratio g/m ~0.98/ch.
  The remainder of this entry documents the RETIRED `20260723T001355Z`
  tape's failures for the record; its mechanisms (fogColor1 warmup at t=0,
  mid-growth waterfall at t=60-80) are closed by construction on the new
  tape.
  Old `scenario_elytra_dip_20260723T001355Z`: 4 failed frames.
  **RETRACTED (2026-07-27): the t=70/t=80 "neighbour brightness for water"
  mechanism above was wrong.** Registry finalization (`Block.java`
  `registerBlocks` tail) sets `useNeighborBrightness` only for stairs, slabs,
  farmland/grass path, translucent, or `lightOpacity == 0` blocks. Water has
  opacity 3 and `MaterialLiquid.blocksLight()` keeps it non-translucent, so
  vanilla samples the water cell's OWN light - exactly what magma already
  did. Forcing the neighbour lookup for water fails `water_dive` 93 frames.
  Lava DOES qualify (registered without `setLightOpacity`, so opacity 0 -
  magma's 255 was the real light bug, fixed with the exact
  `getLightBrightness` port through rk_14 in `game/underwater.c`; all four
  water tapes now diff clean against the oracle's saved SkyLight, see
  `trace/skylight_diff.py`). t=70/t=80's decay improved by neither, which
  fits the mid-growth waterfall below: the oracle's feet crossed water cells
  whose growth state magma's frozen approximation does not carry.
  The other failures are t=0 (4.39/ch plus
  an 85 px one-row registration cluster) and t=60 (10.62/ch water-colour wash,
  463 unexplained px). A separate native `water_flow` quadrant experiment
  removed that cluster locally but caused broad `water_dive`/`water_flow`
  regressions and was also rejected.
  Re-confirmed from the frames (2026-07-26): only t=60 is underwater (a
  one-frame dip); golden's underwater frame is brighter with per-channel
  ratios R 1.084 / G 1.072 / B 1.135, and after resurfacing golden carries a
  decaying brightness excess (1.026 at t=70, 1.016 at t=80, 1.004 at t=90,
  gone by t=110) - a `fogColor1` that dropped less during the dip than
  magma's. With the neighbour-brightness reading retracted, the remaining
  driver is the water cells themselves: the oracle's dip crossed a
  partially-grown curtain whose cell contents (and thus feet light) differ
  from magma's frozen approximation.
  **The t=60 463px cluster is a DEVELOPING waterfall the replay cannot
  represent (2026-07-26).** The scenario fills a single water wall at x=10
  (`/fill 10 4 -3 10 22 3 water`) and starts recording immediately; the x=9
  and x=11 curtain columns are that wall's live sideways spread, still
  growing through the first ~seconds of the tape. The world save is
  post-capture (fully grown, all three columns, oracle skylight 12/9/12 at
  z=0), so `tape_to_script`'s elytra post-capture-spread heuristic freezes an
  approximation: x9+x10 falls patched in at t0, x11's dropped, everything
  cleared at t=65 before player contact. Both directions of "fix" were
  measured and are wrong: keeping x11's falls (sustained-under-source
  exemption in `post_capture_spread`) takes t=60 from 463 to 14047
  UNEXPLAINED px because the golden still sees past the curtain's right edge
  at t=60; the committed drop leaves the 463px top-of-screen sliver where the
  golden's partially-grown x11 fall has water and magma has none. Magma's
  fluid CA does not grow it either: snapshot water is deliberately not
  fluid-marked (re-simulating patched water was the rejected native
  water_flow experiment). The clean fix is in the scenario, not the replay:
  add a settle wait between the water fill and recstart and re-record -
  already on the re-record decision list.

### Waterfall ENTRY window on the dense elytra tape (t=58..65) - CLOSED

Root cause was NOT the water at all: it was the elytra ARMING tick's camera
height, and t=58 is simply the tick where a 1.22-block camera error points at
a waterfall. Measured per-tick before the fix, magma's t=58 frame showed water
where the oracle still showed sky over rows 0..75 (oracle (137,179,255) vs
magma (55,80,223) at x=427), while t=57 and t=59 agreed - a one-tick-early
`getEyeHeight` 1.62 -> 0.4 drop, not a fog/overlay/liquid-boundary difference
(magma's own `gm_uw_eval` reports `fluid=0` across the whole window; the eye
does not enter water until t=67).

Vanilla: the client only SENDS `CPacketEntityAction(START_FALL_FLYING)`
(`EntityPlayerSP.onLivingUpdate`:1028-1036). Entity flag 7 is set on the
SERVER (`NetHandlerPlayServer.processEntityAction` case START_FALL_FLYING:1019
-> `EntityPlayerMP.setElytraFlying`:1441) and reaches the client one tick later
as entity metadata (`EntityTrackerEntry.sendMetadataToAllAssociatedPlayers`).
So on the arming tick the client's `isElytraFlying()` is still FALSE, and
everything the client derives from it is still standing-pose:
`EntityPlayer.updateSize`:372 keeps the 1.8F box and `getEyeHeight`:2486
(`isElytraFlying() || height == 0.6F`) keeps 1.62. magma set `elytra_flying`
inline right after `psv_physics_tick` and then ran `psv_update_elytra_size` in
the SAME tick, so the arming tick rendered from eye 0.4. The flag is now staged
in `PsvPlayer.elytra_flying_pending` and applied at the top of the next
`gm_player_tick`, which leaves the already-correct travel timing untouched
(first elytra travel is still the tick after the jump edge) and additionally
makes that first travel tick move with the 1.8F box, as vanilla does.

Result on scenario_elytra_dense_20260729T082313Z: t=58 19.85 -> 3.83/ch, and
the whole t=58..65 window is now 3.3-4.3/ch (was 19.85/3.4/3.3/3.6/3.7/3.8/
4.0/4.3). Physics still byte-clean over 310 ticks; the best whole-frame
row-shift at t=58 is now 0 rows, i.e. the camera is aligned. Unexplained gate
px 84183 -> 67249, worst cluster 42219 -> 15852. No regressions: elytra_dip 1
failed (t=60), water_dive 0, lava_walk 0, suffocate_camera 1 (t=0, 0 px).

Residual, NOT the entry: the same 10 frames still fail the cluster gate. What
is left in t=58..65 is waterfall SURFACE content - at t=58 magma paints the
lit top face of the y=22 water plateau (cells x=9..11, z=-4..4, meta 1/0/1
over meta 9 falling columns) across rows ~78..165 where the oracle has only
~78..95, i.e. magma's rendered surface sits lower/extends further at a grazing
view; at t=59..65 it is flow-texture streak placement inside the curtain. Both
are the same class as the never-failing 12-15/ch bands at t=50..57, present
before this fix, and belong to the mid-growth/flowing-water surface family
already filed for elytra_dip t=60 - not to the eye-in-fluid family.

### Eye-in-fluid overlay timing: CLOSED (root-caused 2026-07-29)

Found 2026-07-29 on the dense elytra tape
(`scenario_elytra_dense_20260729T082313Z`, frames every tick) via a
per-tick L/R mean-abs scan - the 10-tick gate summary never showed it:
- t=142..151: magma draws the full-screen lava submersion overlay/fog
  (~75/255 mean abs) while the oracle eye is still ABOVE the lava
  surface during the skim. Ten ticks of solid red on magma only.
- t=78: magma still applies underwater fog one tick after the oracle
  eye exits the water curtain (single-tick flicker, ~70/255).

Both filed suspects were wrong. Two independent causes, neither in the
`liquid_height_percent` boundary and neither a tick-phase problem:

**1. The lava band is PHYSICS, not overlay timing.** The tape's first
divergence is tick 141 `vy`: oracle `0.30000001192092896`, magma
`-0.10051` (`= -0.16102 * 0.5 - 0.02`, i.e. magma ran the lava branch
correctly but skipped the climb-out kick). `EntityLivingBase`
`moveEntityWithHeading`:2119 sets `motionY = 0.3` when
`isCollidedHorizontally && isOffsetPositionInLiquid(...)`;
`Entity.isOffsetPositionInLiquid`:651 is TRUE when the offset box is
FREE, and its collision half is `World.getCollisionBoxes`, which keeps a
candidate only if `Block.addCollisionBoxToList`:548 passes
`AxisAlignedBB.intersectsWith` (strict `<`, `AxisAlignedBB.java:341`).
`psv_offset_in_liquid` was calling `psv_collect_blocks` - a broadphase
CELL scan, inclusive on `floor(max)` and reaching one cell below
`floor(minY)` - with no intersects re-filter. The elytra pilot is pressed
against a wall at `x = 37`, so his box maxX is exactly `37.0`; the
broadphase returned the wall cell, the kick never fired, and instead of
popping out of the pool he sank and stayed eye-deep in lava for ten
ticks. Fixed by re-filtering with `mc_aabb_intersects`, exactly as
`psv_update_elytra_size` already documents having to do. Removing that
one line of slack also removes every downstream residual on the tape
(t>=152 went 4.4-5.0/ch to 0.7-1.2/ch) and the physics gate is clean.

**2. The viewpoint is not the eye.** `ActiveRenderInfo.projectViewFromEntity`
adds the static `position` vector, which `updateRenderInfo`:50 gets by
gluUnProject-ing the viewport centre at winZ 0 - the NEAR PLANE - through
the finished modelview. First person that modelview carries
`orientCamera`'s `translate(0,0,0.05)` (EntityRenderer:681) and the
projection is `gluPerspective(..., zNear = 0.05F, ...)` (EntityRenderer:730),
so the camera sits 0.05 ahead of the eye and the sampled point another
0.05 ahead of the camera:
`viewpoint = (x, y + eyeHeight, z) + 0.1 * getVectorForRotation(pitch, yaw)`.
At t=78 the eye is at x 11.98790 - still cell 11, water - but the oracle
viewpoint is 11.98790 + 0.09903 = cell 12, air. The remaining `position`
terms (view bobbing, hurt camera) are zero on these tapes:
`EntityPlayer.onLivingUpdate` zeroes `cameraYaw`'s target whenever
`!onGround`, and no tape frame is inside `hurtTime` at a fluid boundary.
They are NOT modelled; a ground-level tape that crosses a fluid surface
while walking would need them.
Consequence worth remembering: `ItemRenderer.renderOverlays` is gated on
`isInsideOfMaterial(WATER)` alone, off the entity's own eye with no
look-ahead, so the overlay texture and the fog/FOV can legitimately
disagree for a tick at a surface crossing. `gm_uw_eval` no longer nests
the overlay test inside `fluid == 1`.

Result on the dense tape: t=78 70.35 -> 0.80/ch, t=142..151 ~75 -> 0.7-1.3/ch,
no physics divergence at all, unexplained gate frames 178 -> 10 (the
survivors are the pre-existing t=58..65 waterfall-entry cluster and t=77,
unchanged by this work; the t=58..65 cluster was closed later - see the next
section).

### Magma's generated nether lava sea is FLOWING lava: FIXED (2026-07-29)

Was: `CPN_LAVA=10` / `CPN_FLOWING_LAVA=11` in `chunk_provider_nether.h`, and
`nf_to_vanilla` / `npm_cpn_to_vanilla` identity-mapped those wrong numbers.
Vanilla 1.11.2 is 10=`flowing_lava`, 11=`lava` still (`Block.java:2414-2415`);
ChunkProviderHell prepareHeights/buildSurfaces place `Blocks.LAVA` (still).
Sea cells were therefore id-10 flowing; a portal tape's DIM-1 snapshot patch
held 123,556 corrections of exactly this. nether_full golden was a C self-
capture so the gate never saw it.

Fix: enum + remappers to vanilla order (`CPN_FLOWING_LAVA=10`, `CPN_LAVA=11`);
verbatim Java golden constants for chunk_provider_nether; nether_full golden
regenerated (seed 7: 2523 cells 10->11 only; seed 49: 1132). Fortress
`FT_LAVA=10` stays (StructureNetherBridgePieces uses `Blocks.FLOWING_LAVA`).
