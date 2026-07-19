#!/usr/bin/env bash
# build_c_tracer.sh - build the C-side trace runner (app/trace_main.c -> ./trace_game).
#
# Does NOT edit the craster Makefile. It first runs `make game` to build every module
# object (and craster_game) with the project's own rules, then compiles trace_main.o and
# links it against the SAME object set as the `craster_game` target (mirrored from the
# Makefile's craster_game recipe). Re-run any time; it is idempotent.
set -euo pipefail

CRASTER=/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/craster
MCSIM=/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/mc-sim
CC=${CC:-gcc}
CFLAGS="-O2 -ffp-contract=off -Wall -Wextra -Icore -I. -I${MCSIM}/core"
SDL_CFLAGS="$(pkg-config --cflags sdl2 2>/dev/null || true)"
SDL_LIBS="$(pkg-config --libs sdl2 2>/dev/null || echo -lSDL2)"
LDLIBS="-lm"

cd "$CRASTER"

# 1. build all module objects + craster_game via the project's own rules.
echo "[build] make game ..."
make game

# 2. object set == craster_game recipe (Makefile lines 45-46), world/*.o globbed.
# Mirror the Makefile OBJ_GAME (kept in sync): includes game/sky.o + game/caps.o,
# which world_live.c / light.c / populate_mc.c now derive their pools/config from.
OBJ_GAME="game/input_map.o game/world_live.o game/player_ctl.o game/hud.o game/item_render.o game/entity_render.o game/sky.o game/caps.o"
OBJ_WORLD="$(ls world/*.o)"
OBJ_RK="renderkernels/rk_31_facebakery_make_quad.o"
OBJ_ASSETS="assets/blockmodels.o"
OBJ_CORE="core/math.o core/shade.o"
OBJ_RASTER="cpu/raster_cpu.o"
OBJ_XFORM="transform.o"
OBJ_PRESENT="present/present.o"

# 3. compile trace_main.o (same flags as app/game_main.o rule + mc-sim include).
echo "[build] cc app/trace_main.c ..."
$CC $CFLAGS $SDL_CFLAGS -c app/trace_main.c -o app/trace_main.o

# 4. link ./trace_game (present.o + SDL kept only to match the craster_game link line;
#    the tracer opens no window).
echo "[build] link trace_game ..."
$CC $CFLAGS $SDL_CFLAGS \
    app/trace_main.o $OBJ_GAME $OBJ_WORLD $OBJ_RK $OBJ_ASSETS \
    $OBJ_CORE $OBJ_RASTER $OBJ_XFORM $OBJ_PRESENT \
    -o trace_game $SDL_LIBS $LDLIBS

echo "[build] OK -> $CRASTER/trace_game"
