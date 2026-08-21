#!/usr/bin/env bash
# Complete block-id break/place/hit sound map. Default run is the native-only
# self-check; --oracle also compares against real Java via the trace gate.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"
make game/runtime.o game/world_spawn.o game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o core/config.o world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o >/dev/null
# shellcheck disable=SC2086
${CC:-gcc} -O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$BLAZE/core" \
	game/test_block_break_audio.c game/runtime.o game/world_spawn.o game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o core/config.o world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o -lm -o game/test_block_break_audio
if [ "${1:-}" = "--oracle" ]; then
	exec uv run --no-project python trace/test_block_break_audio.py \
		--native game/test_block_break_audio
fi
./game/test_block_break_audio >/dev/null
