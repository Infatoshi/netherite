# The game tick on the device (cuda/tick, GPU plan L17): S6, the chunk loop,
# as the engine's own C compiled for the GPU, a correctness tool.
# Included by the Makefile; nothing here is in `all` (it needs clang 21 with
# the NVPTX target, LLVM's headers, CUDA and a GPU: Linux only).
#
# Two clang builds of engine/*.c share the nw-rename pass (cuda/tick/nwpass.cpp), so
# the host executable's symbol table names every global and function as the
# device module does: $(OUT)/cuda/tick/host/*.o, linked into
# $(OUT)/cuda/tick/test_snapshots (test_snapshots with --phase-kernel-cuda and
# the device child, cuda/tick/host.c), and $(OUT)/cuda/tick/dev/*.bc, linked with
# cuda/tick/dev.c into one module: internalized to its kernels and every
# address-taken function, optimized, instrumented (nw-dev: pointer
# translation, the write barrier, the symbol table), optimized again, then
# PTX (llc) and a cubin (ptxas) at $(OUT)/cuda/tick/tick.cubin.
#
#   make -C csrc gpu-tick                 build both
#   make -C csrc gpu-tick-check [RECS=...] [S6_PHASES=...]  every recording
#                                          (or RECS): its phase digests with S6
#                                          (or S6_PHASES) on the device against
#                                          a C trace, every row exact
#   make -C csrc gpu-tick-batch [RECS=...]  the recordings interleaved, one
#                                          launch per step across them
LLVM_BIN  ?= /usr/lib/llvm-21/bin
S6_CLANG  ?= $(LLVM_BIN)/clang
S6_OUT    := $(OUT)/cuda/tick
S6_PASS   := $(S6_OUT)/nwpass.so
S6_FLAGS  := -O2 -std=c11 -ffp-contract=off -include $(CURDIR)/engine/envheap.h
S6_HFLAGS := $(S6_FLAGS) -Wall -Wno-unused-parameter -Wno-unused-function
# inlining as the host does made functions of 50,000 PTX lines, and ptxas
# took 8 GB on the module; S6_INLINE bounds it on the device
S6_INLINE ?= 25
# -fmath-errno: libm stays calls (dev.c has the exact ones), not intrinsics.
# The nvptx target searches /usr/include but not the host's multiarch
# directory (bits/, gnu/, asm/): named here, after cuda/include's overrides
# (the GPU host found them through libc6-dev-i386's /usr/include/bits link)
S6_MULTIARCH ?= $(shell $(CC) -print-multiarch 2>/dev/null)
S6_DFLAGS := $(S6_FLAGS) --target=nvptx64-nvidia-cuda -march=$(CUDA_ARCH) -fno-vectorize -fno-slp-vectorize \
             -fmath-errno -isystem $(CURDIR)/cuda/include $(if $(S6_MULTIARCH),-idirafter /usr/include/$(S6_MULTIARCH)) \
             -Wno-unused-command-line-argument -mllvm -inline-threshold=$(S6_INLINE)

# the device leaves out what only a host can run (threads, SIMD, the transfer's syscalls)
S6_DEVSRC := $(filter-out engine/raster.c engine/raster_sky2.c engine/texanim.c engine/imgxfer.c,$(SRC))
S6_HOSTO  := $(patsubst engine/%.c,$(S6_OUT)/host/%.o,$(SRC))
S6_DEVBC  := $(patsubst engine/%.c,$(S6_OUT)/dev/%.bc,$(S6_DEVSRC))
$(S6_OUT)/host/lang.o $(S6_OUT)/dev/lang.bc: $(OUT)/lang_generated.h
S6_OPT    := -vectorize-loops=false -vectorize-slp=false -inline-threshold=$(S6_INLINE)
# the callbacks the tick stores for S6 to call that the device runs too: the
# block write record, the drop and spawn records and the replay's adoption of
# a spawned entity, and for S2 and SG the replay's spawner and the siege's
# muster (the rest, chunk provision above all, fall back to C)
S6_CALLBACKS := servertick.c$$st_on_block,servertick.c$$st_drop_sink,servertick.c$$st_tnt_prime,servertick.c$$st_block_env_drop,serverreplay.c$$sr_on_spawn
S6_CALLBACKS := $(S6_CALLBACKS),serverreplay.c$$sr_spawner_tick,serverreplay.c$$sr_siege_muster
CUDA_INC  ?= /usr/local/cuda/include
CUDA_BIN  ?= /usr/local/cuda/bin
CUDA_LIB  ?= /usr/local/cuda/lib64

.PHONY: gpu-tick gpu-tick-check gpu-tick-batch
gpu-tick: $(S6_OUT)/test_snapshots $(S6_OUT)/tick_batch $(S6_OUT)/tick.cubin

$(S6_PASS): cuda/tick/nwpass.cpp
	@mkdir -p $(S6_OUT)
	$(LLVM_BIN)/clang++ -shared -fPIC -O2 $$($(LLVM_BIN)/llvm-config --cxxflags) -o $@ $<

