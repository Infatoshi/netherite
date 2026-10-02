# The light engine on the device (cuda/lighting, GPU plan L16): the server's
# light calls replayed from engine/lightcap.h's capture, and the client world's
# light deferred to the device (D8). Included by csrc/Makefile; needs nvcc:
#   make -C csrc gpu-lighting               every build below
#   make -C csrc gpu-lighting-check [RECS="name ..."] [DRIVERS=1]
#                                             every snapshot recording's light
#                                             calls (or RECS) on the device
#                                             against C's (tests/light_check.sh)
#   make -C csrc gpu-lighting-client-check [CWL_RECS="a b"|all]
#                                             the client world's light
#                                             deferred to the device against
#                                             the host's (client_check.sh)
#   make -C csrc gpu-lighting-gate          the pipeline's gate with the
#                                             client light on the device
#
# $(OUT)/cuda/lighting/test_snapshots is test_snapshots with the device
# consumer of the light capture (engine/lightcap.h) linked in (cap.c: the
# device in a forked child). `--light-cap count` (any build) counts every
# light engine call and the stream it makes; `--light-cap cuda` replays every
# call on the device and compares each result with C's. gpu-lighting-check
# replays every snapshot recording (or RECS) that way on GPU $(CUDA_DEV),
# LC_J at a time under the memory budget, one line each, then the totals;
# DRIVERS=1 adds --light-drivers.
LC_J ?= 8
.PHONY: gpu-lighting gpu-lighting-check
gpu-lighting: $(OUT)/cuda/lighting/test_snapshots $(OUT)/cuda/lighting_check gpu-lighting-client
# the engine's units (dev.cuh: the replay kernel, the update, the drivers,
# the host side), compiled apart and device-linked. The kernel's launch
# bounds (CL_THREADS, one block) allow 64 registers a thread, and nvlink
# refuses a callee in another unit that uses more: every unit is held to 64.
CL_OBJ := $(patsubst %,$(OUT)/obj/cuda_lighting_%.o,replay update drivers host)
$(CL_OBJ): NVFLAGS += -maxrregcount=64
# each host thread its own stream: lighting_check runs several replays at once
$(OUT)/obj/cuda_lighting_host.o: NVFLAGS += --default-stream per-thread
$(CL_OBJ): $(OUT)/obj/cuda_lighting_%.o: cuda/lighting/%.cu cuda/lighting/dev.cuh cuda/lighting/lighting.h engine/lightcap.h $(NVSTAMP)
	@mkdir -p $(OUT)/obj
	$(NVJOB) $(NVFLAGS) -c -o $@ $<
$(OUT)/cuda/lighting/test_snapshots: tests/test_snapshots.c tests/input_guard.c cuda/lighting/cap.c cuda/lighting/lighting.h engine/lightcap.h \
		$(CL_OBJ) $(LIB) $(DEVOBJ)
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -DNETHERITE_DEV -DNETHERITE_GPU_LIGHTING -Dmain=gate_test_main -c -o $(OUT)/obj/test_snapshots_lighting.o tests/test_snapshots.c
	$(CCC) $(CFLAGS) -c -o $(OUT)/obj/input_guard_lighting.o tests/input_guard.c
	$(CCC) $(CFLAGS) -c -o $(OUT)/obj/cuda_lighting_cap.o cuda/lighting/cap.c
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/test_snapshots_lighting.o $(OUT)/obj/input_guard_lighting.o \
	  $(OUT)/obj/cuda_lighting_cap.o $(CL_OBJ) $(DEVOBJ) $(LIB) -lz -lm
