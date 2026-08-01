# magma build. See SPEC.md. C reference + CUDA raster, SDL2 present.
# Parallel by default (clean serial build ~12s, -j ~3s). An explicit -j on the
# command line still wins over this.
MAKEFLAGS += -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
CC      ?= gcc
NVCC    ?= nvcc
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
LDLIBS  ?= -lm
DEPFLAGS = -MMD -MP

# module objects (each owned by one agent per SPEC.md)
OBJ_CORE   = core/math.o core/shade.o
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
           game/sky.o game/caps.o game/hand.o game/timer.o game/live_sim.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/chest_live.o game/container_live.o game/runtime.o game/frame_capture.o game/window_compose.o game/script.o game/rl_mode.o game/overlay.o game/overlay_live.o game/sel_box.o game/fluid_live.o game/underwater.o

.PHONY: all demo world game game-cuda game-metal test test-game test-launch test-config test-block-registry test-particles-live clean cuda test-raster-parity test-fog-radial
all: demo

# --- CUDA raster for the game (RTX 3090 / sm_86 on anvil GPU1) ---
# GPU1 is the RTX 3090; build the .cu for sm_86 and link cudart.
NVFLAGS_GAME ?= -O2 --fmad=false -arch=sm_86 -Icore -I.
CUDA_LIBDIR  ?= $(patsubst %/bin/nvcc,%/lib64,$(shell which nvcc))
CUDA_LIBS    ?= -L$(CUDA_LIBDIR) -lcudart -lstdc++

# CPU spine demo (spinning textured cube)
demo: magma_demo
magma_demo: demo/demo_cube.o demo/scene.o $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS)

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
               -Wl,-U,_cr_raster_cuda_render_layer -Wl,-U,_cr_raster_cuda_render_gather \
               -Wl,-U,_cr_raster_cuda_render_terrain \
               -Wl,-U,_cr_raster_cuda_slab_pool -Wl,-U,_cr_raster_cuda_slab_sync \
               -Wl,-U,_cr_raster_cuda_slabs_reset -Wl,-U,_alloctrack_frame
endif
game: magma_game
magma_game: app/game_main.o $(OBJ_GAME) $(OBJ_WORLD) $(OBJ_RK) $(OBJ_ASSETS) \
              $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS) $(CUDA_WEAK_LD)

# game/ modules include game/game.h + (world_live/player_ctl) player_survival.h.
game/%.o: game/%.c core/types.h game/game.h
	$(CC) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -I$(BLAZE)/core -c $< -o $@

# Generated texture headers: rebuild when the Python generator changes so
# assets/hud_atlas.h cannot stay stale after build_hud_atlas.py edits.
# Requires a MC 1.11.2 jar (MC_JAR or bootstrap_oracle / launcher cache).
UV_ATLAS = uv run --no-project --with pillow python
assets/hud_atlas.h: assets/build_hud_atlas.py
	$(UV_ATLAS) assets/build_hud_atlas.py
game/hud.o: assets/hud_atlas.h

game/runtime.o: game/runtime.c game/runtime.h game/mob_live.h game/dragon_live.h \
                game/portal_live.h game/container_live.h $(BLAZE)/core/crafting_recipes_full.h
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
app/game_main_cuda.o: app/game_main.c game/game.h game/runtime.h game/config.h core/types.h game/caps.h game/particles_live.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DMAGMA_CUDA -I$(BLAZE)/core -c $< -o $@
cuda/raster_cuda_game.o: cuda/raster_cuda.cu core/types.h core/shade.c
	$(NVCC) $(NVFLAGS_GAME) -c $< -o $@
