#!/usr/bin/env bash
# Build entity_oracle_candidate (frame_capture CPU path) and ROI-gate vs goldens.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../magma" && pwd)"  # magma
DIR="../verify/ui_entities"
GOLDENS="${ENTITY_GATE_GOLDENS:-$DIR/goldens}"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore -I$BLAZE/core -I$BLAZE"
OUT="${ENTITY_GATE_CANDIDATE:-$ROOT/../.tmp/magma_entity_oracle_candidate}"
mkdir -p "$(dirname "$OUT")"

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

if [ "${ENTITY_GATE_REUSE_CANDIDATE:-0}" != 1 ]; then
echo "== build runtime objects for frame_capture candidate =="
make -s game/runtime.o game/nbt_blob.o game/fluid_live.o game/config.o game/player_ctl.o \
  game/sel_box.o game/world_live.o game/live_sim.o game/mob_live.o \
  game/randtick.o game/dragon_live.o game/structures_live.o game/village_live.o game/villager_trade.o \
  game/end_city_live.o game/end_population_live.o game/mansion_live.o game/portal_live.o game/fishing_render.o game/particles_live.o \
  game/furnace_live.o game/chest_live.o game/brewing_live.o game/enchanting_live.o game/container_live.o game/caps.o core/config.o \
  game/overlay.o game/overlay_live.o game/hud.o game/hand.o game/item_render.o \
  game/entity_render.o game/frame_capture.o game/sky.o game/screen.o \
  game/structure_render.o \
  game/player_preview.o game/underwater.o game/timer.o \
  world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o \
  world/mesh.o world/world.o \
  renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
  core/math.o core/shade.o transform.o cpu/raster_cpu.o

