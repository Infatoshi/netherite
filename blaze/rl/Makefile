# Native C11 trainer. One host binary; config selects backend.
#   Darwin: cpu | metal  (CPU tick + Metal obs + Metal policy)
#   Linux:  cpu | cuda
# Generated output under root out/blaze/rl/ and out/blaze/env/.
CC ?= cc
METAL_CC ?= clang
REPO := ../..
OUT := $(REPO)/out/blaze/rl
ENV_OUT := $(REPO)/out/blaze/env
NN_SRC := ../nn
ENV_SRC := ../env
CORE_SRC := ../core
CONF := ppo.conf

UNAME_S := $(shell uname -s)

CPPFLAGS_COMMON := -I. -I$(NN_SRC) -I$(ENV_SRC) -I$(CORE_SRC)
# Owned trainer sources: full -Werror.
OWN_CFLAGS := -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror
# Existing Blaze env: do not promote old warnings to errors.
ENV_CFLAGS := -std=c11 -O2 -ffp-contract=off -Wall -fPIC
LDLIBS := -lm -ldl

ifeq ($(UNAME_S),Darwin)
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
OMP_CFLAGS ?= -Xpreprocessor -fopenmp -I$(LIBOMP_PREFIX)/include
OMP_LDFLAGS ?= -L$(LIBOMP_PREFIX)/lib -lomp
NN_OUT := $(REPO)/out/blaze/nn/metal
BACKEND_DEFS := -DNN_HAVE_METAL=1 -DBLAZE_RL_HAVE_METAL=1 -DBLAZE_RL_HAVE_CUDA=0
CPPFLAGS := $(CPPFLAGS_COMMON) $(BACKEND_DEFS)
METAL_OBJCFLAGS := -fobjc-arc -O2 -ffp-contract=off -Wall -Wextra \
	$(BACKEND_DEFS)
# Framework headers can trip -Werror on some SDK versions; keep -Wextra.
METAL_HOST_OBJCFLAGS := -fobjc-arc -O2 -ffp-contract=off -Wall -fPIC \
	-I$(ENV_SRC) -I$(CORE_SRC)
METAL_LIBS := -framework Metal -framework Foundation \
	-framework MetalPerformanceShadersGraph -framework MetalPerformanceShaders
NN_OBJS := $(NN_OUT)/nn.o $(NN_OUT)/cpu.o $(NN_OUT)/fixture.o $(NN_OUT)/metal.o
STAGE_OBJ :=
METAL_OBS_OBJ := $(OUT)/env_metal_obs.o
METAL_HOST_OBJ := $(ENV_OUT)/blaze_metal_obs_host.o
METAL_AIR := $(ENV_OUT)/blaze_metal_obs.air
METAL_LIB := $(ENV_OUT)/blaze_metal_obs.metallib
ENV_CUDA_SO :=
LINK_LIBS := $(METAL_LIBS) $(OMP_LDFLAGS)
else
OMP_CFLAGS ?= -fopenmp
OMP_LDFLAGS ?= -fopenmp
CUDA_HOME ?= /usr/local/cuda
NVCC ?= $(CUDA_HOME)/bin/nvcc
# Gamer: sm_86. Override on anvil: BLAZE_SM=sm_120
BLAZE_SM ?= sm_86
NN_OUT := $(REPO)/out/blaze/nn/cuda
BACKEND_DEFS := -DNN_HAVE_CUDA=1 -DBLAZE_RL_HAVE_CUDA=1 -DBLAZE_RL_HAVE_METAL=0
CPPFLAGS := $(CPPFLAGS_COMMON) -I$(CUDA_HOME)/include $(BACKEND_DEFS)
NVCCFLAGS := -O2 -arch=$(BLAZE_SM) --fmad=false \
	-I$(CORE_SRC) -I$(ENV_SRC) -Xcompiler -fPIC,-ffp-contract=off
CUDA_LIBS := -L$(CUDA_HOME)/lib64 \
	-L$(CUDA_HOME)/targets/x86_64-linux/lib \
	-lcudnn -lcublas -lcudart -lstdc++ -lm -ldl
