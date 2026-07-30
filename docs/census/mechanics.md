# Mechanics census

Oracle mechanics universe: **102** rows (69 brief-fixed mechanics plus 33 mechanics or rule surfaces found in `GameRules` and player/world controllers).

Census rows: **102**.

`docs/GOAL.md` was absent from this checkout during the census. The supplied P1 CENSUS brief and `docs/SCOPE.md` section 1 were used as the phase and cut contracts.

## Movement and physics

| name | oracle ref | magma status | magma cite | tape coverage | scenario candidate |
|---|---|---|---|---|---|
| walk | `net/minecraft/entity/EntityLivingBase.java:travel` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` | - |
| sprint | `net/minecraft/client/entity/EntityPlayerSP.java:onLivingUpdate` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` | - |
| sneak | `net/minecraft/client/entity/EntityPlayerSP.java:onLivingUpdate` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `scenario_slime_bounce_20260723T001527Z` | - |
| jump | `net/minecraft/entity/EntityLivingBase.java:jump` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` | - |
| swim | `net/minecraft/entity/EntityLivingBase.java:travel` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `scenario_water_dive_20260722T234816Z` | - |
| elytra | `net/minecraft/entity/EntityLivingBase.java:updateElytra` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `scenario_elytra_dip_20260727T214459Z`; `scenario_nether_elytra_20260729T110024Z` | - |
| ladders | `net/minecraft/entity/EntityLivingBase.java:travel` | implemented | `magma/game/player_ctl.c:gm_player_tick` | none | `/setblock ~ ~ ~1 minecraft:ladder 2`, then climb, stop, sneak, and fall against it |
| boats | `net/minecraft/entity/item/EntityBoat.java:onUpdate` | partial(route control and mount exist; underwater/ejection state incomplete) | `magma/game/mob_live.c:gm_mobs_tick` | none | `/give @p minecraft:boat`, place it on a pond, mount, turn, beach, submerge, and dismount |
| minecarts | `net/minecraft/entity/item/EntityMinecart.java:onUpdate` | cut | - | none | - |
| falling sand | `net/minecraft/entity/item/EntityFallingBlock.java:onUpdate` | partial(replay/render path only; no live magma fall tick) | `magma/game/item_render.c:gm_falling_blocks_emit` | none | `/setblock ~ ~8 ~ minecraft:sand`, remove its support, and follow the fall and landing |
| falling gravel | `net/minecraft/entity/item/EntityFallingBlock.java:onUpdate` | partial(replay/render path only; no live magma fall tick) | `magma/game/item_render.c:gm_falling_blocks_emit` | none | `/setblock ~ ~8 ~ minecraft:gravel`, remove its support, and follow the fall and landing |
| falling anvils | `net/minecraft/block/BlockAnvil.java:onStartFalling` | cut | - | none | - |
| fall damage | `net/minecraft/entity/EntityLivingBase.java:fall` | implemented | `magma/game/player_ctl.c:gm_vitals_apply` | `scenario_cobweb_fall_20260723T001656Z` | - |
| drowning | `net/minecraft/entity/EntityLivingBase.java:onEntityUpdate` | missing | - | `scenario_water_dive_20260722T234816Z` | - |
| suffocation | `net/minecraft/entity/EntityLivingBase.java:onEntityUpdate` | partial(first-person block overlay only; no live in-wall damage) | `magma/game/overlay_live.c:gm_overlay_block_in_hand_live` | `scenario_suffocate_camera_20260723T001923Z` | - |
| cobweb slowdown | `net/minecraft/block/BlockWeb.java:onEntityCollidedWithBlock` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `scenario_cobweb_fall_20260723T001656Z` | - |
| slime bounce | `net/minecraft/block/BlockSlime.java:onLanded` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `scenario_slime_bounce_20260723T001527Z` | - |
| soul-sand slowdown | `net/minecraft/block/BlockSoulSand.java:onEntityCollidedWithBlock` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `scenario_soulsand_ice_20260723T001810Z` | - |
| ice slipperiness | `net/minecraft/block/Block.java:slipperiness` | implemented | `magma/game/player_ctl.c:gm_player_tick` | `scenario_soulsand_ice_20260723T001810Z` | - |
| creative flight | `net/minecraft/client/entity/EntityPlayerSP.java:onLivingUpdate` | missing | - | none | `/gamemode 1`, toggle flight, ascend, strafe, descend, and land |
| riding and dismount | `net/minecraft/entity/Entity.java:startRiding` | partial(boats only) | `magma/game/mob_live.c:gm_mobs_boat_mount` | none | `/summon Boat ~ ~ ~2`, mount it, steer, then sneak-dismount on land and water |

## Combat and entity interaction

| name | oracle ref | magma status | magma cite | tape coverage | scenario candidate |
|---|---|---|---|---|---|
| melee | `net/minecraft/entity/player/EntityPlayer.java:attackTargetEntityWithCurrentItem` | partial(route mobs and fixed cooldown; vanilla attack-strength, crit, sweep, and knockback branches incomplete) | `magma/game/mob_live.c:gm_mobs_player_attack` | `scenario_blaze_melee_20260722T092705Z`; `scenario_enderman_fight_20260722T093335Z` | - |
| bow | `net/minecraft/item/ItemBow.java:onPlayerStoppedUsing` | partial(draw, arrow consumption, flight, and hits exist; inaccuracy, enchantments, and bow durability incomplete) | `magma/game/runtime.c:spawn_bow_arrow` | `scenario_blaze_bow_20260722T092838Z`; `scenario_dragon_kill_geared_20260730T025316Z` | - |
| critical hits | `net/minecraft/entity/player/EntityPlayer.java:attackTargetEntityWithCurrentItem` | missing | - | none | `/summon Zombie ~ ~ ~3`, compare grounded, falling, sprinting, and full-cooldown attacks |
| knockback | `net/minecraft/entity/EntityLivingBase.java:knockBack` | partial(recorded packet/entity pushes exist; live player melee knockback is absent) | `magma/game/runtime.c:gm_runtime_set_packet_velocity` | `20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` | - |
| armor | `net/minecraft/entity/EntityLivingBase.java:applyArmorCalculations` | implemented | `magma/game/runtime.c:runtime_armor_damage` | `scenario_dragon_kill_geared_20260730T025316Z` | - |
| shields | `net/minecraft/item/ItemShield.java:getItemUseAction` | partial(use state and viewmodel exist; directional damage blocking and disable rules are absent) | `magma/game/player_ctl.c:gm_player_tick` | none | `/give @p minecraft:shield`, block zombie melee and skeleton arrows from front and rear |
| natural mob spawning | `net/minecraft/world/WorldEntitySpawner.java:findChunksForSpawning` | partial(route roster and simplified caps/placement only) | `magma/game/mob_live.c:gm_mobs_tick` | none | `/time set night`, clear nearby mobs, then hold position through hostile and passive spawn cycles |
| mob spawners | `net/minecraft/tileentity/MobSpawnerBaseLogic.java:updateSpawner` | partial(route spawner state exists; discovery/type and full spawn rules are incomplete) | `magma/game/mob_live.c:gm_mobs_register_spawner` | none | `/setblock ~ ~ ~4 minecraft:mob_spawner 0 replace {SpawnData:{id:"minecraft:blaze"}}`, then approach and wait |
| loot and drops | `net/minecraft/entity/EntityLivingBase.java:dropLoot` | partial(route mob/block/chest tables only) | `magma/game/mob_live.c:mob_drop` | none | Summon and kill one of each route mob with `/summon`, then inspect every item and XP drop |
| item pickup | `net/minecraft/entity/item/EntityItem.java:onCollideWithPlayer` | implemented | `magma/game/live_sim.c:gm_live_tick_player` | `20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` | - |
| item toss | `net/minecraft/entity/player/EntityPlayer.java:dropItem` | implemented | `magma/game/container_live.c:ct_drop` | none | `/give @p minecraft:cobblestone 8`, press Q once, then throw the remaining stack from inventory |
| death | `net/minecraft/entity/player/EntityPlayer.java:onDeath` | partial(game-over state exists; vanilla death-time, inventory-drop, and continued-world semantics differ) | `magma/game/runtime.c:gm_runtime_tick` | `scenario_blaze_bow_demo_20260722T104234Z`; `scenario_lava_walk_20260722T234940Z` | - |
| respawn | `net/minecraft/server/management/PlayerList.java:recreatePlayerEntity` | partial(health/UI reset only; pose, dimension, food, and inventory recreation incomplete) | `magma/game/runtime.c:gm_runtime_respawn` | none | `/kill @p`, wait for the button unlock, respawn, and compare pose, dimension, inventory, food, and effects |
| creeper explosion | `net/minecraft/entity/monster/EntityCreeper.java:explode` | implemented | `magma/game/mob_live.c:gm_mobs_take_explosion` | none | `/summon Creeper ~ ~ ~3`, let it fuse beside a striped dirt/stone wall and the player |
| end-crystal explosion | `net/minecraft/entity/item/EntityEnderCrystal.java:attackEntityFrom` | partial(crystal removal exists; its own explosion and fire placement are absent) | `magma/game/dragon_live.c:gm_dragon_player_attack` | `scenario_dragon_kill_20260729T110941Z`; `scenario_dragon_kill_geared_20260730T025316Z` | - |
| TNT explosion | `net/minecraft/entity/item/EntityTNTPrimed.java:onUpdate` | missing | - | none | `/summon PrimedTnt ~ ~1 ~ {Fuse:20}`, record fuse motion, blast, drops, and entity damage |
| fishing | `net/minecraft/entity/projectile/EntityFishHook.java:onUpdate` | missing | - | none | `/give @p minecraft:fishing_rod`, cast into open water, wait for a bite, and reel in |
| villager trading | `net/minecraft/entity/passive/EntityVillager.java:processInteract` | cut | - | none | - |
| projectile collision and damage | `net/minecraft/entity/projectile/EntityArrow.java:onHit` | partial(route arrow/fireball sweep and simple radius hits only) | `magma/game/runtime.c:tick_projectiles` | `scenario_blaze_bow_20260722T092838Z`; `scenario_dragon_kill_geared_20260730T025316Z` | - |
| hostile ranged attacks | `net/minecraft/entity/ai/EntityAIAttackRanged.java:updateTask` | partial(skeleton, blaze, and ghast route attacks; full AI state machines absent) | `magma/game/runtime.c:spawn_hostile_projectiles` | `scenario_blaze_bow_20260722T092838Z`; `scenario_wither_skeleton_20260722T093020Z` | - |
| entity collision and contact damage | `net/minecraft/entity/EntityLivingBase.java:collideWithNearbyEntities` | partial(replay pushers and route hostile contacts only) | `magma/game/runtime.c:gm_runtime_ent_box` | `20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` | - |
| hurt-resistant cooldown | `net/minecraft/entity/EntityLivingBase.java:attackEntityFrom` | implemented | `magma/game/mob_live.c:gm_mobs_attack_player` | `scenario_blaze_melee_20260722T092705Z` | - |
| armor-enchantment damage reduction | `net/minecraft/entity/EntityLivingBase.java:applyPotionDamageCalculations` | cut | - | none | - |
| fire and burning damage | `net/minecraft/entity/Entity.java:onEntityUpdate` | implemented | `magma/game/runtime.c:gm_runtime_tick` | `scenario_lava_walk_20260722T234940Z` | - |
| mob griefing | `net/minecraft/world/GameRules.java:mobGriefing` | partial(creeper block destruction exists; gamerule and other mob edits do not) | `magma/game/runtime.c:runtime_explode` | none | `/gamerule mobGriefing false`, stage creeper, enderman, and ghast edits, then repeat with true |
| generic entity interaction | `net/minecraft/client/multiplayer/PlayerControllerMP.java:interactWithEntity` | partial(boat mount only) | `magma/game/mob_live.c:gm_mobs_boat_mount` | none | Use `/summon Boat`, `/summon Villager`, `/summon Sheep`, `/summon Cow`, and `/summon ItemFrame`, then right-click each |

## World mechanics

| name | oracle ref | magma status | magma cite | tape coverage | scenario candidate |
|---|---|---|---|---|---|
| water flow | `net/minecraft/block/BlockDynamicLiquid.java:updateTick` | partial(synchronous regional CA rather than per-cell scheduled updates) | `magma/game/fluid_live.c:gm_fluid_tick` | `scenario_water_flow_20260722T235050Z` | - |
| lava flow | `net/minecraft/block/BlockDynamicLiquid.java:updateTick` | partial(synchronous regional CA rather than per-cell scheduled updates) | `magma/game/fluid_live.c:gm_fluid_tick` | none | Use `/setblock ~ ~2 ~ minecraft:flowing_lava` over a stepped channel in Overworld and Nether |
| water/lava conversion | `net/minecraft/block/BlockLiquid.java:checkForMixing` | partial(route-neighbor reactions and CA mixing only) | `magma/game/runtime.c:gm_runtime_tick` | `scenario_flow_convert_20260723T002122Z` | - |
| fire spread | `net/minecraft/block/BlockFire.java:updateTick` | missing | - | none | `/gamerule doFireTick true`, ignite a spaced wool/wood grid, and record ignition and burnout |
| leaf decay | `net/minecraft/block/BlockLeaves.java:updateTick` | missing | - | none | Build a leaf canopy with `/fill`, remove its logs, and wait through decay and sapling drops |
| grass spread | `net/minecraft/block/BlockGrass.java:updateTick` | missing | - | none | Stage lit dirt beside grass plus shaded grass, then raise `/gamerule randomTickSpeed 100` |
| crop growth | `net/minecraft/block/BlockCrops.java:updateTick` | partial(wheat only) | `magma/game/live_sim.c:gm_live_tick` | none | Stage each crop with `/setblock` and accelerate with `/gamerule randomTickSpeed 100` |
| sapling growth | `net/minecraft/block/BlockSapling.java:updateTick` | missing | - | none | Plant each sapling type with clear space and `/gamerule randomTickSpeed 100` |
| day cycle | `net/minecraft/world/WorldServer.java:tick` | implemented | `magma/game/world_live.c:gm_world_tick` | none | `/time set 11000`, hold one horizon view continuously through sunset, night, dawn, and day |
| weather | `net/minecraft/world/WorldServer.java:updateWeather` | cut | - | none | - |
| redstone wire, repeaters, and comparators | `net/minecraft/block/BlockRedstoneWire.java:updateSurroundingRedstone` | cut | - | none | - |
| pistons | `net/minecraft/block/BlockPistonBase.java:eventReceived` | cut | - | none | - |
| doors | `net/minecraft/block/BlockDoor.java:onBlockActivated` | cut | - | none | - |
| trapdoors | `net/minecraft/block/BlockTrapDoor.java:onBlockActivated` | partial(click toggles metadata; redstone power and iron rules incomplete) | `magma/game/player_ctl.c:gm_player_tick` | none | Use `/setblock` for wood and iron trapdoors, click both, then power them from each side |
| fence gates | `net/minecraft/block/BlockFenceGate.java:onBlockActivated` | implemented | `magma/game/player_ctl.c:gm_player_tick` | none | Use `/setblock` to stage gates on both axes, click from both sides, then test open and closed collision |
| buttons | `net/minecraft/block/BlockButton.java:onBlockActivated` | partial(press metadata exists; scheduled release and power propagation absent) | `magma/game/player_ctl.c:gm_player_tick` | none | Use `/setblock` for wood and stone buttons, press each, and hold through release |
| levers | `net/minecraft/block/BlockLever.java:onBlockActivated` | partial(toggle metadata exists; power propagation absent) | `magma/game/player_ctl.c:gm_player_tick` | none | Use `/setblock` for floor, wall, and ceiling levers, then toggle them beside lamps |
| pressure plates | `net/minecraft/block/BlockBasePressurePlate.java:updateState` | cut | - | none | - |
| beds | `net/minecraft/block/BlockBed.java:onBlockActivated` | partial(time skip and Nether/End explosion exist; occupancy, sleep checks, pose, and spawn setting incomplete) | `magma/game/runtime.c:gm_runtime_use_block` | none | `/time set night`, sleep in Overworld, then use identical beds in Nether and End |
| nether portal | `net/minecraft/block/BlockPortal.java:trySpawnPortal` | implemented | `magma/game/portal_live.c:gm_portal_ignite` | none | Use `/give @p minecraft:obsidian 14` and `/give @p minecraft:flint_and_steel`, then record ignition, transit, cooldown, and return |
| end portal | `net/minecraft/block/BlockEndPortalFrame.java:getOrCreatePortalShape` | implemented | `magma/game/portal_live.c:gm_end_portal_insert_eye` | none | Stage eleven eyed frames with `/setblock`, `/give @p minecraft:ender_eye`, insert the twelfth eye, and enter |
| dimension transfer | `net/minecraft/server/management/PlayerList.java:transferPlayerToDimension` | implemented | `magma/game/runtime.c:gm_runtime_tick` | none | Use `/give` to build a Nether portal, travel out and back, then `/setblock` an End portal and enter |
| signs | `net/minecraft/block/BlockSign.java:createNewTileEntity` | cut | - | none | - |
| banners | `net/minecraft/tileentity/TileEntityBanner.java:setItemValues` | missing | - | none | `/give @p minecraft:banner`, place patterned standing and wall banners, rotate, break, and pick up |
| chests | `net/minecraft/block/BlockChest.java:onBlockActivated` | partial(single chests and route loot only; double/trapped chest behavior absent) | `magma/game/runtime.c:gm_runtime_use_block` | none | Use `/setblock` for adjacent normal and trapped chests, fill them, reopen, break one half, and inspect drops |
| containers | `net/minecraft/inventory/Container.java:slotClick` | partial(PICKUP, QUICK_MOVE, and THROW; drag, clone, swap, pickup-all, and broad containers absent) | `magma/game/container_live.c:gm_container_click` | none | Stage table, furnace, and chest with `/setblock`, then split, shift-move, swap, drag, double-click, and throw |
| world border | `net/minecraft/world/border/WorldBorder.java:contains` | missing | - | none | `/worldborder set 16`, walk, shoot, place, spawn mobs, and teleport across the shrinking border |
| block breaking | `net/minecraft/client/multiplayer/PlayerControllerMP.java:onPlayerDamageBlock` | partial(progressive survival dig with route harvest table only) | `magma/game/player_ctl.c:dig_destroy` | `scenario_hold_dig_dense_20260725T031854Z` | - |
| block placement | `net/minecraft/client/multiplayer/PlayerControllerMP.java:processRightClickBlock` | partial(route block ids and metadata only) | `magma/game/player_ctl.c:gm_player_tick` | `20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` | - |
| bucket use | `net/minecraft/item/ItemBucket.java:onItemRightClick` | partial(source pickup/place and route reactions; full replaceable/vaporize rules incomplete) | `magma/game/player_ctl.c:bucket_raycast` | `scenario_flow_convert_20260723T002122Z` | - |
| fire ignition and extinguishing | `net/minecraft/item/ItemFlintAndSteel.java:onItemUse` | partial(ignition exists; general extinguish/fire lifecycle absent) | `magma/game/player_ctl.c:gm_player_tick` | none | Use `/give @p minecraft:flint_and_steel`, ignite portal, netherrack, wood, and TNT, then punch out each fire |
| scheduled block updates | `net/minecraft/world/WorldServer.java:tickUpdates` | missing | - | none | Use `/setblock` to stage water, lava, fire, buttons, falling blocks, and repeaters, then record update order |
| tile drops gamerule | `net/minecraft/world/GameRules.java:doTileDrops` | partial(route harvest/drop map only; gamerule and general drop logic absent) | `magma/game/player_ctl.c:harvest_drop` | `scenario_hold_dig_dense_20260725T031854Z` | - |
| spawn-point setting | `net/minecraft/entity/player/EntityPlayer.java:setSpawnPoint` | missing | - | none | Sleep in a moved bed, break and obstruct it, die with `/kill`, and compare valid/invalid respawns |

## Meta, UI, effects, and progression

| name | oracle ref | magma status | magma cite | tape coverage | scenario candidate |
|---|---|---|---|---|---|
| hunger | `net/minecraft/util/FoodStats.java:onUpdate` | implemented | `magma/game/player_ctl.c:gm_vitals_apply` | `scenario_blaze_melee_20260722T092705Z` | - |
| natural regeneration | `net/minecraft/util/FoodStats.java:onUpdate` | implemented | `magma/game/player_ctl.c:gm_vitals_apply` | `scenario_blaze_melee_20260722T092705Z` | - |
| XP gain and levels | `net/minecraft/entity/player/EntityPlayer.java:addExperience` | implemented | `magma/game/mob_live.c:tick_xp_orbs` | `scenario_dragon_kill_20260729T110941Z`; `scenario_dragon_kill_geared_20260730T025316Z` | - |
| durability | `net/minecraft/item/ItemStack.java:damageItem` | partial(route tools, armor, weapons, and elytra; bow and enchantment rules incomplete) | `magma/game/player_ctl.c:dig_destroy` | none | Use `/give` with near-max damage for tools, armor, bow, shield, flint, and elytra, then trigger each wear path |
| crafting | `net/minecraft/item/crafting/CraftingManager.java:findMatchingRecipe` | partial(route KEEP recipe set, not the complete registry) | `magma/game/runtime.c:gm_runtime_craft` | none | Craft offset shaped, mirrored, shapeless, container-item, armor, food, and invalid recipes in 2x2/3x3 |
| smelting | `net/minecraft/tileentity/TileEntityFurnace.java:update` | partial(route smelt/fuel tables, not the complete registry) | `magma/game/furnace_live.c:furnace_live_tick` | none | Use `/give` to stage iron, food, logs, stone, invalid input, coal, wood, and lava-bucket furnace cases |
| brewing | `net/minecraft/tileentity/TileEntityBrewingStand.java:update` | cut | - | none | - |
| enchanting | `net/minecraft/enchantment/Enchantment.java:REGISTRY` | cut | - | none | - |
| status effects | `net/minecraft/potion/PotionEffect.java:onUpdate` | partial(live wither plus tape-fed HUD effects; no general effect engine) | `magma/game/mob_live.c:gm_mobs_attack_player` | `scenario_wither_skeleton_20260722T093020Z`; `scenario_dragon_kill_geared_20260730T025316Z` | - |
| achievements and stats | `net/minecraft/stats/StatList.java:init` | missing | - | none | Mine, craft, kill, travel, open inventory, and inspect `/achievement` plus stat persistence |
| sleeping | `net/minecraft/entity/player/EntityPlayer.java:trySleep` | partial(time skip only; pose, occupancy, checks, and wake state absent) | `magma/game/runtime.c:gm_runtime_use_block` | none | `/time set night`, test safe, unsafe, occupied, obstructed, daytime, Nether, and End beds |
| eating animations | `net/minecraft/client/renderer/ItemRenderer.java:transformEatFirstPerson` | partial(use timer and consumption exist; oracle goldens do not establish the mid-use viewmodel pose) | `magma/game/player_ctl.c:gm_player_tick` | none | Use `/effect @p minecraft:hunger 30 4`, hold bread for 32 ticks, cancel once, then repeat with potion and milk |
| keep inventory rule | `net/minecraft/world/GameRules.java:keepInventory` | missing | - | none | `/gamerule keepInventory false`, die with items/XP, then repeat with true |
| command-block output rule | `net/minecraft/world/GameRules.java:commandBlockOutput` | missing | - | none | Toggle `/gamerule commandBlockOutput` around repeating command-block success and failure |
| admin-command logging rule | `net/minecraft/world/GameRules.java:logAdminCommands` | missing | - | none | Toggle `/gamerule logAdminCommands` and issue visible admin commands from the integrated server |
| death-message rule | `net/minecraft/world/GameRules.java:showDeathMessages` | missing | - | none | Toggle `/gamerule showDeathMessages`, then die to fall, mob, fire, and explosion damage |
| command-feedback rule | `net/minecraft/world/GameRules.java:sendCommandFeedback` | missing | - | none | Toggle `/gamerule sendCommandFeedback` and run successful and failing `/give` and `/setblock` commands |
| reduced-debug-info rule | `net/minecraft/world/GameRules.java:reducedDebugInfo` | missing | - | none | Toggle `/gamerule reducedDebugInfo`, open F3, and compare coordinates, facing, and targeted-block data |
| spectator chunk-generation rule | `net/minecraft/world/GameRules.java:spectatorsGenerateChunks` | missing | - | none | `/gamemode 3`, toggle the rule, then cross the generated edge while monitoring chunk loads |
| spawn-radius rule | `net/minecraft/world/GameRules.java:spawnRadius` | missing | - | none | Set `/gamerule spawnRadius 0` and 10, repeatedly `/kill @p`, and record spawn distributions |
| entity-cramming rule | `net/minecraft/world/GameRules.java:maxEntityCramming` | missing | - | none | Summon 30 cows in one block, compare damage with `/gamerule maxEntityCramming 24` and 0 |

CENSUS mechanics DONE rows=102
