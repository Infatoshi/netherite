# The batched fast-mode tick (lane/batchtick): batchtick.c is the model (a library, its own
# gate in nwbt_selftest), batchtick_check.c the binary that runs it and, with arguments, the
# prototype's bench. Standalone C with OpenMP: no CUDA and no engine needed.
#
#   make -C csrc/runtime batchtick-check    the gate: the physics values and their relative
#                                            errors, the dig rate, the post, the drop, the whole
#                                            state equal on 1 and 32 threads after worlds_init,
#                                            and two stored recordings replayed by the model and
#                                            the engine from one start state (out/java/snapshots,
#                                            gated by out/native/test_snapshots when it is built)
#   make -C csrc/runtime batchtick BT_ARGS="--n 1024 --mode 2 --target 0.3"   the model's bench
#
# engine.tick_scope: batched is the model's door; the pipeline does not step on it yet
# (batchtick.h, DEVLOG 2026-10-24).
BT_CC     := $(or $(RT_CC),$(CC))
BT_CFLAGS := -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -ffp-contract=off -fopenmp
# the recorded comparison's two paths, baked in: the recordings (a checkout's out/java, which a
# worktree symlinks) and the engine's replay gate (out/native/test_snapshots, built by csrc)
BT_DEFS   := -DBT_SNAPS='"$(ROOT)/out/java/snapshots"' -DBT_ENGINE='"$(ROOT)/out/native/test_snapshots"'
BT_OBJ    := $(OUT)/batchtick.o
BT_BIN    := $(OUT)/batchtick_check

$(BT_OBJ): $(RUNTIME)/batchtick.c $(RUNTIME)/batchtick.h
	@mkdir -p $(OUT)
	$(BT_CC) $(BT_CFLAGS) $(BT_DEFS) -c -o $@ $<

$(BT_BIN): $(RUNTIME)/batchtick_check.c $(BT_OBJ)
	$(BT_CC) $(BT_CFLAGS) $(BT_DEFS) -o $@ $< $(BT_OBJ) -lm -lz

.PHONY: batchtick batchtick-check
batchtick: $(BT_BIN)
	@$(BT_BIN) $(BT_ARGS)

batchtick-check: $(BT_BIN)
	@$(BT_BIN)

all: batchtick
