#!/usr/bin/env bash
# Build and run the mob lineup inspection harness.
# Usage: bash tests/inspect_mob_lineup.sh [outdir]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="/tmp/magma_inspect_mob_lineup"

if [ ! -f assets/mob_atlas.h ]; then
  make assets/mob_atlas.h
fi

$CC $CFLAGS \
    tests/inspect_mob_lineup.c \
    game/entity_render.c \
    transform.c \
    core/math.c \
    core/shade.c \
    cpu/raster_cpu.c \
    -lm -o "$OUT"

"$OUT" "${1:-/tmp}"
