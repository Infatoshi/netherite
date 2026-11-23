# The vector env (lane/vecenv, vecenv.h): libneth's envs as a PufferLib-style vector env, the trainer's per-env
# work (the act, the macros, the reward, the scalars, the episodes) in C on the pool's workers. vecenv.c is in
# libneth.so (rlbind.mk's RL_SRC); this fragment builds its bench, which links the library.
#
#   make -C csrc/runtime vec-bench [CONFIG_VEC=configs/train-fast.yaml] [VEC_NS=256,512] [ARGS=...]
#          out/runtime/vec_bench: random actions through the vector env, env-steps/s and the vector env's C
#          per env-step at each N (no Python)
#   make -C csrc/runtime rl-tape-gate [TAPE_CONF=configs/tape-gate.yaml]
#          lane/tape: out/runtime/vectape_gate plays a seeded stand-in policy through the vector env and writes env
#          0's second episode as the oracle's script with its row records; the oracle runs the script on the
#          config's world section (the oracle's registry has no client.camera raycast) and test_snapshots' row
#          records of that recording must equal the episode's, every row
#   make -C csrc/runtime rl-play-gate [PLG_START=LIB/ID/start] [PLG_ACTS=2600] [PLG_SEEDS="1 2"]
#          lane/divlib: out/runtime/vecplay plays seeded random acts from a checkpoint start (default the kills'
#          library's seed-42 End start, by path: the gate needs the library) through the vector env and through the
#          pool alone, every row record equal (playgate.sh); then the start with its checkpoint's save missing, and
#          packed, refused by both. C only, about 10 s
#   make -C csrc/runtime rl-picks-gate
#          lane/picks: the policy's craft picks through vec_bench (32 envs, one 60 s episode each, the base
#          macros): with picks on and none sent nothing is crafted (the craft macro never starts on its own); with
#          random picks items are crafted and no pick the mask disallowed starts the craft macro
#   make -C csrc/runtime rl-obsidian-gate [OG_ENV=E]
#          lane/obsidian: NETH_VEC_OBSIDIAN's gate, rl-tape-gate's way from configs/s64portal-kit.yaml's start (the
#          kit next to lava): vectape_gate's stand-in with recipe, strip, food and obsidian (163839), env OG_ENV's
#          second 10-minute episode written as the oracle's script, the oracle's rows of it equal to the episode's,
#          every row; and the same env without the obsidian bit (32767) must give another script (the macro drove
#          some of the acts the oracle replayed). obsidian-start records the start where it is missing
#   make -C csrc/runtime macro-digests [MD_J=2] [MD_ROWS="spawn-all nether-all"] | macro-digests-pin
#          lane/macromod: the macros' digest matrix (macrodig.sh, macrodig.tsv): vec_bench under random actions from a
#          start where each macro family runs, with options, levels and labellers, its trajectory, label, episode and
#          books digests against the pinned ones, one line a row, then MACRO-DIGESTS PASS or FAIL (CPU, about a minute)
CONFIG_VEC ?= $(RT_ROOT)/configs/train-fast.yaml
MD_J       ?= 2
MD_ROWS    ?=
.PHONY: macro-digests macro-digests-pin
macro-digests: rlbind $(RT_OUT)/vec_bench
	@cd $(RT_ROOT) && bash csrc/runtime/macrodig.sh -j $(MD_J) $(MD_ROWS)
macro-digests-pin: rlbind $(RT_OUT)/vec_bench
	@cd $(RT_ROOT) && bash csrc/runtime/macrodig.sh -j $(MD_J) -p $(MD_ROWS)
VEC_NS     ?= 256,512
TAPE_CONF  ?= $(RT_ROOT)/configs/tape-gate.yaml
TG_OUT     := $(RT_OUT)/tapegate
PG_OUT     := $(RT_OUT)/picksgate
PLG_CONF   ?= $(RT_ROOT)/configs/play-gate.yaml
PLG_START  ?= $(RT_CACHE)/native/library-kills/s42-s42k20-end/start
PLG_ACTS   ?= 2600
PLG_SEEDS  ?= 1 2

