#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"
make -s game/player_preview.o game/entity_render.o game/hud.o \
	game/item_render.o renderkernels/rk_31_facebakery_make_quad.o \
	assets/blockmodels.o core/config.o core/math.o core/shade.o cpu/raster_cpu.o
${CC:-gcc} -O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$BLAZE/core" \
	game/test_player_preview.c game/player_preview.o game/entity_render.o \
	game/hud.o game/item_render.o renderkernels/rk_31_facebakery_make_quad.o \
	assets/blockmodels.o core/config.o core/math.o core/shade.o cpu/raster_cpu.o \
	-lm -o game/test_player_preview
./game/test_player_preview
