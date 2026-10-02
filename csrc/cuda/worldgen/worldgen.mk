# Chunk generation on the device (cuda/worldgen): the Overworld's stages
# (seeds, layers, density, surface, carve; host.cu launches them), the
# Nether's and the End's (dim_*), and generation served ahead of each step
# (ahead.c, GPU plan L15). Included by csrc/Makefile; needs nvcc:
#   make -C csrc gpu-worldgen          every build below
#   make -C csrc gpu-worldgen-check    the three checks: the Overworld
#                                        (gpu-worldgen-overworld-check), the
#                                        Nether and the End with the negative
#                                        controls (gpu-worldgen-dim-check), the
#                                        generation served ahead in every
#                                        recording (gpu-worldgen-ahead-check)
# the Overworld's units and the Nether's and the End's
OW_OBJ    := $(patsubst %,$(OUT)/obj/cuda_worldgen_%.o,host seeds layers density surface carve built)
DIM_OBJ   := $(patsubst %,$(OUT)/obj/cuda_worldgen_dim_%.o,host density surface caves)
WG_OBJ    := $(OW_OBJ) $(DIM_OBJ)
WG_TABLES := $(OUT)/obj/cuda_worldgen_tables.o
.PHONY: gpu-worldgen gpu-worldgen-check gpu-worldgen-overworld gpu-worldgen-overworld-check
gpu-worldgen: gpu-worldgen-overworld gpu-worldgen-dim gpu-worldgen-ahead gpu-worldgen-built
gpu-worldgen-check: gpu-worldgen-overworld-check gpu-worldgen-dim-check gpu-worldgen-ahead-check gpu-worldgen-built-check
$(WG_OBJ): $(OUT)/obj/cuda_worldgen_%.o: cuda/worldgen/%.cu cuda/worldgen/dev.cuh cuda/worldgen/carve.cuh cuda/worldgen/worldgen.h $(NVSTAMP)
	@mkdir -p $(OUT)/obj
	$(NVJOB) $(NVFLAGS) -c -o $@ $<
$(WG_TABLES): cuda/worldgen/tables.c cuda/worldgen/worldgen.h engine/biomes.h
	@mkdir -p $(OUT)/obj
	$(CCC) $(CFLAGS) -c -o $@ $<

# worldgen_check (cuda/worldgen/check.c): the Overworld's kernels against
# provide_chunk, stage by stage, over every chunk dump and the Overworld
# seed-world spawn areas
gpu-worldgen-overworld: $(OUT)/cuda/worldgen_check
$(OUT)/cuda/worldgen_check: cuda/worldgen/check.c cuda/worldgen/ref.h cuda/worldgen/worldgen.h $(OW_OBJ) $(WG_TABLES) $(LIB)
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -c -o $(OUT)/obj/cuda_worldgen_check.o $<
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/cuda_worldgen_check.o $(OW_OBJ) $(WG_TABLES) $(LIB) -lz -lm
# DUMPS="name ..." and SPAWNS="name ..." (out/java/chunks, out/java/seedworld;
# out/java/dims for gpu-worldgen-dim-check) narrow either check to those dumps (the
# mirror rule's quick gates, cuda/mirror.tsv)
GEN_DUMPS = $(if $(filter command line,$(origin DUMPS)),$(patsubst %,$(DATA)/$(1)/%/,$(DUMPS)),$(wildcard $(DATA)/$(1)/*/))
GEN_SPAWNS = $(foreach d,$(if $(filter command line,$(origin SPAWNS)),$(patsubst %,$(DATA)/seedworld/%/,$(SPAWNS)),$(wildcard $(DATA)/seedworld/*/)),--spawn $(d))
# C's side of either check (its stage results, as hashes, and the seed
# worlds' spawn lists) is stored in GENSTORE under the engine library's and
# the check's hash (cuda/worldgen/ref.h): a repeated check runs C only for a chunk
# the device gets wrong. FRESH=1 runs C for every chunk.
GENSTORE ?= $(HOME)/.cache/netherite-devcap/gen
GEN_STORE = $(if $(filter 1,$(FRESH)),,--store $(GENSTORE) $$(mkdir -p $(GENSTORE); cat $(LIB) $(1) cuda/worldgen/ref.h | sha1sum | cut -c1-16))
gpu-worldgen-overworld-check: $(OUT)/cuda/worldgen_check
	CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(OUT)/cuda/worldgen_check $(CUDA_ARGS) $(call GEN_STORE,cuda/worldgen/check.c) $(call GEN_DUMPS,chunks) $(GEN_SPAWNS)