.PHONY: vec_bench vec-bench rl-tape-gate rl-play-gate rl-picks-gate rl-window-gate nether_case rl-obsidian-gate obsidian-start

# lane/obsidian: the kit start (configs/s64portal-kit.yaml's): gold-g5-s42 replayed to tick 3600 and saved (a
# checkpoint), a start recorded from the save, with 20 idle ticks after its snapshot (every directory under
# out/java/snapshots is a job of the native suite, which fails a tape with no rows after the snapshot)
# lane/obsidian2: the seed's folder, so the loader's fallback (checkpoints/SEED/NAME) finds it after the recording tree
OBS_CKPT  := out/java/checkpoints/42/G5-3600
OBS_START := out/java/snapshots/obs-g5-3600
OG_OUT    := $(RT_OUT)/obsgate
# env 1's second 10-minute episode casts, fills and digs 2 obsidian (the other envs' dig none)
OG_ENV    ?= 1
obsidian-start:
	@cd $(RT_ROOT) && [ -f $(OBS_START)/manifest.json ] && [ $$(wc -l < $(OBS_START)/tape.jsonl) -gt 3 ] && exit 0; \
	  [ -L $(OBS_START) ] || rm -rf $(OBS_START); mkdir -p $(OG_OUT) && \
	  fail() { echo "obsidian-start: $$1"; exit 1; }; \
	  [ -f out/java/snapshots/gold-g5-s42/tape.jsonl ] || fail "out/java/snapshots/gold-g5-s42 wanted"; \
	  if [ ! -f $(OBS_CKPT)/manifest.json ]; then \
	    head -n 3601 out/java/snapshots/gold-g5-s42/tape.jsonl > $(OG_OUT)/g5-3600.jsonl; \
	    $(MAKE) --no-print-directory -s -C oracle replay REF=$(OG_OUT)/g5-3600.jsonl SAVEEND=$(RT_ROOT)/$(OBS_CKPT) \
	      > $(OG_OUT)/ckpt.log 2>&1 || fail "the save: $$(tail -1 $(OG_OUT)/ckpt.log)"; \
	  fi; \
	  printf '%s\n' '{"cmd":"run","class":"Snapshot"}' '{"n":20,"act":{}}' > $(OG_OUT)/snap.jsonl; \
	  $(MAKE) --no-print-directory -s -C oracle script SEED=42 FROM=$(RT_ROOT)/$(OBS_CKPT) SCRIPT=$(OG_OUT)/snap.jsonl \
	    TAPE=$(RT_ROOT)/$(OBS_START)/tape.jsonl > $(OG_OUT)/start.log 2>&1 || fail "the start: $$(tail -1 $(OG_OUT)/start.log)"; \
	  echo "obsidian-start: $(OBS_START) recorded"

