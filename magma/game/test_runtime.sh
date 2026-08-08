#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"
RUNTIME_OBJECTS=(
	game/runtime.o game/nbt_blob.o game/fluid_live.o game/config.o
	game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o
	game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o
	game/village_live.o game/villager_trade.o game/end_city_live.o
	game/end_population_live.o game/mansion_live.o game/portal_live.o game/furnace_live.o
	game/chest_live.o game/brewing_live.o game/enchanting_live.o
	game/container_live.o game/caps.o core/config.o world/light.o
	world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o
	world/world.o renderkernels/rk_31_facebakery_make_quad.o
	assets/blockmodels.o core/math.o core/shade.o
)
make "${RUNTIME_OBJECTS[@]}"
# test_bow_live has a dedicated target because it is also run alone during
# projectile work, but remains part of this broad regression sweep.
for test in player_effects player_attack_audio_runtime \
		player_attack_particles_runtime player_movement_audio_runtime \
		redstone_use tnt_explosion shearing_runtime \
		grazing_runtime game_mode_runtime sheep_color_runtime sheep_feed_runtime \
		sheep_mating_runtime zombie_villager_cure_runtime hostile_death_live \
		hostile_environment_death_live passive_environment_death_live \
		anvil_live mending_live equipment_enchantment_live \
		block_break_audio runtime; do
	${CC:-gcc} -O2 -ffp-contract=off -Wall -Wextra \
		-I. -Icore -I"$BLAZE/core" \
		"game/test_${test}.c" "${RUNTIME_OBJECTS[@]}" -lm \
		-o "game/test_${test}"
done
${CC:-gcc} -O2 -Wall -Wextra -I. \
	game/test_nbt_blob.c game/nbt_blob.o -o game/test_nbt_blob
./game/test_nbt_blob
./game/test_player_effects
./game/test_player_attack_audio_runtime
./game/test_player_attack_particles_runtime
./game/test_player_movement_audio_runtime
./game/test_redstone_use
./game/test_tnt_explosion
./game/test_shearing_runtime
make game/test_mooshroom_runtime >/dev/null
./game/test_mooshroom_runtime
./game/test_grazing_runtime
./game/test_game_mode_runtime
./game/test_sheep_color_runtime
./game/test_sheep_feed_runtime
./game/test_sheep_mating_runtime
./game/test_zombie_villager_cure_runtime
./game/test_hostile_death_live
./game/test_hostile_environment_death_live
./game/test_passive_environment_death_live
./game/test_anvil_live
./game/test_mending_live
./game/test_equipment_enchantment_live
make game/test_bow_live >/dev/null
./game/test_bow_live
make game/test_xp_bottle_live >/dev/null
./game/test_xp_bottle_live
make game/test_stack_tag_capacity >/dev/null
./game/test_stack_tag_capacity
make game/test_living_cold_slot >/dev/null
./game/test_living_cold_slot
./game/test_block_break_audio >/dev/null
MAGMA_ITEM_SPAWN_LIMIT=256 ./game/test_runtime
