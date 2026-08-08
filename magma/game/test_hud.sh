#!/usr/bin/env bash
# Standalone build+run of the HUD self-consistency test (no Makefile involvement).
# Run from anywhere: bash game/test_hud.sh
set -euo pipefail
cd "$(dirname "$0")/.."
if [ ! -f assets/hud_atlas.h ] || [ assets/build_hud_atlas.py -nt assets/hud_atlas.h ]; then
    uv run --no-project --with pillow python assets/build_hud_atlas.py
fi
gcc -O2 -ffp-contract=off -Wall -Wextra -I. -Icore \
    game/hud.c game/item_render.c renderkernels/rk_31_facebakery_make_quad.c \
    assets/blockmodels.c game/test_hud.c \
    -o game/test_hud -lm
./game/test_hud

# Convert the PPM preview to PNG so the orchestrator can eyeball it.
if command -v pnmtopng >/dev/null 2>&1; then
    pnmtopng game/hud_preview.ppm > game/hud_preview.png
    echo "wrote game/hud_preview.png (pnmtopng)"
elif command -v uv >/dev/null 2>&1; then
    uv run --no-project --with pillow python3 - <<'PY'
from PIL import Image
Image.open("game/hud_preview.ppm").save("game/hud_preview.png")
print("wrote game/hud_preview.png (pillow)")
PY
else
    echo "no pnmtopng or uv; PNG not generated (PPM is at game/hud_preview.ppm)"
fi
