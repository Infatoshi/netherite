# The env pool (lane/envpool): csrc/runtime/pool.[ch] and its gate and
# benchmark, built into out/runtime against native's objects. The dev host only
# (Linux: the images' address window, perf counters, threads); never part of
# native's `all`, clangcheck or the Mac build.
#
#   make -C csrc/runtime pool                 the library and its programs
#   make -C csrc/runtime pool-gate [N=64] [J=32] [TICKS=4] [RECS="a b"] [EPISODES=E]
#                                   [AGENT=1] [FEED=1] [SPARSE=K] [GEN=c|cuda] [CONFIG=FILE]
#                                               every env's rows against test_snapshots'
#                                               (AGENT: the recordings' own scripts' agent
#                                               acts; FEED: the render feed checked too;
#                                               GEN: raw generation through the pool's
#                                               batched generator, the C engine's or the
#                                               device's, pool_gate_cuda; CONFIG: every
#                                               recording held to a config.yaml's world)
#   make -C csrc/runtime pool-refs [RECS=...] test_snapshots --row-dump for each recording
#   make -C csrc/runtime pool-bench [ARGS=...] env-steps per second (pool_bench.c)
#
# Standalone: make -C csrc/runtime -f pool.mk pool (the same rules).
RT_DIR    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
RT_NATIVE := $(abspath $(RT_DIR)/..)
RT_ROOT   := $(abspath $(RT_NATIVE)/..)
RT_OUT    := $(RT_ROOT)/out/runtime
RT_NOUT   := $(RT_ROOT)/out/native
RT_CC     ?= cc
# every compile and link runs in csrc/runtime with paths relative to it and
# the debug info's directory mapped to ".", so no object names its checkout;
# compiles go through ccache when it is installed (csrc/Makefile's CCC)
RT_CFLAGS := -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Werror -pthread \
             -include ../engine/envheap.h -fdebug-prefix-map=$(RT_DIR)=. -ffunction-sections
ifeq ($(origin RT_CCACHE),undefined)
RT_CCACHE := $(shell command -v ccache 2>/dev/null)
endif
RT_CCC     = $(if $(RT_CCACHE),CCACHE_DIR=$(HOME)/.cache/ccache $(RT_CCACHE) )$(RT_CC)
RT_LDFLAGS := $(if $(shell command -v ld.mold 2>/dev/null),-fuse-ld=mold)
RT_LIBOBJ := $(RT_OUT)/obj/pool.o $(RT_OUT)/obj/feed.o $(RT_OUT)/obj/agent.o $(RT_OUT)/obj/semcam.o
RT_NLIB   := $(RT_NOUT)/libnetherite.a
RT_DEVOBJ := $(RT_NOUT)/obj/dev.o
# the gate's recordings (one name per line; # comments)
RT_LIST   := $(RT_DIR)/pool_gate.list
N        ?= 16
J        ?= 16
TICKS    ?= 4
RECS     ?= $(shell grep -v '^\#' $(RT_LIST) | awk 'NF {print $$1}')
EPISODES ?=
REFJ     ?= 16

all: pool

.PHONY: pool pool-native pool-gate pool-refs pool-bench pool-gencuda

pool: $(RT_OUT)/libpool.a $(RT_OUT)/pool_gate $(RT_OUT)/pool_bench

# native's archive, dev ops and test_snapshots, by native's own rules
pool-native:
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) $(RT_NLIB) $(RT_DEVOBJ) $(RT_NOUT)/test_snapshots

$(RT_NLIB) $(RT_DEVOBJ): pool-native

$(RT_OUT)/obj/%.o: $(RT_DIR)/%.c $(RT_DIR)/pool.h $(RT_DIR)/feed.h $(RT_DIR)/agent.h $(RT_DIR)/semcam.h | pool-native
	@mkdir -p $(RT_OUT)/obj
	cd $(RT_DIR) && $(RT_CCC) $(RT_CFLAGS) -MMD -MP -c -o $@ $(notdir $<)

$(RT_OUT)/libpool.a: $(RT_LIBOBJ)
	@rm -f $@
	ar rcs $@ $^

