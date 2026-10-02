# The action compiler's gate (lane/moveact: moveact.[ch], rlmove.[ch] in
# libnwrl.so; moveact_gate.c). Uses rlbind.mk's and pool.mk's variables.
#
#   make -C csrc/runtime rl-move-gate [MOVERECS="..."]
#          each lever's recording: moveact_gate drives one env from the
#          recording's start with its lever on (the seeded stand-in policy),
#          the compiled script equal to oracle/tests/NAME.jsonl and every tick's
#          row record equal to test_snapshots' over out/java/snapshots/NAME
#   make -C csrc/runtime rl-move-record NAME=moveact-hold-s1
#          re-record one: the script from sr-speedrun-s1, then the Java oracle
#          runs it (configs/speedrun.yaml) into out/java/snapshots/NAME
#
# A recording's driver arguments: oracle/tests/moveact.list (name, lever, ticks a step, steps, seed).
MV_GATE  := $(RT_OUT)/moveact_gate
MV_LIST  := $(RT_ROOT)/oracle/tests/moveact.list
MV_CONF  := $(RT_ROOT)/configs/speedrun.yaml
MOVERECS ?= $(shell awk '!/^#/ && NF {print $$1}' $(MV_LIST) 2>/dev/null)

.PHONY: rl-move-gate rl-move-record

$(MV_GATE): $(RT_DIR)/moveact_gate.c $(RT_DIR)/rlbind.h $(RT_DIR)/rlgui.h $(RT_DIR)/rlmove.h $(RL_LIB)
	@mkdir -p $(RT_OUT)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -o $@ moveact_gate.c -L$(RT_OUT) -lnwrl -Wl,-rpath,$(RT_OUT)

rl-move-gate: rlbind $(MV_GATE)
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(MOVERECS)"
	@cd $(RT_ROOT) && mkdir -p $(RT_OUT)/movegate && bad=0; for r in $(MOVERECS); do \
	  set -- $$(awk -v n=$$r '$$1 == n {print $$2, $$3, $$4, $$5}' $(MV_LIST)); \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(MV_GATE) --config $(MV_CONF) --start out/java/snapshots/$$r --lever $$1 --tps $$2 \
	    --steps $$3 --seed $$4 --check-script oracle/tests/$$r.jsonl --ref $(RT_OUT)/ref/$$r.rows \
	    --rows $(RT_OUT)/movegate/$$r.rows > $(RT_OUT)/movegate/$$r.log 2>&1; rc=$$?; \
	  echo "$$r: $$(grep -E '^(rows|script|FAIL)' $(RT_OUT)/movegate/$$r.log | tr '\n' ' ')$$(tail -1 $(RT_OUT)/movegate/$$r.log)"; \
	  [ $$rc = 0 ] || bad=1; done; \
	  if [ $$bad = 0 ]; then echo "RL-MOVE-GATE PASS"; else echo "RL-MOVE-GATE FAIL"; exit 1; fi

rl-move-record: rlbind $(MV_GATE)
	@cd $(RT_ROOT) && set -- $$(awk -v n=$(NAME) '$$1 == n {print $$2, $$3, $$4, $$5}' $(MV_LIST)) && [ $$# = 4 ] && \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(MV_GATE) --config $(MV_CONF) --start out/java/snapshots/sr-speedrun-s1 --lever $$1 \
	    --tps $$2 --steps $$3 --seed $$4 --script oracle/tests/$(NAME).jsonl && \
	  mkdir -p out/java/snapshots/$(NAME) && \
	  $(MAKE) --no-print-directory -s -C oracle script SEED=1 CONF=../configs/speedrun.yaml SCRIPT=tests/$(NAME).jsonl \
	    TAPE=../out/java/snapshots/$(NAME)/tape.jsonl
