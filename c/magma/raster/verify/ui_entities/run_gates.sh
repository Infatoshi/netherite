#!/usr/bin/env bash
# Focused ui_entities geometry gates. No fabricated pixel goldens.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="/tmp/magma_ui_entities_geom_gates"

if [ ! -f assets/mob_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_mob_atlas.py
fi

$CC $CFLAGS \
    raster/verify/ui_entities/test_geom_gates.c \
    game/entity_render.c \
    game/item_render.c \
    assets/blockmodels.c \
    transform.c \
    core/math.c \
    core/shade.c \
    cpu/raster_cpu.c \
    -lm -o "$OUT"

echo "built: $OUT"
"$OUT"
