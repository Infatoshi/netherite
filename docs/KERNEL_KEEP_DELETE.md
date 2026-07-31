# blaze kernel keep / delete inventory


> **Applied 2026-07-30 on branch `purge/blaze-kernel-trail`:** §C paths deleted, plus `ore_gen` / `ore_gen_natural_stone` (not `noise` drivers — those wrap `mc_noise.h` and stay) (depended on deleted `caves_real` chain) and `blaze/tools/capture_ogns_golden.py`. Blocked set otherwise restored. See `docs/kernel_delete_paths.txt` for the committed delete list.

Generated 2026-07-30 by mechanical rule (include-closure + sweep pins).
Regenerate: re-run the generator logic in this file's provenance (agent session) or
ask to re-emit. **Do not delete until you have reviewed §C and run the sweep.**

## Rule

1. **Production** = transitive `#include` closure of non-`test_*` sources under
   `magma/{game,world,app,renderkernels,cpu,cuda,core,present}` + `blaze/env`.
2. **Verification** = transitive closure of drivers for `sections.mk` sections
   **TRUNK + PHYSICS + TICK** plus GAMERULES / TICKTRACE / ENTITYTRACE / ITEMTRACE.
3. **Trail** = in neither. Safe-delete only if no strong external ref under
   magma / blaze/env / blaze/rl / scripts / verify (`*.h` include, `cpu/X`, `verify-X`, `goldens/X`).

## Counts

| bucket | n |
|--------|---|
| blaze/core headers | 157 |
| A production | 74 |
| B verification-only | 22 |
| trail (neither) | 61 |
| C trail safe-delete headers | 52 |
| D trail blocked | 9 |
| cpu drivers / cuda drivers / golden dirs | 142 / 139 / 71 |
| flat delete paths | 215 |
| sections.mk prune stems | 49 |
| sections.mk keep stems | 87 |

Sweep kernel stems:
```
entity_trace_verify gamerules_wire item_trace_verify pal_fluid_parity path_navigate pathfinding pathfinding12 physics_collision_full physics_collision_math player_physics_full player_physics_world smoke tick_compose_1 tick_compose_full tick_entities tick_fluid_ca tick_light_ca tick_random_block tick_spawn tick_trace_verify tick_world_copy tick_world_halo tick_world_multi trunk world_tick_vanilla world_weather
```

---

## A. PRODUCTION headers (keep)

