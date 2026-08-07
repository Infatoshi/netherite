#!/usr/bin/env bash
# Build+run the mob lineup visual harness (see game/mob_lineup_demo.c).
# Usage: bash game/mob_lineup_demo.sh [outdir]   (default /tmp)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="/tmp/magma_mob_lineup_demo"

if [ ! -f assets/mob_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_mob_atlas.py
fi

$CC $CFLAGS \
    game/mob_lineup_demo.c \
    game/entity_render.c \
    assets/blockmodels.c \
    transform.c \
    core/math.c \
    core/shade.c \
    cpu/raster_cpu.c \
    -lm -o "$OUT"

"$OUT" "${1:-/tmp}"
