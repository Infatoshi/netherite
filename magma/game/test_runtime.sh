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
	game/end_population_live.o game/portal_live.o game/furnace_live.o
	game/chest_live.o game/brewing_live.o game/enchanting_live.o
	game/container_live.o game/caps.o core/config.o world/light.o
	world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o
	world/world.o renderkernels/rk_31_facebakery_make_quad.o
	assets/blockmodels.o core/math.o core/shade.o
)
make "${RUNTIME_OBJECTS[@]}"
for test in player_effects redstone_use tnt_explosion shearing_runtime \
		grazing_runtime sheep_color_runtime sheep_feed_runtime \
		sheep_mating_runtime runtime; do
	${CC:-gcc} -O2 -ffp-contract=off -Wall -Wextra \
		-I. -Icore -I"$BLAZE/core" \
		"game/test_${test}.c" "${RUNTIME_OBJECTS[@]}" -lm \
		-o "game/test_${test}"
done
${CC:-gcc} -O2 -Wall -Wextra -I. \
	game/test_nbt_blob.c game/nbt_blob.o -o game/test_nbt_blob
./game/test_nbt_blob
./game/test_player_effects
./game/test_redstone_use
./game/test_tnt_explosion
./game/test_shearing_runtime
./game/test_grazing_runtime
./game/test_sheep_color_runtime
./game/test_sheep_feed_runtime
./game/test_sheep_mating_runtime
./game/test_runtime