- `biome_props_full.h` _(transitive only)_
- `block_props_table.h` — e.g. `magma/world/light.c`
- `chunk_provider.h` — e.g. `magma/game/structures_live.c`
- `chunk_provider_end.h` — e.g. `magma/world/light.c`
- `chunk_provider_flat.h` — e.g. `magma/world/light.c`
- `chunk_provider_nether.h` _(transitive only)_
- `combat_math.h` — e.g. `magma/game/dragon_live.c`
- `container_click.h` — e.g. `magma/app/game_main.c`
- `crafting_recipes_full.h` — e.g. `blaze/env/blaze_core.h`
- `enchant_table.h` _(transitive only)_
- `end_portal.h` — e.g. `magma/game/portal_live.c`
- `ender_dragon.h` _(transitive only)_
- `ender_dragon_death.h` — e.g. `magma/game/dragon_live.h`
- `entity_base.h` _(transitive only)_
- `entity_hostile_spine.h` — e.g. `magma/game/mob_live.h`
- `entity_xp_orb.h` — e.g. `magma/game/mob_live.h`
- `ew_entity_store.h` _(transitive only)_
- `explosion.h` — e.g. `magma/game/runtime.c`
- `fluid_flow.h` — e.g. `magma/game/fluid_live.c`
- `furnace_full_tick.h` — e.g. `blaze/env/blaze_core.h`
- `genlayer_biomes.h` — e.g. `magma/world/light.c`
- `interact_blocks.h` — e.g. `blaze/env/blaze_core.h`
- `inventory_stack_rules.h` — e.g. `magma/game/container_live.c`
- `item_block_place.h` — e.g. `blaze/env/blaze_core.h`
- `items_core.h` — e.g. `magma/app/game_main.c`
- `items_tools_armor.h` — e.g. `blaze/env/blaze_core.h`
- `living_base.h` _(transitive only)_
- `loot_table.h` _(transitive only)_
- `map_gen_fortress.h` — e.g. `magma/game/structures_live.c`
- `map_gen_mineshaft.h` _(transitive only)_
- `map_gen_stronghold.h` — e.g. `magma/game/structures_live.c`
- `mc.h` _(transitive only)_
- `mc_blocks.h` — e.g. `blaze/env/blaze_core.h`
- `mc_gamerules.h` _(transitive only)_
- `mc_math.h` — e.g. `magma/game/frame_capture.c`
- `mc_noise.h` _(transitive only)_
- `mc_rng.h` — e.g. `magma/game/live_sim.c`
- `mc_world.h` _(transitive only)_
- `mob_ai_zombie_astar.h` _(transitive only)_
- `nether_full.h` — e.g. `magma/world/light.c`
- `nether_portal.h` — e.g. `magma/game/portal_live.c`
- `obs_camera.h` — e.g. `blaze/env/blaze_core.h`
- `overworld_full.h` _(transitive only)_
- `overworld_full_live.h` _(transitive only)_
- `overworld_region.h` — e.g. `magma/world/populate_mc.c`
- `path_finder.h` _(transitive only)_
- `path_node_processor.h` _(transitive only)_
- `pathfinding.h` _(transitive only)_
- `pathfinding12.h` _(transitive only)_
- `physics_collision_full.h` _(transitive only)_
- `physics_collision_math.h` _(transitive only)_
- `plant_growth.h` — e.g. `magma/game/live_sim.c`
- `player_break.h` — e.g. `blaze/env/blaze_core.h`
- `player_physics_full.h` _(transitive only)_
- `player_physics_world.h` _(transitive only)_
- `player_survival.h` — e.g. `blaze/env/blaze_core.h`
- `player_vitals.h` — e.g. `blaze/env/blaze_core.h`
- `populate.h` _(transitive only)_
- `populate_fluid_live.h` _(transitive only)_
- `populate_fluid_shim.h` _(transitive only)_
- `populate_ice_snow.h` _(transitive only)_
- `populate_light_live.h` _(transitive only)_
- `populate_light_shim.h` _(transitive only)_
- `projectile_motion.h` _(transitive only)_
- `smelting_recipes.h` _(transitive only)_
- `stronghold_loot.h` — e.g. `magma/game/chest_live.c`
- `structures.h` — e.g. `magma/world/gen_prefetch.c`
- `terrain_shape.h` _(transitive only)_
- `tick_entities.h` _(transitive only)_
- `tick_world_copy.h` _(transitive only)_
- `tile_entity_chest.h` — e.g. `magma/game/chest_live.h`
- `tile_entity_furnace.h` _(transitive only)_
- `wg_fossils_data.h` _(transitive only)_
- `world_weather.h` — e.g. `magma/game/world_live.c`

---

## B. VERIFICATION-ONLY headers (keep — sweep-pinned, not product)

- `block_tickers.h`
- `block_tickers_crops.h`
- `ender_dragon_damage.h`
- `entity_item.h`
- `light_propagation.h`
- `mob_spawning_world.h`
- `pal_chunk.h`
- `pal_fluid_parity.h`
- `path_navigate.h`
- `player_death.h`
- `scheduled_ticks.h`
- `smoke_core.h`
- `tick_compose_1.h`
- `tick_compose_full.h`
- `tick_fluid_ca.h`
- `tick_light_ca.h`
- `tick_random_block.h`
- `tick_spawn.h`
- `tick_world_halo.h`
- `tick_world_multi.h`
- `trunk_core.h`
- `world_tick_vanilla.h`

---

## C. TRAIL — safe delete

Delete, for each stem: `blaze/core/<stem>.h`, `blaze/cpu/<stem>.c`,
`blaze/cuda/<stem>.cu`, `blaze/oracle/goldens/<stem>/` when present.

**52 stems:**