# the gate replays dev tapes: it links the dev ops (NETHERITE_DEV)
$(RT_OUT)/pool_gate: $(RT_DIR)/pool_gate.c $(RT_DIR)/semcam.h $(RT_OUT)/libpool.a $(RT_DIR)/../tests/rowrec.h $(RT_NLIB) $(RT_DEVOBJ)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -DNETHERITE_DEV $(RT_LDFLAGS) -o $@ $(notdir $<) $(RT_OUT)/libpool.a $(RT_DEVOBJ) $(RT_NLIB) -lz -lm

# pool_gate with the CUDA generators (--gen cuda): the device-linked
# generator object from native's rules
RT_GENOBJ := $(RT_NOUT)/obj/cuda_worldgen.o
RT_CUDALIB = $(dir $(or $(PL_NVCC),/usr/local/cuda/bin/nvcc))../lib64
pool-gencuda:
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) $(RT_GENOBJ)
$(RT_GENOBJ): pool-gencuda
$(RT_OUT)/pool_gate_cuda: $(RT_DIR)/pool_gate.c $(RT_OUT)/libpool.a $(RT_DIR)/../tests/rowrec.h $(RT_NLIB) $(RT_DEVOBJ) $(RT_GENOBJ)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -DNETHERITE_DEV -DPOOL_GATE_CUDA $(RT_LDFLAGS) -o $@ $(notdir $<) $(RT_OUT)/libpool.a \
	  $(RT_DEVOBJ) $(RT_GENOBJ) $(RT_NLIB) -L$(RT_CUDALIB) -Wl,-rpath,$(RT_CUDALIB) -lcudart -lstdc++ -ldl -lz -lm -lpthread
$(RT_OUT)/pool_bench: $(RT_DIR)/pool_bench.c $(RT_OUT)/libpool.a $(RT_NLIB) $(RT_DEVOBJ)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -DNETHERITE_DEV $(RT_LDFLAGS) -o $@ $(notdir $<) $(RT_OUT)/libpool.a $(RT_DEVOBJ) $(RT_NLIB) -lz -lm

-include $(wildcard $(RT_OUT)/obj/*.d)

# test_snapshots' own records of every row (rowrec.h) and its client light
# digests (tests/cwdigest.h: pipe_gate --device-light), remade when the tape
# or test_snapshots is newer
pool-refs: pool-native
	@mkdir -p $(RT_OUT)/ref
	@for r in $(RECS); do \
	  ref=$(RT_OUT)/ref/$$r.rows; tape=$(RT_ROOT)/out/java/snapshots/$$r/tape.jsonl; \
	  if [ ! -s $$ref ] || [ ! -s $(RT_OUT)/ref/$$r.cwd ] || [ $$tape -nt $$ref ] || [ $(RT_NOUT)/test_snapshots -nt $$ref ]; then echo $$r; fi; \
	done | xargs -r -P $(REFJ) -I{} sh -c 'cd $(RT_ROOT) && bash csrc/tests/memslot.sh out/java/snapshots/{} out/native/test_snapshots --row-dump $(RT_OUT)/ref/{}.rows.tmp --cw-digest $(RT_OUT)/ref/{}.cwd.tmp out/java/snapshots/{} > $(RT_OUT)/ref/{}.log 2>&1 && mv $(RT_OUT)/ref/{}.cwd.tmp $(RT_OUT)/ref/{}.cwd && mv $(RT_OUT)/ref/{}.rows.tmp $(RT_OUT)/ref/{}.rows && echo "ref {}: $$(tail -1 $(RT_OUT)/ref/{}.log)" || echo "ref {}: FAIL (see $(RT_OUT)/ref/{}.log)"'

GEN      ?=
pool-gate: pool pool-refs $(if $(filter cuda,$(GEN)),$(RT_OUT)/pool_gate_cuda)
	cd $(RT_ROOT) && $(if $(filter cuda,$(GEN)),CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/pool_gate_cuda,$(RT_OUT)/pool_gate) \
	  -n $(N) -j $(J) --ticks $(TICKS) $(if $(EPISODES),--episodes $(EPISODES)) $(if $(CONFIG),--world $(abspath $(CONFIG))) \
	  $(if $(AGENT),--agent oracle/tests) $(if $(FEED),--feed) $(if $(SPARSE),--sparse $(SPARSE)) $(if $(GEN),--gen $(GEN)) $(RECS)

pool-bench: pool
	cd $(RT_ROOT) && $(RT_OUT)/pool_bench $(ARGS)
