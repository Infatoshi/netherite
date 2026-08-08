#!/usr/bin/env bash
# Standalone build+run for game/item_render.c verification. No Makefile edits.
# Run from anywhere; resolves the magma root relative to this script.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT_DIR="${TMPDIR:-$ROOT/../.tmp}"
mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/magma_test_item_render"

# regenerate the item atlas if missing (idempotent)
if [ ! -f assets/item_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_item_atlas.py
fi
if [ ! -f assets/atlas_gen.h ] || \
   ! grep -q 'CR_SPRITE_PLANKS_BIRCH' assets/atlas_gen.h || \
   ! grep -q 'CR_SPRITE_ITEMFRAME_BACKGROUND' assets/atlas_gen.h || \
   ! grep -q 'CR_SPRITE_COMMAND_BLOCK_FRONT' assets/atlas_gen.h; then
  uv run --no-project --with pillow python assets/build_atlas.py
fi
if ! grep -q 'CR_SPRITE_PLANKS_BIRCH' assets/atlas_gen.h || \
   ! grep -q 'CR_SPRITE_ITEMFRAME_BACKGROUND' assets/atlas_gen.h || \
   ! grep -q 'CR_SPRITE_COMMAND_BLOCK_FRONT' assets/atlas_gen.h; then
  echo "FAIL: atlas_gen.h missing required terrain sprites after rebuild" >&2
  exit 1
fi

$CC $CFLAGS \
    game/test_item_render.c \
    game/item_render.c \
    renderkernels/rk_31_facebakery_make_quad.c \
    assets/blockmodels.c \
    -lm -o "$OUT"

echo "built: $OUT"
"$OUT"
