# lane/chain: the checkpointed search from a spawn to the dragon's death, and its proof (train/chain.c's header has the
# commands and the output):
#
#   make -C csrc/runtime chain                     out/runtime/train/chain (links libneth and the recorder's codec)
#   make -C csrc/runtime chain-search CH_DIR=DIR [CH_RUN=configs/tests/chain.yaml] [CH_ARGS='--n 64 --minutes 60']
#          the search, CUDA_VISIBLE_DEVICES= (no device touched); DIR/nodes.tsv, DIR/frontier.txt, DIR/seg/
CH_RUN  ?= configs/tests/chain.yaml
CH_DIR  ?= $(RT_CACHE)/native/chain/run
CH_ARGS ?=

.PHONY: chain chain-search chain-script-check chain-memory-check
chain: rlbind $(RT_OUT)/train/chain

# (linked to a temporary name and moved over, so a search running the old binary keeps it)
$(RT_OUT)/train/chain: $(RT_DIR)/train/chain.c $(RT_DIR)/train/rec.h $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h $(RL_LIB) \
                       $(RT_OUT)/train/rec.o
	@mkdir -p $(RT_OUT)/train
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@.tmp train/chain.c $(RT_OUT)/train/rec.o -L$(RT_OUT) \
	  -Wl,-rpath,$(RT_OUT) -lneth -lzstd -lm && mv -f $@.tmp $@

chain-search: chain
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain search --run $(CH_RUN) --dir $(CH_DIR) $(CH_ARGS)

# lane/proom: an attempt alone and in batches of 4 and 32 equals itself in a batch of 64, every step
# (train/alone_gate.sh; AG_RUNS the run files, AG_ARGS its flags: -m MOPTS, -t TICKS, -a "ATTEMPTS", -n N, -k "SIZES",
# -b CHAIN, -x "CHAIN ARGS")
AG_RUNS ?=
AG_ARGS ?=
.PHONY: chain-alone-gate
chain-alone-gate: chain
	cd $(RT_ROOT) && bash $(RT_DIR)/train/alone_gate.sh $(AG_ARGS) $(AG_RUNS)

# lane/dleg: the known-answer check of a rollout's attempt (train/ka.sh): attempt KA_ATTEMPT of KA_RUN alone, its
# script on the Java client from the start's checkpoint in the start's own world (CACHE=0 POOL=0), test_snapshots on
# the recording and Java's last row against the attempt's; KA PASS or FAIL (exit 1), refused without the world file.
# KA_ARGS: -m MOPTS, -s SEED, -t TICKS, -r RUNG, -w WORLD, -o DIR, -b CHAIN, -x "THE LEG'S OTHER CHAIN FLAGS"
KA_RUN     ?=
KA_ATTEMPT ?=
KA_ARGS    ?=
.PHONY: chain-ka
chain-ka: chain
	cd $(RT_ROOT) && bash $(RT_DIR)/train/ka.sh -a $(KA_ATTEMPT) $(KA_ARGS) $(KA_RUN)

# lane/armed: an earned start (train/earn.sh): a leg's attempt that reached, played on past its arrival on the Java
# client (ka.sh, KA PASS required), saved at the first quiet row after the arrival, the join's Snapshot as LIB/ID/start
# (C replay 0 mismatches). EARN_ARGS: -l LIB -i ID -a I -t BOUND -T "TARGET" -P "PAST TARGET" [-x -m -s -r -p -q -g]
EARN_RUN  ?=
EARN_ARGS ?=
.PHONY: chain-earn
chain-earn: chain $(RT_OUT)/train/ntstarts
	cd $(RT_ROOT) && bash $(RT_DIR)/train/earn.sh $(EARN_ARGS) $(EARN_RUN)

# lane/chain2: the script writer round-trips every look number as the Java oracle reads it (-0.0 included)
.PHONY: chain-script-check
chain-script-check: chain
	@$(RT_OUT)/train/chain script-check

# Compare every canonical controller byte just before each save with its disk-restored bytes at 500-step seams. The
# macro-digest starts cover the controller's six families; the quiescent run also compares full trajectories.
CH_MEM_RUN   ?= configs/tests/chain.yaml
CH_MEM_STEPS ?= 1501
CH_MEM_OLD   ?=
$(RT_OUT)/train/chain_mem_check: $(RT_DIR)/train/chain_mem_check.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h $(RL_LIB)
	@mkdir -p $(RT_OUT)/train
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@.tmp train/chain_mem_check.c \
	  -L$(dir $(RL_LIB)) -Wl,-rpath,$(dir $(RL_LIB)) -lneth -lzstd -lm && mv -f $@.tmp $@
chain-memory-check: $(RT_OUT)/train/chain_mem_check
	@mkdir -p $(RT_ROOT)/out/chain
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check $(CH_MEM_RUN) $(CH_MEM_STEPS) '' '' 15
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check bytes configs/tests/l4gcase.yaml $(CH_MEM_STEPS) '' '' 33554431
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check bytes configs/tests/l5case.yaml $(CH_MEM_STEPS) '' '' 33554431
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check bytes configs/tests/pearlcase.yaml $(CH_MEM_STEPS) '' '' 33554431 1
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check prefix configs/tests/pearlcase.yaml $(CH_MEM_STEPS) '' '' 33554431 1
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check bytes configs/tests/macrodig-nether.yaml $(CH_MEM_STEPS) '' '' 33554431
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check bytes configs/tests/macrodig-sh.yaml $(CH_MEM_STEPS) '' '' 33554431 1
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check bytes configs/tests/dragon.yaml $(CH_MEM_STEPS) '' '' 33554431
	@if [ -n '$(CH_MEM_OLD)' ]; then cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check \
	  legacy $(CH_MEM_RUN) '$(CH_MEM_OLD)'; fi
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check wait
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/chain_mem_check portal
	@echo 'CHAIN-MEMORY-GATE PASS'