NN_OBJS := $(NN_OUT)/nn.o $(NN_OUT)/cpu.o $(NN_OUT)/fixture.o $(NN_OUT)/cuda.o
STAGE_OBJ := $(OUT)/env_cuda_stage.o
METAL_OBS_OBJ :=
METAL_HOST_OBJ :=
METAL_LIB :=
ENV_CUDA_SO := $(ENV_OUT)/blaze_cuda.so
LINK_LIBS := $(CUDA_LIBS) $(OMP_LDFLAGS)
endif

SAN_CFLAGS := -std=c11 -O1 -g -ffp-contract=off -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-omit-frame-pointer
SAN_OUT := $(OUT)/sanitize

TEST_BIN := $(OUT)/test_train_config
TEST_CHAIN_BIN := $(OUT)/test_chain_reward
TEST_CAP_BIN := $(OUT)/test_capture_cont
TEST_MOB_BIN := $(OUT)/test_mob_snapshot
TEST_SPINE_BIN := $(OUT)/test_entity_spine
TEST_FALL_BIN := $(OUT)/test_falling
TEST_WEATHER_BIN := $(OUT)/test_weather
TEST_CHEST_BIN := $(OUT)/test_chests
TEST_FURNACE_BIN := $(OUT)/test_furnaces
TEST_PROJ_BIN := $(OUT)/test_projectiles
TEST_EXPL_BIN := $(OUT)/test_explosions
TEST_RANDTICK_BIN := $(OUT)/test_randtick
TEST_BIOME_BIN := $(OUT)/test_biome_plane
TEST_MOBS_BIN := $(OUT)/test_mobs
TEST_MOBS_SS_BIN := $(OUT)/test_mobs_ss
TEST_MOBS_END_BIN := $(OUT)/test_mobs_end
TEST_MOBS_WITCH_BIN := $(OUT)/test_mobs_witch
TEST_PASSIVES_BIN := $(OUT)/test_passives
TEST_XP_BIN := $(OUT)/test_xp
TEST_GROUND_BIN := $(OUT)/test_ground_items
TEST_BOATS_BIN := $(OUT)/test_boats
TEST_ELYTRA_BIN := $(OUT)/test_elytra
TEST_HAZARDS_BIN := $(OUT)/test_hazards
TEST_PLACE_BIN := $(OUT)/test_placement
TEST_BEDS_BIN := $(OUT)/test_beds
TEST_SKY_BIN := $(OUT)/test_skylight_water
SMOKE_BIN := $(OUT)/ppo
EVAL_BIN := $(OUT)/eval
SAN_TEST_BIN := $(SAN_OUT)/test_train_config
SAN_CHAIN_BIN := $(SAN_OUT)/test_chain_reward
SAN_SMOKE_BIN := $(SAN_OUT)/ppo

ENV_CPU_SO := $(ENV_OUT)/blaze_cpu.so

TRAIN_CFG_OBJ := $(OUT)/train_config.o
CHAIN_OBJS := $(OUT)/chain_reward.o $(OUT)/chain_curr.o

.PHONY: all test-config test-chain test-capture test-entity-spine \
	test-falling test-weather test-chests test-furnaces test-projectiles test-explosions test-randtick test-biome-plane test-mobs test-mobs-ss test-mobs-end test-mobs-witch test-passives test-hazards test-ground-items test-placement test-beds test-skylight-water smoke smoke-cpu smoke-cuda \
	smoke-metal smoke-reset test sanitize clean env-cpu env-cuda \
	env-metal eval

all: test-config smoke

$(OUT) $(SAN_OUT) $(ENV_OUT):
	mkdir -p $@

# ---- policy objects (platform backend) ----
ifeq ($(UNAME_S),Darwin)
$(NN_OUT)/nn.o $(NN_OUT)/cpu.o $(NN_OUT)/fixture.o $(NN_OUT)/metal.o:
	$(MAKE) -C $(NN_SRC) metal
else
$(NN_OUT)/nn.o $(NN_OUT)/cpu.o $(NN_OUT)/fixture.o $(NN_OUT)/cuda.o:
	$(MAKE) -C $(NN_SRC) cuda BLAZE_SM=$(BLAZE_SM) CUDA_HOME=$(CUDA_HOME)
