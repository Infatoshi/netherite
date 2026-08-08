# Kernel lists per pipeline section (see `make help`).
# Add new units when scaffolding with tools/new_kernel.py.
# 2026-07-30: pruned agent-trail stems (see docs/KERNEL_KEEP_DELETE.md §C).

TRUNK := smoke trunk

WORLDGEN_CORE := noise genlayer_biomes terrain_shape biome_props_full

# WORLDGEN_SURFACE: emptied by trail purge
WORLDGEN_SURFACE :=

# WORLDGEN_CARVE: emptied by trail purge
WORLDGEN_CARVE :=

# WORLDGEN_FEATURES: emptied (ore_gen trail)
WORLDGEN_FEATURES :=
WORLDGEN_STRUCTURES := map_gen_fortress map_gen_mineshaft map_gen_stronghold structures \
	structures_placement

WORLDGEN_DIMS := chunk_provider chunk_provider_nether chunk_provider_end chunk_provider_flat \
	nether_full overworld_full overworld_full_live overworld_region

BLOCKS := block_props_table block_tickers block_tickers_crops interact_blocks \
	plant_growth

FLUIDS := fluid_flow populate_fluid_live populate_fluid_shim

LIGHT := light_propagation populate_light_live populate_light_shim

POPULATE := populate populate_dungeon_golden populate_ice_snow

PHYSICS := physics_collision_math physics_collision_full player_physics_world player_physics_full \
	pathfinding pathfinding12 path_navigate

# PLAYER: survival-player loop from verified physics/inventory kernels.
PLAYER := player_survival player_vitals player_death player_break

# UNIFIED: was world_step (trail); emptied.
# UNIFIED: emptied by trail purge
UNIFIED :=

COMBAT := combat_math projectile_motion explosion

ITEMS := items_core items_tools_armor item_block_place inventory_stack_rules \
	crafting_recipes crafting_recipes_full smelting_recipes tile_entity_furnace \
	tile_entity_chest furnace_full_tick container_click loot_table \
	enchant_table

MOBS := mob_ai_zombie_astar mob_spawning_world

PORTALS := nether_portal end_portal ender_dragon ender_dragon_damage \
	ender_dragon_death

TICK := tick_world_copy tick_random_block tick_fluid_ca tick_light_ca \
	tick_compose_1 tick_entities tick_spawn tick_compose_full \
	tick_world_multi tick_world_halo world_tick_vanilla world_weather \
	pal_fluid_parity

# ENTITY: was entities_world/entity_spine (trail); emptied.
# ENTITY: emptied by trail purge
ENTITY :=

# BATCH: product/env-related kernels only after trail purge.
BATCH := cuda_batch_tick py_gym_env_smoke render_opt_obs_hook obs_camera sps_benchmark

# REGION: region_tensor kept (blocked/tool); reproduce/batch purged.
REGION := region_tensor

# GAMERULES: CPU-only self-check (out of ALL_KERNELS).
GAMERULES := gamerules_wire

# TICKTRACE/ENTITYTRACE/ITEMTRACE: CPU-only real-game traces (out of ALL_KERNELS).
TICKTRACE := tick_trace_verify

ENTITYTRACE := entity_trace_verify

ITEMTRACE := item_trace_verify

WORLDGEN := $(WORLDGEN_CORE) $(WORLDGEN_STRUCTURES) $(WORLDGEN_DIMS)

# ALL_KERNELS = CPU+CUDA-verified units. GAMERULES/traces stay out.
ALL_KERNELS := $(TRUNK) $(WORLDGEN) $(BLOCKS) $(FLUIDS) $(LIGHT) $(POPULATE) \
	$(PHYSICS) $(PLAYER) $(COMBAT) $(ITEMS) $(MOBS) $(PORTALS) $(TICK) $(BATCH) $(REGION)