$(S6_OUT)/host/%.o: engine/%.c | $(S6_PASS)
	@mkdir -p $(S6_OUT)/host
	$(S6_CLANG) $(S6_HFLAGS) -fpass-plugin=$(S6_PASS) -MMD -MP -c -o $@ $<

# the region cache's build hash (engine/regioncache.h), as csrc/Makefile's
# RC_BUILD for this build: every engine source, the pass, the flags and this
# clang. Without it the key was "unknown" for every S6 build, and after
# lane/spillfast replaced zlib the S6 replays read a zlib-era image's packed
# chunks ("a packed chunk does not decompress", lane/s6lz). Kept in a stamp
# that changes only with the hash, made only when this build is.
.PHONY: s6-rc-build
$(S6_OUT)/host/regioncache.build: s6-rc-build
	@mkdir -p $(S6_OUT)/host; h=s6-$$({ cat engine/*.c engine/*.h cuda/tick/nwpass.cpp; \
	  printf '%s\n' '$(filter-out -include %envheap.h,$(S6_HFLAGS))'; $(S6_CLANG) --version | head -1; } | sha1sum | cut -c1-16); \
	  [ "$$(cat $@ 2>/dev/null)" = "$$h" ] || echo "$$h" > $@

$(S6_OUT)/host/regioncache.o: engine/regioncache.c $(S6_OUT)/host/regioncache.build | $(S6_PASS)
	$(S6_CLANG) $(S6_HFLAGS) -DREGIONCACHE_BUILD="\"$$(cat $(S6_OUT)/host/regioncache.build)\"" -fpass-plugin=$(S6_PASS) \
	  -MMD -MP -c -o $@ $<

$(S6_OUT)/dev/%.bc: engine/%.c | $(S6_PASS)
	@mkdir -p $(S6_OUT)/dev
	$(S6_CLANG) $(S6_DFLAGS) -fpass-plugin=$(S6_PASS) -MMD -MP -emit-llvm -c -o $@ $<

# the runtime: no nw-rename (the passes know its nwdev_ names and its kernels)
$(S6_OUT)/dev.bc: cuda/tick/dev.c cuda/tick/dev.h
	@mkdir -p $(S6_OUT)
	$(S6_CLANG) $(S6_DFLAGS) -fno-builtin -fno-math-errno -emit-llvm -c -o $@ $<

# the device link (cuda/tick/build.sh): ptxas on the whole module took 8 GB, so
# the module is split in S6_PARTS parts compiled one at a time (S6_JOBS at
# once, each through the memory budget) and device-linked. S6_PTXAS_O=1 for
# iteration builds (quote no timing from one): csrc/Makefile's NVOPT, 1
# by default and 3 with PERF=1 (lane/cudasplit).
S6_PTXAS_O ?= $(if $(filter 1,$(PERF)),3,1)
S6_PARTS ?= 24
S6_JOBS ?= 4
$(S6_OUT)/inl.bc: cuda/tick/inl.c cuda/tick/dev.h
	@mkdir -p $(S6_OUT)
	$(S6_CLANG) $(S6_DFLAGS) -emit-llvm -c -o $@ $<

$(S6_OUT)/tick.cubin: $(S6_DEVBC) $(S6_OUT)/dev.bc $(S6_OUT)/inl.bc $(S6_PASS) cuda/tick/build.sh $(NVSTAMP)
	LLVM_BIN=$(LLVM_BIN) CUDA_BIN=$(CUDA_BIN) bash cuda/tick/build.sh $(S6_OUT) $(CUDA_ARCH) $(S6_PTXAS_O) $(S6_INLINE) \
	  $(S6_PARTS) $(S6_JOBS) '$(S6_CALLBACKS)'

$(S6_OUT)/obj/%.o: cuda/tick/%.c cuda/tick/host.h cuda/tick/dev.h
	@mkdir -p $(S6_OUT)/obj
	$(S6_CLANG) $(S6_HFLAGS) -I$(CUDA_INC) -DS6_CUBIN='"$(S6_OUT)/tick.cubin"' -c -o $@ $<

$(S6_OUT)/test_snapshots: tests/test_snapshots.c tests/phasekernel.h tests/input_guard.c engine/dev.c $(S6_HOSTO) \
		$(S6_OUT)/obj/host.o
	@mkdir -p $(S6_OUT)/obj
	$(S6_CLANG) $(S6_HFLAGS) -DNETHERITE_DEV -DNETHERITE_GPU_TICK -Dmain=gate_test_main -c -o $(S6_OUT)/obj/test_snapshots.o tests/test_snapshots.c
	$(S6_CLANG) $(S6_HFLAGS) -c -o $(S6_OUT)/obj/input_guard.o tests/input_guard.c
	$(S6_CLANG) $(S6_HFLAGS) -DNETHERITE_DEV -c -o $(S6_OUT)/obj/dev.o engine/dev.c
	$(S6_CLANG) $(LDFLAGS) -o $@ $(S6_OUT)/obj/test_snapshots.o $(S6_OUT)/obj/input_guard.o $(S6_OUT)/obj/dev.o \
	  $(S6_OUT)/obj/host.o $(S6_HOSTO) -L$(CUDA_LIB) -lcuda -lz -lm -lpthread

$(S6_OUT)/tick_batch: cuda/tick/batch.c tests/test_snapshots.c tests/phasekernel.h engine/dev.c $(S6_HOSTO) $(S6_OUT)/obj/host.o \
		$(S6_OUT)/test_snapshots
	$(S6_CLANG) $(S6_HFLAGS) -I$(CUDA_INC) -DNETHERITE_DEV -DNETHERITE_GPU_TICK -c -o $(S6_OUT)/obj/batch.o cuda/tick/batch.c
	$(S6_CLANG) $(LDFLAGS) -o $@ $(S6_OUT)/obj/batch.o $(S6_OUT)/obj/dev.o $(S6_OUT)/obj/host.o $(S6_HOSTO) \
	  -L$(CUDA_LIB) -lcuda -lz -lm -lpthread

# every recording (or RECS): its C trace by the same host build (kept, keyed
# by the build), then
# S6 on the device against it, in batches of S6_GROUP recordings (at most
# S6_BUDGET MB of their measured peaks) per tick_batch process, S6_SWEEP_J
# processes at once, one launch per step for each batch (cuda/tick/sweep.sh).
# S6_GROUP=1 runs each alone.
S6_GROUP ?= 8
S6_BUDGET ?= 600
# each process holds 4.5 GB of device memory on the 3090 (the stack reserve
# and the module) and its environments' granules (0.2 GB for a small
# recording, 2.4 GB for a large one; S6_BUDGET's host peaks follow them):
# three fit its 24 GB with the groups alternating large and small; a
# recording whose group the memory refused runs again alone at the end
# (cuda/tick/sweep.sh). A process replays one row at a time: the sweep is
# bound by host threads, not by the group's size
S6_SWEEP_J ?= 3
# S6_PHASES: the phases on the device (tick_batch --phases: S6, or a comma
# list of S1, S2, S1T, S4, S5, S6 and SG, which servertick_phase runs alone;
# S1T always comes back to C, its sky angle's cos is glibc's)
S6_PHASES ?= S6
gpu-tick-check: $(S6_OUT)/tick_batch $(S6_OUT)/tick.cubin $(S6_OUT)/test_snapshots
	@CUDA_VISIBLE_DEVICES=$(CUDA_DEV) SWEEP_J=$(S6_SWEEP_J) PHASES=$(S6_PHASES) bash cuda/tick/sweep.sh $(OUT) $(S6_GROUP) $(S6_BUDGET) \
	  $(if $(filter command line,$(origin RECS)),$(RECS),$(notdir $(patsubst %/,%,$(dir $(wildcard $(DATA)/snapshots/*/tape.jsonl))))) \
	  | tee $(S6_OUT)/check.rc | grep -v ' rc=0: ' || true
	@echo "gpu-tick-check: $$(grep -c ' rc=0: ' $(S6_OUT)/check.rc) of $$(wc -l < $(S6_OUT)/check.rc) recordings pass with $(S6_PHASES) on the device; \
	$$(sed -n 's/.* rc=0: \([0-9]*\) [A-Za-z0-9]* runs on the device, \([0-9]*\) flagged.*/\1 \2/p' $(S6_OUT)/check.rc | awk '{d += $$1; f += $$2} END {print d " device runs, " f " redone in C"}'); logs $(S6_OUT)/sweep.gK.txt"
	@! grep -qv ' rc=0: ' $(S6_OUT)/check.rc

