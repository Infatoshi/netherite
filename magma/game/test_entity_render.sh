#!/usr/bin/env bash
# Standalone build+run for game/entity_render.c verification. No Makefile edits.
# Run from anywhere; resolves the magma root relative to this script.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="/tmp/magma_test_entity_render"

# Clean trees and partial atlases: rebuild when missing required sprites
# (portal sheet, dissolve mask, ParticleExplosionLarge custom texture).
need_atlas=0
if [ ! -f assets/mob_atlas.h ]; then
  need_atlas=1
elif ! grep -q 'CR_MOB_PARTICLES' assets/mob_atlas.h || \
     ! grep -q 'CR_MOB_DRAGON_EXPLODING' assets/mob_atlas.h || \
     ! grep -q 'CR_MOB_EXPLOSION' assets/mob_atlas.h || \
     ! grep -q 'CR_ENDERCRYSTAL_BEAM_RGBA' assets/mob_atlas.h; then
  need_atlas=1
fi
if [ "$need_atlas" = 1 ]; then
  uv run --no-project --with pillow python assets/build_mob_atlas.py
fi
# Fail closed if still missing (jar unavailable): checker would SKIP, tests must not.
if ! grep -q 'CR_MOB_PARTICLES' assets/mob_atlas.h || \
   ! grep -q 'CR_MOB_EXPLOSION' assets/mob_atlas.h || \
   ! grep -q 'CR_ENDERCRYSTAL_BEAM_RGBA' assets/mob_atlas.h; then
  echo "FAIL: mob_atlas.h missing an entity pixel-path texture after rebuild" >&2
  exit 1
fi

$CC $CFLAGS \
    game/test_entity_render.c \
    game/entity_render.c \
    assets/blockmodels.c \
    transform.c \
    core/config.c \
    core/math.c \
    core/shade.c \
    cpu/raster_cpu.c \
    -lm -o "$OUT"

echo "built: $OUT"
"$OUT"
