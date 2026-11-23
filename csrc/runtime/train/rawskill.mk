# lane/rawskill: one physical skill on the raw controls (the run file's task section, vecenv_task.c), its starts and
# its loop.
#
#   make -C csrc/runtime rawstarts      out/runtime/train/rawstarts (train/rawstarts.c's header: the dev starts with a
#                                       stone pickaxe and iron ore in view, from the seed set's spawns)
#   make -C csrc/runtime rawskill [RS_RUN=configs/tests/rawskill-pot.yaml] [RS_STEPS=400000] [RS_SEED=K] [RS_V=w1.dk.or]
#                                 [RS_OUT=DIR] [RS_EVAL_N=64] [RS_EVAL_SEED=7] [RS_NS=256]
#       the loop: the starts made where missing (RS_TRAIN, seeds RS_TRAIN_SEEDS; RS_HELD, seeds RS_HELD_SEEDS, never
#       trained on), a fresh net (ntinit RS_SEED), PPO for RS_STEPS env-steps (ntloop --run RS_RUN --dev), then the
#       frozen policy on the held-out starts (ntloop --score, RS_EVAL_N envs, sampler seed RS_EVAL_SEED, one episode
#       an env counted: its first), and the lines RAWSKILL-TIME (the wall's parts: the process and CUDA start, the
#       starts' loading, the env steps with the learner beside them, the eval) and RAWSKILL (held-out success, the
#       median steps to the pickup, deaths a run; the training curve's last rows). Inside a GPU window:
#       ~/dev/netherite-v2/out/flywheel/bin/gpuq.sh check --mem 8000 -- make -C csrc/runtime rawskill ...
#       RS_V=w1.dk.or (16x16 with the dig and the seen ore's scalars), s1.dk.or (64x64),
#       ...: the variant is built first (a no-op make is 0.8 s). RS_NS envs (256: about 18 GB resident with the 26
#       starts), RS_ARGS more ntloop flags for the training alone (--lr 1e-3, --library LIB, --start-pin off, ...),
#       RS_TASK the task's own (--sticky on, --task-crack X: the training, its checkpoint evals and the held-out eval
#       alike, so the frozen policy plays the task it learned); RS_CURVE=N (0 off):
#       a checkpoint every N env-steps played frozen on the held-out starts beside the training (ntloop --eval-every,
#       32 envs, --eval-extra), its episodes in RS_OUT/ckpt-*.ntw.eval (the RAWSKILL-HELDOUT line: success by env-steps)
.PHONY: rawstarts rawskill
rawstarts: $(NT_OUT)/rawstarts
$(NT_OUT)/rawstarts: $(NT_DIR)/rawstarts.c $(RT_ROOT)/out/native/libnetherite.a
	@mkdir -p $(NT_OUT)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -o $@ train/rawstarts.c $(RT_ROOT)/out/native/libnetherite.a -lz -lm

RS_RUN         ?= configs/tests/rawskill-pot.yaml
RS_STEPS       ?= 400000
RS_SEED        ?= 1
RS_V           ?= w1.dk.or
RS_BIN          = $(NT_OUT)/$(if $(RS_V),$(RS_V)/,)ntloop
RS_INIT         = $(NT_OUT)/$(if $(RS_V),$(RS_V)/,)ntinit
RS_OUT         ?= $(RT_OUT)/rawskill/run-$(if $(RS_V),$(RS_V)-,)s$(RS_SEED)
RS_SET         ?= $(firstword $(wildcard $(RT_ROOT)/out/java/try/seedset-d989661987) $(wildcard $(RT_ROOT)/out/java/try/seedset-*))
RS_TRAIN       ?= $(RT_OUT)/rawskill/train
RS_TRAIN_SEEDS ?= 1-16
RS_HELD        ?= $(RT_OUT)/rawskill/heldout
RS_HELD_SEEDS  ?= 501-516
RS_EVAL_N      ?= 64
RS_EVAL_SEED   ?= 7
RS_NS          ?= 256
RS_ARGS        ?=
RS_CURVE       ?= 0
RS_TASK        ?=
# lane/rawpix: RS_HELD2=LIB a second held-out library (the dark caves: rawstarts --cave --no-torch --seed 2), its
# eval in RS_OUT/eval2 and a RAWSKILL held-out2 line; empty (the default) none
RS_HELD2       ?=
# lane/rawtree: an eval episode's minutes (the held-out evals and the curve's)
RS_MIN         ?= 0.1667

