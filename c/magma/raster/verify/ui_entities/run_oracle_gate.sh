#!/usr/bin/env bash
# Build entity_oracle_candidate (frame_capture CPU path) and ROI-gate vs goldens.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"  # c/magma
DIR="raster/verify/ui_entities"
GOLDENS="$DIR/goldens"
MCSIM="$(cd "$ROOT/../mc-sim" && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore -I$MCSIM/core -I$MCSIM"
OUT="/tmp/magma_entity_oracle_candidate"

# Atlases required by frame_capture / entity path.
need_atlas() {
  local hdr="$1" py="$2"
  if [ ! -f "$hdr" ] || [ "$py" -nt "$hdr" ]; then
    echo "== regen $hdr =="
    uv run --no-project --with pillow python "$py"
  fi
}
need_atlas assets/mob_atlas.h assets/build_mob_atlas.py
need_atlas assets/item_atlas.h assets/build_item_atlas.py
if [ ! -f assets/blockmodels.h ] || [ ! -f assets/atlas.h ]; then
  uv run --no-project --with pillow python assets/build_atlas.py || true
fi

echo "== build runtime objects for frame_capture candidate =="
make -s game/runtime.o game/fluid_live.o game/config.o game/player_ctl.o \
  game/sel_box.o game/world_live.o game/live_sim.o game/mob_live.o \
  game/dragon_live.o game/structures_live.o game/portal_live.o \
  game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o \
  game/overlay.o game/overlay_live.o game/hud.o game/hand.o game/item_render.o \
  game/entity_render.o game/frame_capture.o game/sky.o game/screen.o \
  game/player_preview.o game/underwater.o game/timer.o \
  world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o \
  world/mesh.o world/world.o \
  renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
  core/math.o core/shade.o transform.o cpu/raster_cpu.o

OBJS=(
  game/runtime.o game/fluid_live.o game/config.o game/player_ctl.o
  game/sel_box.o game/world_live.o game/live_sim.o game/mob_live.o
  game/dragon_live.o game/structures_live.o game/portal_live.o
  game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o
  game/overlay.o game/overlay_live.o game/hud.o game/hand.o game/item_render.o
  game/entity_render.o game/frame_capture.o game/sky.o game/screen.o
  game/player_preview.o game/underwater.o game/timer.o
  world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o
  world/mesh.o world/world.o
  renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o
  core/math.o core/shade.o transform.o cpu/raster_cpu.o
)

echo "== build $OUT =="
$CC $CFLAGS \
  "$DIR/entity_oracle_candidate.c" \
  "${OBJS[@]}" \
  -lm -o "$OUT"

if [ ! -d "$GOLDENS" ] || [ -z "$(ls "$GOLDENS"/*_a.png 2>/dev/null)" ]; then
  echo "FAIL: no Java goldens under $GOLDENS — run capture_ui_entities.sh first" >&2
  exit 1
fi

echo "== ROI gate vs goldens =="
uv run --no-project --with pillow --with numpy \
  python "$DIR/compare_ui_entities_oracle.py" \
  --goldens "$GOLDENS" \
  --candidate "$OUT" \
  --c-out /tmp/magma_ui_entities_c
