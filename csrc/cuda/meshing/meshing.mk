# The device mesher (cuda/meshing: kernels.h, kernels.c): render_blocks*.c
# compiled for the GPU as they are, by clang to nvptx64 bitcode with the
# files they reach (the block and material tables, the noise), linked with
# kernels.c's runtime and kernels, cut to what the kernels reach, then PTX
# (llc, no FMA) and a cubin (ptxas --fmad=false) that the renderer loads
# (cuda/render). Included by csrc/Makefile; needs clang 21 with NVPTX and
# CUDA, so it stays out of `all`, `test` and clangcheck:
#   make -C csrc gpu-meshing          the cubin (gpu-render links it)
#   make -C csrc gpu-meshing-check    the gate (cuda/render/gate.sh --mesh)
#   make -C csrc gpu-meshing-stress   the mesher under the pipeline's load
MESH_DIR   := cuda/meshing
CUDA_BIN   ?= /usr/local/cuda/bin
MESH_OUT   := $(OUT)/cuda/meshing
MESH_CUBIN := $(MESH_OUT)/mesh.cubin
MESH_LLVM  ?= /usr/lib/llvm-21/bin
# the host's inlining made one kernel of 8.5 MB of PTX that ptxas took minutes
# and 3.5 GB on; the device keeps calls
MESH_INLINE ?= 25
# ptxas -O level: 1 for an iteration build (AGENTS.md "GPU kernel lanes: the loop"), 3 for a measurement
# (csrc/Makefile's NVOPT: PERF=1 builds at 3)
MESH_PTXAS_O ?= $(if $(filter 1,$(PERF)),3,1)
# ptxas's threads (the kernels compiled apart, the same SASS): the -O3 build
# 34.2 s alone, 24.1 at 16, 16.3 at 32 and 16.5 at 64 (its largest kernel is
# the floor; the GPU host, 2026-09-29, lane/meshgpu)
MESH_SPLIT ?= 32
# registers a thread for the spread pass's drawing kernels (mesh_draw and
# the ones that draw again): 128 lets 4 blocks of 128 stay on an SM (the
# drawing is bound by memory latency, and more warps hide more of it; 255
# held 2). mesh_run keeps ptxas's own choice (at 128 its spills slowed a
# launch of whole views by 6%). Set as .maxnreg on the entries in the PTX.
MESH_MAXREG ?= 128
MESH_MAXREG_K := mesh_fix
# MESH_G=1: source lines in the cubin (compute-sanitizer and ncu name them); rebuild the cubin
# without it after (make -B gpu-meshing), the stamp does not see it
MESH_G ?=
MESH_MULTIARCH ?= $(shell $(CC) -print-multiarch 2>/dev/null)
# the engine's headers read for an nvptx64 target: cuda/include's
# overrides first (glibc's arch tests, pthread.h), no thread-local storage
# (the mesher never reads nw_env); -fno-math-errno keeps sqrt and floor
# the exact instructions (cos, sin and atan2 go through rb_cos and the
# others, looked up)
MESH_DFLAGS := -O2 -std=c11 -ffp-contract=off --target=nvptx64-nvidia-cuda -march=$(CUDA_ARCH) \
               -fno-vectorize -fno-slp-vectorize -fno-math-errno -mllvm -inline-threshold=$(MESH_INLINE) -isystem $(CURDIR)/cuda/include -DNW_MESH_DEVICE \
               $(if $(MESH_MULTIARCH),-idirafter /usr/include/$(MESH_MULTIARCH)) -D_Thread_local= '-Dtls_model(x)=unused' \
               -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-command-line-argument $(if $(filter 1,$(MESH_G)),-gline-tables-only)
MESH_SRC   := engine/render_blocks.c engine/render_blocks2.c engine/render_blocks3.c engine/blocks_table.c engine/noise.c $(MESH_DIR)/kernels.c
MESH_BC    := $(patsubst %.c,$(MESH_OUT)/%.bc,$(notdir $(MESH_SRC)))
MESH_API   := mesh_setup,mesh_run,mesh_run_cube,mesh_copy,mesh_class,mesh_wsum,mesh_order,mesh_plan,mesh_cut,mesh_pack,mesh_place,mesh_keys,mesh_sort,mesh_draw,mesh_draw_cube,mesh_check,mesh_fix,mesh_vcopy,mesh_wcopy
MESH_GLOBALS_OK := block_class|block_class_once|meshing_sh|meshing_lw|meshing_ob|meshing_cb|meshing_ps|meshing_fb|meshing_pw

.PHONY: gpu-meshing gpu-meshing-check
gpu-meshing: $(MESH_CUBIN)

$(MESH_OUT)/%.bc: engine/%.c
	@mkdir -p $(MESH_OUT)
	$(MESH_LLVM)/clang $(MESH_DFLAGS) -MMD -MP -emit-llvm -c -o $@ $<
$(MESH_OUT)/kernels.bc: $(MESH_DIR)/kernels.c
	@mkdir -p $(MESH_OUT)
	$(MESH_LLVM)/clang $(MESH_DFLAGS) -fconvergent-functions -fno-builtin -MMD -MP -emit-llvm -c -o $@ $<
-include $(MESH_BC:.bc=.d)

