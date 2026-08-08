#!/usr/bin/env bash
# Rung-3 end-to-end golden: real generated 3x3-chunk scene rendered by our
# transform+raster (candidate) vs OpenGL/OSMesa (golden) from identical geometry,
# atlas, camera matrices, colour folding, draw order, viewport and depth func.
# Only the triangle->pixel step differs; the diff should sit at the fill-rule
# subpixel noise floor (a few dozen silhouette/seam pixels, interior mean ~0).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAGMA="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$MAGMA/.." && pwd)"
cd "$MAGMA"

BLAZE="$ROOT/blaze/core"
FLAGS=(-O2 -ffp-contract=off -Wall -Icore -I. -I"$BLAZE")
DIFF="$ROOT/java/render-opt/wholeframe/diff_frame.py"

echo "== build objects =="
for u in world/mesh_mc world/light world/populate_mc assets/blockmodels \
         renderkernels/rk_31_facebakery_make_quad \
         game/village_live game/caps core/math core/shade core/config \
         cpu/raster_cpu transform; do
  gcc "${FLAGS[@]}" -c "$u.c" -o "$u.o"
done

echo "== build golden + candidate =="
gcc "${FLAGS[@]}" ../verify/chunk_golden.c \
    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
    renderkernels/rk_31_facebakery_make_quad.o game/village_live.o game/caps.o \
    core/math.o core/config.o \
    -o /tmp/chunk_golden -lOSMesa -lGL -lm
gcc "${FLAGS[@]}" ../verify/chunk_candidate.c \
    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
    renderkernels/rk_31_facebakery_make_quad.o game/village_live.o game/caps.o \
    core/math.o core/config.o core/shade.o cpu/raster_cpu.o transform.o \
    -o /tmp/chunk_candidate -lm

echo "== render =="
/tmp/chunk_golden    /tmp/chunk_golden.ppm
/tmp/chunk_candidate /tmp/chunk_candidate.ppm

echo "== diff =="
uv run --no-project --with numpy --with pillow python "$DIFF" \
    /tmp/chunk_golden.ppm /tmp/chunk_candidate.ppm --crop none --out /tmp/chunk_diff
