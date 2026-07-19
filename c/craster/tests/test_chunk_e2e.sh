#!/usr/bin/env bash
# Rung-3 end-to-end golden: real generated 3x3-chunk scene rendered by our
# transform+raster (candidate) vs OpenGL/OSMesa (golden) from identical geometry,
# atlas, camera matrices, colour folding, draw order, viewport and depth func.
# Only the triangle->pixel step differs; the diff should sit at the fill-rule
# subpixel noise floor (a few dozen silhouette/seam pixels, interior mean ~0).
set -euo pipefail
cd "$(dirname "$0")/.."

MCSIM="$(cd "$(dirname "$0")/../../mc-sim/core" && pwd)"
FLAGS=(-O2 -ffp-contract=off -Wall -Icore -I. -I"$MCSIM")
DIFF="$(cd "$(dirname "$0")/../../render-opt/wholeframe" && pwd)/diff_frame.py"

echo "== build objects =="
for u in world/mesh_mc world/light world/populate_mc assets/blockmodels \
         renderkernels/rk_31_facebakery_make_quad \
         core/math core/shade cpu/raster_cpu transform; do
  gcc "${FLAGS[@]}" -c "$u.c" -o "$u.o"
done

echo "== build golden + candidate =="
gcc "${FLAGS[@]}" raster/verify/chunk_golden.c \
    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
    renderkernels/rk_31_facebakery_make_quad.o core/math.o \
    -o /tmp/chunk_golden -lOSMesa -lGL -lm
gcc "${FLAGS[@]}" raster/verify/chunk_candidate.c \
    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
    renderkernels/rk_31_facebakery_make_quad.o core/math.o \
    core/shade.o cpu/raster_cpu.o transform.o \
    -o /tmp/chunk_candidate -lm

echo "== render =="
/tmp/chunk_golden    /tmp/chunk_golden.ppm
/tmp/chunk_candidate /tmp/chunk_candidate.ppm

echo "== diff =="
uv run --no-project --with numpy --with pillow python "$DIFF" \
    /tmp/chunk_golden.ppm /tmp/chunk_candidate.ppm --crop none --out /tmp/chunk_diff