- `animal_breed` — `blaze/core/animal_breed.h`, `blaze/cpu/animal_breed.c`, `blaze/cuda/animal_breed.cu`, `blaze/oracle/goldens/animal_breed/`
- `batch_region_tensor` — `blaze/core/batch_region_tensor.h`, `blaze/cpu/batch_region_tensor.c`, `blaze/cuda/batch_region_tensor.cu`
- `boat_control` — `blaze/core/boat_control.h`, `blaze/cpu/boat_control.c`, `blaze/cuda/boat_control.cu`, `blaze/oracle/goldens/boat_control/`
- `caves` — `blaze/core/caves.h`, `blaze/cpu/caves.c`, `blaze/cuda/caves.cu`, `blaze/oracle/goldens/caves/`
- `caves_real` — `blaze/core/caves_real.h`, `blaze/cpu/caves_real.c`, `blaze/cuda/caves_real.cu`, `blaze/oracle/goldens/caves_real/`
- `chunk_provider_biome_wired` — `blaze/core/chunk_provider_biome_wired.h`, `blaze/cpu/chunk_provider_biome_wired.c`, `blaze/cuda/chunk_provider_biome_wired.cu`, `blaze/oracle/goldens/chunk_provider_biome_wired/`
- `combat_knockback_resist` — `blaze/core/combat_knockback_resist.h`, `blaze/cpu/combat_knockback_resist.c`, `blaze/cuda/combat_knockback_resist.cu`, `blaze/oracle/goldens/combat_knockback_resist/`
- `cuda_batch_worldgen` — `blaze/core/cuda_batch_worldgen.h`, `blaze/cpu/cuda_batch_worldgen.c`, `blaze/cuda/cuda_batch_worldgen.cu`
- `difficulty_scale` — `blaze/core/difficulty_scale.h`, `blaze/cpu/difficulty_scale.c`, `blaze/cuda/difficulty_scale.cu`, `blaze/oracle/goldens/difficulty_scale/`
- `enchant_damage_full` — `blaze/core/enchant_damage_full.h`, `blaze/cpu/enchant_damage_full.c`, `blaze/cuda/enchant_damage_full.cu`, `blaze/oracle/goldens/enchant_damage_full/`
- `enchant_protection_full` — `blaze/core/enchant_protection_full.h`, `blaze/cpu/enchant_protection_full.c`, `blaze/cuda/enchant_protection_full.cu`, `blaze/oracle/goldens/enchant_protection_full/`
- `end_full` — `blaze/core/end_full.h`, `blaze/cpu/end_full.c`, `blaze/cuda/end_full.cu`
- `entities_world` — `blaze/core/entities_world.h`, `blaze/cpu/entities_world.c`, `blaze/cuda/entities_world.cu`
- `entity_spine` — `blaze/core/entity_spine.h`, `blaze/cpu/entity_spine.c`, `blaze/cuda/entity_spine.cu`
- `item_bow_use` — `blaze/core/item_bow_use.h`, `blaze/cpu/item_bow_use.c`, `blaze/cuda/item_bow_use.cu`
- `item_bucket_world` — `blaze/core/item_bucket_world.h`, `blaze/cpu/item_bucket_world.c`, `blaze/cuda/item_bucket_world.cu`
- `item_food_eat` — `blaze/core/item_food_eat.h`, `blaze/cpu/item_food_eat.c`, `blaze/cuda/item_food_eat.cu`
- `lake_gen` — `blaze/core/lake_gen.h`, `blaze/cpu/lake_gen.c`, `blaze/cuda/lake_gen.cu`, `blaze/oracle/goldens/lake_gen/`
- `lake_gen_real` — `blaze/core/lake_gen_real.h`, `blaze/cpu/lake_gen_real.c`, `blaze/cuda/lake_gen_real.cu`, `blaze/oracle/goldens/lake_gen_real/`
- `mathhelper` — `blaze/core/mathhelper.h`, `blaze/cpu/mathhelper.c`, `blaze/cuda/mathhelper.cu`, `blaze/oracle/goldens/mathhelper/`
- `mc_entity` — `blaze/core/mc_entity.h`
- `mc_tick` — `blaze/core/mc_tick.h`
- `mob_ai_creeper` — `blaze/core/mob_ai_creeper.h`, `blaze/cpu/mob_ai_creeper.c`, `blaze/cuda/mob_ai_creeper.cu`
- `mob_ai_enderman` — `blaze/core/mob_ai_enderman.h`, `blaze/cpu/mob_ai_enderman.c`, `blaze/cuda/mob_ai_enderman.cu`
- `mob_ai_skeleton` — `blaze/core/mob_ai_skeleton.h`, `blaze/cpu/mob_ai_skeleton.c`, `blaze/cuda/mob_ai_skeleton.cu`
- `mob_ai_spider` — `blaze/core/mob_ai_spider.h`, `blaze/cpu/mob_ai_spider.c`, `blaze/cuda/mob_ai_spider.cu`
- `mob_ai_zombie` — `blaze/core/mob_ai_zombie.h`, `blaze/cpu/mob_ai_zombie.c`, `blaze/cuda/mob_ai_zombie.cu`
- `mob_spawning` — `blaze/core/mob_spawning.h`, `blaze/cpu/mob_spawning.c`, `blaze/cuda/mob_spawning.cu`
- `mob_spawning_oracle` — `blaze/core/mob_spawning_oracle.h`, `blaze/cpu/mob_spawning_oracle.c`, `blaze/cuda/mob_spawning_oracle.cu`, `blaze/oracle/goldens/mob_spawning_oracle/`
- `mob_spawning_passive` — `blaze/core/mob_spawning_passive.h`, `blaze/cpu/mob_spawning_passive.c`, `blaze/cuda/mob_spawning_passive.cu`
- `nether_portal_make` — `blaze/core/nether_portal_make.h`, `blaze/cpu/nether_portal_make.c`, `blaze/cuda/nether_portal_make.cu`
- `nether_portal_world` — `blaze/core/nether_portal_world.h`, `blaze/cpu/nether_portal_world.c`, `blaze/cuda/nether_portal_world.cu`
- `populate_animals` — `blaze/core/populate_animals.h`, `blaze/cpu/populate_animals.c`, `blaze/cuda/populate_animals.cu`
- `potion_effects_combat` — `blaze/core/potion_effects_combat.h`, `blaze/cpu/potion_effects_combat.c`, `blaze/cuda/potion_effects_combat.cu`, `blaze/oracle/goldens/potion_effects_combat/`
- `projectile_entity_hit` — `blaze/core/projectile_entity_hit.h`, `blaze/cpu/projectile_entity_hit.c`, `blaze/cuda/projectile_entity_hit.cu`
- `ravines` — `blaze/core/ravines.h`, `blaze/cpu/ravines.c`, `blaze/cuda/ravines.cu`, `blaze/oracle/goldens/ravines/`
- `ravines_real` — `blaze/core/ravines_real.h`, `blaze/cpu/ravines_real.c`, `blaze/cuda/ravines_real.cu`, `blaze/oracle/goldens/ravines_real/`
- `region_reproduce` — `blaze/core/region_reproduce.h`, `blaze/cpu/region_reproduce.c`, `blaze/cuda/region_reproduce.cu`
- `spawner_activate` — `blaze/core/spawner_activate.h`, `blaze/cpu/spawner_activate.c`, `blaze/cuda/spawner_activate.cu`, `blaze/oracle/goldens/spawner_activate/`
- `sps_benchmark` — `blaze/core/sps_benchmark.h`, `blaze/cpu/sps_benchmark.c`, `blaze/cuda/sps_benchmark.cu`
- `superflat_populate` — `blaze/core/superflat_populate.h`, `blaze/cpu/superflat_populate.c`, `blaze/cuda/superflat_populate.cu`, `blaze/oracle/goldens/superflat_populate/`
- `surface_blocks` — `blaze/core/surface_blocks.h`, `blaze/cpu/surface_blocks.c`, `blaze/cuda/surface_blocks.cu`, `blaze/oracle/goldens/surface_blocks/`
- `surface_blocks_real` — `blaze/core/surface_blocks_real.h`, `blaze/cpu/surface_blocks_real.c`, `blaze/cuda/surface_blocks_real.cu`, `blaze/oracle/goldens/surface_blocks_real/`
- `tile_entity_brewing` — `blaze/core/tile_entity_brewing.h`, `blaze/cpu/tile_entity_brewing.c`, `blaze/cuda/tile_entity_brewing.cu`, `blaze/oracle/goldens/tile_entity_brewing/`
- `tile_entity_spawner` — `blaze/core/tile_entity_spawner.h`, `blaze/cpu/tile_entity_spawner.c`, `blaze/cuda/tile_entity_spawner.cu`
- `tree_gen` — `blaze/core/tree_gen.h`, `blaze/cpu/tree_gen.c`, `blaze/cuda/tree_gen.cu`, `blaze/oracle/goldens/tree_gen/`
- `tree_gen_big_oak` — `blaze/core/tree_gen_big_oak.h`, `blaze/cpu/tree_gen_big_oak.c`, `blaze/cuda/tree_gen_big_oak.cu`, `blaze/oracle/goldens/tree_gen_big_oak/`
- `tree_gen_birch_real` — `blaze/core/tree_gen_birch_real.h`, `blaze/cpu/tree_gen_birch_real.c`, `blaze/cuda/tree_gen_birch_real.cu`, `blaze/oracle/goldens/tree_gen_birch_real/`
- `tree_gen_jungle` — `blaze/core/tree_gen_jungle.h`, `blaze/cpu/tree_gen_jungle.c`, `blaze/cuda/tree_gen_jungle.cu`, `blaze/oracle/goldens/tree_gen_jungle/`
- `tree_gen_oak_real` — `blaze/core/tree_gen_oak_real.h`, `blaze/cpu/tree_gen_oak_real.c`, `blaze/cuda/tree_gen_oak_real.cu`, `blaze/oracle/goldens/tree_gen_oak_real/`
- `tree_gen_taiga` — `blaze/core/tree_gen_taiga.h`, `blaze/cpu/tree_gen_taiga.c`, `blaze/cuda/tree_gen_taiga.cu`, `blaze/oracle/goldens/tree_gen_taiga/`
- `world_step` — `blaze/core/world_step.h`, `blaze/cpu/world_step.c`, `blaze/cuda/world_step.cu`

