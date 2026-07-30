#!/usr/bin/env bash
# Standalone build + run for game/world_live.c verification (no Makefile edits).
# Mirrors the `make test-mesh` object set (world mesher + light + populate + block
# models + facebakery kernel + core math/shade) and adds game/world_live.c + the test.
set -euo pipefail

MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$(dirname "$0")/../../blaze" && pwd)"
cd "$MAGMA"

CC="${CC:-gcc}"
# ALLOCATE-ONCE: the live mesh/light/owr pools are sized for caps.view_radius (=8, the
# DECISION max), and gm_world_mesh_view clamps the runtime radius to it. Build the frozen-
# pose regression at SCN_VIEW_RADIUS=8 so BOTH sides (chunkscene_init AND gm_world_mesh_view)
# use R=8 and the byte-identical mesh==chunkscene lock still holds within the sized pools.
CFLAGS="-O2 -ffp-contract=off -Wall -Wextra -DSCN_VIEW_RADIUS=8"
INCLUDES="-I. -Icore -I${BLAZE}/core"

SRCS=(
  game/test_world_live.c
  game/world_live.c
  game/caps.c
  world/mesh_mc.c
  world/light.c
  world/populate_mc.c
  assets/blockmodels.c
  renderkernels/rk_31_facebakery_make_quad.c
  core/math.c
  core/shade.c
)

OUT="game/test_world_live"
echo "== compiling =="
$CC $CFLAGS $INCLUDES "${SRCS[@]}" -o "$OUT" -lm
echo "== running =="
"./$OUT"
