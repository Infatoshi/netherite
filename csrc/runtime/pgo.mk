# The library's profile (lane/cpuhot): gcc's profile feedback for libneth's C (rlbind.mk's RL_PGOFLAGS), made from a
# fixed CPU run of the vector env and committed in csrc/runtime/pgo, so every tree builds the same library from it.
#
#   make -C csrc/runtime rl-pgo
#          the library built instrumented (RL_PGO=gen: out/runtime/pgogen, the engine's and the runtime's PIC objects
#          compiled again with -fprofile-generate, the plain build's device objects linked), PGO_RUNS run through it
#          (vec_bench, CPU only, the trainer's 16x16 host cast: the brief's 512 envs in 4 groups without resets,
#          100-step episodes, and seed starts through a window), and the counts written to csrc/runtime/pgo (native/
#          the engine's objects, runtime/ the runtime's), replacing the profile there; commit it. About a minute.
#          Remake it when the env's hot code changed much (a function edited since compiles without its counts).
#
# The counts are racy (-fprofile-update=single, the workers' increments unlocked), so gcc reads them with
# -fprofile-correction (without it a lost count is an inconsistent edge and an error); lost counts only blur the
# weights.
RL_PGOGEN  := $(RT_OUT)/pgogen
PGO_SEEDS  ?= $(RT_ROOT)/out/java/try/seedset-d989661987
PGO_BENCH   = cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= LD_LIBRARY_PATH=$(RL_PGOGEN) timeout 300 $(RT_OUT)/vec_bench \
              --config $(NT_CFG)/host.yaml --macros 63 --threads 32
PGO_RUNS   ?= "--ns 512 --groups 4 --warm 50 --steps 300 --ep-len 3600" \
              "--ns 512 --groups 4 --warm 20 --steps 200 --ep-len 100" \
              "--ns 256 --groups 4 --warm 20 --steps 200 --ep-len 100 --starts $(PGO_SEEDS) --window 8:4"

# pool_gate on the library's objects (pool.mk's LIBPIC=1): the runtime's PIC objects pool_gate links from libpool.a,
# the PIC engine and its dev ops (the device objects are not needed: libpool.a's gate links none)
RL_GATEPIC := $(addprefix $(RL_PICR)/,pool.o feed.o agent.o semcam.o raycast.o rcmirror.o raycast_f32.o raycast_fx.o raycast_f16.o)
$(RT_OUT)/pool_gate_pic: $(RT_DIR)/pool_gate.c $(RT_DIR)/semcam.h $(RT_DIR)/raycast.h $(RT_DIR)/../tests/rowrec.h $(RL_GATEPIC) \
  $(RL_PICN)/libnetherite.a $(RL_PICN)/obj/dev.o
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -DNETHERITE_DEV $(RT_LDFLAGS) -o $@ $(notdir $<) $(RL_GATEPIC) $(RL_PICN)/obj/dev.o \
	  $(RL_PICN)/libnetherite.a -lz -lm

.PHONY: rl-pgo
rl-pgo: rl-cuda $(RT_OUT)/vec_bench train-loop-cfg
	@rm -rf $(RL_PGOGEN)/prof $(RL_PGOGEN)/libneth.so
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) RL_PGO=gen RL_PICN=$(RL_PGOGEN)/native RL_PICR=$(RL_PGOGEN)/pic \
	  RL_LIB=$(RL_PGOGEN)/libneth.so RL_CUDAN=$(RL_PICN) $(RL_PGOGEN)/libneth.so
	@for a in $(PGO_RUNS); do echo "rl-pgo: vec_bench $$a"; ( $(PGO_BENCH) $$a 2>&1 | grep -E 'env-steps/s|FAIL' ) || exit 1; done
	@n=$(RL_PGOGENDIR)$(RL_PGOGEN)/native/obj; r=$(RL_PGOGENDIR)$(RL_PGOGEN)/pic; \
	  ls $$n/*.gcda $$r/*.gcda > /dev/null || { echo "rl-pgo: no counts written"; exit 1; }; \
	  rm -rf $(RL_PGODIR) && mkdir -p $(RL_PGODIR)/native $(RL_PGODIR)/runtime && cp $$n/*.gcda $(RL_PGODIR)/native/ && \
	  cp $$r/*.gcda $(RL_PGODIR)/runtime/ && \
	  echo "rl-pgo: $$(ls $(RL_PGODIR)/native | wc -l) engine and $$(ls $(RL_PGODIR)/runtime | wc -l) runtime profiles, $$(du -sk $(RL_PGODIR) | cut -f1) KB in $(RL_PGODIR)"

# Device-cast ntloop PGO (lane/hill-train-fidelity): profiles real device-cast training (steady + multi-seed resets)
# so .text.hot reflects simulation rather than host raycast, paired with -Wl,-z,keep-text-section-prefix in rlbind.mk.
PGO_DEV_CFG   ?= $(RT_ROOT)/configs/workstation-scalable.yaml
PGO_DEV_INIT  ?= $(RT_ROOT)/cache/models/s64-it42.ntw
PGO_DEV_NT    ?= $(RT_OUT)/train/s1.ls/ntloop
PGO_DEV_SEEDS ?= 100005598,1019266621,1044203914,106545754,1086470609,1113382357,1134009793,1155280576
PGO_DEV_SDIR  ?= $(RT_ROOT)/cache/statebank-2026-11-19/starts

.PHONY: rl-pgo-devcast
rl-pgo-devcast: rl-cuda $(PGO_DEV_NT)
	@rm -rf $(RL_PGOGEN)/prof $(RL_PGOGEN)/libneth.so
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) RL_PGO=gen RL_PICN=$(RL_PGOGEN)/native RL_PICR=$(RL_PGOGEN)/pic \
	  RL_LIB=$(RL_PGOGEN)/libneth.so RL_CUDAN=$(RL_PICN) $(RL_PGOGEN)/libneth.so
	@cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) LD_LIBRARY_PATH=$(RL_PGOGEN) $(PGO_DEV_NT) \
	  --config $(PGO_DEV_CFG) --policy actor --init $(PGO_DEV_INIT) --ns 256 --groups 8 --threads 8 \
	  --warm 50 --steps 500 --ep-len 12000 --learn 0 | grep -E 'env-steps/s|FAIL'
	@cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) LD_LIBRARY_PATH=$(RL_PGOGEN) $(PGO_DEV_NT) \
	  --config $(PGO_DEV_CFG) --policy actor --init $(PGO_DEV_INIT) --ns 256 --groups 8 --threads 8 \
	  --warm 50 --steps 500 --ep-len 250 --learn 0 --seeds $(PGO_DEV_SEEDS) --seed-set $(PGO_DEV_SDIR) | grep -E 'env-steps/s|FAIL'
	@n=$(RL_PGOGENDIR)$(RL_PGOGEN)/native/obj; r=$(RL_PGOGENDIR)$(RL_PGOGEN)/pic; \
	  ls $$n/*.gcda $$r/*.gcda > /dev/null || { echo "rl-pgo-devcast: no counts written"; exit 1; }; \
	  rm -rf $(RL_PGODIR) && mkdir -p $(RL_PGODIR)/native $(RL_PGODIR)/runtime && cp $$n/*.gcda $(RL_PGODIR)/native/ && \
	  cp $$r/*.gcda $(RL_PGODIR)/runtime/ && \
	  echo "rl-pgo-devcast: $$(ls $(RL_PGODIR)/native | wc -l) engine and $$(ls $(RL_PGODIR)/runtime | wc -l) runtime profiles, $$(du -sk $(RL_PGODIR) | cut -f1) KB in $(RL_PGODIR)"