endif

$(OUT)/train_config.o: train_config.c train_config.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -c -o $@ train_config.c

$(OUT)/chain_reward.o: chain_reward.c chain_reward.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -c -o $@ chain_reward.c

$(OUT)/chain_curr.o: chain_curr.c chain_curr.h chain_reward.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -c -o $@ chain_curr.c

# ---- Linux CUDA staging ----
ifneq ($(UNAME_S),Darwin)
$(OUT)/env_cuda_stage.o: env_cuda_stage.c env_cuda_stage.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -c -o $@ env_cuda_stage.c
endif

# ---- Darwin Metal observation (metallib + host under out/blaze/env/) ----
ifeq ($(UNAME_S),Darwin)
$(METAL_AIR): $(ENV_SRC)/blaze_metal_obs.metal | $(ENV_OUT)
	xcrun -sdk macosx metal -fno-fast-math -ffp-contract=off \
		-c $(ENV_SRC)/blaze_metal_obs.metal -o $@

$(METAL_LIB): $(METAL_AIR)
	xcrun -sdk macosx metallib $(METAL_AIR) -o $@

$(METAL_HOST_OBJ): $(ENV_SRC)/blaze_metal_obs_host.m $(ENV_SRC)/blaze_metal_obs.h \
		| $(ENV_OUT)
	$(METAL_CC) $(METAL_HOST_OBJCFLAGS) -c -o $@ \
		$(ENV_SRC)/blaze_metal_obs_host.m

$(OUT)/env_metal_obs.o: env_metal_obs.m env_metal_obs.h \
		$(ENV_SRC)/blaze_metal_obs.h | $(OUT)
	$(METAL_CC) $(METAL_OBJCFLAGS) $(CPPFLAGS) -c -o $@ env_metal_obs.m
endif

# Shared env drivers under out/blaze/env/ (same symbols; dlopen selects).
env-cpu: $(ENV_CPU_SO)
ifeq ($(UNAME_S),Darwin)
env-metal: $(METAL_LIB)
env-cuda:
	@echo "env-cuda: CUDA backend is Linux-only" >&2; exit 1
else
env-cuda: $(ENV_CUDA_SO)
env-metal:
	@echo "env-metal: Metal backend is Darwin-only" >&2; exit 1
endif

$(ENV_CPU_SO): $(ENV_SRC)/blaze_cpu.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_core.h $(ENV_SRC)/blaze_abi.h \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/entity_spine.h \
		$(CORE_SRC)/falling_live.h $(CORE_SRC)/explosion_live.h \
		$(CORE_SRC)/explosion.h | $(ENV_OUT)
	$(CC) $(ENV_CFLAGS) $(OMP_CFLAGS) -I$(ENV_SRC) -I$(CORE_SRC) -shared \
		-o $@ $(ENV_SRC)/blaze_cpu.c $(ENV_SRC)/blaze_snapshot.c \
		-lm $(OMP_LDFLAGS)

ifneq ($(UNAME_S),Darwin)
$(ENV_CUDA_SO): $(ENV_SRC)/blaze_cuda.cu $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_core.h $(ENV_SRC)/blaze_abi.h \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/entity_spine.h \
		$(CORE_SRC)/falling_live.h $(CORE_SRC)/explosion_live.h \
		$(CORE_SRC)/explosion.h | $(ENV_OUT)
	$(NVCC) $(NVCCFLAGS) --shared \
		-o $@ $(ENV_SRC)/blaze_cuda.cu $(ENV_SRC)/blaze_snapshot.c \
		-lcudart

TEST_SPINE_FP_BIN := $(OUT)/test_entity_spine_fp
$(TEST_SPINE_FP_BIN): $(ENV_SRC)/test_entity_spine_fp.cu \
		$(CORE_SRC)/entity_spine.h $(CORE_SRC)/mc_math.h | $(OUT)
	$(NVCC) $(NVCCFLAGS) -o $@ $(ENV_SRC)/test_entity_spine_fp.cu -lcudart