rl-obsidian-gate: rlbind pool-native $(RT_OUT)/vectape_gate obsidian-start
	@cd $(RT_ROOT) && O=$(OG_OUT)/run && rm -rf $$O out/java/try/obsgate && mkdir -p $$O && \
	  fail() { echo "$$1"; echo "RL-OBSIDIAN-GATE FAIL"; exit 1; }; \
	  for m in 163839 32767; do \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/vectape_gate --config configs/s64portal-kit.yaml --n 8 --groups 1 \
	      --threads 8 --seconds 600 --macros $$m --env $(OG_ENV) --script $$O/script-$$m.jsonl --rows $$O/vec-$$m.rows \
	      > $$O/gate-$$m.log 2>&1 || fail "vectape_gate --macros $$m: $$(tail -1 $$O/gate-$$m.log)" & \
	  done; wait; \
	  for m in 163839 32767; do grep -q '^episode:' $$O/gate-$$m.log || fail "vectape_gate --macros $$m: $$(tail -1 $$O/gate-$$m.log)"; done; \
	  cmp -s $$O/script-163839.jsonl $$O/script-32767.jsonl && fail "the obsidian bit changed no act of env $(OG_ENV)'s episode"; \
	  printf 'world:\n  seed: 42\n  render_distance: 4\n' > $$O/world.yaml; \
	  $(MAKE) --no-print-directory -s -C oracle script SEED=42 CONF=$$O/world.yaml FROM=$(RT_ROOT)/$(OBS_CKPT) \
	    SCRIPT=$$O/script-163839.jsonl TAPE=../out/java/try/obsgate/tape.jsonl CACHE=0 > $$O/java.log 2>&1 || \
	    fail "oracle: $$(tail -1 $$O/java.log)"; \
	  $(RT_NOUT)/test_snapshots --row-dump $$O/java.rows out/java/try/obsgate > $$O/replay.log 2>&1 || \
	    fail "test_snapshots: $$(tail -1 $$O/replay.log)"; \
	  awk '/^t=2 /{k++} k == 1' $$O/vec-163839.rows | sed 's/ d\.cw=[^ ]*//' > $$O/vec.ep.rows; \
	  grep -v '^end ' $$O/java.rows | sed 's/ d\.cw=[^ ]*//' > $$O/java.ep.rows; \
	  n=$$(wc -l < $$O/java.ep.rows); \
	  [ $$n -gt 0 ] && cmp -s $$O/vec.ep.rows $$O/java.ep.rows || \
	    fail "rows differ: $$(diff $$O/vec.ep.rows $$O/java.ep.rows | head -2 | cut -c1-200 | tr '\n' ' ')"; \
	  echo "$$(tail -1 $$O/gate-163839.log); the script differs from the one without the bit; $$n rows equal to the" \
	    "oracle's (but d.cw, the client world's Random, which the vector env does not know on a checkpoint's start)"; \
	  echo "RL-OBSIDIAN-GATE PASS"

# lane/seedwin: the mix's start window (neth_vec_opts.start_window) over a seed set of configs/brute.yaml's world
# (SEEDSET: a directory of starts, as mcsr-speedrun's fastppo --seeds captures them), 10 rounds of 200-step
# episodes at window 8 stride 4: equal trajectory digests at 2 and 4 groups, the 44 starts those rounds name played,
# at most 16 loaded at once
SEEDSET ?= out/java/try/seedset-d989661987
WG_OUT  := $(RT_OUT)/windowgate
rl-window-gate: rlbind $(RT_OUT)/vec_bench
	@cd $(RT_ROOT) && rm -rf $(WG_OUT) && mkdir -p $(WG_OUT) && \
	  fail() { echo "$$1"; echo "RL-WINDOW-GATE FAIL"; exit 1; }; \
	  [ $$(ls -d $(SEEDSET)/s*/ 2>/dev/null | wc -l) -ge 48 ] || fail "SEEDSET=$(SEEDSET): 48 starts or more wanted"; \
	  for g in 2 4; do \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/vec_bench --config configs/brute.yaml --ns 32 --groups $$g \
	      --threads $$((2 * g)) --ep-len 200 --warm 0 --steps 2000 --starts $(SEEDSET) --window 8:4 --digest \
	      > $(WG_OUT)/g$$g.log 2>&1 || fail "vec_bench --groups $$g: $$(tail -1 $(WG_OUT)/g$$g.log)"; \
	  done; \
	  d2=$$(grep '^trajectory digest' $(WG_OUT)/g2.log); d4=$$(grep '^trajectory digest' $(WG_OUT)/g4.log); \
	  s4=$$(grep '^starts:' $(WG_OUT)/g4.log); echo "2 groups: $$d2"; echo "4 groups: $$d4"; echo "$$s4"; \
	  [ -n "$$d2" ] && [ "$$d2" = "$$d4" ] || fail "the digests differ"; \
	  echo "$$s4" | grep -q '(44 starts played' || fail "not the 44 starts ten rounds name"; \
	  echo "$$s4" | grep -q 'at most 16 loaded' || fail "more than window + 2 strides loaded"; \
	  echo "RL-WINDOW-GATE PASS"

