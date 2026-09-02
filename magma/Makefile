# magma build. See SPEC.md. C reference + CUDA raster, SDL2 present.
# Parallel by default (clean serial build ~12s, -j ~3s). An explicit -j on the
# command line still wins over this.
MAKEFLAGS += -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
CC      ?= gcc
CUDA_HOME ?= /usr/local/cuda
NVCC    ?= $(CUDA_HOME)/bin/nvcc
CFLAGS  ?= -O2 -ffp-contract=off -Wall -Wextra -Icore -I.
NVFLAGS ?= -O2 --fmad=false -arch=sm_120 -Icore -I.
# SDL2 discovery: pkg-config, then sdl2-config (Homebrew path included for
# non-interactive mac shells that lack /opt/homebrew/bin in PATH), then -lSDL2.
SDL2_CONFIG := $(shell command -v sdl2-config 2>/dev/null || \
                       ls /opt/homebrew/bin/sdl2-config 2>/dev/null)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || \
                      $(SDL2_CONFIG) --cflags 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || \
                      $(SDL2_CONFIG) --libs 2>/dev/null || echo -lSDL2)
# present.c includes <SDL2/SDL.h>; sdl2-config points at .../include/SDL2, so
# also search its parent (no-op on Linux where /usr/include is a default path).
SDL_CFLAGS += $(patsubst -I%/SDL2,-I%,$(filter -I%/SDL2,$(SDL_CFLAGS)))
# Interactive audio (OpenAL + libvorbisfile), OPT-IN:  make MAGMA_AUDIO_OPENAL=1
# Off by default on purpose. The default build must not need audio dev headers,
# and auto-detecting them would make the binary's behaviour depend on what
# happens to be installed on the box - the same class of hidden input the rest
# of this build refuses. With the flag off game/audio_live.c compiles to
# do-nothing stubs, so the simulation is bit-identical either way.
ifeq ($(MAGMA_AUDIO_OPENAL),1)
AUDIO_PC := $(shell pkg-config --exists openal vorbisfile 2>/dev/null && echo 1)
ifneq ($(AUDIO_PC),1)
$(error MAGMA_AUDIO_OPENAL=1 requires the openal and vorbisfile pkg-config \
modules (Debian/Ubuntu: apt install libopenal-dev libvorbis-dev))
endif
AUDIO_CFLAGS := $(shell pkg-config --cflags openal vorbisfile) -DMAGMA_AUDIO_OPENAL
AUDIO_LIBS   := $(shell pkg-config --libs openal vorbisfile)
endif
LDLIBS  ?= -lm
DEPFLAGS = -MMD -MP

# module objects (each owned by one agent per SPEC.md)
OBJ_CORE   = core/math.o core/shade.o core/config.o
OBJ_RASTER = cpu/raster_cpu.o
OBJ_XFORM  = transform.o
OBJ_PRESENT= present/present.o