.PHONY: test-entity-spine-fp
test-entity-spine-fp: $(TEST_SPINE_FP_BIN)
	cd $(REPO) && ./out/blaze/rl/test_entity_spine_fp
else
test-entity-spine-fp:
	@echo "test-entity-spine-fp: CUDA is Linux-only" >&2; exit 1
endif

$(TEST_BIN): test_train_config.c $(TRAIN_CFG_OBJ) | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ test_train_config.c $(TRAIN_CFG_OBJ)

$(TEST_CHAIN_BIN): test_chain_reward.c $(CHAIN_OBJS) | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ test_chain_reward.c $(CHAIN_OBJS)

$(TEST_CAP_BIN): test_capture_cont.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(ENV_CPU_SO) | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ test_capture_cont.c \
		$(ENV_SRC)/blaze_snapshot.c -ldl -lm

$(TEST_MOB_BIN): $(ENV_SRC)/test_mob_snapshot.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_mob_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_SPINE_BIN): $(ENV_SRC)/test_entity_spine.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/entity_spine.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_entity_spine.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_FALL_BIN): $(ENV_SRC)/test_falling.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/falling_live.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_falling.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_WEATHER_BIN): $(ENV_SRC)/test_weather.c $(CORE_SRC)/world_weather.h | $(OUT)
	$(CC) $(ENV_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_weather.c -lm
$(TEST_CHEST_BIN): $(ENV_SRC)/test_chests.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/tile_entity_chest.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_chests.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_FURNACE_BIN): $(ENV_SRC)/test_furnaces.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/smelting_recipes.h \
		$(CORE_SRC)/furnace_full_tick.h | $(OUT)
	mkdir -p $(REPO)/out/verify
	uv run --no-project python $(REPO)/verify/furnace_registry.py --c-header \
	    > $(REPO)/out/verify/furnace_registry_expect.h
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_furnaces.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_PROJ_BIN): $(ENV_SRC)/test_projectiles.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/projectile_live.h \
		$(CORE_SRC)/inventory_stack_rules.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_projectiles.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_EXPL_BIN): $(ENV_SRC)/test_explosions.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/explosion_live.h \
		$(CORE_SRC)/explosion.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_explosions.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_RANDTICK_BIN): $(ENV_SRC)/test_randtick.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/randtick_live.h | $(OUT)
	$(CC) $(ENV_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_randtick.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_BIOME_BIN): $(ENV_SRC)/test_biome_plane.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/randtick_live.h \
		$(CORE_SRC)/hostile_spawn.h $(CORE_SRC)/genlayer_biomes.h | $(OUT)
	$(CC) $(ENV_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_biome_plane.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_MOBS_BIN): $(ENV_SRC)/test_mobs.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/hostile_live.h \
		$(CORE_SRC)/hostile_spawn.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_mobs.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_MOBS_SS_BIN): $(ENV_SRC)/test_mobs_ss.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/hostile_live.h \
		$(CORE_SRC)/hostile_spawn.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_mobs_ss.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_MOBS_END_BIN): $(ENV_SRC)/test_mobs_end.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/hostile_live.h \
		$(CORE_SRC)/hostile_spawn.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_mobs_end.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_MOBS_WITCH_BIN): $(ENV_SRC)/test_mobs_witch.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/hostile_live.h \
		$(CORE_SRC)/hostile_spawn.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_mobs_witch.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_PASSIVES_BIN): $(ENV_SRC)/test_passives.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/passive_live.h \
		$(CORE_SRC)/hostile_spawn.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_passives.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_XP_BIN): $(ENV_SRC)/test_xp.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/xp_live.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_xp.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_GROUND_BIN): $(ENV_SRC)/test_ground_items.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/entity_item.h \
		$(CORE_SRC)/item_live.h $(CORE_SRC)/item_overflow.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_ground_items.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_BOATS_BIN): $(ENV_SRC)/test_boats.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/boat_live.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_boats.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_ELYTRA_BIN): $(ENV_SRC)/test_elytra.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/elytra_live.h | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_elytra.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_HAZARDS_BIN): $(ENV_SRC)/test_hazards.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/player_survival.h | $(OUT)
	$(CC) $(ENV_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_hazards.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_PLACE_BIN): $(ENV_SRC)/test_placement.c $(ENV_SRC)/blaze_snapshot.c \
		$(ENV_SRC)/blaze_snapshot.h $(CORE_SRC)/block_may_place.h \
		$(CORE_SRC)/crafting_recipes_full.h $(CORE_SRC)/crafting_remaining.h | $(OUT)
	$(CC) $(ENV_CFLAGS) $(CPPFLAGS) -o $@ $(ENV_SRC)/test_placement.c \
		$(ENV_SRC)/blaze_snapshot.c -lm

