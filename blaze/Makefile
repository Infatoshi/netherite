# blaze Makefile — hierarchical CPU/CUDA kernel build (CUTLASS-style sections).
#
#   make help
#   make cpu-chunk_provider
#   make worldgen-dims
#   make verify-cpu-tick_compose_full
#   make verify-chunk_provider          # full CPU==CUDA (~7s to compile each side)
#
# Each cpu/<name>.c -> build/obj/cpu/<name>.o -> build/bin/cpu/<name>
# Every CUDA kernel compiles standalone in seconds: heavy worldgen device functions are
# MC_NOINLINE (core/mc.h), which stops nvcc/cicc inlining the whole tree into one giant kernel.
# Iterate on CPU; run verify-* (no --cpu-only) before commit.

include make/sections.mk

CC      ?= cc
NVCC    ?= nvcc
SM      ?= sm_120
PYTHON  ?= python3

CFLAGS  := -O2 -ffp-contract=off -Wall -Icore
NVFLAGS := -arch=$(SM) -O3 --fmad=false -Icore

BUILD    := build
OBJ_CPU  := $(BUILD)/obj/cpu
BIN_CPU  := $(BUILD)/bin/cpu
BIN_CUDA := $(BUILD)/bin/cuda

$(shell mkdir -p $(OBJ_CPU) $(BIN_CPU) $(BIN_CUDA))

.SECONDARY: $(foreach k,$(ALL_KERNELS),$(OBJ_CPU)/$(k).o)

