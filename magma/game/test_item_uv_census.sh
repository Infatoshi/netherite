#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CC=${CC:-cc}
CFLAGS="-ffp-contract=off -Wall -Wextra -O2 -I. -Icore"
OUT="${TMPDIR:-/tmp}/magma_test_item_uv_census"

# CFLAGS is intentionally a conventional space-separated compiler flag string.
# shellcheck disable=SC2086
$CC $CFLAGS \
    game/test_item_uv_census.c \
    game/item_render.c \
    renderkernels/rk_31_facebakery_make_quad.c \
    assets/blockmodels.c \
    -lm -o "$OUT"

"$OUT"
