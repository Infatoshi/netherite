#!/usr/bin/env bash
# Standalone build+run for game/hand.c held-item emit verification.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="/tmp/magma_test_hand"

if [ ! -f assets/item_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_item_atlas.py
fi
if [ ! -f assets/hand_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_hand_atlas.py
fi

$CC $CFLAGS \
    game/test_hand.c \
    game/hand.c \
    game/item_render.c \
    renderkernels/rk_31_facebakery_make_quad.c \
    assets/blockmodels.c \
    transform.c \
    core/math.c \
    core/shade.c \
    cpu/raster_cpu.c \
    -lm -o "$OUT"

echo "built: $OUT"
"$OUT"
