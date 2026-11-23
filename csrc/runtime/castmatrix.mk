# The cast's precision matrix (lane/cxmatrix, castmatrix.sh): every observation size against every instance of the
# textured cast, in one table. Uses gates.mk's, raycast.mk's and train/loop.mk's variables (in recipes only).
#
#   make -C csrc/runtime cast-matrix [SIZES="16 32 64 128"] [FPS="64 32 16 fixed"] [STAGES="gpu table"]
#
# For each size and instance: (a) the device kernel's ms a launch on stored casts (rcbench in one gpu window; the
# dumps made once per size and instance in CM_DUMPS, keyed by the sources of the dump's format and of the device
# cast), (b) the host cast's cycles an env-step, (c) env-steps/s end to end: vec_bench with the host cast at every size
# (the same perf run as (b), in one bench window), the trainer where a net exists (16x16 w1 with the host and the
# device cast, 64x64 s1 with the device cast; one bench window), (d) rays and pixels per million that differ from FP64
# (pool_gate --cast-cmp-with over CM_RECS). Then one line a size: the kernel's (the host cast's) speedup against
# FP64's and the env-steps/s speedup it became, with the share of the gain that reached it. An instance the tree does
# not have (its config refused) is a row that says "missing"; a probe that fails (each binary runs small first: 16
# envs, 200 steps) says "failed", its log in out/runtime/castmatrix/probe. The table is
# out/runtime/castmatrix/table.txt and lines.txt; STAGES=table prints it again from the stored runs. About 8 minutes
# of windows at the defaults (gpu about 1, host about 3, train about 4); CM_TSTEPS sets the trainer's measured steps.
CM_SIZES  ?= $(or $(SIZES),16 32 64 128)
CM_FPS    ?= $(or $(FPS),64 32 16 fixed)
CM_RECS   ?= $(RCRECS)
# (600 measured steps were a 1 s window at 512 envs: two runs 4 to 24% apart, 2026-10-31)
CM_TSTEPS ?= 2000
CM_NETS   ?= $(NTW_ACTOR)/weights.ntw $(RT_CACHE)/native/actor-fresh-s1/weights.ntw
CM_OUT    := $(RT_OUT)/castmatrix
# the stored casts' key: the dump's format (gates.mk's RCFMT files) and the device cast's sources (an instance's kernel
# that changes the frames it draws makes new dumps: rcbench holds the frames to the dump's)
CM_KEY    := $(firstword $(shell cat $(RT_ROOT)/csrc/runtime/rchead.h $(sort $(wildcard $(RT_ROOT)/csrc/cuda/raycast/*)) \
               2>/dev/null | cksum))
CM_DUMPS  ?= $(RT_CACHE)/native/cmdumps-$(CM_KEY)

.PHONY: cast-matrix cast-matrix-build

cast-matrix-build:
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) all assets $(RT_NOUT)/cuda/rcbench
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool rlbind $(RT_OUT)/vec_bench $(NT_OUT)/ntloop $(NT_OUT)/$(NTC_V64)/ntloop \
	  train-loop-cfg
	@setsid -w $(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(CM_RECS)" | grep -v ': OK: every row replayed$$' || true

cast-matrix: cast-matrix-build
	@cd $(RT_ROOT) && bash $(RT_DIR)/castmatrix.sh -s "$(CM_SIZES)" -p "$(CM_FPS)" -o $(CM_OUT) -d $(CM_DUMPS) -q "$(GPUQ)" \
	  -r "$(CM_RECS)" -n "$(CM_NETS)" -t $(CM_TSTEPS) $(STAGES)