# worldgen_built_check (cuda/worldgen/built_check.c): the built stage (built.cu:
# the constructor's bands and the sky map, what the env pool's device
# generation serves) against chunkgen_construct over the device's raw arrays,
# chunk by chunk, over every chunk dump (DUMPS narrows it), then its negative
# control (the sky fill's start), which must fail at the sky light
.PHONY: gpu-worldgen-built gpu-worldgen-built-check
gpu-worldgen-built: $(OUT)/cuda/worldgen_built_check
$(OUT)/cuda/worldgen_built_check: cuda/worldgen/built_check.c cuda/worldgen/worldgen.h engine/genahead.h $(OW_OBJ) $(WG_TABLES) $(LIB)
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -c -o $(OUT)/obj/cuda_worldgen_built_check.o $<
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/cuda_worldgen_built_check.o $(OW_OBJ) $(WG_TABLES) $(LIB) -lz -lm
gpu-worldgen-built-check: $(OUT)/cuda/worldgen_built_check
	CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(OUT)/cuda/worldgen_built_check $(CUDA_ARGS) $(call GEN_DUMPS,chunks)
	@o=$$(CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(OUT)/cuda/worldgen_built_check --negative $(firstword $(call GEN_DUMPS,chunks))); \
	  rc=$$?; echo "$$o" | grep NEGATIVE; [ $$rc = 0 ]

# the generators as one relocatable object any linker takes (the kernels,
# their tables, ahead.c's batch generator; device-linked): the env pool's
# device generation (runtime: pipe_bench, pipe_gate, pool_gate_cuda)
$(OUT)/obj/cuda_worldgen_ahead.o: cuda/worldgen/ahead.c cuda/worldgen/worldgen.h engine/genahead.h
	@mkdir -p $(OUT)/obj
	$(CCC) $(CFLAGS) -c -o $@ $<
$(OUT)/obj/cuda_worldgen.o: $(WG_OBJ) $(WG_TABLES) $(OUT)/obj/cuda_worldgen_ahead.o
	$(NVCC) -arch=$(CUDA_ARCH) -dlink -o $(OUT)/obj/cuda_worldgen_dlink.o $(WG_OBJ)
	ld -r -o $@ $^ $(OUT)/obj/cuda_worldgen_dlink.o

# worldgen_bench (cuda/worldgen/bench.c): the generators' device time per
# batch of N fresh chunks and per stage, and C's raw generation on one host
# thread beside it. `make -C csrc gpu-worldgen-bench [GENBENCH_ARGS="--reps 5 1024"]`
GENBENCH_ARGS ?= --reps 5 64 256 1024
.PHONY: gpu-worldgen-bench
gpu-worldgen-bench: $(OUT)/cuda/worldgen_bench
	CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(OUT)/cuda/worldgen_bench $(GENBENCH_ARGS)
$(OUT)/cuda/worldgen_bench: cuda/worldgen/bench.c cuda/worldgen/worldgen.h $(WG_OBJ) $(WG_TABLES) $(LIB)
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -I$(dir $(NVCC))../include -c -o $(OUT)/obj/cuda_worldgen_bench.o $<
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/cuda_worldgen_bench.o $(WG_OBJ) $(WG_TABLES) $(LIB) -lz -lm

