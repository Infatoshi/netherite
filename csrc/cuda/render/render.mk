# The device renderer (cuda/render: render.h, host.cu) and its gate
# (check.c; gate.sh drives it over the frame judges'
# recordings). Included by csrc/Makefile; needs nvcc, so it stays out of
# `all`, `test` and clangcheck:
#   make -C csrc gpu-render           out/native/cuda/render_check
#   make -C csrc gpu-render-check     the gate: every frame judge's shots at
#                                       the observation (128x128, F1, render
#                                       distance 4), RENDER_BATCH recordings
#                                       per device render, at RENDER_PREC
#                                       (exact; fast: engine/raster_prec.h)
#   make -C csrc gpu-render-bench     frames per second by batch size
RENDER_DIR   := cuda/render
RENDER_BIN   := $(OUT)/cuda/render_check
RENDER_BATCH ?= 8
RENDER_SIZE  ?= 128x128
RENDER_ARGS  ?=
# the renderer's precision (engine/raster_prec.h): exact, fast or fast:STAGE,...
RENDER_PREC  ?= exact
# each recording's dumps are kept (gate.sh --store; lane/cudasplit)
# within RSTORE_MB, and drawn again on the device alone; FRESH=1 dumps
# every recording again (the nightly)
RSTORE       ?= $(HOME)/.cache/netherite-devcap/render
RSTORE_MB    ?= 3000
RENDER_STORE  = --store $(RSTORE) --store-mb $(RSTORE_MB) $(if $(filter 1,$(FRESH)),--fresh)

.PHONY: gpu-render gpu-render-check gpu-render-bench
gpu-render: $(RENDER_BIN)
# The renderer's units (lane/cudasplit): host.cu the host side, the
# geometry's kernels by family (lane/renderbuild: geom_quads.cu the section
# quads' lean kernel, geom_equads.cu the entity quads', geom_gen.cu the
# generic kernel with its path for the sky and the clipped section quads,
# geom_rec.cu its path for the recorded passes, geom.cuh what
# they share), raster.cu the binning and the pixels, sort.cu
# cub's scans and sorts, dev.cuh what they all share; each compiled
# apart (-rdc, at NVOPT, through ccache) so an edit
# rebuilds its unit only, then device-linked into cuda_render.o, one
# relocatable object any linker takes (runtime/pipeline.mk links it with
# cc). The device units are position independent and hidden, so the
# judges' plugin (framedev.cu, judge.mk) links the same objects, the host side's too
# (host.cu is compiled once; lane/renderbuild).
RENDER_GEOM := $(patsubst %,$(OUT)/obj/cuda_render_%.o,geom_quads geom_equads geom_gen geom_rec)
RENDER_DEV := $(RENDER_GEOM) $(patsubst %,$(OUT)/obj/cuda_render_%.o,raster sort)
# raster.cu compiles whole: split (NVSPLIT), pixel() is left out of line at
# -O1 with 82 registers and nvlink refuses it from raster's 64 (lane/splitcompile)
$(OUT)/obj/cuda_render_raster.o: NVSPLITN = 1
RENDER_HDR := $(RENDER_DIR)/dev.cuh $(RENDER_DIR)/render.h engine/raster_obs.h engine/meshfeed.h cuda/meshing/meshing.h $(NVSTAMP)
$(RENDER_GEOM): $(RENDER_DIR)/geom.cuh
$(RENDER_DEV): $(OUT)/obj/cuda_render_%.o: $(RENDER_DIR)/%.cu $(RENDER_HDR)
	@mkdir -p $(dir $@)
	$(NVJOB) $(NVDEV) -rdc=true -Xcompiler -fPIC -Xcompiler -fvisibility=hidden -c -o $@ $<
$(OUT)/obj/cuda_render_host.o: $(RENDER_DIR)/host.cu $(RENDER_HDR)
	@mkdir -p $(dir $@)
	$(NVJOB) $(NVDEV) -rdc=true -Xcompiler -fPIC -Xcompiler -fvisibility=hidden -c -o $@ $<
$(OUT)/obj/cuda_render_dlink.o: $(OUT)/obj/cuda_render_host.o $(RENDER_DEV)
	$(NVCC) -arch=$(CUDA_ARCH) -dlink -o $@ $^
# with the device mesher it runs (cuda/meshing: host.c and the cubin meshing.mk
# builds, embedded)
$(OUT)/obj/cuda_render.o: $(OUT)/obj/cuda_render_host.o $(RENDER_DEV) $(OUT)/obj/cuda_render_dlink.o $(OUT)/obj/cuda_meshing_host.o $(OUT)/obj/cuda_meshing_cubin.o \
  $(MESH_TAB_OBJ)
	ld -r -o $@ $^
RENDER_OBJS := $(OUT)/obj/cuda_render_host.o $(RENDER_DEV) $(OUT)/obj/cuda_meshing_host.o $(OUT)/obj/cuda_meshing_cubin.o $(MESH_TAB_OBJ)
$(RENDER_BIN): $(RENDER_DIR)/check.c $(RENDER_DIR)/render.h $(RENDER_OBJS) $(LIB)
	@mkdir -p $(dir $@)
	$(CCC) $(filter-out -include %envheap.h,$(CFLAGS)) -c -o $(OUT)/obj/cuda_render_check.o $<
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/cuda_render_check.o $(RENDER_OBJS) $(LIB) -lcuda -lz -lm -lpthread
gpu-render-check: $(RENDER_BIN) $(OUT)/play $(OUT)/play-dev
	CUDA_VISIBLE_DEVICES=$(CUDA_DEV) bash $(RENDER_DIR)/gate.sh $(RENDER_STORE) --size $(RENDER_SIZE) --batch $(RENDER_BATCH) --prec $(RENDER_PREC) $(RENDER_ARGS)
gpu-render-bench: $(RENDER_BIN) $(OUT)/play $(OUT)/play-dev
	CUDA_VISIBLE_DEVICES=$(CUDA_DEV) bash $(RENDER_DIR)/gate.sh --size $(RENDER_SIZE) --bench $(RENDER_ARGS)