### Flat path list

```
blaze/core/animal_breed.h
blaze/core/batch_region_tensor.h
blaze/core/boat_control.h
blaze/core/caves.h
blaze/core/caves_real.h
blaze/core/chunk_provider_biome_wired.h
blaze/core/combat_knockback_resist.h
blaze/core/crafting_recipes.h
blaze/core/cuda_batch_tick.h
blaze/core/cuda_batch_worldgen.h
blaze/core/difficulty_scale.h
blaze/core/enchant_damage_full.h
blaze/core/enchant_protection_full.h
blaze/core/end_full.h
blaze/core/entities_world.h
blaze/core/entity_spine.h
blaze/core/item_bow_use.h
blaze/core/item_bucket_world.h
blaze/core/item_food_eat.h
blaze/core/lake_gen.h
blaze/core/lake_gen_real.h
blaze/core/mathhelper.h
blaze/core/mc_entity.h
blaze/core/mc_tick.h
blaze/core/mob_ai_creeper.h
blaze/core/mob_ai_enderman.h
blaze/core/mob_ai_skeleton.h
blaze/core/mob_ai_spider.h
blaze/core/mob_ai_zombie.h
blaze/core/mob_spawning.h
blaze/core/mob_spawning_oracle.h
blaze/core/mob_spawning_passive.h
blaze/core/nether_portal_make.h
blaze/core/nether_portal_world.h
blaze/core/ore_gen.h
blaze/core/ore_gen_natural_stone.h
blaze/core/populate_animals.h
blaze/core/populate_dungeon_golden.h
blaze/core/potion_effects_combat.h
blaze/core/projectile_entity_hit.h
blaze/core/py_gym_env_smoke.h
blaze/core/ravines.h
blaze/core/ravines_real.h
blaze/core/region_reproduce.h
blaze/core/region_tensor.h
blaze/core/render_opt_obs_hook.h
blaze/core/spawner_activate.h
blaze/core/sps_benchmark.h
blaze/core/structures_placement.h
blaze/core/superflat_populate.h
blaze/core/surface_blocks.h
blaze/core/surface_blocks_real.h
blaze/core/tile_entity_brewing.h
blaze/core/tile_entity_spawner.h
blaze/core/tree_gen.h
blaze/core/tree_gen_big_oak.h
blaze/core/tree_gen_birch_real.h
blaze/core/tree_gen_jungle.h
blaze/core/tree_gen_oak_real.h
blaze/core/tree_gen_taiga.h
blaze/core/world_step.h
blaze/cpu/animal_breed.c
blaze/cpu/batch_region_tensor.c
blaze/cpu/boat_control.c
blaze/cpu/caves.c
blaze/cpu/caves_real.c
blaze/cpu/chunk_provider_biome_wired.c
blaze/cpu/combat_knockback_resist.c
blaze/cpu/crafting_recipes.c
blaze/cpu/cuda_batch_tick.c
blaze/cpu/cuda_batch_worldgen.c
blaze/cpu/difficulty_scale.c
blaze/cpu/enchant_damage_full.c
blaze/cpu/enchant_protection_full.c
blaze/cpu/end_full.c
blaze/cpu/entities_world.c
blaze/cpu/entity_spine.c
blaze/cpu/item_bow_use.c
blaze/cpu/item_bucket_world.c
blaze/cpu/item_food_eat.c
blaze/cpu/lake_gen.c
blaze/cpu/lake_gen_real.c
blaze/cpu/mathhelper.c
blaze/cpu/mob_ai_creeper.c
blaze/cpu/mob_ai_enderman.c
blaze/cpu/mob_ai_skeleton.c
blaze/cpu/mob_ai_spider.c
blaze/cpu/mob_ai_zombie.c
blaze/cpu/mob_spawning.c
blaze/cpu/mob_spawning_oracle.c
blaze/cpu/mob_spawning_passive.c
blaze/cpu/nether_portal_make.c
blaze/cpu/nether_portal_world.c
blaze/cpu/noise.c
blaze/cpu/ore_gen.c
blaze/cpu/ore_gen_natural_stone.c
blaze/cpu/populate_animals.c
blaze/cpu/populate_dungeon_golden.c
blaze/cpu/potion_effects_combat.c
blaze/cpu/projectile_entity_hit.c
blaze/cpu/py_gym_env_smoke.c
blaze/cpu/ravines.c
blaze/cpu/ravines_real.c
blaze/cpu/region_reproduce.c
blaze/cpu/region_tensor.c
blaze/cpu/render_opt_obs_hook.c
blaze/cpu/spawner_activate.c
blaze/cpu/sps_benchmark.c
blaze/cpu/structures_placement.c
blaze/cpu/superflat_populate.c
blaze/cpu/surface_blocks.c
blaze/cpu/surface_blocks_real.c
blaze/cpu/tile_entity_brewing.c
blaze/cpu/tile_entity_spawner.c
blaze/cpu/tree_gen.c
blaze/cpu/tree_gen_big_oak.c
blaze/cpu/tree_gen_birch_real.c
blaze/cpu/tree_gen_jungle.c
blaze/cpu/tree_gen_oak_real.c
blaze/cpu/tree_gen_taiga.c
blaze/cpu/world_step.c
blaze/cuda/animal_breed.cu
blaze/cuda/batch_region_tensor.cu
blaze/cuda/bench_k1_noise.cu
blaze/cuda/boat_control.cu
blaze/cuda/caves.cu
blaze/cuda/caves_real.cu
blaze/cuda/chunk_provider_biome_wired.cu
blaze/cuda/combat_knockback_resist.cu
blaze/cuda/crafting_recipes.cu
blaze/cuda/cuda_batch_tick.cu
blaze/cuda/cuda_batch_worldgen.cu
blaze/cuda/difficulty_scale.cu
blaze/cuda/enchant_damage_full.cu
blaze/cuda/enchant_protection_full.cu
blaze/cuda/end_full.cu
blaze/cuda/entities_world.cu
blaze/cuda/entity_spine.cu
blaze/cuda/item_bow_use.cu
blaze/cuda/item_bucket_world.cu
blaze/cuda/item_food_eat.cu
blaze/cuda/lake_gen.cu
blaze/cuda/lake_gen_real.cu
blaze/cuda/mathhelper.cu
blaze/cuda/mob_ai_creeper.cu
blaze/cuda/mob_ai_enderman.cu
blaze/cuda/mob_ai_skeleton.cu
blaze/cuda/mob_ai_spider.cu
blaze/cuda/mob_ai_zombie.cu
blaze/cuda/mob_spawning.cu
blaze/cuda/mob_spawning_oracle.cu
blaze/cuda/mob_spawning_passive.cu
blaze/cuda/nether_portal_make.cu
blaze/cuda/nether_portal_world.cu
blaze/cuda/noise.cu
blaze/cuda/ore_gen.cu
blaze/cuda/ore_gen_natural_stone.cu
blaze/cuda/populate_animals.cu
blaze/cuda/populate_dungeon_golden.cu
blaze/cuda/potion_effects_combat.cu
blaze/cuda/projectile_entity_hit.cu
blaze/cuda/py_gym_env_smoke.cu
blaze/cuda/ravines.cu
blaze/cuda/ravines_real.cu
blaze/cuda/region_reproduce.cu
blaze/cuda/region_tensor.cu
blaze/cuda/render_opt_obs_hook.cu
blaze/cuda/spawner_activate.cu
blaze/cuda/sps_benchmark.cu
blaze/cuda/structures_placement.cu
blaze/cuda/superflat_populate.cu
blaze/cuda/surface_blocks.cu
blaze/cuda/surface_blocks_real.cu
blaze/cuda/tile_entity_brewing.cu
blaze/cuda/tile_entity_spawner.cu
blaze/cuda/tree_gen.cu
blaze/cuda/tree_gen_big_oak.cu
blaze/cuda/tree_gen_birch_real.cu
blaze/cuda/tree_gen_jungle.cu
blaze/cuda/tree_gen_oak_real.cu
blaze/cuda/tree_gen_taiga.cu
blaze/cuda/world_step.cu
blaze/oracle/goldens/animal_breed/
blaze/oracle/goldens/boat_control/
blaze/oracle/goldens/caves/
blaze/oracle/goldens/caves_real/
blaze/oracle/goldens/chunk_provider_biome_wired/
blaze/oracle/goldens/combat_knockback_resist/
blaze/oracle/goldens/crafting_recipes/
blaze/oracle/goldens/difficulty_scale/
blaze/oracle/goldens/enchant_damage_full/
blaze/oracle/goldens/enchant_protection_full/
blaze/oracle/goldens/lake_gen/
blaze/oracle/goldens/lake_gen_real/
blaze/oracle/goldens/mathhelper/
blaze/oracle/goldens/mob_spawning_oracle/
blaze/oracle/goldens/noise/
blaze/oracle/goldens/ore_gen/
blaze/oracle/goldens/ore_gen_natural_stone/
blaze/oracle/goldens/populate_dungeon_golden/
blaze/oracle/goldens/potion_effects_combat/
blaze/oracle/goldens/ravines/
blaze/oracle/goldens/ravines_real/
blaze/oracle/goldens/spawner_activate/
blaze/oracle/goldens/structures_placement/
blaze/oracle/goldens/superflat_populate/
blaze/oracle/goldens/surface_blocks/
blaze/oracle/goldens/surface_blocks_real/
blaze/oracle/goldens/tile_entity_brewing/
blaze/oracle/goldens/tree_gen/
blaze/oracle/goldens/tree_gen_big_oak/
blaze/oracle/goldens/tree_gen_birch_real/
blaze/oracle/goldens/tree_gen_jungle/
blaze/oracle/goldens/tree_gen_oak_real/
blaze/oracle/goldens/tree_gen_taiga/
```

