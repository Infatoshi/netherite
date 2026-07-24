#!/usr/bin/env bash
# Focused gates for owned HUD / hand / overlay / underwater modules.
# Numerical formula checks + end-to-end frame composition + live inventory/
# overlay_live path. Does not touch shared mc_capture or tape scripts. Pixel
# parity vs Java requires goldens listed in ORACLE_CAPTURE.md (do not fabricate).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"  # -> c/magma
MCSIM="$(cd "$ROOT/../mc-sim" && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore -I$MCSIM/core"
DIR="raster/verify/ui_hud"

regen_if_stale() {
  local hdr="$1" py="$2"
  if [ ! -f "$hdr" ] || [ "$py" -nt "$hdr" ]; then
    echo "== regen $hdr (builder newer or missing) =="
    uv run --no-project --with pillow python "$py"
  fi
}

regen_if_stale assets/item_atlas.h assets/build_item_atlas.py
regen_if_stale assets/hand_atlas.h assets/build_hand_atlas.py
regen_if_stale assets/hud_atlas.h assets/build_hud_atlas.py
if [ ! -f assets/loading_bg.h ]; then
  uv run --no-project --with pillow python assets/build_loading_bg.py
fi

COMMON_SRC=(
  game/hud.c game/hand.c game/overlay.c
  game/item_render.c
  assets/blockmodels.c
  transform.c
  core/math.c core/shade.c
  cpu/raster_cpu.c
)

echo "== build $DIR/test_ui_hud_numerical =="
$CC $CFLAGS \
    "$DIR/test_ui_hud_numerical.c" \
    "${COMMON_SRC[@]}" \
    -lm -o /tmp/magma_test_ui_hud_numerical
echo "== run numerical =="
/tmp/magma_test_ui_hud_numerical

echo "== build $DIR/test_ui_hud_compose =="
$CC $CFLAGS \
    "$DIR/test_ui_hud_compose.c" \
    "${COMMON_SRC[@]}" \
    -lm -o /tmp/magma_test_ui_hud_compose
echo "== run compose =="
/tmp/magma_test_ui_hud_compose

# Live path: real inventory armor + overlay_live against GmWorld (runtime stack).
echo "== build live runtime objects =="
make -s game/runtime.o game/fluid_live.o game/config.o game/player_ctl.o \
  game/sel_box.o game/world_live.o game/live_sim.o game/mob_live.o \
  game/dragon_live.o game/structures_live.o game/portal_live.o \
  game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o \
  game/overlay.o game/overlay_live.o game/hud.o game/item_render.o \
  world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o \
  world/mesh.o world/world.o \
  renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
  core/math.o core/shade.o

LIVE_OBJS=(
  game/runtime.o game/fluid_live.o game/config.o game/player_ctl.o
  game/sel_box.o game/world_live.o game/live_sim.o game/mob_live.o
  game/dragon_live.o game/structures_live.o game/portal_live.o
  game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o
  game/overlay.o game/overlay_live.o game/hud.o game/item_render.o
  world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o
  world/mesh.o world/world.o
  renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o
  core/math.o core/shade.o
)

echo "== build $DIR/test_ui_hud_live =="
$CC $CFLAGS \
    "$DIR/test_ui_hud_live.c" \
    "${LIVE_OBJS[@]}" \
    -lm -o /tmp/magma_test_ui_hud_live
echo "== run live =="
/tmp/magma_test_ui_hud_live

echo "ui_hud gates: PASS"
