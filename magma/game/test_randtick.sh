#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"
make game/runtime.o game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o game/container_live.o \
	game/caps.o core/config.o world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o \
	world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o \
	assets/blockmodels.o core/math.o core/shade.o
${CC:-gcc} -O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$BLAZE/core" \
	game/test_randtick.c game/runtime.o game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o \
	game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o core/config.o world/light.o world/mesh_mc.o world/populate_mc.o \
	world/blocks.o world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o \
	assets/blockmodels.o core/math.o core/shade.o -lm -o game/test_randtick
./game/test_randtick
