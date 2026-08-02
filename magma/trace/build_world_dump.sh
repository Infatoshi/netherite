#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"

make game >/dev/null

CFLAGS=(-O2 -ffp-contract=off -Wall -Wextra -Icore -I. "-I$BLAZE/core")
read -r -a SDL_CFLAGS <<<"$(pkg-config --cflags sdl2 2>/dev/null || true)"
read -r -a SDL_LIBS <<<"$(pkg-config --libs sdl2 2>/dev/null || printf '%s' '-lSDL2')"

cc "${CFLAGS[@]}" "${SDL_CFLAGS[@]}" -c trace/world_dump.c -o trace/world_dump.o
cc "${CFLAGS[@]}" "${SDL_CFLAGS[@]}" \
	trace/world_dump.o game/input_map.o game/world_live.o game/player_ctl.o \
	game/hud.o game/entity_render.o game/item_render.o game/sky.o game/caps.o core/config.o game/sel_box.o \
	world/*.o renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o \
	core/math.o core/shade.o cpu/raster_cpu.o transform.o present/present.o \
	-o trace/world_dump "${SDL_LIBS[@]}" -lm
