#!/usr/bin/env bash
# Build and run the live composition harness (dig/place/interact/inv/worldTime/live_sim).
# Links the SHIPPED game modules the same way make game does (not a reimplementation).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
# shellcheck source=game/standalone_test_common.sh
source "$(dirname "$0")/standalone_test_common.sh"

make game

# Force rebuild of composition objects, then link harness against them.
make game/player_ctl.o game/sel_box.o game/live_sim.o game/timer.o \
  game/input_map.o "${MAGMA_WORLD_LIVE_OBJS[@]}"

OBJS=(
  game/player_ctl.o game/sel_box.o game/live_sim.o game/input_map.o game/timer.o
  "${MAGMA_WORLD_LIVE_OBJS[@]}"
)

# shellcheck disable=SC2086
$CC $MAGMA_STANDALONE_CFLAGS $MAGMA_STANDALONE_INCLUDES \
  game/test_play_compose.c "${OBJS[@]}" -lm -o game/test_play_compose
./game/test_play_compose
echo "test_play_compose.sh: exit $?"