# worldgen_dim_check (cuda/worldgen/dim_check.c): the Nether and End kernels
# against nether.c, end.c and world.c's dimension chunk, stage by stage, over
# every dimension dump, the Nether and End seed-world spawn areas and the End
# platform area; then each negative control over the dimension dumps, which
# must be caught at its own stage.
DIM_NEGATIVES ?= field terrain surface caves construct
.PHONY: gpu-worldgen-dim gpu-worldgen-dim-check
gpu-worldgen-dim: $(OUT)/cuda/worldgen_dim_check
$(OUT)/cuda/worldgen_dim_check: cuda/worldgen/dim_check.c cuda/worldgen/ref.h cuda/worldgen/worldgen.h $(WG_OBJ) $(WG_TABLES) $(LIB)
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -c -o $(OUT)/obj/cuda_worldgen_dim_check.o $<
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/cuda_worldgen_dim_check.o $(WG_OBJ) $(WG_TABLES) $(LIB) -lz -lm
gpu-worldgen-dim-check: $(OUT)/cuda/worldgen_dim_check
	CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(OUT)/cuda/worldgen_dim_check $(CUDA_ARGS) $(call GEN_STORE,cuda/worldgen/dim_check.c) --platform $(call GEN_DUMPS,dims) $(GEN_SPAWNS)
	@for n in $(DIM_NEGATIVES); do o=$$(CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(OUT)/cuda/worldgen_dim_check --negative=$$n $(call GEN_STORE,cuda/worldgen/dim_check.c) \
	  $(call GEN_DUMPS,dims)); rc=$$?; echo "$$o" | grep NEGATIVE; [ $$rc = 0 ] || exit 1; done

# Generation in the loop (GPU plan L15, engine/genahead.h): $(OUT)/cuda/worldgen/test_snapshots
# is test_snapshots with the CUDA generators linked in (cuda/worldgen/ahead.c; its
# own directory, since the input guard knows a test by its name), so
# `--gen-ahead cuda` serves every predicted chunk generation from the device
# (`--gen-ahead c` runs the same seam on the C engine, in either binary).
# `make -C csrc gpu-worldgen-ahead-check [RECS="name ..."] [GEN=cuda|c]` replays every
# snapshot recording (or RECS) with it on GPU $(CUDA_DEV), under the memory
# budget, and prints each one's result, hit rate and misses, then the totals
# (tests/genahead_check.sh).
GEN ?= cuda
GA_J ?= 8
.PHONY: gpu-worldgen-ahead gpu-worldgen-ahead-check
gpu-worldgen-ahead: $(OUT)/cuda/worldgen/test_snapshots
$(OUT)/cuda/worldgen/test_snapshots: tests/test_snapshots.c tests/input_guard.c $(OUT)/obj/cuda_worldgen_ahead.o cuda/worldgen/worldgen.h engine/genahead.h \
		$(WG_OBJ) $(WG_TABLES) $(LIB) $(DEVOBJ)
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -DNETHERITE_DEV -DNETHERITE_GPU_WORLDGEN -Dmain=gate_test_main -c -o $(OUT)/obj/test_snapshots_worldgen.o tests/test_snapshots.c
	$(CCC) $(CFLAGS) -c -o $(OUT)/obj/input_guard_worldgen.o tests/input_guard.c
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/test_snapshots_worldgen.o $(OUT)/obj/input_guard_worldgen.o \
	  $(OUT)/obj/cuda_worldgen_ahead.o $(DEVOBJ) $(WG_OBJ) $(WG_TABLES) $(LIB) -lz -lm
gpu-worldgen-ahead-check: $(if $(filter cuda,$(GEN)),$(OUT)/cuda/worldgen/test_snapshots,$(OUT)/test_snapshots)
	@CUDA_VISIBLE_DEVICES=$(CUDA_DEV) bash tests/genahead_check.sh -j $(GA_J) \
	  $(if $(filter cuda,$(GEN)),$(OUT)/cuda/worldgen/test_snapshots,$(OUT)/test_snapshots) $(GEN) \
	  $(if $(filter command line,$(origin RECS)),$(RECS),$(notdir $(patsubst %/,%,$(dir $(wildcard $(DATA)/snapshots/*/tape.jsonl)))))
