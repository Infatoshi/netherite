#!/usr/bin/env bash
# Focused numerical gates for owned HUD / hand / overlay / underwater modules.
# Does not touch shared mc_capture or tape scripts.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"  # -> c/magma
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="/tmp/magma_test_ui_hud_numerical"
DIR="raster/verify/ui_hud"

if [ ! -f assets/item_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_item_atlas.py
fi
if [ ! -f assets/hand_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_hand_atlas.py
fi
if [ ! -f assets/hud_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_hud_atlas.py
fi
if [ ! -f assets/loading_bg.h ]; then
  uv run --no-project --with pillow python assets/build_loading_bg.py
fi

echo "== build $DIR/test_ui_hud_numerical =="
# underwater.c is formula-gated in the C test without linking the world stack;
# overlay loading font stubs live in the test when not needed via hud.
$CC $CFLAGS \
    "$DIR/test_ui_hud_numerical.c" \
    game/hud.c game/hand.c game/overlay.c \
    game/item_render.c \
    assets/blockmodels.c \
    transform.c \
    core/math.c core/shade.c \
    cpu/raster_cpu.c \
    -lm -o "$OUT"

echo "== run =="
"$OUT"
echo "ui_hud gates: PASS"
