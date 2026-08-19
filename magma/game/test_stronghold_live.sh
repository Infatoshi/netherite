#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
# shellcheck source=game/standalone_test_common.sh
source "$(dirname "$0")/standalone_test_common.sh"

make game/structures_live.o "${MAGMA_WORLD_LIVE_OBJS[@]}"
# shellcheck disable=SC2086
$CC $MAGMA_STANDALONE_CFLAGS $MAGMA_STANDALONE_INCLUDES \
  game/test_stronghold_live.c game/structures_live.o "${MAGMA_WORLD_LIVE_OBJS[@]}" \
  -lm -o game/test_stronghold_live
./game/test_stronghold_live