rl-picks-gate: rlbind $(RT_OUT)/vec_bench
	@cd $(RT_ROOT) && rm -rf $(PG_OUT) && mkdir -p $(PG_OUT) && \
	  fail() { echo "$$1"; echo "RL-PICKS-GATE FAIL"; exit 1; }; \
	  for p in 1 2; do \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/vec_bench --config configs/tape-gate.yaml --ns 32 --groups 1 \
	      --threads 8 --warm 0 --steps 1250 --ep-len 1200 --macros 15 --picks $$p > $(PG_OUT)/picks$$p.log 2>&1 || \
	      fail "vec_bench --picks $$p: $$(tail -1 $(PG_OUT)/picks$$p.log)"; \
	  done; \
	  l1=$$(grep '^picks 1:' $(PG_OUT)/picks1.log); l2=$$(grep '^picks 2:' $(PG_OUT)/picks2.log); echo "$$l1"; echo "$$l2"; \
	  echo "$$l1" | grep -q ', 0 items crafted' || fail "no picks sent, and something was crafted"; \
	  echo "$$l2" | grep -q 'and 0 disallowed;' || fail "a disallowed pick started the craft macro"; \
	  echo "$$l2" | grep -q ', 0 items crafted' && fail "random picks crafted nothing"; \
	  echo "RL-PICKS-GATE PASS"

# lane/nether: NETH_VEC_NETHER's case (nether_case.c's header), out/runtime/nether_case
nether_case: $(RT_OUT)/nether_case
$(RT_OUT)/nether_case: $(RT_DIR)/nether_case.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ nether_case.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

# lane/rod: the End kit macros' case (kit_case.c's header: armor_kit), out/runtime/kit_case
.PHONY: kit_case
kit_case: $(RT_OUT)/kit_case
$(RT_OUT)/kit_case: $(RT_DIR)/kit_case.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ kit_case.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

# lane/obsidian2: NETH_VEC_OBSIDIAN's case (obsidian_case.c's header), out/runtime/obsidian_case
.PHONY: obsidian_case
obsidian_case: $(RT_OUT)/obsidian_case
$(RT_OUT)/obsidian_case: $(RT_DIR)/obsidian_case.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ obsidian_case.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

# lane/eyetrade: the eye-of-ender trade route measured (trade_case.c's and trade_census.c's headers):
# out/runtime/trade_case (the strip's goods an hour in the vec env) and out/runtime/trade_census (the villages near each
# spawn, the ore at the strip's y in the seed world, the trade ladder through csrc/engine/trades.c; the engine alone)
.PHONY: trade_case trade_census
trade_case: $(RT_OUT)/trade_case
$(RT_OUT)/trade_case: $(RT_DIR)/trade_case.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ trade_case.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm
trade_census: $(RT_OUT)/trade_census
$(RT_OUT)/trade_census: $(RT_DIR)/trade_census.c $(RT_ROOT)/out/native/libnetherite.a
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -o $@ trade_census.c $(RT_ROOT)/out/native/libnetherite.a -lz -lm

# lane/obsidian5: the lava a player can reach from the surface on each seed (lava_survey.c's header), the engine alone
.PHONY: lava_survey
lava_survey: $(RT_OUT)/lava_survey
$(RT_OUT)/lava_survey: $(RT_DIR)/lava_survey.c $(RT_ROOT)/out/native/libnetherite.a
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -o $@ lava_survey.c $(RT_ROOT)/out/native/libnetherite.a -lz -lm

# lane/strip2: the strip mine's case (strip_case.c's header): goods an hour, deaths by cause, where each env stopped
.PHONY: strip_case
strip_case: $(RT_OUT)/strip_case
$(RT_OUT)/strip_case: $(RT_DIR)/strip_case.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ strip_case.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

vec_bench: $(RT_OUT)/vec_bench

# lane/stronghold: NETH_VEC_STRONGHOLD's case (sh_check.c's header): make -C csrc/runtime sh-check ARGS="--start DIR --dev
# --seed 1"
.PHONY: sh-check
$(RT_OUT)/sh_check: $(RT_DIR)/sh_check.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlgui.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ sh_check.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

sh-check: rlbind $(RT_OUT)/sh_check
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/sh_check --config configs/train-fast.yaml $(ARGS)

