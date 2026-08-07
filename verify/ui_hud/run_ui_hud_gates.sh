#!/usr/bin/env bash
# Focused gates for owned HUD / hand / overlay / underwater modules.
# Numerical formula checks + end-to-end frame composition + live inventory/
# overlay_live path. Does not touch shared mc_capture or tape scripts. Pixel
# parity vs Java requires goldens listed in ORACLE_CAPTURE.md (do not fabricate).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../magma" && pwd)"  # -> magma
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore -I$BLAZE/core"
DIR="../verify/ui_hud"

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
make -s game/runtime.o game/nbt_blob.o game/fluid_live.o game/config.o game/player_ctl.o \
  game/sel_box.o game/world_live.o game/live_sim.o game/mob_live.o \
  game/randtick.o game/dragon_live.o game/structures_live.o game/end_city_live.o game/end_population_live.o game/portal_live.o \
  game/furnace_live.o game/chest_live.o game/brewing_live.o game/enchanting_live.o game/container_live.o game/caps.o core/config.o \
  game/overlay.o game/overlay_live.o game/hud.o game/item_render.o \
  world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o \
  world/mesh.o world/world.o \
  renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
  core/math.o core/shade.o

LIVE_OBJS=(
  game/runtime.o game/nbt_blob.o game/fluid_live.o game/config.o game/player_ctl.o
  game/sel_box.o game/world_live.o game/live_sim.o game/mob_live.o
  game/randtick.o game/dragon_live.o game/structures_live.o game/end_city_live.o game/end_population_live.o game/portal_live.o
  game/furnace_live.o game/chest_live.o game/brewing_live.o game/enchanting_live.o game/container_live.o game/caps.o core/config.o
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

# Oracle pixel ROI gate: requires goldens/ from capture_ui_hud.sh.
GOLDEN_DIR="$DIR/goldens"
CFRAME_DIR="$DIR/c_frames"
if [ -d "$GOLDEN_DIR" ] && ls "$GOLDEN_DIR"/*_a.png >/dev/null 2>&1; then
  echo "== build $DIR/ui_hud_candidate =="
  mkdir -p "$CFRAME_DIR"
  $CC $CFLAGS \
      "$DIR/ui_hud_candidate.c" \
      "${COMMON_SRC[@]}" \
      game/underwater.c \
      -lm -o /tmp/magma_ui_hud_candidate
  echo "== render C composition frames =="
  /tmp/magma_ui_hud_candidate --out "$CFRAME_DIR"
  echo "== oracle ROI compare (core oracle∪C + fullscreen hard_px + death/hand) =="
  # RESIDUAL (hard C residual) and FAIL both exit nonzero — no false parity.
  set +e
  uv run --no-project --with pillow --with numpy python \
    "$DIR/compare_ui_hud_oracle.py" \
    --goldens "$GOLDEN_DIR" \
    --cframes "$CFRAME_DIR" \
    --margin "${UI_HUD_MARGIN:-2.0}" \
    --report "$DIR/oracle_roi_report.json"
  roi_rc=$?
  set -e

  echo "== mutation regressions (fullscreen hard_px: inside + portal + underwater) =="
  set +e
  uv run --no-project --with pillow --with numpy python \
    "$DIR/test_ui_hud_mutations.py" \
    --goldens "$GOLDEN_DIR" \
    --cframes "$CFRAME_DIR"
  mut_rc=$?
  set -e

  if [ "$mut_rc" -ne 0 ]; then
    echo "ui_hud mutations: FAIL rc=$mut_rc"
    echo "ui_hud gates: FAIL (mutation leak or honest gate broken)"
    exit "$mut_rc"
  fi
  echo "== death + core HUD + underwater mutation self-test =="
  uv run --no-project --with pillow --with numpy python \
    "$DIR/compare_ui_hud_oracle.py" \
    --goldens "$GOLDEN_DIR" \
    --cframes "$CFRAME_DIR" \
    --margin "${UI_HUD_MARGIN:-2.0}" \
    --mutation-self-test
  if [ "$roi_rc" -ne 0 ]; then
    echo "ui_hud oracle ROI: nonzero (fail or hard residual) rc=$roi_rc"
    echo "ui_hud gates: RESIDUAL_OR_FAIL (composition + mutations ran; mutations PASS)"
    exit "$roi_rc"
  fi
else
  echo "== oracle ROI compare: SKIP (no goldens under $GOLDEN_DIR; run capture_ui_hud.sh) =="
  echo "oracle pixel parity: BLOCKED (no Java PNGs under ../verify/ui_hud/goldens/)"
fi

echo "ui_hud gates: PASS"