---

## D. TRAIL — blocked (do not auto-delete)

- `crafting_recipes.h` — `blaze/tools/gen_crafting_recipes_full.py`
- `cuda_batch_tick.h` — `blaze/env/DESIGN.md`
- `ore_gen.h` — `blaze/tools/capture_ogns_golden.py`
- `ore_gen_natural_stone.h` — `blaze/tools/capture_ogns_golden.py`
- `populate_dungeon_golden.h` — `blaze/tools/capture_populate_dungeon_golden.py`
- `py_gym_env_smoke.h` — `blaze/py/mcsim_gym.cpp`
- `region_tensor.h` — `blaze/env/DESIGN.md`
- `render_opt_obs_hook.h` — `blaze/tools/crosscheck_render_opt_obs_hook.py`
- `structures_placement.h` — `blaze/tools/capture_structures_placement_golden.py`

---

## E. sections.mk

**Prune** (remove from section variables after file delete):
```
animal_breed batch_region_tensor caves caves_real chunk_provider_biome_wired combat_knockback_resist cuda_batch_worldgen difficulty_scale enchant_damage_full enchant_protection_full end_full entities_world entity_spine item_bow_use item_bucket_world item_food_eat lake_gen lake_gen_real mathhelper mob_ai_creeper mob_ai_enderman mob_ai_skeleton mob_ai_spider mob_ai_zombie mob_spawning mob_spawning_oracle mob_spawning_passive nether_portal_make nether_portal_world populate_animals potion_effects_combat projectile_entity_hit ravines ravines_real region_reproduce spawner_activate sps_benchmark superflat_populate surface_blocks surface_blocks_real tile_entity_brewing tile_entity_spawner tree_gen tree_gen_big_oak tree_gen_birch_real tree_gen_jungle tree_gen_oak_real tree_gen_taiga world_step
```