# wave 2: world mesh feed (consumes blaze core worldgen headers)
BLAZE     ?= $(abspath ../blaze)
OBJ_WORLD  = $(patsubst %.c,%.o,$(wildcard world/*.c))
# wave 6: facebakery kernel that bakes non-cube block models (world/mesh_mc.c)
OBJ_RK     = renderkernels/rk_31_facebakery_make_quad.o
# block-model / atlas table (world/mesh_mc.c consumes it)
OBJ_ASSETS = assets/blockmodels.o

# seam: playable game (verified blaze player/world tick + magma render + HUD + mobs)
OBJ_GAME = game/config.o game/input_map.o game/world_live.o game/player_ctl.o game/hud.o game/screen.o game/player_preview.o game/entity_render.o game/particles_live.o game/item_render.o \
           game/sky.o game/caps.o game/hand.o game/timer.o game/live_sim.o game/randtick.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o game/container_live.o game/runtime.o game/world_spawn.o game/frame_capture.o game/window_compose.o game/script.o game/rl_mode.o game/overlay.o game/overlay_live.o game/sel_box.o game/fluid_live.o game/underwater.o game/audio_live.o

.PHONY: all world game game-cuda game-metal test test-raster-smoke test-world-spawn test-fall-reanchor test-sky-weather test-game test-launch test-config test-block-registry test-particles-live test-audio-live test-block-break-audio test-block-audio-oracle clean cuda test-raster-parity test-fog-radial assets test-asset-build test-asset-ui test-beds test-xp-orbs test-potions
all: game

# --- CUDA raster for the game ---
# Auto-detect arch from nvidia-smi (anvil: 2x Blackwell sm_120; gamer: 3090
# sm_86). Override with NVFLAGS_GAME=... or GAME_SM=sm_86 when needed. The
# object is arch-neutral by name - after switching GPUs `rm cuda/*.o` or the
# recipe rebuilds only when sources change (see comment on game-cuda below).
GAME_SM ?= $(shell nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | \
             head -1 | grep -qi "3090" && echo sm_86 || echo sm_120)
NVFLAGS_GAME ?= -O2 --fmad=false -arch=$(GAME_SM) -Icore -I.
CUDA_LIBDIR  ?= $(patsubst %/bin/nvcc,%/lib64,$(shell which nvcc))
CUDA_LIBS    ?= -L$(CUDA_LIBDIR) -lcudart -lstdc++

# CPU raster integration smoke test (spinning textured cube)
tests/test_raster_smoke: tests/test_raster_smoke.o tests/raster_smoke_scene.o $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS)

test-raster-smoke: tests/test_raster_smoke
	SDL_VIDEODRIVER=dummy ./tests/test_raster_smoke --frames 1

# playable world app
# NB: the world objects (world/light.o, world/populate_mc.o) now derive their fixed
# pools from game/caps.o, so it is linked into every binary that pulls them in.
world: magma_world
magma_world: app/main.o $(OBJ_WORLD) $(OBJ_RK) $(OBJ_ASSETS) $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT) game/caps.o
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS)

world/%.o: world/%.c core/types.h
	$(CC) $(CFLAGS) $(DEPFLAGS) -I$(BLAZE)/core -c $< -o $@

# playable game binary: seam-wires the verified sim tick to the magma renderer.
# The CPU binary references cr_raster_cuda_* only as weak symbols (frame_capture,
# game_main check them for NULL). ELF resolves absent weak refs to NULL; Mach-O's
# ld needs them explicitly allowed, so Darwin gets -Wl,-U per symbol.
ifeq ($(shell uname -s),Darwin)
CUDA_WEAK_LD = -Wl,-U,_cr_raster_cuda_pre -Wl,-U,_cr_raster_cuda_into \
               -Wl,-U,_cr_raster_cuda_frame_begin -Wl,-U,_cr_raster_cuda_frame_end \
               -Wl,-U,_cr_raster_cuda_post -Wl,-U,_cr_raster_cuda_sky \
               -Wl,-U,_cr_raster_cuda_atlas_dirty \
               -Wl,-U,_cr_raster_cuda_pin -Wl,-U,_cr_raster_cuda_unpin \
               -Wl,-U,_cr_raster_cuda_uploads_mark -Wl,-U,_cr_raster_cuda_uploads_wait \
               -Wl,-U,_cr_raster_cuda_frame_end_async -Wl,-U,_cr_raster_cuda_frame_wait \
               -Wl,-U,_cr_raster_cuda_render_layer -Wl,-U,_cr_raster_cuda_screen_tris \
               -Wl,-U,_cr_raster_cuda_render_gather \
               -Wl,-U,_cr_raster_cuda_render_terrain \
               -Wl,-U,_cr_raster_cuda_slab_pool -Wl,-U,_cr_raster_cuda_slab_sync \
               -Wl,-U,_cr_raster_cuda_slabs_reset \
               -Wl,-U,_alloctrack_frame -Wl,-U,_alloctrack_arm
endif
game: magma_game
magma_game: app/game_main.o $(OBJ_GAME) $(OBJ_WORLD) $(OBJ_RK) $(OBJ_ASSETS) \
              $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(AUDIO_LIBS) $(LDLIBS) $(CUDA_WEAK_LD)

# game/ modules include game/game.h + (world_live/player_ctl) player_survival.h.
game/%.o: game/%.c core/types.h game/game.h
	$(CC) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -I$(BLAZE)/core -c $< -o $@
game/randtick.o: ../blaze/core/randtick_live.h
game/live_sim.o: ../blaze/core/falling_live.h ../blaze/core/item_live.h \
                 ../blaze/core/item_overflow.h ../blaze/core/entity_item.h
game/world_live.o: ../blaze/core/world_weather.h
game/runtime.o: ../blaze/core/projectile_live.h ../blaze/core/explosion_live.h ../blaze/core/explosion.h
game/mob_live.o: ../blaze/core/explosion_live.h ../blaze/core/hostile_live.h

# Generated texture headers come from the native asset binary.
# MC_JAR is a Make variable (bootstrap pointer). The binary takes --jar.
# Search order matches the old mc_jar.py candidates, minus getenv in C.
MC_JAR ?= $(firstword $(wildcard \
	../java/Minecraft/run/gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar \
	$(HOME)/.gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar \
	$(HOME)/.minecraft/versions/1.11.2/1.11.2.jar \
	$(HOME)/Library/Application Support/minecraft/versions/1.11.2/1.11.2.jar \
	$(HOME)/Library/Application Support/PrismLauncher/libraries/com/mojang/minecraft/1.11.2/minecraft-1.11.2-client.jar \
	$(HOME)/.local/share/PrismLauncher/libraries/com/mojang/minecraft/1.11.2/minecraft-1.11.2-client.jar))
MC_ASSET_INDEX ?= $(firstword $(wildcard \
	../java/Minecraft/run/gradle/caches/minecraft/assets/indexes/1.11.json \
	../java/Minecraft/.gradle/minecraft/assets/indexes/1.11.json))
ASSET_BIN = ../out/magma/assets
SOUND_BIN = ../out/magma/sound
ASSET_SRC = assets/jar.c assets/image.c assets/build_world.c assets/build_ui.c
ASSET_TIMEOUT ?= 30
ASSET_RUN = perl -e 'alarm shift; exec @ARGV' $(ASSET_TIMEOUT)

$(ASSET_BIN): assets/build.c $(ASSET_SRC) assets/build.h assets/jar.h assets/image.h
	mkdir -p ../out/magma
	$(CC) $(CFLAGS) -std=c11 -o $@ assets/build.c $(ASSET_SRC) -lz

$(SOUND_BIN): assets/build_sound.c
	mkdir -p ../out/magma
	$(CC) $(CFLAGS) -std=c11 -o $@ assets/build_sound.c

.PHONY: assets
assets: $(ASSET_BIN) $(SOUND_BIN)
	@test -n "$(MC_JAR)" || { echo "minecraft-1.11.2.jar not found; set MC_JAR="; exit 1; }
	@test -n "$(MC_ASSET_INDEX)" || { echo "1.11 asset index not found; set MC_ASSET_INDEX="; exit 1; }
	$(ASSET_RUN) $(ASSET_BIN) --jar "$(MC_JAR)" --out assets
	$(ASSET_RUN) $(SOUND_BIN) --index "$(MC_ASSET_INDEX)" --out assets/sound_manifest.h

assets/hud_atlas.h: $(ASSET_BIN)
	@test -n "$(MC_JAR)" || { echo "minecraft-1.11.2.jar not found; set MC_JAR="; exit 1; }
	$(ASSET_RUN) $(ASSET_BIN) --jar "$(MC_JAR)" --out assets --only hud
game/hud.o: assets/hud_atlas.h
# Sound-seam unit gates (standalone builders, same shape as test_runtime.sh).
test-audio-live:
	bash game/test_audio_live.sh

test-block-break-audio:
	bash game/test_block_break_audio.sh

# magma-vs-real-Java block sound map. Needs the Java oracle (gradle), so it is
# not part of `make test-game`; the gate exits 3 BLOCKED when it is unavailable.
test-block-audio-oracle:
	bash game/test_block_break_audio.sh --oracle

# Sound hash manifest: owned 1.11 asset index. No jar. No env.
# Only game/audio_live.o consumes it, and only the MAGMA_AUDIO_OPENAL=1 build
# dereferences the hashes, but generate it either way so the opt-in build is
# one flag and not a flag plus a remembered bootstrap step.
assets/sound_manifest.h: $(SOUND_BIN)
	@test -n "$(MC_ASSET_INDEX)" || { echo "1.11 asset index not found; set MC_ASSET_INDEX="; exit 1; }
	$(ASSET_RUN) $(SOUND_BIN) --index "$(MC_ASSET_INDEX)" --out assets/sound_manifest.h
game/audio_live.o: game/audio_live.c game/audio_live.h assets/sound_manifest.h
	$(CC) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) $(AUDIO_CFLAGS) -I$(BLAZE)/core -c $< -o $@

game/runtime.o: game/runtime.c game/runtime.h game/mob_live.h game/dragon_live.h \
                game/portal_live.h game/container_live.h game/world_spawn.h \
                $(BLAZE)/core/crafting_recipes_full.h
game/world_spawn.o: game/world_spawn.c game/world_spawn.h
game/container_live.o: game/container_live.c game/container_live.h game/runtime.h \
                game/chest_live.h \
                $(BLAZE)/core/container_click.h $(BLAZE)/core/crafting_recipes_full.h \
                $(BLAZE)/core/smelting_recipes.h
game/chest_live.o: game/chest_live.c game/chest_live.h \
                $(BLAZE)/core/tile_entity_chest.h $(BLAZE)/core/stronghold_loot.h

# CUDA-accelerated game binary: same sources as magma_game but game_main.c is
# compiled with -DMAGMA_CUDA (adds the --cuda / MAGMA_CUDA=1 switch that
# routes render_layer through the alloc-once cr_raster_cuda_into) and links the
# raster object + cudart. `make game` (pure CPU) is unaffected.
#
# The object name is arch-NEUTRAL on purpose. It used to be raster_cuda_sm86.o
# while NVFLAGS_GAME is overridable (GPU0 needs -arch=sm_120), so the same file
# held whichever arch was built last and the name lied. Nothing rebuilds it on
# an NVFLAGS_GAME change either - the recipe depends on sources only - so after
# switching GPUs you must `rm cuda/*.o` or you silently link the wrong arch.
game-cuda: magma_game_cuda
app/game_main_cuda.o: app/game_main.c game/game.h game/runtime.h game/config.h core/config.h core/types.h game/caps.h game/particles_live.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DMAGMA_CUDA -I$(BLAZE)/core -c $< -o $@
cuda/raster_cuda_game.o: cuda/raster_cuda.cu core/types.h core/shade.c
	$(NVCC) $(NVFLAGS_GAME) -c $< -o $@
magma_game_cuda: app/game_main_cuda.o cuda/raster_cuda_game.o $(OBJ_GAME) $(OBJ_WORLD) \
              $(OBJ_RK) $(OBJ_ASSETS) $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(AUDIO_LIBS) $(LDLIBS) $(CUDA_LIBS)

# Metal-accelerated game binary (macOS / Apple silicon only). Same seam shape
# as game-cuda: game_main.c AND frame_capture.c recompile with -DMAGMA_METAL
# (adds --backend metal and maps the frame-capture GPU calls onto the
# cr_raster_metal_* mirrors), linked against the ObjC Metal host
# (metal/raster_metal_host.m) plus the MSL kernel library
# metal/raster_kernels.metallib (built via xcrun metal/metallib; loaded at
# runtime by the host). Determinism: C objects keep -ffp-contract=off via
# CFLAGS; the MSL compile pins -fno-fast-math -ffp-contract=off (float discipline is documented
# with the kernels). SDL2 comes from the sdl2-config/pkg-config discovery at
# the top of this file (Homebrew path included).
METALC   ?= xcrun -sdk macosx metal
METALLIB ?= xcrun -sdk macosx metallib
METAL_FRAMEWORKS = -framework Metal -framework Foundation -framework QuartzCore
# Mach-O ld does not resolve absent weak refs to NULL on its own (see
# CUDA_WEAK_LD above): allow every cr_raster_metal_* weak ref from
# game/frame_capture_metal.o to stay undefined. Defined symbols make the
# corresponding -U a no-op, so a full host implementation is unaffected.
METAL_WEAK_LD = $(foreach s,pre into frame_begin frame_end post atlas_dirty \
                  pin unpin render_layer sky frame_end_async frame_wait \
                  slab_pool slab_sync slabs_reset render_gather render_terrain \
                  uploads_mark uploads_wait,-Wl,-U,_cr_raster_metal_$(s)) \
                $(foreach s,pre into frame_begin frame_end sky post atlas_dirty \
                  render_layer screen_tris render_gather \
                  slab_pool slab_sync slabs_reset,-Wl,-U,_cr_raster_cuda_$(s)) \
                -Wl,-U,_alloctrack_frame -Wl,-U,_alloctrack_arm
OBJ_GAME_METAL = $(patsubst game/window_compose.o,game/window_compose_metal.o,\
                 $(patsubst game/frame_capture.o,game/frame_capture_metal.o,$(OBJ_GAME)))
# Fail fast off-macOS: everything below needs xcrun + the Metal frameworks.
# The guard runs before any sub-make, so a Linux `make game-metal` dies with
# one clear message instead of a wall of clang/xcrun errors.
game-metal:
	@if [ "$$(uname -s)" != "Darwin" ]; then \
	  echo "error: game-metal builds only on macOS (needs xcrun + Metal frameworks); this host is $$(uname -s)" >&2; \
	  exit 1; \
	fi
	$(MAKE) magma_game_metal
app/game_main_metal.o: app/game_main.c game/game.h game/runtime.h game/config.h core/config.h core/types.h game/caps.h game/particles_live.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DMAGMA_METAL -I$(BLAZE)/core -c $< -o $@
game/frame_capture_metal.o: game/frame_capture.c game/frame_capture.h game/config.h core/types.h game/game.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DMAGMA_METAL -I$(BLAZE)/core -c $< -o $@
game/window_compose_metal.o: game/window_compose.c game/window_compose.h game/config.h core/types.h game/game.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DMAGMA_METAL -I$(BLAZE)/core -c $< -o $@
metal/raster_metal_host.o: metal/raster_metal_host.m core/types.h
	$(CC) -x objective-c -fobjc-arc $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@
metal/raster_kernels.air: metal/raster_kernels.metal core/types.h
	$(METALC) -fno-fast-math -ffp-contract=off -Icore -I. -o $@ -c $<
metal/raster_kernels.metallib: metal/raster_kernels.air
	$(METALLIB) -o $@ $<
magma_game_metal: app/game_main_metal.o metal/raster_metal_host.o $(OBJ_GAME_METAL) \
              $(OBJ_WORLD) $(OBJ_RK) $(OBJ_ASSETS) $(OBJ_CORE) $(OBJ_RASTER) \
              $(OBJ_XFORM) $(OBJ_PRESENT) metal/raster_kernels.metallib
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(filter %.o,$^) -o $@ $(SDL_LIBS) $(AUDIO_LIBS) $(LDLIBS) $(METAL_FRAMEWORKS) $(METAL_WEAK_LD)

# ALLOCATE-ONCE alloc-tripwire build: magma_game with debug symbols (so the
# trace/alloctrack.so summary offsets resolve via addr2line -e magma_game_dbg).
# Same sources as magma_game, one -g compile. See trace/alloctrack.c + caps.h.
# Same game sources as OBJ_GAME (plus app/game_main + world/assets/core/present).
GAME_DBG_SRCS = app/game_main.c \
                game/config.c game/input_map.c game/world_live.c game/player_ctl.c game/hud.c \
                game/screen.c game/player_preview.c \
                game/entity_render.c game/particles_live.c game/item_render.c game/sky.c game/caps.c game/hand.c game/timer.c game/live_sim.c game/randtick.c game/mob_live.c game/dragon_live.c game/structures_live.c game/portal_live.c game/furnace_live.c game/chest_live.c game/container_live.c game/runtime.c game/world_spawn.c game/frame_capture.c game/window_compose.c game/script.c game/rl_mode.c \
                game/overlay.c game/overlay_live.c game/sel_box.c game/fluid_live.c game/underwater.c \
                world/mesh_mc.c world/light.c world/populate_mc.c \
                world/world.c world/mesh.c world/blocks.c \
                renderkernels/rk_31_facebakery_make_quad.c assets/blockmodels.c \
                core/math.c core/shade.c core/config.c cpu/raster_cpu.c transform.c present/present.c
.PHONY: game-dbg
game-dbg: magma_game_dbg
magma_game_dbg: $(GAME_DBG_SRCS)
	$(CC) $(CFLAGS) -g $(SDL_CFLAGS) -I$(BLAZE)/core $(GAME_DBG_SRCS) -o $@ $(SDL_LIBS) $(LDLIBS) $(CUDA_WEAK_LD)

core/config.o: core/config.c core/config.h core/config.def game/caps.h
game/caps.o: game/caps.c game/caps.h core/config.h

app/game_main.o: app/game_main.c game/game.h game/runtime.h game/config.h core/config.h core/types.h game/particles_live.h
	$(CC) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -I$(BLAZE)/core -c $< -o $@

# non-cube mesh unit test: build a synthetic chunk, assert facebakery geometry
.PHONY: test-mesh
test-mesh: $(OBJ_WORLD) $(OBJ_RK) core/math.o core/shade.o core/config.o game/caps.o
	$(CC) $(CFLAGS) -I$(BLAZE)/core tests/test_mesh_models.c \
	    world/mesh_mc.o world/light.o world/populate_mc.o world/gen_prefetch.o \
	    assets/blockmodels.o \
	    $(OBJ_RK) core/math.o core/shade.o core/config.o game/caps.o -o tests/test_mesh_models $(LDLIBS)
	./tests/test_mesh_models

# Structural model contracts (BmBlock table vs vanilla expectations). Catches
# full-cube lily pads etc. that pixel-diff seed-0 cannot see. See VERIFY.md.
.PHONY: test-model-oracle
test-model-oracle: assets/blockmodels.o
	$(CC) $(CFLAGS) tests/test_model_oracle.c assets/blockmodels.o -o tests/test_model_oracle $(LDLIBS)
	./tests/test_model_oracle

# Parse vanilla 1.11.2 jar block models; assert waterlily plane + cross parents.
.PHONY: test-jar-models
test-jar-models:
	uv run --no-project python tests/check_jar_models.py

# Harsh visual-parity suite: jar models + model oracle + mesh + multi-scene pixels.
# See VERIFY.md. Multi-scene requires mc_frame.png (+ optional mc_seed7.png).
# hard-scene-verify: seed0 leaf-canopy scene golden + render ablations (the hard case).
.PHONY: verify-harsh multi-verify hard-scene-verify
multi-verify:
	bash ../verify/mc_capture/run_multi_verify.sh
hard-scene-verify:
	bash ../verify/mc_capture/run_hard_scene.sh
verify-harsh: test-jar-models test-model-oracle test-mesh
	@echo "== verify-harsh: structural PASS; hard-scene + multi-scene pixel gates =="
	@if [ -s ../verify/mc_capture/mc_frame.png ]; then \
	  $(MAKE) hard-scene-verify; \
	  $(MAKE) multi-verify; \
	else \
	  echo "note: no mc_frame.png, skipping pixel gates (structural gates still PASS)"; \
	fi
# light->brightness curve test: assert the MC lightBrightnessTable + sky/block
# combine that world/mesh_mc.c bakes match the JAVA ground truth (tests/Golden.java)
# bit-for-bit. Regenerates the golden with JDK8 if java is on PATH; otherwise reuses
# the committed tests/golden_light.txt. Standalone (no game objects).
JAVA_HOME ?= /usr/lib/jvm/java-8-openjdk-amd64
.PHONY: test-light-brightness
test-light-brightness:
	@if [ -x "$(JAVA_HOME)/bin/javac" ]; then \
	  cd tests && "$(JAVA_HOME)/bin/javac" Golden.java && "$(JAVA_HOME)/bin/java" Golden > golden_light.txt && rm -f Golden.class; \
	else echo "note: JDK8 not found, reusing committed tests/golden_light.txt"; fi
	$(CC) $(CFLAGS) tests/test_light_brightness.c -o tests/test_light_brightness $(LDLIBS)
	./tests/test_light_brightness

# frustum cull test: bit-match render-opt kernels 05/06 + no-holes cull==no-cull
.PHONY: test-frustum
test-frustum:
	bash tests/test_frustum.sh

app/main.o: app/main.c world/world.h core/types.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

present/present.o: present/present.c core/types.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@
%.o: %.c core/types.h
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(OBJ_CORE:.o=.d) $(OBJ_RASTER:.o=.d) $(OBJ_XFORM:.o=.d) \
         $(OBJ_PRESENT:.o=.d) $(OBJ_WORLD:.o=.d) $(OBJ_RK:.o=.d) \
         $(OBJ_ASSETS:.o=.d) $(OBJ_GAME:.o=.d) app/game_main.d

# CUDA raster backend
cuda: cuda/raster_cuda.o
cuda/raster_cuda.o: cuda/raster_cuda.cu core/types.h core/shade.c
	$(NVCC) $(NVFLAGS) -c $< -o $@

# Rung 1: cr_raster_cpu == cr_raster_cuda, bit-exact (tests/test_raster_parity.c).
# Built for sm_86 so it runs on anvil GPU1 (RTX 3090). nvcc compiles the whole
# TU (C sources included) so the verified cr_raster_cuda + the CPU path agree.
test-raster-parity:
	$(NVCC) $(NVFLAGS_GAME) -I../verify tests/test_raster_parity.c \
	    cpu/raster_cpu.c core/shade.c core/math.c cuda/raster_cuda.cu \
	    -o tests/test_raster_parity
	CUDA_VISIBLE_DEVICES=1 ./tests/test_raster_parity

# Minecraft selects GL_EYE_RADIAL_NV on the live llvmpipe renderer. Keep fog
# distance radial through transform, clipping, and perspective interpolation.
test-fog-radial: core/math.o core/shade.o cpu/raster_cpu.o transform.o
	$(CC) $(CFLAGS) tests/test_fog_radial.c core/math.o core/shade.o \
	    cpu/raster_cpu.o transform.o -o tests/test_fog_radial $(LDLIBS)
	./tests/test_fog_radial

test-particles-live: game/particles_live.o assets/blockmodels.o
	$(CC) $(CFLAGS) -I$(BLAZE)/core game/test_particles_live.c \
	    game/particles_live.o assets/blockmodels.o \
	    -o game/test_particles_live $(LDLIBS)
	./game/test_particles_live

test-world-spawn: game/world_spawn.o
	$(CC) $(CFLAGS) -I$(BLAZE)/core game/test_world_spawn.c game/world_spawn.o -o game/test_world_spawn $(LDLIBS)
	./game/test_world_spawn

test-fall-reanchor:
	bash game/test_fall_reanchor.sh

test-sky-weather:
	$(CC) $(CFLAGS) tests/test_sky_weather.c -o tests/test_sky_weather $(LDLIBS)
	./tests/test_sky_weather

# World.checkLightFor decrease: 3x3x3 water cube centre sky==9, not 12.
.PHONY: test-water-skylight
test-water-skylight: $(OBJ_WORLD) $(OBJ_RK) core/math.o core/shade.o core/config.o game/caps.o
	$(CC) $(CFLAGS) -I$(BLAZE)/core tests/test_water_skylight.c \
	    world/mesh_mc.o world/light.o world/populate_mc.o world/gen_prefetch.o \
	    assets/blockmodels.o \
	    $(OBJ_RK) core/math.o core/shade.o core/config.o game/caps.o \
	    -o tests/test_water_skylight $(LDLIBS)
	./tests/test_water_skylight

.PHONY: test-look-helper test-pathfinder-null
test-look-helper:
	$(CC) $(CFLAGS) -I$(BLAZE)/core tests/test_look_helper.c -o tests/test_look_helper $(LDLIBS)
	./tests/test_look_helper

test-pathfinder-null:
	$(CC) $(CFLAGS) -I$(BLAZE)/core tests/test_pathfinder_null.c -o tests/test_pathfinder_null $(LDLIBS)
	./tests/test_pathfinder_null

.PHONY: test-placement
test-placement:
	$(CC) $(CFLAGS) -I$(BLAZE)/core tests/test_placement.c -o tests/test_placement $(LDLIBS)
	./tests/test_placement

.PHONY: test-portals-dimensions
test-portals-dimensions:
	$(CC) $(CFLAGS) -I$(BLAZE)/core -I$(BLAZE)/env $(BLAZE)/env/test_portals_dimensions.c $(BLAZE)/env/blaze_snapshot.c -o tests/test_portals_dimensions $(LDLIBS)
	./tests/test_portals_dimensions

.PHONY: test-beds
test-beds:
	$(CC) $(CFLAGS) -I$(BLAZE)/core tests/test_beds.c -o tests/test_beds $(LDLIBS)
	./tests/test_beds

.PHONY: test-potions
test-potions:
	$(CC) $(CFLAGS) -I$(BLAZE)/core tests/test_potions.c -o tests/test_potions $(LDLIBS)
	./tests/test_potions

.PHONY: test-explosions
test-explosions:
	$(CC) $(CFLAGS) -I$(BLAZE)/core tests/test_explosions.c -o tests/test_explosions $(LDLIBS)
	./tests/test_explosions

.PHONY: test-furnace-registry
test-furnace-registry:
	mkdir -p ../out/verify
	uv run --no-project python ../verify/furnace_registry.py --c-header \
	    > ../out/verify/furnace_registry_expect.h
	$(CC) $(CFLAGS) -I$(BLAZE)/core -I. tests/test_furnace_registry.c \
	    -o tests/test_furnace_registry $(LDLIBS)
	./tests/test_furnace_registry

test: test-sky-weather test-raster-smoke test-world-spawn test-fall-reanchor test-look-helper test-pathfinder-null test-water-skylight test-furnace-registry test-placement test-explosions test-live-items test-beds test-xp-orbs test-potions
	@echo "run per-module tests in tests/ (see SPEC.md)"

.PHONY: test-live-items
test-live-items:
	$(CC) $(CFLAGS) -I$(BLAZE)/core -I. tests/test_live_items.c -o tests/test_live_items $(LDLIBS)
	./tests/test_live_items

.PHONY: test-xp-orbs
test-xp-orbs:
	$(CC) $(CFLAGS) -I$(BLAZE)/core -I. tests/test_xp_orbs.c -o tests/test_xp_orbs $(LDLIBS)
	./tests/test_xp_orbs

# Standalone-game semantic gates. These deliberately exercise the real streamed
# world boundary, not only synthetic vanilla-id chunks.
test-block-registry:
	bash game/test_block_registry.sh

test-config:
	bash game/test_config.sh

test-launch: game
	SDL_VIDEODRIVER=dummy ./magma_game --world superflat --view-distance 1 \
		--width 160 --height 90 --frames 1 >/tmp/magma-test-launch.log 2>&1
	grep -q 'world=superflat' /tmp/magma-test-launch.log
	grep -q 'slot0=0' /tmp/magma-test-launch.log
	grep -q 'pos 8\.5,5\.0,8\.5' /tmp/magma-test-launch.log
	SDL_VIDEODRIVER=dummy ./magma_game --world superflat --view-distance 1 \
		--width 160 --height 90 --frames 100 --kill-frame 0 \
		>/tmp/magma-test-death.log 2>&1
	grep -q 'frame 0 .* dead 1 deaths 1' /tmp/magma-test-death.log
	# GuiGameOver holds the interactive/headless run after death (Respawn /
	# Title Screen only end it). Regression: stop-on-death would omit frame 1;
	# auto-respawn would clear dead before the requested frame budget.
	grep -q 'frame 1 .* dead 1 deaths 1' /tmp/magma-test-death.log
	grep -q 'frame 99 .* dead 1 deaths 1' /tmp/magma-test-death.log

# Ensure mob atlas exists with pixel-path sprites before atlas-dependent gates.
assets/mob_atlas.h: $(ASSET_BIN)
	@test -n "$(MC_JAR)" || { echo "minecraft-1.11.2.jar not found; set MC_JAR="; exit 1; }
	$(ASSET_RUN) $(ASSET_BIN) --jar "$(MC_JAR)" --out assets --only mob

../out/magma/test_build_world: assets/test_build_world.c $(ASSET_SRC) assets/build.h assets/jar.h assets/image.h
	mkdir -p ../out/magma
	$(CC) $(CFLAGS) -std=c11 -Werror -O1 -g -fsanitize=address,undefined \
		-o $@ assets/test_build_world.c $(ASSET_SRC) -lz

../out/magma/test_build_ui: assets/test_build_ui.c $(ASSET_SRC) assets/build.h assets/jar.h assets/image.h
	mkdir -p ../out/magma
	$(CC) $(CFLAGS) -std=c11 -Werror -O1 -g -fsanitize=address,undefined \
		-o $@ assets/test_build_ui.c $(ASSET_SRC) -lz

test-asset-ui: ../out/magma/test_build_ui
	@test -n "$(MC_JAR)" || { echo "minecraft-1.11.2.jar not found; set MC_JAR="; exit 1; }
	mkdir -p ../out/magma/asset_test_ui
	$(ASSET_RUN) ../out/magma/test_build_ui "$(MC_JAR)" ../out/magma/asset_test_ui

../out/magma/test_build_sound: assets/test_build_sound.c assets/build_sound.c
	mkdir -p ../out/magma
	$(CC) $(CFLAGS) -std=c11 -Werror -O1 -g -fsanitize=address,undefined \
		-DSOUND_NO_MAIN -o $@ assets/test_build_sound.c assets/build_sound.c

.PHONY: test-asset-sound
test-asset-sound: ../out/magma/test_build_sound
	@test -n "$(MC_ASSET_INDEX)" || { echo "1.11 asset index not found; set MC_ASSET_INDEX="; exit 1; }
	$(ASSET_RUN) ../out/magma/test_build_sound "$(MC_ASSET_INDEX)" \
		../out/magma/asset_test_sound.h assets/sound_manifest.h

test-asset-build: test-asset-ui test-asset-sound ../out/magma/test_build_world
	@test -n "$(MC_JAR)" || { echo "minecraft-1.11.2.jar not found; set MC_JAR="; exit 1; }
	mkdir -p ../out/magma/asset_test
	$(ASSET_RUN) ../out/magma/test_build_world "$(MC_JAR)" ../out/magma/asset_test

# Standalone game suite. The driver classifies every semantic subtest and
# continues after failures; only build/generated-asset prerequisites remain
# here because no runnable suite exists when they fail.
test-game: game assets/mob_atlas.h
	bash game/run_test_game.sh

.PHONY: test-mob-live
test-mob-live:
	bash game/test_mob_live.sh

# KernelBench-style rasterizer verification: rasterize one fixed clip-space scene
# with real Mesa software GL (OSMesa golden) and with our cr_raster_cpu (candidate),
# then tolerance-diff. GL triangle->pixel is hardware/driver-defined (subpixel fill
# rule, interpolation, filtering) so the metric is pixel diff, not bitwise.
DIFF ?= $(abspath ../render-opt/wholeframe/diff_frame.py)
.PHONY: raster-verify
raster-verify: cpu/raster_cpu.o core/shade.o
	$(CC) $(CFLAGS) -I../verify ../verify/gl_golden.c -o /tmp/cr_gl_golden -lOSMesa -lGL $(LDLIBS)
	$(CC) $(CFLAGS) -I. ../verify/c_candidate.c cpu/raster_cpu.o core/shade.o -o /tmp/cr_candidate $(LDLIBS)
	/tmp/cr_gl_golden /tmp/raster_golden.ppm
	/tmp/cr_candidate /tmp/raster_candidate.ppm
	uv run --no-project --with numpy --with pillow python $(DIFF) \
	    /tmp/raster_golden.ppm /tmp/raster_candidate.ppm --crop none --out /tmp/raster_diff

# Rung 4 (live game, COARSE): render the seed-0 ChunkScene through magma at the
# MC client resolution/pose and whole-frame pixel-diff vs a captured REAL MC frame
# (../verify/mc_capture/mc_frame.png). Capture the golden first with
# `bash ../verify/mc_capture/capture.sh` (needs the live game on display :1);
# this target only renders the candidate + diffs against whatever golden is present.
.PHONY: rung4-verify
rung4-verify:
	bash ../verify/mc_capture/run_rung4.sh

# ---- batched env (../blaze/env): CPU reference build. CC-only, same float
# discipline as everything else (-ffp-contract=off); the CUDA .so (M2) will
# compile the same blaze_core.h under nvcc --fmad=false with the same ABI.
# OpenMP parallelizes the env loop in blaze_step (per-env AABB scratch).
# Linux: -fopenmp (libgomp). Darwin: Apple clang has no bundled OpenMP; use
# Homebrew libomp (-Xpreprocessor -fopenmp + -lomp). Keep the output name
# blaze_cpu.so on both (Mach-O dylib with a .so suffix still dlopens via
# ctypes); no .dylib rename so blaze.py / verify_cpu.py stay platform-neutral.
BLAZE_SRCS = ../blaze/env/blaze_cpu.c ../blaze/env/blaze_snapshot.c
BLAZE_PARITY = ../blaze/core/port_parity.h
BLAZE_RANDTICK = ../blaze/core/randtick_live.h
BLAZE_FALLING = ../blaze/core/falling_live.h
BLAZE_WEATHER = ../blaze/core/world_weather.h
BLAZE_BED = ../blaze/core/player_bed.h
BLAZE_CHEST = ../blaze/core/tile_entity_chest.h ../blaze/core/container_click.h
BLAZE_PROJECTILES = ../blaze/core/projectile_live.h
BLAZE_EXPLOSIONS = ../blaze/core/explosion.h ../blaze/core/explosion_live.h
BLAZE_MOBS = ../blaze/core/hostile_live.h ../blaze/core/hostile_spawn.h \
             ../blaze/core/passive_live.h
BLAZE_XP = ../blaze/core/xp_live.h ../blaze/core/entity_xp_orb.h \
             ../blaze/core/xp_world_tick.h
BLAZE_ITEMS = ../blaze/core/entity_item.h ../blaze/core/item_live.h \
              ../blaze/core/item_overflow.h
BLAZE_BOATS = ../blaze/core/boat_live.h
BLAZE_ELYTRA = ../blaze/core/elytra_live.h
ifeq ($(shell uname -s),Darwin)
# Prefer Homebrew libomp; avoid `ls` (may inject color codes into Make vars).
ifeq ($(wildcard /opt/homebrew/opt/libomp/include/omp.h),)
  ifeq ($(wildcard /usr/local/opt/libomp/include/omp.h),)
    LIBOMP_PREFIX :=
  else
    LIBOMP_PREFIX := /usr/local/opt/libomp
  endif
else
  LIBOMP_PREFIX := /opt/homebrew/opt/libomp
endif
BLAZE_OMP ?= -Xpreprocessor -fopenmp -I$(LIBOMP_PREFIX)/include -L$(LIBOMP_PREFIX)/lib -lomp
else
BLAZE_OMP ?= -fopenmp
endif
.PHONY: blaze_verify blaze_so
blaze_verify: ../blaze/env/blaze_verify
../blaze/env/blaze_verify: ../blaze/env/blaze_verify.c $(BLAZE_SRCS) \
                       ../blaze/env/blaze_core.h ../blaze/env/blaze_snapshot.h $(BLAZE_PARITY) $(BLAZE_RANDTICK) $(BLAZE_FALLING) $(BLAZE_ITEMS) $(BLAZE_WEATHER) $(BLAZE_BED) $(BLAZE_PROJECTILES) $(BLAZE_EXPLOSIONS) $(BLAZE_MOBS) $(BLAZE_XP) $(BLAZE_BOATS) $(BLAZE_ELYTRA)
	$(CC) $(CFLAGS) $(BLAZE_OMP) -I$(BLAZE)/core -I../blaze/env ../blaze/env/blaze_verify.c \
	    $(BLAZE_SRCS) -o $@ $(LDLIBS) $(BLAZE_OMP)
blaze_so: ../blaze/env/blaze_cpu.so
../blaze/env/blaze_cpu.so: $(BLAZE_SRCS) ../blaze/env/blaze_core.h ../blaze/env/blaze_snapshot.h \
                        $(BLAZE_PARITY) $(BLAZE)/core/entity_spine.h $(BLAZE_RANDTICK) $(BLAZE_FALLING) \
                        $(BLAZE_ITEMS) $(BLAZE_WEATHER) $(BLAZE_BED) $(BLAZE_CHEST) $(BLAZE_PROJECTILES) $(BLAZE_EXPLOSIONS) \
                        $(BLAZE_MOBS) $(BLAZE_XP) $(BLAZE_BOATS) $(BLAZE_ELYTRA)
	$(CC) $(CFLAGS) $(BLAZE_OMP) -I$(BLAZE)/core -I../blaze/env -shared -fPIC $(BLAZE_SRCS) \
	    -o $@ $(LDLIBS) $(BLAZE_OMP)

# Metal k_obs (obs_camera oc_pixel) for Darwin. Allocate-once host + metallib;
# tick stays on blaze_cpu. Gate: blaze/env/verify_metal_obs.py --chain.
.PHONY: blaze_metal_obs
blaze_metal_obs: ../blaze/env/blaze_metal_obs.so
ifeq ($(shell uname -s),Darwin)
METALC   ?= xcrun -sdk macosx metal
METALLIB ?= xcrun -sdk macosx metallib
../blaze/env/blaze_metal_obs.air: ../blaze/env/blaze_metal_obs.metal
	$(METALC) -fno-fast-math -ffp-contract=off -o $@ -c $<
../blaze/env/blaze_metal_obs.metallib: ../blaze/env/blaze_metal_obs.air
	$(METALLIB) -o $@ $<
../blaze/env/blaze_metal_obs.so: ../blaze/env/blaze_metal_obs_host.m \
                                 ../blaze/env/blaze_metal_obs.h \
                                 ../blaze/env/blaze_metal_obs.metallib
	$(CC) $(CFLAGS) -fPIC -fobjc-arc -shared -I$(BLAZE)/core -I../blaze/env \
	    ../blaze/env/blaze_metal_obs_host.m -o $@ \
	    -framework Metal -framework Foundation $(LDLIBS)
else
../blaze/env/blaze_metal_obs.so:
	@echo "blaze_metal_obs: Darwin/Metal only (host is $$(uname -s))" >&2; exit 1
endif
# CUDA .so (M2): same core under nvcc, blaze core determinism flags (--fmad=false,
# NEVER fast-math). GPU0 (RTX PRO 6000) is sm_120; override BLAZE_SM for other
# parts. BLAZE_SM accepts a space-separated arch list for a dual-arch fatbin
# (e.g. BLAZE_SM="sm_86 sm_120" for one .so loadable on both anvil GPUs;
# bare numbers do not match the sm_% filter and fail nvcc). Per-arch
# SASS is identical to two single-arch builds: same source, same flags, so the
# CPU==CUDA bitwise contract holds per arch. Same C ABI as blaze_cpu.so -
# Python picks the backend at load time.
# Auto-select the CUDA arch from the host GPU when BLAZE_SM is unset, so the
# same tree builds correctly on any box (anvil RTX PRO 6000 Blackwell =
# sm_120, gamer RTX 3090 = sm_86) without remembering to pass BLAZE_SM.
# nvidia-smi "RTX 3090" -> sm_86, else (Blackwell / unknown) -> sm_120.
ifeq ($(origin BLAZE_SM),undefined)
BLAZE_SM := $(shell nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | \
             grep -qi "3090" && echo sm_86 || echo sm_120)
endif
BLAZE_NVCODE := $(if $(filter sm_%,$(BLAZE_SM)),$(foreach a,$(patsubst sm_%,%,$(BLAZE_SM)),-gencode arch=compute_$(a),code=sm_$(a)),-arch=$(BLAZE_SM))
.PHONY: blaze_cuda_so
blaze_cuda_so: ../blaze/env/blaze_cuda.so
../blaze/env/blaze_cuda.so: ../blaze/env/blaze_cuda.cu ../blaze/env/blaze_snapshot.c \
                        ../blaze/env/blaze_core.h ../blaze/env/blaze_snapshot.h \
                        $(BLAZE_PARITY) $(BLAZE)/core/entity_spine.h $(BLAZE_RANDTICK) $(BLAZE_FALLING) \
                        $(BLAZE_ITEMS) $(BLAZE_WEATHER) $(BLAZE_BED) $(BLAZE_CHEST) $(BLAZE_PROJECTILES) $(BLAZE_EXPLOSIONS) \
                        $(BLAZE_MOBS) $(BLAZE_XP) $(BLAZE_BOATS) $(BLAZE_ELYTRA)
	$(NVCC) -O2 --fmad=false $(BLAZE_NVCODE) -I$(BLAZE)/core -I../blaze/env \
	    -Xcompiler -fPIC --shared ../blaze/env/blaze_cuda.cu \
	    ../blaze/env/blaze_snapshot.c -o $@

clean:
	rm -f magma_world magma_game magma_game_dbg magma_game_cuda magma_game_metal
	rm -f metal/raster_kernels.air metal/raster_kernels.metallib
	rm -f ../blaze/env/blaze_verify ../blaze/env/blaze_cpu.so ../blaze/env/blaze_cuda.so ../blaze/env/blaze_metal_obs.so ../blaze/env/blaze_metal_obs.air ../blaze/env/blaze_metal_obs.metallib
	find . -name '*.o' -delete
	find . -name '*.d' -delete
	rm -f tests/test_raster_smoke tests/test_transform tests/test_present tests/test_scene tests/test_world \
	      tests/test_raster_parity tests/test_assets tests/test_light tests/test_water_skylight tests/test_mesh_mc \
	      tests/test_live_items \
	      tests/test_mesh_models tests/test_model_oracle tests/test_light_brightness \
	      tests/test_sky_weather \
	      tests/test_fog_radial tests/test_portals_dimensions \
	      tests/Golden.class renderkernels/test_rk *.ppm
	rm -f game/test_config game/test_block_registry game/test_player_ctl game/test_world_live game/test_play_compose game/test_particles_live
	rm -f ../out/magma/assets ../out/magma/sound \
		../out/magma/test_build_world ../out/magma/test_build_ui \
		../out/magma/test_build_sound

# Metal verification targets (Darwin-guarded; see make/metal.mk)
-include make/metal.mk