$(RT_OUT)/vec_bench: $(RT_DIR)/vec_bench.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ vec_bench.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

# lane/dragon: the End fight's case (dragon_case.c's header)
$(RT_OUT)/dragon_case: $(RT_DIR)/dragon_case.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ dragon_case.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

dragon-case: rlbind $(RT_OUT)/dragon_case

vec-bench: rlbind $(RT_OUT)/vec_bench
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/vec_bench --config $(CONFIG_VEC) --ns $(VEC_NS) $(ARGS)

# lane/pearl: NETH_VEC_PEARL's case (pearl_check.c's header)
.PHONY: pearl_check
pearl_check: $(RT_OUT)/pearl_check
$(RT_OUT)/pearl_check: $(RT_DIR)/pearl_check.c $(RT_DIR)/vecenv.h $(RT_DIR)/../mods/macros/pearl.h $(RT_DIR)/rlbind.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ pearl_check.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

$(RT_OUT)/vectape_gate: $(RT_DIR)/vectape_gate.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlgui.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ vectape_gate.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

$(RT_OUT)/vecplay: $(RT_DIR)/vecplay.c $(RT_DIR)/vecenv.h $(RT_DIR)/rlgui.h $(RT_DIR)/rlgui_int.h $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ vecplay.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

rl-play-gate: rlbind $(RT_OUT)/vecplay
	@cd $(RT_ROOT) && bash csrc/runtime/playgate.sh $(PLG_CONF) $(PLG_START) $(RT_OUT)/playgate $(PLG_ACTS) "$(PLG_SEEDS)"

rl-tape-gate: rlbind pool-native $(RT_OUT)/vectape_gate
	@cd $(RT_ROOT) && rm -rf $(TG_OUT) out/java/try/tapegate && mkdir -p $(TG_OUT) && \
	  fail() { echo "$$1"; echo "RL-TAPE-GATE FAIL"; exit 1; }; \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/vectape_gate --config $(TAPE_CONF) --script $(TG_OUT)/script.jsonl \
	    --rows $(TG_OUT)/vec.rows > $(TG_OUT)/gate.log 2>&1 || fail "$$(tail -1 $(TG_OUT)/gate.log)"; \
	  seed=$$(awk '$$1 == "seed:" {print $$2; exit}' $(TAPE_CONF)); \
	  awk '/^world:/ {w = 1; print; next} w && /^[^ #]/ {w = 0} w' $(TAPE_CONF) > $(TG_OUT)/world.yaml; \
	  $(MAKE) --no-print-directory -s -C oracle script SEED=$$seed CONF=$(TG_OUT)/world.yaml SCRIPT=$(TG_OUT)/script.jsonl \
	    TAPE=../out/java/try/tapegate/tape.jsonl CACHE=0 > $(TG_OUT)/java.log 2>&1 || fail "oracle: $$(tail -1 $(TG_OUT)/java.log)"; \
	  $(RT_NOUT)/test_snapshots --row-dump $(TG_OUT)/java.rows out/java/try/tapegate > $(TG_OUT)/replay.log 2>&1 || \
	    fail "test_snapshots: $$(tail -1 $(TG_OUT)/replay.log)"; \
	  awk '/^t=2 /{k++} k == 1' $(TG_OUT)/vec.rows > $(TG_OUT)/vec.ep.rows; \
	  grep -v '^end ' $(TG_OUT)/java.rows > $(TG_OUT)/java.ep.rows; \
	  n=$$(wc -l < $(TG_OUT)/java.ep.rows); \
	  [ $$n -gt 0 ] && cmp -s $(TG_OUT)/vec.ep.rows $(TG_OUT)/java.ep.rows || \
	    fail "rows differ: $$(diff $(TG_OUT)/vec.ep.rows $(TG_OUT)/java.ep.rows | head -2 | cut -c1-200 | tr '\n' ' ')"; \
	  echo "$$(tail -1 $(TG_OUT)/gate.log); $$n rows equal to the oracle's"; echo "RL-TAPE-GATE PASS"
