#!/usr/bin/env bash
# Standalone build+run for game/entity_render.c verification. No Makefile edits.
# Run from anywhere; resolves the craster root relative to this script.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="/tmp/craster_test_entity_render"

# regenerate the mob atlas if missing (idempotent)
if [ ! -f assets/mob_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_mob_atlas.py
fi

$CC $CFLAGS \
    game/test_entity_render.c \
    game/entity_render.c \
    transform.c \
    core/math.c \
    core/shade.c \
    cpu/raster_cpu.c \
    -lm -o "$OUT"

echo "built: $OUT"
"$OUT"
