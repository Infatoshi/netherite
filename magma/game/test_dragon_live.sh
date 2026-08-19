#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
# shellcheck source=game/standalone_test_common.sh
source "$(dirname "$0")/standalone_test_common.sh"

make game/dragon_live.o "${MAGMA_WORLD_LIVE_OBJS[@]}"
# shellcheck disable=SC2086
$CC $MAGMA_STANDALONE_CFLAGS $MAGMA_STANDALONE_INCLUDES \
  game/test_dragon_live.c game/dragon_live.o "${MAGMA_WORLD_LIVE_OBJS[@]}" \
  -lm -o game/test_dragon_live
./game/test_dragon_live
