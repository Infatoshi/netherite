#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"
make game/portal_live.o game/world_live.o game/caps.o core/config.o world/light.o world/mesh_mc.o \
	world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
	renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o
${CC:-gcc} -O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$BLAZE/core" \
	game/test_portal_live.c game/portal_live.o game/world_live.o game/caps.o core/config.o world/light.o \
	world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o world/world.o \
	renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o \
	-lm -o game/test_portal_live
./game/test_portal_live
