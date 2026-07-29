#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MCSIM="$(cd "$ROOT/../mc-sim" && pwd)"
cd "$ROOT"
make -s game/player_preview.o core/math.o core/shade.o cpu/raster_cpu.o
${CC:-gcc} -O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$MCSIM/core" \
	game/test_player_preview.c game/player_preview.o \
	core/math.o core/shade.o cpu/raster_cpu.o \
	-lm -o game/test_player_preview
./game/test_player_preview
