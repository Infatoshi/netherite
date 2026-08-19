#!/usr/bin/env bash
# Canonical source/object closure for magma standalone game tests.
#
# Source from a game/test_*.sh after cd'ing to the magma root:
#   # shellcheck source=game/standalone_test_common.sh
#   source "$(dirname "$0")/standalone_test_common.sh"
#
# Every translation unit that may call cr_cfg() must link core/config.c (or
# core/config.o). The world mesher stack always does (light, mesh_mc, populate,
# shade, caps, world_live); render tests that pull core/shade.c do too.
#
# Do not hand-list these arrays in individual scripts; extend this file.
# Arrays in this sourced library are intentionally consumed by different callers.
# shellcheck disable=SC2034

# Resolve roots relative to this file (magma/game/standalone_test_common.sh).
_MAGMA_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAGMA_ROOT="$(cd "${_MAGMA_COMMON_DIR}/.." && pwd)"
BLAZE_ROOT="$(cd "${MAGMA_ROOT}/../blaze" && pwd)"
REPO_ROOT="$(cd "${MAGMA_ROOT}/.." && pwd)"


CC="${CC:-gcc}"
# Standalone scripts default to the same float discipline as the Makefile.
MAGMA_STANDALONE_CFLAGS="${MAGMA_STANDALONE_CFLAGS:--O2 -ffp-contract=off -Wall -Wextra}"
# -I.. so verify/chunk_scene.h (and other repo-root verify headers) resolve.
MAGMA_STANDALONE_INCLUDES="-I. -Icore -I${BLAZE_ROOT}/core -I${REPO_ROOT}"

# World mesher + caps + config registry. Required for anything that pulls
# world/light.c, world/mesh_mc.c, world/populate_mc.c, game/world_live.c, or
# game/caps.c. Order is not load-bearing for gcc, but keep config next to shade.
MAGMA_WORLD_MESH_SRCS=(
  world/mesh_mc.c
  world/light.c
  world/populate_mc.c
  assets/blockmodels.c
  renderkernels/rk_31_facebakery_make_quad.c
  core/math.c
  core/shade.c
  core/config.c
  game/caps.c
)

MAGMA_WORLD_MESH_OBJS=(
  world/mesh_mc.o
  world/light.o
  world/populate_mc.o
  assets/blockmodels.o
  renderkernels/rk_31_facebakery_make_quad.o
  core/math.o
  core/shade.o
  core/config.o
  game/caps.o
)

# Live world stack for make-based scripts: world_live + mesh closure + legacy
# world/*.o still pulled by some modules. Always includes core/config.o.
MAGMA_WORLD_LIVE_OBJS=(
  game/world_live.o
  "${MAGMA_WORLD_MESH_OBJS[@]}"
  world/blocks.o
  world/mesh.o
  world/world.o
)

# Minimal core for unit tests that only need math/shade (entity, hand, preview).
# Always include config.c: shade.c calls cr_cfg().
MAGMA_RENDER_CORE_SRCS=(
  core/math.c
  core/shade.c
  core/config.c
)

MAGMA_RENDER_CORE_OBJS=(
  core/math.o
  core/shade.o
  core/config.o
)

# Compile and link a standalone binary from source files (relative to MAGMA_ROOT).
# Usage: magma_standalone_build OUT src1.c src2.c ...
magma_standalone_build() {
  local out="$1"; shift
  # CFLAGS is a conventional space-separated flag string.
  # shellcheck disable=SC2086
  $CC $MAGMA_STANDALONE_CFLAGS $MAGMA_STANDALONE_INCLUDES "$@" -o "$out" -lm
}

# Ensure generated atlas headers the world/render stack needs exist.
# Scripts may still regenerate specialized atlases (mob/item/hud) themselves.
magma_standalone_require_block_assets() {
  local missing=0
  for h in assets/atlas_gen.h assets/colormap_gen.h assets/water_frames.h assets/portal_tex.h; do
    if [ ! -f "$h" ]; then
      echo "missing $h (run assets/build_*.py or bootstrap)" >&2
      missing=1
    fi
  done
  return "$missing"
}
