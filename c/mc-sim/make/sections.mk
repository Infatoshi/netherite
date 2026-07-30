# Kernel lists per pipeline section (see `make help`).
# Add new units when scaffolding with tools/new_kernel.py.

TRUNK := smoke trunk

WORLDGEN_CORE := noise mathhelper genlayer_biomes terrain_shape biome_props_full
WORLDGEN_SURFACE := surface_blocks surface_blocks_real
WORLDGEN_CARVE := caves caves_real ravines ravines_real
WORLDGEN_FEATURES := ore_gen ore_gen_natural_stone tree_gen tree_gen_oak_real \
	tree_gen_big_oak tree_gen_birch_real tree_gen_taiga tree_gen_jungle \
	lake_gen lake_gen_real
WORLDGEN_STRUCTURES := map_gen_fortress map_gen_mineshaft map_gen_stronghold structures structures_placement
WORLDGEN_DIMS := chunk_provider chunk_provider_biome_wired chunk_provider_nether \
	chunk_provider_end chunk_provider_flat nether_full end_full superflat_populate \
	overworld_full overworld_full_live overworld_region

BLOCKS := block_props_table block_tickers block_tickers_crops interact_blocks plant_growth
FLUIDS := fluid_flow populate_fluid_live populate_fluid_shim
LIGHT := light_propagation populate_light_live populate_light_shim
POPULATE := populate populate_animals populate_dungeon_golden populate_ice_snow
PHYSICS := physics_collision_math physics_collision_full player_physics_world player_physics_full pathfinding pathfinding12 path_navigate
# PLAYER/ENTITY drivers: survival-player loop composed from verified physics/inventory/props kernels.
# player_vitals: MC 1.11.2 FoodStats.onUpdate + natural regen + fall damage, verbatim-Java golden
# (java==cpu==cuda). The VANILLA vitals model the game uses (replaces the test-harness drain).
PLAYER := player_survival player_vitals player_death player_break
# UNIFIED: one composed world-step tick = env CAs (tick_world_halo) + player (player_survival).
UNIFIED := world_step
COMBAT := combat_math combat_knockback_resist enchant_damage_full enchant_protection_full \
	potion_effects_combat projectile_motion projectile_entity_hit explosion difficulty_scale
ITEMS := items_core items_tools_armor item_food_eat item_bow_use item_bucket_world \
	item_block_place \
	inventory_stack_rules crafting_recipes crafting_recipes_full smelting_recipes \
	tile_entity_furnace tile_entity_chest tile_entity_brewing tile_entity_spawner furnace_full_tick \
	spawner_activate container_click loot_table enchant_table
MOBS := mob_ai_zombie mob_ai_zombie_astar mob_ai_skeleton mob_ai_creeper mob_ai_spider \
	mob_ai_enderman mob_spawning mob_spawning_world mob_spawning_passive mob_spawning_oracle \
	animal_breed
PORTALS := nether_portal nether_portal_make nether_portal_world end_portal ender_dragon \
	ender_dragon_damage ender_dragon_death
TICK := tick_world_copy tick_random_block tick_fluid_ca tick_light_ca tick_compose_1 \
	tick_entities tick_spawn tick_compose_full tick_world_multi tick_world_halo \
	world_tick_vanilla world_weather pal_fluid_parity
# ENTITY: standalone entities-in-a-persistent-multi-chunk-world driver (spawn + zombie AI + A*
# pathfinding + cross-chunk movement/collision + melee combat), composed READ-ONLY over
# tick_world_multi's gen. Own section (NOT TICK/UNIFIED) to avoid a merge conflict with the
# concurrent world+player-tick unification agent.
ENTITY := entities_world entity_spine
BATCH := cuda_batch_worldgen cuda_batch_tick py_gym_env_smoke render_opt_obs_hook sps_benchmark \
	obs_camera
# REGION: worldgen "flywheel" - dense block TENSORS over an arbitrary (seed, origin, dims),
# tiled from the Java-golden-anchored cp_provide_chunk (core/region_tensor.h rt_fill). Each
# driver dumps every element so runner.py's line diff is a literal ELEMENT-WISE check:
#   region_tensor      - materialize + dump one tensor; CPU==CUDA element-wise.
#   region_reproduce   - idempotence (K regens) + tiling invariance (2x2) + origin-shift overlap
#                        + a CPU==CUDA fingerprint (same seed/origin -> identical every time).
#   batch_region_tensor- B envs in parallel on CUDA; element-wise CPU==CUDA + blocks/chunks-per-sec.
REGION := region_tensor region_reproduce batch_region_tensor
# GAMERULES: CPU-only unit test for the P0 GameRules wire-up (mc_gamerules.h consumers). Asserts
# each rule's toggle-diff behavior + default-rules parity; no CUDA/golden (self-checking, exit code).
GAMERULES := gamerules_wire
# TICKTRACE: CPU-only REAL-GAME tick-trace verifier (PORT_MATRIX P1). Loads scenario JSONL
# captured from the live Java game (verify/tick_trace) and diffs wt_vanilla_tick against it.
# Reads external files at runtime (no CPU==CUDA golden), so kept OUT of ALL_KERNELS like
# GAMERULES; built by cpu-all, run manually with scenario paths.
TICKTRACE := tick_trace_verify
# ENTITYTRACE: CPU-only REAL-GAME per-tick ENTITY verifier (PORT_MATRIX P1). Loads scenario
# JSONL captured from the live Java game (verify/entity_trace) and forward-integrates the C
# entity model (arrow via projectile_motion.h) diffing bit-exact. Same class as TICKTRACE:
# reads external files at runtime (no CPU==CUDA golden), OUT of ALL_KERNELS, built by cpu-all.
ENTITYTRACE := entity_trace_verify
# ITEMTRACE: CPU-only REAL-GAME per-tick verifier for EntityItem + EntityXPOrb (PORT_MATRIX P2).
# Loads scenario JSONL from the live Java game (verify/entity_trace) and forward-integrates the C
# item/orb model (entity_item.h / entity_xp_orb.h) diffing bit-exact. Same class as ENTITYTRACE:
# reads external files at runtime (no CPU==CUDA golden), OUT of ALL_KERNELS, built by cpu-all.
ITEMTRACE := item_trace_verify

WORLDGEN := $(WORLDGEN_CORE) $(WORLDGEN_SURFACE) $(WORLDGEN_CARVE) $(WORLDGEN_FEATURES) \
	$(WORLDGEN_STRUCTURES) $(WORLDGEN_DIMS)

# ALL_KERNELS = CPU+CUDA-verified units. GAMERULES is a CPU-only self-checking test (no .cu / no
# golden), so it is kept OUT of ALL_KERNELS (which drives cuda-all + verify-*) and wired into the
# CPU-only targets in the Makefile instead.
ALL_KERNELS := $(TRUNK) $(WORLDGEN) $(BLOCKS) $(FLUIDS) $(LIGHT) $(POPULATE) $(PHYSICS) \
	$(PLAYER) $(UNIFIED) $(COMBAT) $(ITEMS) $(MOBS) $(PORTALS) $(TICK) $(ENTITY) $(BATCH) \
	$(REGION)

# Native Metal currently covers deliberately selected FP32/integer leaf kernels.
# FP64-heavy worldgen/full-tick closures remain explicit non-members rather than
# silently narrowing their Java-compatible doubles.
METAL_KERNELS := smoke obs_camera