$(TEST_BEDS_BIN): $(REPO)/magma/tests/test_beds.c $(CORE_SRC)/player_bed.h | $(OUT)
	$(CC) $(ENV_CFLAGS) $(CPPFLAGS) -o $@ $(REPO)/magma/tests/test_beds.c -lm

# Magma light.o graph matches magma/Makefile test-water-skylight.
MAGMA := $(REPO)/magma
SKY_MAGMA_OBJS := $(MAGMA)/world/mesh_mc.o $(MAGMA)/world/light.o \
	$(MAGMA)/world/populate_mc.o $(MAGMA)/world/gen_prefetch.o \
	$(MAGMA)/assets/blockmodels.o \
	$(MAGMA)/renderkernels/rk_31_facebakery_make_quad.o \
	$(MAGMA)/core/math.o $(MAGMA)/core/shade.o $(MAGMA)/core/config.o \
	$(MAGMA)/game/caps.o
$(TEST_SKY_BIN): $(ENV_SRC)/test_skylight_water.c \
		$(ENV_SRC)/test_skylight_magma_oracle.c \
		$(ENV_SRC)/blaze_core.h | $(OUT)
	$(MAKE) -C $(MAGMA) world/light.o world/mesh_mc.o world/populate_mc.o \
	    world/gen_prefetch.o assets/blockmodels.o \
	    renderkernels/rk_31_facebakery_make_quad.o \
	    core/math.o core/shade.o core/config.o game/caps.o
	$(CC) $(ENV_CFLAGS) -c -o $(OUT)/test_skylight_water.o \
	    $(CPPFLAGS) $(ENV_SRC)/test_skylight_water.c
	$(CC) $(ENV_CFLAGS) -c -o $(OUT)/test_skylight_magma_oracle.o \
	    -I$(MAGMA) -I$(MAGMA)/core -I$(CORE_SRC) \
	    $(ENV_SRC)/test_skylight_magma_oracle.c
	$(CC) $(ENV_CFLAGS) -o $@ $(OUT)/test_skylight_water.o \
	    $(OUT)/test_skylight_magma_oracle.o $(SKY_MAGMA_OBJS) -lm

