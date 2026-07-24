#!/usr/bin/env bash
# Focused gates for owned HUD / hand / overlay / underwater modules.
# Numerical formula checks + end-to-end frame composition. Does not touch
# shared mc_capture or tape scripts. Pixel parity vs Java requires goldens
# listed in ORACLE_CAPTURE.md (do not fabricate).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"  # -> c/magma
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
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

echo "ui_hud gates: PASS"