magma_game_cuda: app/game_main_cuda.o cuda/raster_cuda_game.o $(OBJ_GAME) $(OBJ_WORLD) \
              $(OBJ_RK) $(OBJ_ASSETS) $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS) $(CUDA_LIBS)

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
                $(foreach s,pre into frame_begin frame_end sky post,\
                  -Wl,-U,_cr_raster_cuda_$(s)) \
                -Wl,-U,_alloctrack_frame
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
app/game_main_metal.o: app/game_main.c game/game.h game/runtime.h game/config.h core/types.h game/caps.h game/particles_live.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DMAGMA_METAL -I$(BLAZE)/core -c $< -o $@
game/frame_capture_metal.o: game/frame_capture.c game/frame_capture.h core/types.h game/game.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DMAGMA_METAL -I$(BLAZE)/core -c $< -o $@
game/window_compose_metal.o: game/window_compose.c game/window_compose.h core/types.h game/game.h
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
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(filter %.o,$^) -o $@ $(SDL_LIBS) $(LDLIBS) $(METAL_FRAMEWORKS) $(METAL_WEAK_LD)

# ALLOCATE-ONCE alloc-tripwire build: magma_game with debug symbols (so the
# trace/alloctrack.so summary offsets resolve via addr2line -e magma_game_dbg).
# Same sources as magma_game, one -g compile. See trace/alloctrack.c + caps.h.
# Same game sources as OBJ_GAME (plus app/game_main + world/assets/core/present).
GAME_DBG_SRCS = app/game_main.c \
                game/config.c game/input_map.c game/world_live.c game/player_ctl.c game/hud.c \
                game/screen.c game/player_preview.c \
                game/entity_render.c game/particles_live.c game/item_render.c game/sky.c game/caps.c game/hand.c game/timer.c game/live_sim.c game/mob_live.c game/dragon_live.c game/structures_live.c game/portal_live.c game/furnace_live.c game/chest_live.c game/container_live.c game/runtime.c game/frame_capture.c game/window_compose.c game/script.c game/rl_mode.c \
                game/overlay.c game/overlay_live.c game/sel_box.c game/fluid_live.c game/underwater.c \
                world/mesh_mc.c world/light.c world/populate_mc.c \
                world/world.c world/mesh.c world/blocks.c \
                renderkernels/rk_31_facebakery_make_quad.c assets/blockmodels.c \
                core/math.c core/shade.c cpu/raster_cpu.c transform.c present/present.c
.PHONY: game-dbg
game-dbg: magma_game_dbg
magma_game_dbg: $(GAME_DBG_SRCS)
	$(CC) $(CFLAGS) -g $(SDL_CFLAGS) -I$(BLAZE)/core $(GAME_DBG_SRCS) -o $@ $(SDL_LIBS) $(LDLIBS) $(CUDA_WEAK_LD)

app/game_main.o: app/game_main.c game/game.h game/runtime.h game/config.h core/types.h game/particles_live.h
	$(CC) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -I$(BLAZE)/core -c $< -o $@

# non-cube mesh unit test: build a synthetic chunk, assert facebakery geometry
.PHONY: test-mesh
test-mesh: $(OBJ_WORLD) $(OBJ_RK) core/math.o core/shade.o game/caps.o
	$(CC) $(CFLAGS) -I$(BLAZE)/core tests/test_mesh_models.c \
	    world/mesh_mc.o world/light.o world/populate_mc.o assets/blockmodels.o \
	    $(OBJ_RK) core/math.o core/shade.o game/caps.o -o tests/test_mesh_models $(LDLIBS)
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
demo/demo_cube.o: demo/demo_cube.c core/types.h
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

test: magma_demo
	@echo "run per-module tests in tests/ (see SPEC.md)"

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
assets/mob_atlas.h:
	uv run --no-project --with pillow python assets/build_mob_atlas.py

