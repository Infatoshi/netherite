#!/usr/bin/env bash
# Build+run the detmob pose gate. Not part of `make test`.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"
FIXTURE="${1:?fixture}"
OUT="${2:?out jsonl}"
make game/runtime.o game/world_spawn.o game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o \
	game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o core/config.o world/light.o world/mesh_mc.o world/populate_mc.o \
	world/gen_prefetch.o world/blocks.o world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o \
	assets/blockmodels.o core/math.o core/shade.o
${CC:-cc} -O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$BLAZE/core" \
	game/detmob_gate.c game/runtime.o game/world_spawn.o game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o \
	game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o core/config.o world/light.o world/mesh_mc.o \
	world/populate_mc.o world/gen_prefetch.o world/blocks.o world/mesh.o world/world.o \
	renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o \
	-lm -o game/detmob_gate
./game/detmob_gate "$FIXTURE" "$OUT"