OBJS=(
  game/runtime.o game/nbt_blob.o game/fluid_live.o game/config.o game/player_ctl.o
  game/sel_box.o game/world_live.o game/live_sim.o game/mob_live.o
  game/randtick.o game/dragon_live.o game/structures_live.o game/village_live.o game/villager_trade.o
  game/end_city_live.o game/end_population_live.o game/mansion_live.o game/portal_live.o game/fishing_render.o game/particles_live.o
  game/furnace_live.o game/chest_live.o game/brewing_live.o game/enchanting_live.o game/container_live.o game/caps.o core/config.o
  game/overlay.o game/overlay_live.o game/hud.o game/hand.o game/item_render.o
  game/entity_render.o game/frame_capture.o game/sky.o game/screen.o
  game/structure_render.o
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
elif [ ! -x "$OUT" ]; then
  echo "FAIL: ENTITY_GATE_REUSE_CANDIDATE=1 but candidate is missing: $OUT" >&2
  exit 1
fi

if [ "${ENTITY_GATE_BUILD_ONLY:-0}" = 1 ]; then
    echo "run_oracle_gate: build-only complete"
    exit 0
fi

if [ "${ENTITY_GATE_ENDER_CHEST_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_ender_chest_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  "$OUT" --state gui_ender_chest \
    --meta "$HUD_GOLDENS/meta/gui_ender_chest.json" \
    --ppm "$C_OUT/gui_ender_chest.ppm" --w 854 --h 480
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_ender_chest_gui.py" \
    --goldens "$HUD_GOLDENS" \
    --c-frame "$C_OUT/gui_ender_chest.ppm" \
    --json-out "$C_OUT/ender_chest_gui_report.json"
  echo "run_oracle_gate: Ender Chest GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_LARGE_CHEST_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_large_chest_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  "$OUT" --state gui_large_chest \
    --meta "$HUD_GOLDENS/meta/gui_large_chest.json" \
    --ppm "$C_OUT/gui_large_chest.ppm" --w 854 --h 480
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_large_chest_gui.py" \
    --goldens "$HUD_GOLDENS" \
    --c-frame "$C_OUT/gui_large_chest.ppm" \
    --json-out "$C_OUT/large_chest_gui_report.json"
  echo "run_oracle_gate: Large Chest GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_STATIC_CONTAINER_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/static_container_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  for state in gui_dispenser gui_dropper gui_hopper; do
    "$OUT" --state "$state" \
      --meta "$HUD_GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_static_container_gui.py" \
    --goldens "$HUD_GOLDENS" \
    --c-frames "$C_OUT" \
    --json-out "$C_OUT/static_container_gui_report.json"
  echo "run_oracle_gate: static-container GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_PROCESSING_CONTAINER_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/processing_container_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  for state in gui_furnace gui_brewing_stand; do
    "$OUT" --state "$state" \
      --meta "$HUD_GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_processing_container_gui.py" \
    --goldens "$HUD_GOLDENS" \
    --c-frames "$C_OUT" \
    --json-out "$C_OUT/processing_container_gui_report.json"
  echo "run_oracle_gate: processing-container GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_STANDARD_CONTAINER_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/standard_container_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  for state in gui_crafting_table gui_anvil gui_merchant gui_enchanting; do
    "$OUT" --state "$state" \
      --meta "$HUD_GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
    uv run --no-project --with pillow --with numpy \
      python "$DIR/measure_standard_container_gui.py" \
      --state "$state" --goldens "$HUD_GOLDENS" \
      --c-frame "$C_OUT/$state.ppm" \
      --json-out "$C_OUT/${state}_report.json"
  done
  echo "run_oracle_gate: standard populated-container GUIs pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_ENCHANTING_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/enchanting_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  "$OUT" --state gui_enchanting \
    --meta "$HUD_GOLDENS/meta/gui_enchanting.json" \
    --ppm "$C_OUT/gui_enchanting.ppm" --w 854 --h 480
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_standard_container_gui.py" \
    --state gui_enchanting \
    --goldens "$HUD_GOLDENS" \
    --c-frame "$C_OUT/gui_enchanting.ppm" \
    --json-out "$C_OUT/enchanting_gui_report.json"
  echo "run_oracle_gate: Enchanting GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_CRAFTING_TABLE_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/crafting_table_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  "$OUT" --state gui_crafting_table \
    --meta "$HUD_GOLDENS/meta/gui_crafting_table.json" \
    --ppm "$C_OUT/gui_crafting_table.ppm" --w 854 --h 480
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_standard_container_gui.py" \
    --state gui_crafting_table \
    --goldens "$HUD_GOLDENS" \
    --c-frame "$C_OUT/gui_crafting_table.ppm" \
    --json-out "$C_OUT/crafting_table_gui_report.json"
  echo "run_oracle_gate: Crafting Table GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_ANVIL_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/anvil_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  "$OUT" --state gui_anvil \
    --meta "$HUD_GOLDENS/meta/gui_anvil.json" \
    --ppm "$C_OUT/gui_anvil.ppm" --w 854 --h 480
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_standard_container_gui.py" \
    --state gui_anvil \
    --goldens "$HUD_GOLDENS" \
    --c-frame "$C_OUT/gui_anvil.ppm" \
    --json-out "$C_OUT/anvil_gui_report.json"
  echo "run_oracle_gate: Anvil GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_MERCHANT_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/merchant_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  "$OUT" --state gui_merchant \
    --meta "$HUD_GOLDENS/meta/gui_merchant.json" \
    --ppm "$C_OUT/gui_merchant.ppm" --w 854 --h 480
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_standard_container_gui.py" \
    --state gui_merchant \
    --goldens "$HUD_GOLDENS" \
    --c-frame "$C_OUT/gui_merchant.ppm" \
    --json-out "$C_OUT/merchant_gui_report.json"
  echo "run_oracle_gate: Merchant GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_BEACON_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/beacon_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  "$OUT" --state gui_beacon \
    --meta "$HUD_GOLDENS/meta/gui_beacon.json" \
    --ppm "$C_OUT/gui_beacon.ppm" --w 854 --h 480
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_beacon_gui.py" \
    --goldens "$HUD_GOLDENS" \
    --c-frame "$C_OUT/gui_beacon.ppm" \
    --json-out "$C_OUT/beacon_gui_report.json"
  echo "run_oracle_gate: Beacon GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_BEACON_WORLD_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/beacon_world_c_$$}"
  mkdir -p "$C_OUT"
  for state in beacon_world_colored beacon_world_background; do
    "$OUT" --state "$state" \
      --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_beacon_world.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" \
    --json-out "$C_OUT/beacon_world_report.json"
  echo "run_oracle_gate: Beacon world stable bounded-pixel PASS"
  exit 0
fi

if [ "${ENTITY_GATE_SPAWNER_WORLD_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/spawner_world_c_$$}"
  mkdir -p "$C_OUT"
  for state in \
    spawner_pig_saved spawner_zombie_noai_saved spawner_background
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_spawner_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/spawner_subject_report.json"
  echo "run_oracle_gate: saved mob-spawner miniature bounded-pixel PASS"
  exit 0
fi

if [ "${ENTITY_GATE_SLIME_MAGMA_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/slime_magma_c_$$}"
  mkdir -p "$C_OUT"
  for state in \
    slime_size1 slime_size2 slime_size4 slime_squish \
    magma_size1 magma_size2 magma_size4 magma_squish \
    entity_gallery_background
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_slime_magma_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/slime_magma_subject_report.json"
  echo "run_oracle_gate: Slime and Magma Cube bounded-pixel PASS"
  exit 0
fi

if [ "${ENTITY_GATE_VISUAL_TAIL_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/visual_tail_c_$$}"
  mkdir -p "$C_OUT"
  for state in \
    dragon_death_50 dragon_death_100 dragon_death_190 dragon_background \
    dig_background \
    fireball_small fireball_dragon xp_orb projectile_background
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/compare_ui_entities_oracle.py" \
    --goldens "$GOLDENS" --candidate "$OUT" --c-out "$C_OUT" --info \
    --states dig_stone dig_grass \
    --json-out "$C_OUT/dig_full_frame_diagnostic.json"
  extra=()
  if [ "${ENTITY_GATE_REPORT_ONLY:-0}" = 1 ]; then
    extra+=(--report-only)
  fi
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_visual_tail_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --debug-out "$C_OUT/debug" \
    --json-out "$C_OUT/visual_tail_subject_report.json" "${extra[@]}"
  echo "run_oracle_gate: visual-tail stable same-scene gate PASS"
  exit 0
fi

if [ "${ENTITY_GATE_BAT_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/bat_entity_c_$$}"
  mkdir -p "$C_OUT"
  for state in bat_flying bat_hanging bat_background; do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_bat_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/bat_subject_report.json"
  echo "run_oracle_gate: Bat same-scene subject PASS"
  exit 0
fi

if [ "${ENTITY_GATE_SQUID_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/squid_entity_c_$$}"
  mkdir -p "$C_OUT"
  for state in squid_swim_pose squid_dry_pose squid_background; do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_squid_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/squid_subject_report.json"
  echo "run_oracle_gate: Squid same-scene subject PASS"
  exit 0
fi

if [ "${ENTITY_GATE_MOOSHROOM_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/mooshroom_entity_c_$$}"
  mkdir -p "$C_OUT"
  for state in mooshroom_adult_idle mooshroom_adult_head_pose \
               mooshroom_child mooshroom_background; do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_mooshroom_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/mooshroom_subject_report.json"
  echo "run_oracle_gate: Mooshroom same-scene subject PASS"
  exit 0
fi

if [ "${ENTITY_GATE_MINECART_TNT_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/minecart_tnt_c_$$}"
  mkdir -p "$C_OUT"
  for state in \
    minecart_tnt_fuse80_flash minecart_tnt_fuse79_dark \
    minecart_tnt_fuse4_flash minecart_tnt_fuse5_dark \
    minecart_tnt_unprimed minecart_tnt_background
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_minecart_tnt_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/minecart_tnt_subject_report.json"
  echo "run_oracle_gate: TNT minecart phase and bounded-raster PASS"
  exit 0
fi

if [ "${ENTITY_GATE_MINECART_VARIANTS_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/minecart_variants_c_$$}"
  mkdir -p "$C_OUT"
  for state in \
    minecart_empty_model minecart_chest_model minecart_furnace_model \
    minecart_hopper_model minecart_spawner_model minecart_command_model \
    minecart_tnt_background
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_minecart_variant_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/minecart_variant_subject_report.json"
  echo "run_oracle_gate: six non-TNT minecart variants bounded-pixel PASS"
  exit 0
fi

if [ "${ENTITY_GATE_BOAT_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/boat_variants_c_$$}"
  mkdir -p "$C_OUT"
  for state in \
    boat_oak_model boat_spruce_model boat_birch_model \
    boat_jungle_model boat_acacia_model boat_darkoak_model boat_background
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_boat_variant_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/boat_variant_subject_report.json"
  echo "run_oracle_gate: six Boat wood variants bounded-pixel PASS"
  exit 0
fi

if [ "${ENTITY_GATE_SHULKER_BOX_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_shulker_box_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  "$OUT" --state gui_shulker_box \
    --meta "$HUD_GOLDENS/meta/gui_shulker_box.json" \
    --ppm "$C_OUT/gui_shulker_box.ppm" --w 854 --h 480
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_shulker_box_gui.py" \
    --goldens "$HUD_GOLDENS" \
    --c-frame "$C_OUT/gui_shulker_box.ppm" \
    --json-out "$C_OUT/shulker_box_gui_report.json"
  echo "run_oracle_gate: Shulker Box GUI pixel-exact PASS"
  exit 0
fi

if [ "${ENTITY_GATE_ENDER_CHEST_WORLD_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_ender_chest_world_c_$$}"
  mkdir -p "$C_OUT"
  for state in ender_chest_closed ender_chest_open ender_chest_background; do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_ender_chest_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/ender_chest_subject_report.json"
  echo "run_oracle_gate: Ender Chest TESR bounded fixed-function PASS"
  exit 0
fi

if [ "${ENTITY_GATE_CHEST_WORLD_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_chest_world_c_$$}"
  mkdir -p "$C_OUT"
  for state in \
    chest_normal_closed chest_normal_open \
    chest_trapped_closed chest_trapped_open \
    chest_normal_double_x_open chest_trapped_double_z_open \
    chest_background
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_chest_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/chest_subject_report.json"
  echo "run_oracle_gate: wooden Chest TESR bounded fixed-function PASS"
  exit 0
fi

if [ "${ENTITY_GATE_SHULKER_BOX_WORLD_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/shulker_box_world_c_$$}"
  mkdir -p "$C_OUT"
  for state in \
    shulker_box_white_up_closed shulker_box_white_up_open \
    shulker_box_orange_down_open shulker_box_purple_north_half \
    shulker_box_blue_south_open shulker_box_red_west_open \
    shulker_box_black_east_open shulker_box_background
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_shulker_box_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest --require \
    --json-out "$C_OUT/shulker_box_subject_report.json"
  echo "run_oracle_gate: Shulker Box TESR bounded fixed-function PASS"
  exit 0
fi

if [ "${ENTITY_GATE_HORSE_GUI_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_horse_gui_c_$$}"
  HUD_GOLDENS="$ROOT/../verify/ui_hud/goldens"
  mkdir -p "$C_OUT"
  for state in gui_horse_armor gui_horse_donkey_chest gui_horse_llama_chest; do
    "$OUT" --state "$state" --meta "$HUD_GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_horse_gui.py" \
    --goldens "$HUD_GOLDENS" --c-frames "$C_OUT" \
    --json-out "$C_OUT/horse_gui_report.json"
  echo "run_oracle_gate: horse GUI bounded PASS"
  exit 0
fi

if [ "${ENTITY_GATE_HORSE_PARTICLES_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_horse_particles_c_$$}"
  mkdir -p "$C_OUT"
  uv run --no-project --with pillow --with numpy \
    python "$DIR/compare_ui_entities_oracle.py" \
    --goldens "$GOLDENS" --candidate "$OUT" --c-out "$C_OUT" --info \
    --states horse_particle_control horse_taming_smoke horse_breeding_heart \
    --json-out "$C_OUT/full_frame_diagnostic.json"
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_horse_particles.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/horse_particle_report.json"
  echo "run_oracle_gate: horse particle subject PASS"
  exit 0
fi

if [ "${ENTITY_GATE_HORSE_MODEL_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_horse_model_c_$$}"
  mkdir -p "$C_OUT"
  "$OUT" --state horse_background \
    --meta "$GOLDENS/baselines/horse_background.json" \
    --ppm "$C_OUT/horse_background.ppm" --w 854 --h 480
  "$OUT" --state skeleton_trap_group_background \
    --meta "$GOLDENS/meta/skeleton_trap_group_background.json" \
    --ppm "$C_OUT/skeleton_trap_group_background.ppm" --w 854 --h 480
  for state in \
    horse_marked_armor horse_iron_idle horse_saddled_idle horse_eating \
    horse_rearing horse_mouth horse_gait horse_tail horse_saddled_pose \
    horse_child donkey_chested_saddled mule_base skeleton_horse zombie_horse \
    skeleton_trap_rider skeleton_trap_group
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_horse_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" \
    --max-native-px 5000 --bounded-fixed-function --selftest \
    --json-out "$C_OUT/horse_subject_report.json"
  echo "run_oracle_gate: horse model fixed-function boundary PASS"
  exit 0
fi

if [ "${ENTITY_GATE_LLAMA_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_llama_entity_c_$$}"
  mkdir -p "$C_OUT"
  states=(
    llama_creamy_idle llama_white_idle llama_brown_idle llama_gray_idle
    llama_decor_white llama_decor_orange llama_decor_magenta
    llama_decor_light_blue llama_decor_yellow llama_decor_lime
    llama_decor_pink llama_decor_gray llama_decor_silver llama_decor_cyan
    llama_decor_purple llama_decor_blue llama_decor_brown llama_decor_green
    llama_decor_red llama_decor_black llama_gait
    llama_gray_decor_chest llama_child_decor llama_spit
  )
  "$OUT" --state llama_background \
    --meta "$GOLDENS/meta/llama_background.json" \
    --ppm "$C_OUT/llama_background.ppm" --w 854 --h 480
  for state in "${states[@]}"; do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_llama_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" \
    --bounded-fixed-function --selftest \
    --json-out "$C_OUT/llama_subject_report.json"
  echo "run_oracle_gate: llama fixed-function boundary PASS"
  exit 0
fi

if [ "${ENTITY_GATE_HANGING_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/.tmp/hanging_entity_c_$$}"
  mkdir -p "$C_OUT"
  states=(
    hanging_painting_kebab hanging_painting_pointer
    hanging_frame_empty hanging_frame_stick hanging_frame_dirt
    hanging_frame_map hanging_leash_knot hanging_leashed_llama
    hanging_wall_background hanging_fence_background
  )
  for state in "${states[@]}"; do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_hanging_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/hanging_subject_report.json"
  echo "run_oracle_gate: hanging family fixed-function boundary PASS"
  exit 0
fi

if [ ! -d "$GOLDENS" ] || [ -z "$(ls "$GOLDENS"/*_a.png 2>/dev/null)" ]; then
  echo "FAIL: no Java goldens under $GOLDENS — run capture_ui_entities.sh first" >&2
  exit 1
fi

if [ "${ENTITY_GATE_GLOWING_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-$ROOT/../.tmp/glowing_c_$$}"
  mkdir -p "$C_OUT"
  for state in \
    magma_size2 magma_size2_glowing slime_size2 slime_size2_glowing
  do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_glowing_outline.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --selftest \
    --json-out "$C_OUT/glowing_report.json"
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_glowing_outline.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" \
    --control slime_size2 --glowing slime_size2_glowing --slime-stress \
    --json-out "$C_OUT/glowing_slime_stress_report.json"
  echo "run_oracle_gate: Glowing exact plus Slime bounded-stress PASS"
  exit 0
fi

if [ "${ENTITY_GATE_WITHER_ONLY:-0}" = 1 ]; then
  C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_wither_entity_c_$$}"
  mkdir -p "$C_OUT"
  for state in wither_normal wither_invul wither_armored; do
    "$OUT" --state "$state" --meta "$GOLDENS/meta/$state.json" \
      --ppm "$C_OUT/$state.ppm" --w 854 --h 480
  done
  "$OUT" --state wither_background \
    --meta "$GOLDENS/baselines/wither_background.json" \
    --ppm "$C_OUT/wither_background.ppm" --w 854 --h 480
  uv run --no-project --with pillow --with numpy \
    python "$DIR/measure_wither_subject.py" \
    --goldens "$GOLDENS" --c-frames "$C_OUT" --max-native-px 0 \
    --json-out "$C_OUT/wither_subject_report.json"
  echo "run_oracle_gate: Wither subject PASS"
  exit 0
fi

# Private fixture path (override with ENTITY_GATE_C_OUT). Never write shared
# /tmp/magma_ui_entities_c from review/corrective runs by default.
C_OUT="${ENTITY_GATE_C_OUT:-/tmp/magma_ui_entities_c_$$}"
mkdir -p "$C_OUT"

echo "== validate Java goldens (presence / A/B / inter-state) =="
uv run --no-project --with pillow --with numpy \
  python "$DIR/validate_ui_entities_goldens.py" \
  --goldens "$GOLDENS" \
  --json-out "$C_OUT/validate_report.json"

echo "== hard owned-pixel gate vs goldens (c-out=$C_OUT) =="
# Gate residual/CAPTURE_BLOCKED is nonzero exit; capture status without
# aborting under set -e so mutations still run (policy self-test first-class).
GATE_RC=0
uv run --no-project --with pillow --with numpy \
  python "$DIR/compare_ui_entities_oracle.py" \
  --goldens "$GOLDENS" \
  --candidate "$OUT" \
  --c-out "$C_OUT" \
  --json-out "$C_OUT/gate_report.json" || GATE_RC=$?

echo "== mutation self-tests (nonzero A/B blocked + synth zero-noise + holes) =="
MUT_RC=0
uv run --no-project --with pillow --with numpy \
  python "$DIR/test_ui_entities_mutations.py" \
  --goldens "$GOLDENS" \
  --c-frames "$C_OUT" || MUT_RC=$?

# Residual/CAPTURE_BLOCKED expected until renderer closes hard_px / recapture
# freezes A/B; mutations must still PASS.
if [ "$MUT_RC" -ne 0 ]; then
  echo "FAIL: mutation suite" >&2
  exit 1
fi
if [ "$GATE_RC" -ne 0 ]; then
  echo "run_oracle_gate: nonzero (FAIL/RESIDUAL/CAPTURE_BLOCKED — see $C_OUT/gate_report.json)"
  exit "$GATE_RC"
fi
echo "run_oracle_gate: all 16 hard PASS"
