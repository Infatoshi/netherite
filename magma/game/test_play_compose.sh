#!/usr/bin/env bash
# Build and run the live composition harness (dig/place/interact/inv/worldTime/live_sim).
# Links the SHIPPED game modules the same way make game does (not a reimplementation).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"

make game

CC=${CC:-gcc}
CFLAGS="-O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I$BLAZE/core"

# Force rebuild of composition objects, then link harness against them.
make game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o game/caps.o core/config.o game/timer.o \
  game/input_map.o world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o \
  world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o \
  assets/blockmodels.o core/math.o core/shade.o

OBJS="game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o game/caps.o core/config.o game/input_map.o \
  game/timer.o \
  world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
  renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
  core/math.o core/shade.o"

$CC $CFLAGS game/test_play_compose.c $OBJS -lm -o game/test_play_compose
./game/test_play_compose
echo "test_play_compose.sh: exit $?"
