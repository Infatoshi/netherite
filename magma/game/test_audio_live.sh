#!/usr/bin/env bash
# Standalone build+run of the sound-seam consumer test. Needs NO audio device:
# the OpenAL path is compiled out unless MAGMA_AUDIO_OPENAL=1 is exported, and
# every assertion here is about the ring, not about playback.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"
AUDIO_CFLAGS=""
AUDIO_LIBS=""
if [ "${MAGMA_AUDIO_OPENAL:-0}" = "1" ]; then
	if ! pkg-config --exists openal vorbisfile 2>/dev/null; then
		echo "MAGMA_AUDIO_OPENAL=1 needs libopenal-dev libvorbis-dev" >&2
		exit 3
	fi
	AUDIO_CFLAGS="$(pkg-config --cflags openal vorbisfile) -DMAGMA_AUDIO_OPENAL"
	AUDIO_LIBS="$(pkg-config --libs openal vorbisfile)"
	make assets/sound_manifest.h >/dev/null
fi
make game/runtime.o game/world_spawn.o game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o core/config.o world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o >/dev/null
# shellcheck disable=SC2086
${CC:-gcc} -O2 -ffp-contract=off -Wall -Wextra -I. -Icore -I"$BLAZE/core" $AUDIO_CFLAGS \
	game/test_audio_live.c game/audio_live.c game/runtime.o game/world_spawn.o game/fluid_live.o game/config.o game/player_ctl.o game/sel_box.o game/world_live.o game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o game/container_live.o game/caps.o core/config.o world/light.o world/mesh_mc.o world/populate_mc.o world/blocks.o world/mesh.o world/world.o renderkernels/rk_31_facebakery_make_quad.o assets/blockmodels.o core/math.o core/shade.o $AUDIO_LIBS -lm -o game/test_audio_live
./game/test_audio_live