# lighting_check (cuda/lighting/check.c): stored captures (test_snapshots
# --light-cap save:FILE) replayed on the device alone
$(OUT)/cuda/lighting_check: cuda/lighting/check.c cuda/lighting/lighting.h engine/lightcap.h $(CL_OBJ)
	@mkdir -p $(dir $@)
	$(CCC) $(filter-out -include %envheap.h,$(CFLAGS)) -c -o $(OUT)/obj/cuda_lighting_check.o $<
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/cuda_lighting_check.o $(CL_OBJ) -lpthread
# C's side of the check is stored once per recording and C build and the
# device replays it alone (tests/light_check.sh: the store, KEY and eviction;
# LCSTORE, LCSTORE_MB); FRESH=1 captures every recording again (the
# nightly), LIVE=1 checks the old way, the device beside each C replay
# (out/native/cuda/lighting/test_snapshots).
LCSTORE    ?= $(HOME)/.cache/netherite-devcap/light
LCSTORE_MB ?= 12000
gpu-lighting-check: $(if $(filter 1,$(LIVE)),$(OUT)/cuda/lighting/test_snapshots,$(OUT)/test_snapshots $(OUT)/cuda/lighting_check)
	@CUDA_VISIBLE_DEVICES=$(CUDA_DEV) bash tests/light_check.sh -j $(LC_J) $(if $(filter 1,$(DRIVERS)),-d) \
	  $(if $(filter 1,$(LIVE)),--live) $(if $(filter 1,$(FRESH)),--fresh) --store $(LCSTORE) --store-mb $(LCSTORE_MB) \
	  $(if $(filter command line,$(origin RECS)),$(RECS),$(notdir $(patsubst %/,%,$(dir $(wildcard $(DATA)/snapshots/*/tape.jsonl)))))

# The client world's light on the device (lane/cwlight; SPEC.md's GPU
# section, D8): the light calls the client world's tick makes are deferred
# (engine/lightdefer.h) and run by the device's light engine (replay.cu) on its
# own resident copy of the client world's light, which comes back to the
# host's chunks at each sync (engine/lightcap.h's deferred mode).
#   make -C csrc gpu-lighting-client  out/native/cuda/lighting_client/test_snapshots
#                                       and out/native/obj/cuda_lighting.o (the
#                                       pipeline's link: csrc/runtime/pipeline.mk)
#   make -C csrc gpu-lighting-gate    the pipeline's gate (csrc/runtime pipe-gate) with
#                                       the client light on the device and the device's
#                                       meshes checked: rows, frames and every frame's
#                                       client light against the host's run
#   make -C csrc gpu-lighting-client-check [CWL_RECS="a b"|all]
#                                       the quick gate: each recording replayed with
#                                       the client light deferred to the device, every
#                                       row's client light digest against the replay
#                                       that runs it on the host, byte for byte
CWL_RECS ?= idx-cwlight-s1 idx-cwcheck-s1 nether-trip-s1 break-place-s1 fight-skeleton-s1
CWL_J    ?= 2

.PHONY: gpu-lighting-client gpu-lighting-client-check gpu-lighting-gate
gpu-lighting-client: $(OUT)/cuda/lighting_client/test_snapshots $(OUT)/obj/cuda_lighting.o

# the light engine's units device-linked, and the capture's device sinks,
# one relocatable object a host link takes (runtime/pipeline.mk); nvcc links
# the units themselves (it device-links any object with relocatable device
# code, and would link cuda_lighting.o's again)
$(OUT)/obj/cuda_lighting_dlink.o: $(CL_OBJ)
	$(NVCC) -arch=$(CUDA_ARCH) -dlink -o $@ $^
$(OUT)/obj/cuda_lighting_client_cap.o: cuda/lighting/cap.c cuda/lighting/lighting.h engine/lightcap.h engine/lightdefer.h
	@mkdir -p $(dir $@)
	$(CCC) $(filter-out -include %envheap.h,$(CFLAGS)) -c -o $@ $<
$(OUT)/obj/cuda_lighting.o: $(CL_OBJ) $(OUT)/obj/cuda_lighting_dlink.o $(OUT)/obj/cuda_lighting_client_cap.o
	ld -r -o $@ $^
$(OUT)/cuda/lighting_client/test_snapshots: tests/test_snapshots.c tests/input_guard.c tests/cwdigest.h $(CL_OBJ) $(OUT)/obj/cuda_lighting_client_cap.o $(LIB) $(DEVOBJ)
	@mkdir -p $(dir $@)
	$(CCC) $(CFLAGS) -DNETHERITE_DEV -DNETHERITE_GPU_LIGHTING -Dmain=gate_test_main -c -o $(OUT)/obj/test_snapshots_lighting_client.o tests/test_snapshots.c
	$(CCC) $(CFLAGS) -c -o $(OUT)/obj/input_guard_lighting_client.o tests/input_guard.c
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/test_snapshots_lighting_client.o $(OUT)/obj/input_guard_lighting_client.o \
	  $(OUT)/obj/cuda_lighting_client_cap.o $(CL_OBJ) $(DEVOBJ) $(LIB) -lz -lm
gpu-lighting-client-check: $(OUT)/cuda/lighting_client/test_snapshots $(OUT)/test_snapshots
	@CUDA_VISIBLE_DEVICES=$(CUDA_DEV) bash cuda/lighting/client_check.sh -j $(CWL_J) \
	  $(if $(filter all,$(CWL_RECS)),$(notdir $(patsubst %/,%,$(dir $(wildcard $(DATA)/snapshots/*/tape.jsonl)))),$(CWL_RECS))
gpu-lighting-gate:
	@$(MAKE) --no-print-directory -C runtime pipe-gate LIGHT=1 MESH=2 N=$(or $(N),16) J=$(or $(J),16)