**Keep** in ALL_KERNELS (product or sweep):
```
biome_props_full block_props_table block_tickers block_tickers_crops chunk_provider chunk_provider_end chunk_provider_flat chunk_provider_nether combat_math container_click crafting_recipes crafting_recipes_full cuda_batch_tick enchant_table end_portal ender_dragon ender_dragon_damage ender_dragon_death explosion fluid_flow furnace_full_tick genlayer_biomes interact_blocks inventory_stack_rules item_block_place items_core items_tools_armor light_propagation loot_table map_gen_fortress map_gen_mineshaft map_gen_stronghold mob_ai_zombie_astar mob_spawning_world nether_full nether_portal noise obs_camera ore_gen ore_gen_natural_stone overworld_full overworld_full_live overworld_region pal_fluid_parity path_navigate pathfinding pathfinding12 physics_collision_full physics_collision_math plant_growth player_break player_death player_physics_full player_physics_world player_survival player_vitals populate populate_dungeon_golden populate_fluid_live populate_fluid_shim populate_ice_snow populate_light_live populate_light_shim projectile_motion py_gym_env_smoke region_tensor render_opt_obs_hook smelting_recipes smoke structures structures_placement terrain_shape tick_compose_1 tick_compose_full tick_entities tick_fluid_ca tick_light_ca tick_random_block tick_spawn tick_world_copy tick_world_halo tick_world_multi tile_entity_chest tile_entity_furnace trunk world_tick_vanilla world_weather
```

