# The textured ray cast on the device (cuda/raycast, lane/rcdev; rcdev.h): runtime/raycast.c's rays on the GPU over a
# copy of each env's client world (runtime/rcmirror.c), its frames and seen records byte for byte the CPU's.
# Included by csrc/Makefile; needs nvcc:
#   make -C csrc gpu-raycast           out/native/obj/cuda_raycast.o (the runtime's pool_gate and libneth link it)
#   make -C csrc gpu-raycast-check [RCRECS=...] [RCSPECS=...]   the device gate below, from csrc (mirror.tsv's)
#   make -C csrc/runtime raycast-device-gate [RCRECS=...] [RCSPECS=...]
#                                      pool_gate --raycast-device: after every tick of raycast-gate's recordings, each
#                                      env's device cast against raycast_ref's, frames and seen records equal
#   make -C csrc gpu-raycast-bench     out/native/cuda/rcbench DUMP: the cast kernel alone on a dump (rcdev_dump), its
#                                      frames and seen records against the dump's, its time a launch (bench.cu)
.PHONY: gpu-raycast gpu-raycast-check gpu-raycast-bench gpu-raycast-bench-stats
RCD_OBJ := $(patsubst %,$(OUT)/obj/cuda_raycast_%.o,cast host cast_fast)
# cast.cu's one kernel compiles unsplit: split (--split-compile, NVSPLITN) ptxas gave rc_cast 168 registers and a
# 200-byte stack where whole it takes 140 and 88 (lane/rcsplit)
$(OUT)/obj/cuda_raycast_cast.o $(OUT)/obj/cuda_raycast_cast_st.o $(OUT)/obj/cuda_raycast_cast_fast.o $(OUT)/cuda/rcbench_stats: NVSPLITN := 1
# cast_fast.cu: training-optimized fast-math kernels (isolated from strict IEEE cast.cu)
RCD_FAST_FLAGS = -O3 -std=c++17 -arch=$(CUDA_ARCH) --fmad=true -prec-div=false -prec-sqrt=false -ftz=true \
                 $(NVOPT) -rdc=true -Xcompiler -Wall
gpu-raycast: $(OUT)/obj/cuda_raycast.o
$(OUT)/obj/cuda_raycast_cast_fast.o: cuda/raycast/cast_fast.cu cuda/raycast/rcdev.h cuda/raycast/kern.h runtime/rchead.h runtime/rcmirror.h $(NVSTAMP)
	@mkdir -p $(OUT)/obj
	$(NVJOB) $(RCD_FAST_FLAGS) -c -o $@ $<
$(OUT)/obj/cuda_raycast_cast.o $(OUT)/obj/cuda_raycast_host.o: $(OUT)/obj/cuda_raycast_%.o: cuda/raycast/%.cu cuda/raycast/rcdev.h cuda/raycast/kern.h runtime/rchead.h runtime/rcmirror.h $(NVSTAMP)
	@mkdir -p $(OUT)/obj
	$(NVJOB) $(NVFLAGS) -c -o $@ $<
# the units device-linked, one relocatable object a host link takes
$(OUT)/obj/cuda_raycast_dlink.o: $(RCD_OBJ)
	$(NVCC) -arch=$(CUDA_ARCH) -dlink -o $@ $^
$(OUT)/obj/cuda_raycast.o: $(RCD_OBJ) $(OUT)/obj/cuda_raycast_dlink.o
	ld -r -o $@ $^
gpu-raycast-bench: $(OUT)/cuda/rcbench
$(OUT)/cuda/rcbench: cuda/raycast/bench.cu $(OUT)/obj/cuda_raycast_cast.o $(OUT)/obj/cuda_raycast_cast_fast.o cuda/raycast/kern.h runtime/rchead.h $(NVSTAMP)
	@mkdir -p $(dir $@)
	$(NVJOB) $(NVFLAGS) -c -o $(OUT)/obj/cuda_raycast_bench.o $<
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/cuda_raycast_bench.o $(OUT)/obj/cuda_raycast_cast.o $(OUT)/obj/cuda_raycast_cast_fast.o
# the stats build (RC_STATS: each pixel's steps, jumps and turns, and the lanes a warp keeps busy)
gpu-raycast-bench-stats: $(OUT)/cuda/rcbench_stats
$(OUT)/cuda/rcbench_stats: cuda/raycast/bench.cu cuda/raycast/cast.cu $(OUT)/obj/cuda_raycast_cast_fast.o cuda/raycast/kern.h runtime/rchead.h $(NVSTAMP)
	@mkdir -p $(dir $@)
	$(NVJOB) $(NVFLAGS) -DRC_STATS -c -o $(OUT)/obj/cuda_raycast_bench_st.o cuda/raycast/bench.cu
	$(NVJOB) $(NVFLAGS) -DRC_STATS -c -o $(OUT)/obj/cuda_raycast_cast_st.o cuda/raycast/cast.cu
	$(NVCC) -arch=$(CUDA_ARCH) -rdc=true -o $@ $(OUT)/obj/cuda_raycast_bench_st.o $(OUT)/obj/cuda_raycast_cast_st.o $(OUT)/obj/cuda_raycast_cast_fast.o
# the device gate from csrc (the mirror rule's gates are csrc targets): runtime's raycast-device-gate
gpu-raycast-check:
	@$(MAKE) --no-print-directory -C runtime raycast-device-gate $(if $(RCRECS),RCRECS="$(RCRECS)") $(if $(RCSPECS),RCSPECS="$(RCSPECS)")
