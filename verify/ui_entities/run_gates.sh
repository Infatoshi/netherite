#!/usr/bin/env bash
# Focused ui_entities geometry gates. No fabricated pixel goldens.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../magma" && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="/tmp/magma_ui_entities_geom_gates"

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
if ! grep -q 'CR_MOB_PARTICLES' assets/mob_atlas.h || \
   ! grep -q 'CR_MOB_EXPLOSION' assets/mob_atlas.h || \
   ! grep -q 'CR_ENDERCRYSTAL_BEAM_RGBA' assets/mob_atlas.h; then
  echo "FAIL: mob_atlas.h missing an entity pixel-path texture after rebuild" >&2
  exit 1
fi

$CC $CFLAGS \
    ../verify/ui_entities/test_geom_gates.c \
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