# RECS interleaved in one process, S6 of all of them in one launch each step
# (cuda/tick/batch.c); COPIES=K runs each K times, ROWS=N stops each replay N
# rows past its snapshot
COPIES ?= 1
gpu-tick-batch: $(S6_OUT)/tick_batch $(S6_OUT)/tick.cubin $(S6_OUT)/test_snapshots
	@mkdir -p $(S6_OUT)/traces
	@h=$$(sha1sum $(S6_OUT)/test_snapshots | cut -c1-16); for r in $(RECS); do [ -s $(S6_OUT)/traces/$$r.$$h.ref ] || \
	  bash tests/memslot.sh $(DATA)/snapshots/$$r $(S6_OUT)/test_snapshots --phase-trace $(S6_OUT)/traces/$$r.$$h.ref \
	  $(DATA)/snapshots/$$r > $(S6_OUT)/traces/$$r.log 2>&1 || exit 1; done; \
	CUDA_VISIBLE_DEVICES=$(CUDA_DEV) bash tests/memslot.sh --key tick_batch $(S6_OUT) $(S6_OUT)/tick_batch $(if $(ROWS),--rows $(ROWS)) \
	  $(foreach r,$(RECS),$(foreach k,$(shell seq $(COPIES)),$(DATA)/snapshots/$(r) $(S6_OUT)/traces/$(r).$$h.ref)) > $(S6_OUT)/batch.txt 2>&1; \
	  rc=$$?; grep -E '^(tick_batch|phase-kernel cuda|FAIL)|OK: ' $(S6_OUT)/batch.txt; exit $$rc

-include $(wildcard $(S6_OUT)/host/*.d $(S6_OUT)/dev/*.d)
