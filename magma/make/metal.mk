# make/metal.mk - Metal (macOS) verification targets for the magma raster.
#
# Include-ready fragment: the main Makefile pulls it in via `-include
# make/metal.mk` (agent-1 wiring; this file must stay self-contained and must
# not redefine main-Makefile variables, hence the ?= defaults and the METAL_
# prefix). Darwin-guarded: on any non-Darwin host the target fails fast with a
# message instead of half-building.
#
# test-raster-parity-metal mirrors the CUDA rung-1 recipe (Makefile
# `test-raster-parity`: one nvcc TU over the test + CPU raster + shade + math
# + raster_cuda.cu), swapping nvcc for clang and raster_cuda.cu for the Metal
# host layer metal/raster_metal_host.m, linked against the Metal/Foundation
# frameworks.
#
# Determinism flags: -ffp-contract=off is the clang analog of the CUDA build's
# --fmad=false (NVFLAGS_GAME) - no silent fma contraction in the host C paths,
# so the CPU reference here computes the same bits as on anvil. The .metal
# shader side (agent 2) must hold the same discipline internally.
#
# Shader artifacts: this target builds only host code. If the host layer loads
# a precompiled .metallib (or compiles metal/raster_kernels.metal at runtime
# via a path relative to magma/), that artifact/path comes from the game-metal
# build (agent 1). scripts/mac_metal_verify.sh therefore runs
# `make game-metal` BEFORE this target, and the test runs with cwd magma/.

METAL_UNAME_S := $(shell uname -s)
METAL_CC        ?= clang
METAL_CFLAGS    ?= -O2 -ffp-contract=off -I. -Icore -I../verify
# Agent 2's host layer decides ARC vs MRC; override to empty for MRC.
METAL_OBJC_ARC  ?= -fobjc-arc
METAL_FRAMEWORKS ?= -framework Metal -framework Foundation

.PHONY: test-raster-parity-metal
ifeq ($(METAL_UNAME_S),Darwin)
test-raster-parity-metal:
	$(METAL_CC) $(METAL_CFLAGS) $(METAL_OBJC_ARC) -DMAGMA_METAL \
	    tests/test_raster_parity_metal.c \
	    cpu/raster_cpu.c core/shade.c core/math.c metal/raster_metal_host.m \
	    $(METAL_FRAMEWORKS) -o tests/test_raster_parity_metal
	MAGMA_METAL_REQUIRE=1 ./tests/test_raster_parity_metal
else
test-raster-parity-metal:
	@echo "test-raster-parity-metal: requires macOS + Metal (uname -s = $(METAL_UNAME_S));" \
	     "run scripts/mac_metal_verify.sh on the MacBook" >&2; exit 1
endif