ifeq ($(UNAME_S),Darwin)
$(SMOKE_BIN): ppo.c obs_pack.h rl_ckpt.h $(TRAIN_CFG_OBJ) $(CHAIN_OBJS) \
		$(METAL_OBS_OBJ) $(METAL_HOST_OBJ) $(NN_OBJS) $(ENV_CPU_SO) \
		$(METAL_LIB) | $(OUT)
	$(METAL_CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ ppo.c $(TRAIN_CFG_OBJ) \
		$(CHAIN_OBJS) $(METAL_OBS_OBJ) $(METAL_HOST_OBJ) $(NN_OBJS) \
		$(LINK_LIBS) $(LDLIBS)

$(EVAL_BIN): eval.c obs_pack.h rl_ckpt.h $(CHAIN_OBJS) $(METAL_OBS_OBJ) \
		$(METAL_HOST_OBJ) $(NN_OBJS) $(ENV_CPU_SO) $(METAL_LIB) | $(OUT)
	$(METAL_CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ eval.c $(CHAIN_OBJS) \
		$(METAL_OBS_OBJ) $(METAL_HOST_OBJ) $(NN_OBJS) \
		$(LINK_LIBS) $(LDLIBS)
else
$(SMOKE_BIN): ppo.c obs_pack.h rl_ckpt.h $(TRAIN_CFG_OBJ) $(CHAIN_OBJS) \
		$(STAGE_OBJ) $(NN_OBJS) $(ENV_CPU_SO) $(ENV_CUDA_SO) | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ ppo.c $(TRAIN_CFG_OBJ) $(CHAIN_OBJS) \
		$(STAGE_OBJ) $(NN_OBJS) $(LINK_LIBS)

$(EVAL_BIN): eval.c obs_pack.h rl_ckpt.h $(CHAIN_OBJS) $(STAGE_OBJ) $(NN_OBJS) \
		$(ENV_CPU_SO) $(ENV_CUDA_SO) | $(OUT)
	$(CC) $(OWN_CFLAGS) $(CPPFLAGS) -o $@ eval.c $(CHAIN_OBJS) $(STAGE_OBJ) \
		$(NN_OBJS) $(LINK_LIBS)
endif

test-config: $(TEST_BIN)
	cd $(CURDIR) && $(TEST_BIN)

test-chain: $(TEST_CHAIN_BIN)
	cd $(CURDIR) && $(TEST_CHAIN_BIN)

test-capture: $(TEST_CAP_BIN) $(TEST_MOB_BIN) $(TEST_SPINE_BIN) $(TEST_FALL_BIN) \
		$(TEST_WEATHER_BIN) $(TEST_CHEST_BIN) $(TEST_FURNACE_BIN) $(TEST_PROJ_BIN) $(TEST_EXPL_BIN) \
		$(TEST_RANDTICK_BIN) $(TEST_BIOME_BIN) \
		$(TEST_MOBS_BIN) $(TEST_MOBS_SS_BIN) $(TEST_MOBS_END_BIN) $(TEST_MOBS_WITCH_BIN) $(TEST_PASSIVES_BIN) $(TEST_XP_BIN) $(TEST_GROUND_BIN) $(TEST_BOATS_BIN) $(TEST_ELYTRA_BIN) \
		$(TEST_HAZARDS_BIN) \
		$(TEST_PLACE_BIN) \
		$(TEST_BEDS_BIN) \
		$(TEST_SKY_BIN)
	cd $(REPO) && ./out/blaze/rl/test_capture_cont
	cd $(REPO) && ./out/blaze/rl/test_mob_snapshot
	cd $(REPO) && ./out/blaze/rl/test_entity_spine
	cd $(REPO) && ./out/blaze/rl/test_falling
	cd $(REPO) && ./out/blaze/rl/test_weather
	cd $(REPO) && ./out/blaze/rl/test_chests
	cd $(REPO) && ./out/blaze/rl/test_furnaces
	cd $(REPO) && ./out/blaze/rl/test_projectiles
	cd $(REPO) && ./out/blaze/rl/test_explosions
	cd $(REPO) && ./out/blaze/rl/test_randtick
	cd $(REPO) && ./out/blaze/rl/test_biome_plane
	cd $(REPO) && ./out/blaze/rl/test_mobs
	cd $(REPO) && ./out/blaze/rl/test_mobs_ss
	cd $(REPO) && ./out/blaze/rl/test_mobs_end
	cd $(REPO) && ./out/blaze/rl/test_mobs_witch
	cd $(REPO) && ./out/blaze/rl/test_passives
	cd $(REPO) && ./out/blaze/rl/test_xp
	cd $(REPO) && ./out/blaze/rl/test_ground_items
	cd $(REPO) && ./out/blaze/rl/test_boats
	cd $(REPO) && ./out/blaze/rl/test_elytra
	cd $(REPO) && ./out/blaze/rl/test_hazards
	cd $(REPO) && ./out/blaze/rl/test_placement
	cd $(REPO) && ./out/blaze/rl/test_beds
	cd $(REPO) && ./out/blaze/rl/test_skylight_water

test-hazards: $(TEST_HAZARDS_BIN)
	cd $(REPO) && ./out/blaze/rl/test_hazards

test-ground-items: $(TEST_GROUND_BIN)
	cd $(REPO) && ./out/blaze/rl/test_ground_items

test-placement: $(TEST_PLACE_BIN)
	cd $(REPO) && ./out/blaze/rl/test_placement

test-beds: $(TEST_BEDS_BIN)
	cd $(REPO) && ./out/blaze/rl/test_beds

test-entity-spine: $(TEST_SPINE_BIN)
	cd $(REPO) && ./out/blaze/rl/test_entity_spine

test-falling: $(TEST_FALL_BIN)
	cd $(REPO) && ./out/blaze/rl/test_falling

test-weather: $(TEST_WEATHER_BIN)
	cd $(REPO) && ./out/blaze/rl/test_weather
test-chests: $(TEST_CHEST_BIN)
	cd $(REPO) && ./out/blaze/rl/test_chests

test-furnaces: $(TEST_FURNACE_BIN)
	cd $(REPO) && ./out/blaze/rl/test_furnaces

test-projectiles: $(TEST_PROJ_BIN)
	cd $(REPO) && ./out/blaze/rl/test_projectiles

test-explosions: $(TEST_EXPL_BIN)
	cd $(REPO) && ./out/blaze/rl/test_explosions

test-randtick: $(TEST_RANDTICK_BIN)
	cd $(REPO) && ./out/blaze/rl/test_randtick

test-biome-plane: $(TEST_BIOME_BIN)
	cd $(REPO) && ./out/blaze/rl/test_biome_plane

test-mobs: $(TEST_MOBS_BIN)
	cd $(REPO) && ./out/blaze/rl/test_mobs

test-mobs-ss: $(TEST_MOBS_SS_BIN)
	cd $(REPO) && ./out/blaze/rl/test_mobs_ss

test-mobs-end: $(TEST_MOBS_END_BIN)
	cd $(REPO) && ./out/blaze/rl/test_mobs_end

test-mobs-witch: $(TEST_MOBS_WITCH_BIN)
	cd $(REPO) && ./out/blaze/rl/test_mobs_witch

test-passives: $(TEST_PASSIVES_BIN)
	cd $(REPO) && ./out/blaze/rl/test_passives

test-skylight-water: $(TEST_SKY_BIN)
	cd $(REPO) && ./out/blaze/rl/test_skylight_water

# Default smoke: committed ppo.conf (backend=cpu).
smoke: smoke-cpu

smoke-cpu: $(SMOKE_BIN)
	cd $(REPO) && ./out/blaze/rl/ppo --conf blaze/rl/$(CONF)

ifeq ($(UNAME_S),Darwin)
smoke-metal: $(SMOKE_BIN)
	cd $(REPO) && ./out/blaze/rl/ppo --conf blaze/rl/$(CONF) \
		--set backend=metal --set device=0

smoke-cuda:
	@echo "smoke-cuda: CUDA backend is Linux-only" >&2; exit 1
else
smoke-cuda: $(SMOKE_BIN)
	cd $(REPO) && ./out/blaze/rl/ppo --conf blaze/rl/$(CONF) \
		--set backend=cuda --set device=0

smoke-metal:
	@echo "smoke-metal: Metal backend is Darwin-only" >&2; exit 1
endif

# CPU: two chunks + short horizon so truncation/reset/burn-in fire.
# Not on `make test` (default test stays test-config + test-chain + smoke-cpu).
smoke-reset: $(SMOKE_BIN)
	cd $(REPO) && ./out/blaze/rl/ppo --conf blaze/rl/$(CONF) \
		--set max_chunks=2 --set ep_dec=3
	cd $(REPO) && ./out/blaze/rl/ppo --conf blaze/rl/$(CONF) \
		--set max_chunks=2 --set ep_dec=3 --set mb=3

# Native 13-seed transfer eval. Not part of `test` (minutes, needs a ckpt
# and t0 snaps). CUDA/Metal slot via --backend; default is CPU.
eval: $(EVAL_BIN)

test: test-config test-chain test-capture smoke

# Sanitizer build of owned sources; env via dlopen of the same .so files.
$(SAN_OUT)/train_config.o: train_config.c train_config.h | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -c -o $@ train_config.c

$(SAN_OUT)/chain_reward.o: chain_reward.c chain_reward.h | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -c -o $@ chain_reward.c

$(SAN_OUT)/chain_curr.o: chain_curr.c chain_curr.h chain_reward.h | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -c -o $@ chain_curr.c

ifeq ($(UNAME_S),Darwin)
$(SAN_OUT)/env_metal_obs.o: env_metal_obs.m env_metal_obs.h | $(SAN_OUT)
	$(METAL_CC) -fobjc-arc -O1 -g -ffp-contract=off -Wall -Wextra \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CPPFLAGS) -c -o $@ env_metal_obs.m

$(SAN_TEST_BIN): test_train_config.c $(SAN_OUT)/train_config.o | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -o $@ test_train_config.c \
		$(SAN_OUT)/train_config.o -fsanitize=address,undefined

$(SAN_CHAIN_BIN): test_chain_reward.c $(SAN_OUT)/chain_reward.o \
		$(SAN_OUT)/chain_curr.o | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -o $@ test_chain_reward.c \
		$(SAN_OUT)/chain_reward.o $(SAN_OUT)/chain_curr.o \
		-fsanitize=address,undefined

$(SAN_SMOKE_BIN): ppo.c $(SAN_OUT)/train_config.o $(SAN_OUT)/chain_reward.o \
		$(SAN_OUT)/chain_curr.o $(SAN_OUT)/env_metal_obs.o \
		$(METAL_HOST_OBJ) $(NN_OBJS) $(ENV_CPU_SO) $(METAL_LIB) | $(SAN_OUT)
	$(METAL_CC) $(SAN_CFLAGS) $(CPPFLAGS) -o $@ ppo.c \
		$(SAN_OUT)/train_config.o $(SAN_OUT)/chain_reward.o \
		$(SAN_OUT)/chain_curr.o $(SAN_OUT)/env_metal_obs.o \
		$(METAL_HOST_OBJ) $(NN_OBJS) $(LINK_LIBS) $(LDLIBS) \
		-fsanitize=address,undefined
else
$(SAN_OUT)/env_cuda_stage.o: env_cuda_stage.c env_cuda_stage.h | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -c -o $@ env_cuda_stage.c

$(SAN_TEST_BIN): test_train_config.c $(SAN_OUT)/train_config.o | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -o $@ test_train_config.c \
		$(SAN_OUT)/train_config.o -fsanitize=address,undefined

$(SAN_CHAIN_BIN): test_chain_reward.c $(SAN_OUT)/chain_reward.o \
		$(SAN_OUT)/chain_curr.o | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -o $@ test_chain_reward.c \
		$(SAN_OUT)/chain_reward.o $(SAN_OUT)/chain_curr.o \
		-fsanitize=address,undefined

$(SAN_SMOKE_BIN): ppo.c $(SAN_OUT)/train_config.o $(SAN_OUT)/chain_reward.o \
		$(SAN_OUT)/chain_curr.o $(SAN_OUT)/env_cuda_stage.o \
		$(NN_OBJS) $(ENV_CPU_SO) | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -o $@ ppo.c $(SAN_OUT)/train_config.o \
		$(SAN_OUT)/chain_reward.o $(SAN_OUT)/chain_curr.o \
		$(SAN_OUT)/env_cuda_stage.o $(NN_OBJS) $(LINK_LIBS) \
		-fsanitize=address,undefined
endif

sanitize: $(SAN_TEST_BIN) $(SAN_CHAIN_BIN) $(SAN_SMOKE_BIN)
	cd $(CURDIR) && $(SAN_TEST_BIN)
	cd $(CURDIR) && $(SAN_CHAIN_BIN)
	cd $(REPO) && ./out/blaze/rl/sanitize/ppo --conf blaze/rl/$(CONF)

# Only products this Makefile owns under out/blaze/rl and out/blaze/env.
# Leaves out/blaze/nn untouched.
clean:
	rm -rf $(OUT)
	rm -f $(ENV_CPU_SO)
ifneq ($(UNAME_S),Darwin)
	rm -f $(ENV_CUDA_SO)
endif
ifeq ($(UNAME_S),Darwin)
	rm -f $(METAL_AIR) $(METAL_LIB) $(METAL_HOST_OBJ)
	rm -f $(ENV_OUT)/blaze_metal_obs.so
endif
	rmdir $(ENV_OUT) 2>/dev/null || true
