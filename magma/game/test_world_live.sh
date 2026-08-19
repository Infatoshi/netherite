#!/usr/bin/env bash
# Standalone build + run for game/world_live.c verification (no Makefile edits).
# Uses the canonical world-mesh source closure (includes core/config.c).
set -euo pipefail

MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
cd "$MAGMA"
# shellcheck source=game/standalone_test_common.sh
source "$(dirname "$0")/standalone_test_common.sh"

# ALLOCATE-ONCE: the live mesh/light/owr pools are sized for caps.view_radius (=8, the
# DECISION max), and gm_world_mesh_view clamps the runtime radius to it. Build the frozen-
# pose regression at SCN_VIEW_RADIUS=8 so BOTH sides (chunkscene_init AND gm_world_mesh_view)
# use R=8 and the column-frustum lock still holds within the sized pools.
MAGMA_STANDALONE_CFLAGS="$MAGMA_STANDALONE_CFLAGS -DSCN_VIEW_RADIUS=8"

magma_standalone_require_block_assets

SRCS=(
  game/test_world_live.c
  game/world_live.c
  "${MAGMA_WORLD_MESH_SRCS[@]}"
)

OUT="game/test_world_live"
echo "== compiling =="
magma_standalone_build "$OUT" "${SRCS[@]}"
echo "== running =="
"./$OUT"
