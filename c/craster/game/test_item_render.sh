#!/usr/bin/env bash
# Standalone build+run for game/item_render.c verification. No Makefile edits.
# Run from anywhere; resolves the craster root relative to this script.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="/tmp/craster_test_item_render"

# regenerate the item atlas if missing (idempotent)
if [ ! -f assets/item_atlas.h ]; then
  uv run --no-project --with pillow python assets/build_item_atlas.py
fi

$CC $CFLAGS \
    game/test_item_render.c \
    game/item_render.c \
    assets/blockmodels.c \
    -lm -o "$OUT"

echo "built: $OUT"
"$OUT"