# the kernels and what they reach; nothing may stay undefined (a C library
# function the mesh path calls needs its device version in kernels.c)
$(MESH_CUBIN): $(MESH_BC) $(NVSTAMP)
	$(MESH_LLVM)/llvm-link -o $(MESH_OUT)/all.bc $(MESH_BC)
	$(MESH_LLVM)/opt -passes='internalize,globaldce' -internalize-public-api-list=$(MESH_API) -o $(MESH_OUT)/roots.bc $(MESH_OUT)/all.bc
	$(MESH_LLVM)/opt -passes='default<O2>' -vectorize-loops=false -vectorize-slp=false -inline-threshold=$(MESH_INLINE) -o $(MESH_OUT)/opt.bc $(MESH_OUT)/roots.bc
	@u=$$($(MESH_LLVM)/llvm-nm -u $(MESH_OUT)/opt.bc | grep -v ' llvm\.'); [ -z "$$u" ] || { echo "gpu-meshing: undefined on the device: $$u"; exit 1; }
	@# every device thread shares a global: none may be written while meshers run (a function
	@# static cache in rb3_render_cocoa was read half filled). Allowed: the block class table,
	@# built by mesh_setup's one thread first, and the kernels' shared memory
	@w=$$($(MESH_LLVM)/llvm-dis -o - $(MESH_OUT)/opt.bc | grep -E '^@[^ ]+ = ' | grep -v ' constant ' | sed -E 's/^@([^ ]+) .*/\1/' | \
	  grep -vxE '$(MESH_GLOBALS_OK)'); [ -z "$$w" ] || { echo "gpu-meshing: writable device globals: $$w"; exit 1; }
	$(MESH_LLVM)/llvm-nm --defined-only $(MESH_OUT)/roots.bc | awk '$$2 ~ /^[Tt]$$/ {print $$3}' | sort > $(MESH_OUT)/kept.txt
	$(MESH_LLVM)/llc -O2 -mcpu=$(CUDA_ARCH) -nvptx-fma-level=0 -o $(MESH_OUT)/mesh.ptx $(MESH_OUT)/opt.bc
	sed -i -E '/^\.visible \.entry ($(MESH_MAXREG_K))\(/,/^\{/ s/^\{/.maxnreg $(MESH_MAXREG)\n{/' $(MESH_OUT)/mesh.ptx
	$(CUDA_BIN)/ptxas -arch=$(CUDA_ARCH) -O$(MESH_PTXAS_O) --fmad=false --split-compile=$(MESH_SPLIT) $(if $(filter 1,$(MESH_G)),-lineinfo) -v -o $@ $(MESH_OUT)/mesh.ptx 2> $(MESH_OUT)/ptxas.log || { cat $(MESH_OUT)/ptxas.log; exit 1; }

# the host side (host.c: C over the CUDA runtime and driver) and the
# cubin as data, both linked into cuda_render.o (cuda/render/render.mk)
$(OUT)/obj/cuda_meshing_host.o: $(MESH_DIR)/host.c $(MESH_DIR)/meshing.h $(MESH_DIR)/kernels.h engine/meshfeed.h engine/render_blocks.h engine/world.h
	@mkdir -p $(dir $@)
	$(CCC) $(filter-out -include %envheap.h,$(CFLAGS)) -I$(CUDA_BIN)/../include -c -o $@ $<
$(MESH_OUT)/meshing_cubin.c: $(MESH_CUBIN)
	cd $(MESH_OUT) && xxd -i -n meshing_cubin mesh.cubin | sed 's/^unsigned/const unsigned/' > meshing_cubin.c
$(OUT)/obj/cuda_meshing_cubin.o: $(MESH_OUT)/meshing_cubin.c
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $<

# the gate: the frame judges' recordings (or MESH_ARGS names), every frame
# dumped by play with its mesh feed and the host's meshes, fed to the device
# in order (cuda/render/gate.sh --mesh, render_check --seq)
MESH_ARGS ?=
# (render_check by its path: render.mk, which names it RENDER_BIN, is read
# after this file, so $(RENDER_BIN) here was empty and a mesher edit ran the
# render_check built before it)
gpu-meshing-check: $(MESH_CUBIN) $(OUT)/cuda/render_check $(OUT)/play $(OUT)/play-dev
	CUDA_VISIBLE_DEVICES=$(CUDA_DEV) bash cuda/render/gate.sh $(RENDER_STORE) --mesh --size $(RENDER_SIZE) --batch $(RENDER_BATCH) $(MESH_ARGS)

# the device mesher under the pipeline's load: pipe_bench's exploring kind
# (seed 1 walking out: a jungle's cocoa, hundreds of sections a launch)
# meshed on the device for 60 steps, rc 0. rb3_render_cocoa's icon cache,
# one for every device thread, failed it with an illegal address
# (2026-09-29, lane/perfmesh). A seed per env (--env-seeds) walks eight
# worlds' terrain: more render types and fuller runs a launch
.PHONY: gpu-meshing-stress
gpu-meshing-stress:
	@$(MAKE) --no-print-directory -s -C runtime pipeline
	@mkdir -p $(MESH_OUT)
	cd .. && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) out/runtime/pipe_bench --kinds explore --ns 8 -j 16 --steps 60 --device-mesh 1 --env-seeds \
	  > $(MESH_OUT)/stress.log 2>&1; rc=$$?; grep -v '^play:' $(MESH_OUT)/stress.log | grep -E '^explore|meshing|render'; exit $$rc