rawskill: $(NT_OUT)/rawstarts
	@cd $(RT_ROOT) && set -e; \
	  $(MAKE) --no-print-directory -s -C $(RT_DIR) $(if $(RS_V),train-loop-v NT_V=$(RS_V),train-loop); \
	  [ -x $(RS_INIT) ] || $(MAKE) --no-print-directory -s -C $(RT_DIR) $(RS_INIT); \
	  mkdir -p $(RS_TRAIN) $(RS_HELD); \
	  [ -f $(RS_TRAIN)/index.tsv ] || $(NT_OUT)/rawstarts --set $(RS_SET) --seeds $(RS_TRAIN_SEEDS) --per 2 --out $(RS_TRAIN) | tail -1; \
	  [ -f $(RS_HELD)/index.tsv ] || $(NT_OUT)/rawstarts --set $(RS_SET) --seeds $(RS_HELD_SEEDS) --per 2 --seed 2 --out $(RS_HELD) | tail -1; \
	  rm -rf $(RS_OUT) && mkdir -p $(RS_OUT); \
	  held0=$$(awk -F'\t' 'NR == 2 {print $$9}' $(RS_HELD)/index.tsv); \
	  t0=$$(date +%s.%N); \
	  $(RS_INIT) $(RS_OUT)/init.ntw $(RS_SEED) > /dev/null; \
	  $(RS_BIN) --run $(RS_RUN) --dev --policy actor --init $(RS_OUT)/init.ntw --fresh-opt --env-steps $(RS_STEPS) \
	    --ns $(RS_NS) --seed $(RS_SEED) --out $(RS_OUT) $(RS_TASK) $(RS_ARGS) \
	    $(if $(filter-out 0,$(RS_CURVE)),--eval-every $(RS_CURVE) --eval-runs 32 --eval-minutes $(RS_MIN) --eval-seed $(RS_EVAL_SEED) \
	      --eval-threads 8 --eval-extra "--dev --start $$held0 --library $(RS_HELD) $(RS_TASK)") > $(RS_OUT)/train.log 2>&1 || \
	    { tail -5 $(RS_OUT)/train.log; exit 1; }; \
	  t1=$$(date +%s.%N); \
	  $(RS_BIN) --score --run $(RS_RUN) --dev --init $(RS_OUT)/model.ntw --start $$held0 --library $(RS_HELD) \
	    --ns $(RS_EVAL_N) --minutes $(RS_MIN) --seed $(RS_EVAL_SEED) --out $(RS_OUT)/eval $(RS_TASK) > $(RS_OUT)/eval.log 2>&1 || \
	    { tail -5 $(RS_OUT)/eval.log; exit 1; }; \
	  t2=$$(date +%s.%N); \
	  bash $(NT_DIR)/rawskill_sum.sh $(RS_OUT) $$t0 $$t1 $$t2; \
	  $(if $(RS_HELD2),held2=$$(awk -F'\t' 'NR == 2 {print $$9}' $(RS_HELD2)/index.tsv); \
	  $(RS_BIN) --score --run $(RS_RUN) --dev --init $(RS_OUT)/model.ntw --start $$held2 --library $(RS_HELD2) \
	    --ns $(RS_EVAL_N) --minutes $(RS_MIN) --seed $(RS_EVAL_SEED) --out $(RS_OUT)/eval2 $(RS_TASK) > $(RS_OUT)/eval2.log 2>&1 || \
	    { tail -5 $(RS_OUT)/eval2.log; exit 1; }; \
	  RS_EVAL=eval2 bash $(NT_DIR)/rawskill_sum.sh $(RS_OUT) $$t0 $$t1 $$t2 | grep '^RAWSKILL held' | sed 's/held-out/held-out2/',)
