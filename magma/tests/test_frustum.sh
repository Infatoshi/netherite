#!/usr/bin/env bash
# test_frustum.sh - verify core/frustum.h two ways:
#  (A) BIT-MATCH the two "verified" render-opt frustum kernels (05 plane-extract,
#      06 aabb-test): feed each kernel's own deterministic inputs to BOTH the
#      render-opt reference candidate and our port; assert byte-identical output.
#      That transitively anchors our port to their MC goldens (Golden.java).
#  (B) NO-HOLES cull correctness: render the rung-4 ChunkScene with frustum culling
#      ON and with it fully OFF (registry no_cull=1, meshes every chunk in radius).
#      The two frames must be PIXEL-IDENTICAL: a chunk the conservative AABB test
#      culls is fully outside the frustum, so it contributes zero pixels; and the
#      test never culls a visible chunk (no false negatives). Any hole would show
#      as a differing pixel.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAGMA="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$MAGMA/.." && pwd)"
cd "$MAGMA"

BLAZE="$ROOT/blaze/core"
RO="$ROOT/java/render-opt/kernels"
FLAGS=(-O2 -ffp-contract=off -Wall -Icore -I. -I"$BLAZE")
T=/tmp/magma_frustum
mkdir -p "$T"

echo "== (A) bit-match render-opt kernels 05 + 06 =="
gcc "${FLAGS[@]}" tests/test_frustum.c -o "$T/mine" -lm
gcc "${FLAGS[@]}" "$RO/05_frustum_plane_extract/candidate.c" -o "$T/ref05" -lm
gcc "${FLAGS[@]}" "$RO/06_aabb_frustum_test/candidate.c"     -o "$T/ref06" -lm

uv run --no-project python "$RO/05_frustum_plane_extract/gen_inputs.py" > "$T/in05.txt"
uv run --no-project python "$RO/06_aabb_frustum_test/gen_inputs.py"     > "$T/in06.txt"

"$T/ref05" < "$T/in05.txt" > "$T/out05_ref.txt"
"$T/mine" --mode extract < "$T/in05.txt" > "$T/out05_mine.txt"
"$T/ref06" < "$T/in06.txt" > "$T/out06_ref.txt"
"$T/mine" --mode aabb    < "$T/in06.txt" > "$T/out06_mine.txt"

if ! diff -q "$T/out05_ref.txt" "$T/out05_mine.txt" >/dev/null; then
  echo "FAIL: frustum_plane_extract differs from render-opt kernel 05"; exit 1; fi
if ! diff -q "$T/out06_ref.txt" "$T/out06_mine.txt" >/dev/null; then
  echo "FAIL: aabb_frustum_test differs from render-opt kernel 06"; exit 1; fi
echo "  extract: $(wc -l < "$T/out05_mine.txt") records bit-identical"
echo "  aabb   : $(wc -l < "$T/out06_mine.txt") records bit-identical, $(grep -c '^1' "$T/out06_mine.txt") inside"

echo "== (B) no-holes: culled render == cull-off render (pixel-identical) =="
# Reuse the rung-4 candidate (it meshes the ChunkScene = view-distance + culling).
# core/config.o is required: pose_scene + shade + mesh_mc + light read the registry.
for u in world/mesh_mc world/light world/populate_mc assets/blockmodels \
         renderkernels/rk_31_facebakery_make_quad game/sky game/caps \
         game/village_live core/math core/shade core/config cpu/raster_cpu transform; do
  gcc "${FLAGS[@]}" -c "$u.c" -o "$u.o"
done
gcc "${FLAGS[@]}" ../verify/mc_capture/rung4_candidate.c \
    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
    renderkernels/rk_31_facebakery_make_quad.o game/sky.o game/caps.o \
    game/village_live.o core/math.o core/shade.o core/config.o \
    cpu/raster_cpu.o transform.o \
    -o "$T/rung4" -lm

echo "  -- culling ON --"
"$T/rung4" "$T/cull_on.ppm"  | sed -n 's/^view-distance/  &/p'
echo "  -- culling OFF (--set no_cull=1) --"
"$T/rung4" --set no_cull=1 "$T/cull_off.ppm" | sed -n 's/^view-distance/  &/p'

if ! cmp -s "$T/cull_on.ppm" "$T/cull_off.ppm"; then
  echo "FAIL: culled frame differs from cull-off frame (a visible chunk was dropped)"
  cmp "$T/cull_on.ppm" "$T/cull_off.ppm" || true
  exit 1
fi
echo "  culled frame is pixel-identical to the cull-off frame (no holes)"

echo "TEST_FRUSTUM PASS"