# ---- pattern rules ----
# Heavy worldgen device functions are MC_NOINLINE (core/mc.h), so every CUDA kernel compiles
# standalone in seconds. No cached-object / device-link step is needed anymore.
$(OBJ_CPU)/%.o: cpu/%.c core/*.h
	@echo "  [cpu]    $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(BIN_CPU)/%: $(OBJ_CPU)/%.o
	@echo "  [link]   $@"
	@$(CC) -o $@ $< -lm

$(BIN_CUDA)/%: cuda/%.cu core/*.h
	@echo "  [cuda]   $<"
	@$(NVCC) $(NVFLAGS) -o $@ $<

# ---- section aggregates ----
# Each section gets: build (cpu-/cuda-) and verify (verify-cpu-/verify-) a whole pipeline stage.
define SECTION_RULE
.PHONY: $(1) cpu-$(1) cuda-$(1) verify-cpu-$(1) verify-$(1)
$(1): cpu-$(1)
cpu-$(1): $(addprefix $(BIN_CPU)/,$(2))
cuda-$(1): $(addprefix $(BIN_CUDA)/,$(2))
verify-cpu-$(1): $(addprefix verify-cpu-,$(2))
verify-$(1): $(addprefix verify-,$(2))
endef

$(eval $(call SECTION_RULE,trunk,$(TRUNK)))
$(eval $(call SECTION_RULE,worldgen,$(WORLDGEN)))
$(eval $(call SECTION_RULE,worldgen-core,$(WORLDGEN_CORE)))
$(eval $(call SECTION_RULE,worldgen-surface,$(WORLDGEN_SURFACE)))
$(eval $(call SECTION_RULE,worldgen-carve,$(WORLDGEN_CARVE)))
$(eval $(call SECTION_RULE,worldgen-features,$(WORLDGEN_FEATURES)))
$(eval $(call SECTION_RULE,worldgen-structures,$(WORLDGEN_STRUCTURES)))
$(eval $(call SECTION_RULE,worldgen-dims,$(WORLDGEN_DIMS)))
$(eval $(call SECTION_RULE,blocks,$(BLOCKS)))
$(eval $(call SECTION_RULE,fluids,$(FLUIDS)))
$(eval $(call SECTION_RULE,light,$(LIGHT)))
$(eval $(call SECTION_RULE,populate,$(POPULATE)))
$(eval $(call SECTION_RULE,physics,$(PHYSICS)))
$(eval $(call SECTION_RULE,combat,$(COMBAT)))
$(eval $(call SECTION_RULE,items,$(ITEMS)))
$(eval $(call SECTION_RULE,mobs,$(MOBS)))
$(eval $(call SECTION_RULE,portals,$(PORTALS)))
$(eval $(call SECTION_RULE,tick,$(TICK)))
$(eval $(call SECTION_RULE,batch,$(BATCH)))

.PHONY: all cpu-all cuda-all
all: trunk
# GAMERULES = CPU-only tests: built by cpu-all, excluded from cuda-all / verify-*.
cpu-all: $(addprefix $(BIN_CPU)/,$(ALL_KERNELS) $(GAMERULES) $(TICKTRACE) $(ENTITYTRACE) $(ITEMTRACE))
cuda-all: $(addprefix $(BIN_CUDA)/,$(ALL_KERNELS))

.PHONY: $(addprefix cpu-,$(ALL_KERNELS) $(GAMERULES) $(TICKTRACE) $(ENTITYTRACE) $(ITEMTRACE))
$(addprefix cpu-,$(ALL_KERNELS) $(GAMERULES) $(TICKTRACE) $(ENTITYTRACE) $(ITEMTRACE)): cpu-%: $(BIN_CPU)/%

# ---- oracle verify ----
.PHONY: $(addprefix verify-cpu-,$(ALL_KERNELS)) $(addprefix verify-,$(ALL_KERNELS))
$(addprefix verify-cpu-,$(ALL_KERNELS)):
	@$(PYTHON) oracle/runner.py --cpu-only $(patsubst verify-cpu-%,%,$@)

$(addprefix verify-,$(ALL_KERNELS)):
	@$(PYTHON) oracle/runner.py $(patsubst verify-%,%,$@)

.PHONY: oracle-smoke clean help
oracle-smoke:
	@$(PYTHON) oracle/runner.py smoke 12345 256

clean:
	rm -rf $(BUILD)

help:
	@echo "blaze kernel build (CPU reference + CUDA batch)"
	@echo ""
	@echo "Sections (build CPU binaries for whole pipeline stage):"
	@echo "  trunk                 scaffold + trunk smoke"
	@echo "  worldgen              full terrain pipeline"
	@echo "    worldgen-core         noise, genlayer, terrain_shape, biome_props"
	@echo "    worldgen-surface      surface_blocks (+ real wiring)"
	@echo "    worldgen-carve        caves, ravines (+ real wiring)"
	@echo "    worldgen-features     ores, trees, lakes"
	@echo "    worldgen-structures   fortress, mineshaft, stronghold"
	@echo "    worldgen-dims         chunk_provider*, overworld/nether/end full"
	@echo "  blocks                property tables + tickers"
	@echo "  fluids                fluid_flow CA"
	@echo "  light                 light_propagation"
	@echo "  populate              decoration pass"
	@echo "  physics               collision + player + pathfinding"
	@echo "  combat                damage, enchant, projectiles"
	@echo "  items                 inventory, crafting, tile entities"
	@echo "  mobs                  AI + spawning"
	@echo "  portals               nether/end + dragon"
	@echo "  tick                  phased tick compose"
	@echo "  batch                 cuda_batch_*, py_gym, render_opt hook"
	@echo ""
	@echo "Per section (build or verify a whole stage):"
	@echo "  make cuda-worldgen-dims        compile every worldgen-dims CUDA kernel"
	@echo "  make verify-worldgen-dims      full CPU==CUDA oracle for the stage"
	@echo "  make verify-cpu-worldgen-dims  fast CPU-only oracle for the stage"
	@echo ""
	@echo "Single kernel:"
	@echo "  make cpu-chunk_provider           CPU binary"
	@echo "  make $(BIN_CUDA)/chunk_provider   CUDA binary"
	@echo "  make verify-cpu-chunk_provider    fast oracle (~seconds)"
	@echo "  make verify-chunk_provider          full CPU==CUDA (~7s to compile now)"
	@echo ""
	@echo "Other:"
	@echo "  make cpu-all              all CPU binaries"
	@echo "  make cuda-all            all CUDA binaries"
	@echo "  make oracle-smoke         smoke oracle"
	@echo ""
	@echo "Artifacts:"
	@echo "  $(BIN_CPU)/<name>"
	@echo "  $(BIN_CUDA)/<name>"

.DEFAULT_GOAL := help