---

## F. Related non-kernel cleanup

| path | action | reason |
|------|--------|--------|
| `blaze/ref/render-opt` | delete or repoint → `../../java/render-opt` | broken symlink |
| `verify/trace/out/` | delete local cache | ~27G regenerable |
| `blaze/rl/out/` except `snaps/`, `coal_prefixes.json`, `chain_actions_s10.json`, `chain_net*.pt` | prune | train junk |
| `tapes/smoke-v0.json` | delete if unused | leftover; real tapes in `verify/tapes/` |
| `blaze/rl/ppo_break.py`, `eval_coal.py`, `make_*videos*` | optional later | not on sweep |

---

## G. Do not delete

- Entire §A + §B header set and matching cpu/cuda/goldens
- `verify/tapes/` (gitignored, irreplaceable)
- `blaze/rl/out/{snaps,coal_prefixes.json,chain_actions_s10.json,chain_net*.pt}`
- `blaze/rl/{chain_probe,vec_env,ppo_coal,walk_break,test_vec_env,eval_chain_rl}.py` while sweep imports them
- Anything in §D until refs are rewired

---

## H. Acceptance

```bash
bash netherite_sweep.sh --quick
bash netherite_sweep.sh --full   # GPU free
```

Any FAIL ⇒ restore that stem; list is wrong for that unit.

---

## I. Sanity checks performed at generation

- sections.mk TICK/PHYSICS/TRUNK expand to non-empty real stems
- no sweep-critical stem appears in flat delete list
- product_direct seeds: 37; product_trans: 74; sweep_trans: 58

