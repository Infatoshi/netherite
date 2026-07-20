# craster build. See SPEC.md. C reference + CUDA raster, SDL2 present.
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

# wave 2: world mesh feed (consumes mc-sim worldgen headers)
MCSIM     ?= $(abspath ../mc-sim)
OBJ_WORLD  = $(patsubst %.c,%.o,$(wildcard world/*.c))
# wave 6: facebakery kernel that bakes non-cube block models (world/mesh_mc.c)
OBJ_RK     = renderkernels/rk_31_facebakery_make_quad.o
# block-model / atlas table (world/mesh_mc.c consumes it)
OBJ_ASSETS = assets/blockmodels.o

# seam: playable game (verified mc-sim player/world tick + craster render + HUD + mobs)
OBJ_GAME = game/config.o game/input_map.o game/world_live.o game/player_ctl.o game/hud.o game/screen.o game/entity_render.o game/item_render.o \
           game/sky.o game/caps.o game/hand.o game/timer.o game/live_sim.o game/mob_live.o game/dragon_live.o game/structures_live.o game/portal_live.o game/furnace_live.o game/container_live.o game/runtime.o game/frame_capture.o game/script.o game/rl_mode.o game/overlay.o game/sel_box.o game/fluid_live.o game/underwater.o

.PHONY: all demo world game game-cuda test test-game test-launch test-config test-block-registry clean cuda test-raster-parity test-fog-radial
all: demo

# --- CUDA raster for the game (RTX 3090 / sm_86 on anvil GPU1) ---
# GPU1 is the RTX 3090; build the .cu for sm_86 and link cudart.
NVFLAGS_GAME ?= -O2 --fmad=false -arch=sm_86 -Icore -I.
CUDA_LIBDIR  ?= $(patsubst %/bin/nvcc,%/lib64,$(shell which nvcc))
CUDA_LIBS    ?= -L$(CUDA_LIBDIR) -lcudart -lstdc++

# CPU spine demo (spinning textured cube)
demo: craster_demo
craster_demo: demo/demo_cube.o demo/scene.o $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS)

# playable world app
# NB: the world objects (world/light.o, world/populate_mc.o) now derive their fixed
# pools from game/caps.o, so it is linked into every binary that pulls them in.
world: craster_world
craster_world: app/main.o $(OBJ_WORLD) $(OBJ_RK) $(OBJ_ASSETS) $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT) game/caps.o
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS)

world/%.o: world/%.c core/types.h
	$(CC) $(CFLAGS) $(DEPFLAGS) -I$(MCSIM)/core -c $< -o $@

# playable game binary: seam-wires the verified sim tick to the craster renderer.
# The CPU binary references cr_raster_cuda_* only as weak symbols (frame_capture,
# game_main check them for NULL). ELF resolves absent weak refs to NULL; Mach-O's
# ld needs them explicitly allowed, so Darwin gets -Wl,-U per symbol.
ifeq ($(shell uname -s),Darwin)
CUDA_WEAK_LD = -Wl,-U,_cr_raster_cuda_pre -Wl,-U,_cr_raster_cuda_into \
               -Wl,-U,_cr_raster_cuda_frame_begin -Wl,-U,_cr_raster_cuda_frame_end \
               -Wl,-U,_cr_raster_cuda_post -Wl,-U,_alloctrack_frame
endif
game: craster_game
craster_game: app/game_main.o $(OBJ_GAME) $(OBJ_WORLD) $(OBJ_RK) $(OBJ_ASSETS) \
              $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS) $(CUDA_WEAK_LD)

# game/ modules include game/game.h + (world_live/player_ctl) player_survival.h.
game/%.o: game/%.c core/types.h game/game.h
	$(CC) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -I$(MCSIM)/core -c $< -o $@

game/runtime.o: game/runtime.c game/runtime.h game/mob_live.h game/dragon_live.h \
                game/portal_live.h game/container_live.h $(MCSIM)/core/crafting_recipes_full.h
game/container_live.o: game/container_live.c game/container_live.h game/runtime.h \
                $(MCSIM)/core/container_click.h $(MCSIM)/core/crafting_recipes_full.h \
                $(MCSIM)/core/smelting_recipes.h

# CUDA-accelerated game binary: same sources as craster_game but game_main.c is
# compiled with -DCRASTER_CUDA (adds the --cuda / CRASTER_CUDA=1 switch that
# routes render_layer through the alloc-once cr_raster_cuda_into) and links the
# sm_86 raster object + cudart. `make game` (pure CPU) is unaffected.
game-cuda: craster_game_cuda
app/game_main_cuda.o: app/game_main.c game/game.h game/runtime.h game/config.h core/types.h game/caps.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -DCRASTER_CUDA -I$(MCSIM)/core -c $< -o $@
cuda/raster_cuda_sm86.o: cuda/raster_cuda.cu core/types.h core/shade.c
	$(NVCC) $(NVFLAGS_GAME) -c $< -o $@
craster_game_cuda: app/game_main_cuda.o cuda/raster_cuda_sm86.o $(OBJ_GAME) $(OBJ_WORLD) \
              $(OBJ_RK) $(OBJ_ASSETS) $(OBJ_CORE) $(OBJ_RASTER) $(OBJ_XFORM) $(OBJ_PRESENT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $^ -o $@ $(SDL_LIBS) $(LDLIBS) $(CUDA_LIBS)

# ALLOCATE-ONCE alloc-tripwire build: craster_game with debug symbols (so the
# trace/alloctrack.so summary offsets resolve via addr2line -e craster_game_dbg).
# Same sources as craster_game, one -g compile. See trace/alloctrack.c + caps.h.
GAME_DBG_SRCS = app/game_main.c \
                game/config.c game/input_map.c game/world_live.c game/player_ctl.c game/hud.c \
                game/entity_render.c game/item_render.c game/sky.c game/caps.c game/hand.c game/timer.c game/live_sim.c game/mob_live.c game/dragon_live.c game/structures_live.c game/portal_live.c game/furnace_live.c game/runtime.c game/frame_capture.c game/script.c \
                world/mesh_mc.c world/light.c world/populate_mc.c \
                world/world.c world/mesh.c world/blocks.c \
                renderkernels/rk_31_facebakery_make_quad.c assets/blockmodels.c \
                core/math.c core/shade.c cpu/raster_cpu.c transform.c present/present.c
.PHONY: game-dbg
game-dbg: craster_game_dbg
craster_game_dbg: $(GAME_DBG_SRCS)
	$(CC) $(CFLAGS) -g $(SDL_CFLAGS) -I$(MCSIM)/core $(GAME_DBG_SRCS) -o $@ $(SDL_LIBS) $(LDLIBS) $(CUDA_WEAK_LD)

app/game_main.o: app/game_main.c game/game.h game/runtime.h game/config.h core/types.h
	$(CC) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -I$(MCSIM)/core -c $< -o $@

# non-cube mesh unit test: build a synthetic chunk, assert facebakery geometry
.PHONY: test-mesh
test-mesh: $(OBJ_WORLD) $(OBJ_RK) core/math.o core/shade.o game/caps.o
	$(CC) $(CFLAGS) -I$(MCSIM)/core tests/test_mesh_models.c \
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
	bash raster/verify/mc_capture/run_multi_verify.sh
hard-scene-verify:
	bash raster/verify/mc_capture/run_hard_scene.sh
verify-harsh: test-jar-models test-model-oracle test-mesh
	@echo "== verify-harsh: structural PASS; hard-scene + multi-scene pixel gates =="
	@if [ -s raster/verify/mc_capture/mc_frame.png ]; then \
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
	$(NVCC) $(NVFLAGS_GAME) -Iraster/verify tests/test_raster_parity.c \
	    cpu/raster_cpu.c core/shade.c cuda/raster_cuda.cu -o tests/test_raster_parity
	CUDA_VISIBLE_DEVICES=1 ./tests/test_raster_parity

# Minecraft selects GL_EYE_RADIAL_NV on the live llvmpipe renderer. Keep fog
# distance radial through transform, clipping, and perspective interpolation.
test-fog-radial: core/math.o core/shade.o cpu/raster_cpu.o transform.o
	$(CC) $(CFLAGS) tests/test_fog_radial.c core/math.o core/shade.o \
	    cpu/raster_cpu.o transform.o -o tests/test_fog_radial $(LDLIBS)
	./tests/test_fog_radial

test: craster_demo
	@echo "run per-module tests in tests/ (see SPEC.md)"

# Standalone-game semantic gates. These deliberately exercise the real streamed
# world boundary, not only synthetic vanilla-id chunks.
test-block-registry:
	bash game/test_block_registry.sh

test-config:
	bash game/test_config.sh

test-launch: game
	SDL_VIDEODRIVER=dummy ./craster_game --world superflat --view-distance 1 \
		--width 160 --height 90 --frames 1 >/tmp/craster-test-launch.log 2>&1
	grep -q 'world=superflat' /tmp/craster-test-launch.log
	grep -q 'slot0=0' /tmp/craster-test-launch.log
	grep -q 'pos 8\.5,5\.0,8\.5' /tmp/craster-test-launch.log
	SDL_VIDEODRIVER=dummy ./craster_game --world superflat --view-distance 1 \
		--width 160 --height 90 --frames 100 --kill-frame 0 \
		>/tmp/craster-test-death.log 2>&1
	grep -q 'frame 0 .* dead 1 deaths 1' /tmp/craster-test-death.log
	! grep -q 'frame 1 ' /tmp/craster-test-death.log

test-game: test-config test-block-registry test-launch
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
	bash game/test_screen.sh
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
	$(CC) $(CFLAGS) -Iraster/verify raster/verify/gl_golden.c -o /tmp/cr_gl_golden -lOSMesa -lGL $(LDLIBS)
	$(CC) $(CFLAGS) -I. raster/verify/c_candidate.c cpu/raster_cpu.o core/shade.o -o /tmp/cr_candidate $(LDLIBS)
	/tmp/cr_gl_golden /tmp/raster_golden.ppm
	/tmp/cr_candidate /tmp/raster_candidate.ppm
	uv run --no-project --with numpy --with pillow python $(DIFF) \
	    /tmp/raster_golden.ppm /tmp/raster_candidate.ppm --crop none --out /tmp/raster_diff

# Rung 4 (live game, COARSE): render the seed-0 ChunkScene through craster at the
# MC client resolution/pose and whole-frame pixel-diff vs a captured REAL MC frame
# (raster/verify/mc_capture/mc_frame.png). Capture the golden first with
# `bash raster/verify/mc_capture/capture.sh` (needs the live game on display :1);
# this target only renders the candidate + diffs against whatever golden is present.
.PHONY: rung4-verify
rung4-verify:
	bash raster/verify/mc_capture/run_rung4.sh

# ---- batched env (rl/cuenv): CPU reference build. CC-only, same float
# discipline as everything else (-ffp-contract=off); the CUDA .so (M2) will
# compile the same cuenv_core.h under nvcc --fmad=false with the same ABI.
# OpenMP parallelizes the env loop in cuenv_step (per-env AABB scratch).
CUENV_SRCS = rl/cuenv/cuenv_cpu.c rl/cuenv/cuenv_snapshot.c
CUENV_OMP ?= -fopenmp
.PHONY: cuenv_verify cuenv_so
cuenv_verify: rl/cuenv/cuenv_verify
rl/cuenv/cuenv_verify: rl/cuenv/cuenv_verify.c $(CUENV_SRCS) \
                       rl/cuenv/cuenv_core.h rl/cuenv/cuenv_snapshot.h
	$(CC) $(CFLAGS) $(CUENV_OMP) -I$(MCSIM)/core -Irl/cuenv rl/cuenv/cuenv_verify.c \
	    $(CUENV_SRCS) -o $@ $(LDLIBS) $(CUENV_OMP)
cuenv_so: rl/cuenv/cuenv_cpu.so
rl/cuenv/cuenv_cpu.so: $(CUENV_SRCS) rl/cuenv/cuenv_core.h rl/cuenv/cuenv_snapshot.h
	$(CC) $(CFLAGS) $(CUENV_OMP) -I$(MCSIM)/core -Irl/cuenv -shared -fPIC $(CUENV_SRCS) \
	    -o $@ $(LDLIBS) $(CUENV_OMP)
# CUDA .so (M2): same core under nvcc, mc-sim determinism flags (--fmad=false,
# NEVER fast-math). GPU0 (RTX PRO 6000) is sm_120; override CUENV_SM for other
# parts. CUENV_SM accepts a space-separated arch list for a dual-arch fatbin
# (e.g. CUENV_SM="86 120" for one .so loadable on both anvil GPUs). Per-arch
# SASS is identical to two single-arch builds: same source, same flags, so the
# CPU==CUDA bitwise contract holds per arch. Same C ABI as cuenv_cpu.so -
# Python picks the backend at load time.
CUENV_SM ?= sm_120
CUENV_NVCODE := $(if $(filter sm_%,$(CUENV_SM)),$(foreach a,$(patsubst sm_%,%,$(CUENV_SM)),-gencode arch=compute_$(a),code=sm_$(a)),-arch=$(CUENV_SM))
.PHONY: cuenv_cuda_so
cuenv_cuda_so: rl/cuenv/cuenv_cuda.so
rl/cuenv/cuenv_cuda.so: rl/cuenv/cuenv_cuda.cu rl/cuenv/cuenv_snapshot.c \
                        rl/cuenv/cuenv_core.h rl/cuenv/cuenv_snapshot.h
	$(NVCC) -O2 --fmad=false $(CUENV_NVCODE) -I$(MCSIM)/core -Irl/cuenv \
	    -Xcompiler -fPIC --shared rl/cuenv/cuenv_cuda.cu \
	    rl/cuenv/cuenv_snapshot.c -o $@

clean:
	rm -f craster_demo craster_world craster_game craster_game_dbg craster_game_cuda
	rm -f rl/cuenv/cuenv_verify rl/cuenv/cuenv_cpu.so rl/cuenv/cuenv_cuda.so
	find . -name '*.o' -delete
	find . -name '*.d' -delete
	rm -f tests/test_transform tests/test_present tests/test_scene tests/test_world \
	      tests/test_raster_parity tests/test_assets tests/test_light tests/test_mesh_mc \
	      tests/test_mesh_models tests/test_model_oracle tests/test_light_brightness \
	      tests/test_fog_radial \
	      tests/Golden.class renderkernels/test_rk *.ppm
	rm -f game/test_config game/test_block_registry game/test_player_ctl game/test_world_live game/test_play_compose
