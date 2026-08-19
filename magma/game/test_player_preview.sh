#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
# shellcheck source=game/standalone_test_common.sh
source "$(dirname "$0")/standalone_test_common.sh"

# shade.o pulls cr_cfg(); always link core/config.o from the render-core closure.
make -s game/player_preview.o "${MAGMA_RENDER_CORE_OBJS[@]}" cpu/raster_cpu.o
# shellcheck disable=SC2086
$CC $MAGMA_STANDALONE_CFLAGS $MAGMA_STANDALONE_INCLUDES \
  game/test_player_preview.c game/player_preview.o \
  "${MAGMA_RENDER_CORE_OBJS[@]}" cpu/raster_cpu.o \
  -lm -o game/test_player_preview
./game/test_player_preview