test-game: test-config test-block-registry test-launch assets/mob_atlas.h
	uv run --no-project --with pillow python tests/check_mob_atlas.py
	bash game/test_input_map.sh
	bash game/test_view.sh
	bash game/test_overlay.sh
	bash game/test_entity_render.sh
	bash game/test_item_render.sh
	bash game/test_hand.sh
	bash game/test_player_ctl.sh
	bash game/test_world_live.sh
	bash game/test_play_compose.sh
	bash game/test_runtime.sh
	bash game/test_script.sh
	bash game/test_fluid_live.sh
	bash game/test_plants_live.sh
	bash game/test_furnace_live.sh
	bash game/test_container_live.sh
	bash game/test_armor_live.sh
	bash game/test_screen.sh
	bash game/test_player_preview.sh
	bash game/test_chest_loot.sh
	bash game/test_mob_live.sh
	bash game/test_stronghold_live.sh
	bash game/test_portal_live.sh
	bash game/test_dimensions_live.sh
	bash game/test_dragon_live.sh
	bash game/test_route_e2e.sh

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
BLAZE_SRCS = ../blaze/env/blaze_cpu.c ../blaze/env/blaze_snapshot.c
BLAZE_OMP ?= -fopenmp
.PHONY: blaze_verify blaze_so
blaze_verify: ../blaze/env/blaze_verify
../blaze/env/blaze_verify: ../blaze/env/blaze_verify.c $(BLAZE_SRCS) \
                       ../blaze/env/blaze_core.h ../blaze/env/blaze_snapshot.h
	$(CC) $(CFLAGS) $(BLAZE_OMP) -I$(BLAZE)/core -I../blaze/env ../blaze/env/blaze_verify.c \
	    $(BLAZE_SRCS) -o $@ $(LDLIBS) $(BLAZE_OMP)
blaze_so: ../blaze/env/blaze_cpu.so
../blaze/env/blaze_cpu.so: $(BLAZE_SRCS) ../blaze/env/blaze_core.h ../blaze/env/blaze_snapshot.h
	$(CC) $(CFLAGS) $(BLAZE_OMP) -I$(BLAZE)/core -I../blaze/env -shared -fPIC $(BLAZE_SRCS) \
	    -o $@ $(LDLIBS) $(BLAZE_OMP)
# CUDA .so (M2): same core under nvcc, blaze core determinism flags (--fmad=false,
# NEVER fast-math). GPU0 (RTX PRO 6000) is sm_120; override BLAZE_SM for other
# parts. BLAZE_SM accepts a space-separated arch list for a dual-arch fatbin
# (e.g. BLAZE_SM="86 120" for one .so loadable on both anvil GPUs). Per-arch
# SASS is identical to two single-arch builds: same source, same flags, so the
# CPU==CUDA bitwise contract holds per arch. Same C ABI as blaze_cpu.so -
# Python picks the backend at load time.
BLAZE_SM ?= sm_120
BLAZE_NVCODE := $(if $(filter sm_%,$(BLAZE_SM)),$(foreach a,$(patsubst sm_%,%,$(BLAZE_SM)),-gencode arch=compute_$(a),code=sm_$(a)),-arch=$(BLAZE_SM))
.PHONY: blaze_cuda_so
blaze_cuda_so: ../blaze/env/blaze_cuda.so
../blaze/env/blaze_cuda.so: ../blaze/env/blaze_cuda.cu ../blaze/env/blaze_snapshot.c \
                        ../blaze/env/blaze_core.h ../blaze/env/blaze_snapshot.h
	$(NVCC) -O2 --fmad=false $(BLAZE_NVCODE) -I$(BLAZE)/core -I../blaze/env \
	    -Xcompiler -fPIC --shared ../blaze/env/blaze_cuda.cu \
	    ../blaze/env/blaze_snapshot.c -o $@

clean:
	rm -f magma_demo magma_world magma_game magma_game_dbg magma_game_cuda magma_game_metal
	rm -f metal/raster_kernels.air metal/raster_kernels.metallib
	rm -f ../blaze/env/blaze_verify ../blaze/env/blaze_cpu.so ../blaze/env/blaze_cuda.so
	find . -name '*.o' -delete
	find . -name '*.d' -delete
	rm -f tests/test_transform tests/test_present tests/test_scene tests/test_world \
	      tests/test_raster_parity tests/test_assets tests/test_light tests/test_mesh_mc \
	      tests/test_mesh_models tests/test_model_oracle tests/test_light_brightness \
	      tests/test_fog_radial \
	      tests/Golden.class renderkernels/test_rk *.ppm
	rm -f game/test_config game/test_block_registry game/test_player_ctl game/test_world_live game/test_play_compose game/test_particles_live

# Metal verification targets (Darwin-guarded; see make/metal.mk)
-include make/metal.mk
