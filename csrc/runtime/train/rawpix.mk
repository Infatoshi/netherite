# lane/rawpix: the ore skill from pixels alone (lane/rawskill's task, starts and PPO), in one command:
#
#   make -C csrc/runtime rawpix [RP_SEED=K] [RP_RUN=configs/rawpix64.yaml] [RP_V=s1.dk.or] [RP_TSTEPS=400000]
#                               [RP_STEPS=800000] [RP_ARGS=--beta 0.9] [RP_OUT=DIR] [RP_TEACHER=W.ntw] [RP_CURVE=N]
#     The defaults are the measured recipe (DEVLOG 2026-11-05 lane/rawpix): a teacher on the 16 seeds' plain starts,
#     the student on the 100 seeds' plain and dark-cave mix by DAgger (beta 0.9), the auto exposure, the evals at
#     temperature 0.5 and sampled, 128 envs each; about a minute a run on the dev host's 3090.
#       1. the starts where missing: rawskill's plain training and held-out libraries, the dark caves' (rawstarts --cave
#          --no-torch: seeds 1-16 to train, 501-516 held out) and the mix (plain and caves in one index, RP_MIX); the
#          wide ones, plain and caves from seeds RP_WIDE_SEEDS (1-100) and their mix RP_WMIX, and RP_HMIX: RP_WMIX's
#          starts of seeds 1 to RP_HMIX_MAX (50) (RP_LIB=... to train on any of them)
#       2. the teacher: PPO from a fresh net that sees the seen-ore scalars (RP_V, a .or build), RP_TSTEPS env-steps on
#          RP_TLIB's starts (the plain ones; make rawskill's loop; RP_TARGS more flags, --ent X); RP_TEACHER=W.ntw
#          skips it and takes W
#       3. the student: a fresh net of the same build with zero weights on the ore scalars (the build without .or's
#          ntinit), distilled from the teacher's actions on RP_LIB's starts (the mix of plain and dark caves; ntloop
#          --teacher, --student-zero ore: the scalars read 0 in its loss, so their weights stay 0), RP_STEPS env-steps,
#          RP_ARGS more flags (--beta B: DAgger, --soft 1, --student-prev env)
#       4. the frozen student on the held-out plain and dark-cave starts with the ore scalars zeroed (--zero-sc ore:
#          pixels, the crack scalar and the player's own state), RP_EVAL_N envs (64), eval seed RP_EVAL_SEED, each env's first
#          episode, in each build of RP_EVALS: RP_V.t5 (the keys and camera drawn at temperature 0.5, net.h NETH_ACTOR_TEMP)
#          and RP_V (sampled, as the training's actor); RP_V.gkc is the greedy one
#     RP_TONE=auto|gamma:G (empty: none; auto the default): every observation the teacher, the student and the evals read through ntloop
#     --obs-tone (auto: each frame times 255 over its brightest byte, the camera's auto exposure);
#     RP_CURVE=N (0 off): the student's checkpoint every N env-steps played frozen on RP_CURVE_LIB (the plain held-out
#     starts; 32 envs, sampled; its ore weights are 0, so no --zero-sc is needed), RP_OUT/ckpt-*.ntw.eval;
#     and the lines RAWPIX-TIME (the parts' wall seconds) and RAWPIX (held-out success by build, plain and dark caves).
#     lane/rawsearch: RP_SV=BUILD the student's build (RP_V, the teacher's, by default; s1.dk.or.r1 a minGRU student of a
#     feed-forward teacher: distill.cu takes the teacher's blob with the minGRU's tensors zero), its evals RP_SV.t5 and
#     RP_SV; RP_VIEW=K (empty: none) the evals' view books of seen kind K (ntloop --view-books: 0 iron ore, 2 logs),
#     EVAL/view.jsonl and a RAWPIX-VIEW line each (viewbooks.sh: of the first episodes, won and lost, the share that saw
#     the object, lost it from view, got it back, and had it in view at the end); those evals run with --task-hint 0
#     (the books read the seen-object flag, which a run file's task.hint would fill when nothing is drawn; the student's
#     scalars are zeroed either way, so its play is the same).
#     Inside a GPU window: ~/dev/netherite-v2/out/flywheel/bin/gpuq.sh check --mem 20000 -- make -C csrc/runtime rawpix
#     Models: RP_OUT/teacher.ntw and RP_OUT/model.ntw (the student).
.PHONY: rawpix rawpix-starts
RP_RUN         ?= configs/rawpix64.yaml
RP_V           ?= s1.dk.or
RP_SV          ?= $(RP_V)
RP_VIEW        ?=
RP_SEED        ?= 1
RP_TSTEPS      ?= 400000
RP_STEPS       ?= 800000
RP_ARGS        ?= --beta 0.9
RP_TEACHER     ?=
RP_EVAL_SEED   ?= 7
RP_NS          ?= 256
RP_CURVE       ?= 0
# lane/rawtree: an eval episode's minutes (the held-out evals and the curve's), and the item a success holds
RP_MIN         ?= 0.1667
RP_ITEM        ?= 15
# lane/rawsearch: the count of RP_ITEM a success holds (1; task.count's N for several logs)
RP_NEED        ?= 1
RP_CURVE_LIB   ?= $(RS_HELD)
RP_OUT         ?= $(RT_OUT)/rawpix/run-$(RP_V)-s$(RP_SEED)
RP_CAVE        ?= $(RT_OUT)/rawskill/train-cave
RP_HCAVE       ?= $(RT_OUT)/rawskill/heldout-cave
RP_MIX         ?= $(RT_OUT)/rawskill/train-mix
RP_WIDE_SEEDS  ?= 1-100
RP_WIDE        ?= $(RT_OUT)/rawskill/train100
RP_WCAVE       ?= $(RT_OUT)/rawskill/train100-cave
RP_WMIX        ?= $(RT_OUT)/rawskill/train100-mix
RP_HMIX_MAX    ?= 50
RP_HMIX        ?= $(RT_OUT)/rawskill/train$(RP_HMIX_MAX)-mix
RP_TLIB        ?= $(RS_TRAIN)
RP_LIB         ?= $(RP_WMIX)
RP_EVALS       ?= $(RP_SV).t5 $(RP_SV)
RP_TONE        ?= auto
RP_EVAL_N      ?= 128
RP_TARGS       ?=
RP_TONEF        = $(if $(RP_TONE),--obs-tone $(RP_TONE))
RP_BIN          = $(NT_OUT)/$(RP_SV)/ntloop
RP_SINIT        = $(NT_OUT)/$(subst .or,,$(RP_SV))/ntinit

rawpix-starts: $(NT_OUT)/rawstarts
	@cd $(RT_ROOT) && set -e; \
	  for d in $(RS_TRAIN):$(RS_TRAIN_SEEDS):1: $(RS_HELD):$(RS_HELD_SEEDS):2: \
	           $(RP_CAVE):$(RS_TRAIN_SEEDS):1:--cave $(RP_HCAVE):$(RS_HELD_SEEDS):2:--cave \
	           $(RP_WIDE):$(RP_WIDE_SEEDS):1: $(RP_WCAVE):$(RP_WIDE_SEEDS):1:--cave; do \
	    set -- $$(echo $$d | tr : ' '); \
	    [ -f $$1/index.tsv ] && continue; mkdir -p $$1; \
	    $(NT_OUT)/rawstarts --set $(RS_SET) --seeds $$2 --per 2 --seed $$3 $${4:+--cave --no-torch} --out $$1 | tail -1; \
	  done; \
	  for d in $(RP_MIX):$(RS_TRAIN):$(RP_CAVE) $(RP_WMIX):$(RP_WIDE):$(RP_WCAVE); do \
	    set -- $$(echo $$d | tr : ' '); \
	    if [ ! -f $$1/index.tsv ] || [ $$2/index.tsv -nt $$1/index.tsv ] || [ $$3/index.tsv -nt $$1/index.tsv ]; then \
	      mkdir -p $$1; { cat $$2/index.tsv; tail -n +2 $$3/index.tsv | sed 's/^s/c/'; } > $$1/index.tsv; \
	    fi; \
	  done; \
	  if [ ! -f $(RP_HMIX)/index.tsv ] || [ $(RP_WMIX)/index.tsv -nt $(RP_HMIX)/index.tsv ]; then \
	    mkdir -p $(RP_HMIX); awk -F'\t' 'NR == 1 || $$2 <= $(RP_HMIX_MAX)' $(RP_WMIX)/index.tsv > $(RP_HMIX)/index.tsv; fi

rawpix: rawpix-starts
	@cd $(RT_ROOT) && set -e; \
	  for v in $(sort $(RP_V) $(RP_SV)) $(RP_EVALS); do $(MAKE) --no-print-directory -s -C $(RT_DIR) train-loop-v NT_V=$$v; done; \
	  [ -x $(RP_SINIT) ] || $(MAKE) --no-print-directory -s -C $(RT_DIR) train-loop-v NT_V=$(subst .or,,$(RP_SV)) $(RP_SINIT); \
	  rm -rf $(RP_OUT) && mkdir -p $(RP_OUT); \
	  t0=$$(date +%s.%N); \
	  if [ -n "$(RP_TEACHER)" ]; then cp $(RP_TEACHER) $(RP_OUT)/teacher.ntw; else \
	    $(MAKE) --no-print-directory -s -C $(RT_DIR) rawskill RS_ITEM=$(RP_ITEM) RS_MIN=$(RP_MIN) RS_V=$(RP_V) RS_RUN=$(RP_RUN) RS_SEED=$(RP_SEED) \
	      RS_STEPS=$(RP_TSTEPS) RS_NS=$(RP_NS) RS_OUT=$(RP_OUT)/teacher RS_ARGS='--library $(RP_TLIB) $(RP_TARGS)' RS_TASK='$(RP_TONEF)' | \
	      grep '^RAWSKILL held' | sed 's/^RAWSKILL/RAWPIX teacher (sampled, seen-ore scalars)/'; \
	    cp $(RP_OUT)/teacher/model.ntw $(RP_OUT)/teacher.ntw; fi; \
	  t1=$$(date +%s.%N); \
	  $(RP_SINIT) $(RP_OUT)/init.ntw $(RP_SEED) > /dev/null; \
	  $(RP_BIN) --run $(RP_RUN) --dev --policy actor --teacher $(RP_OUT)/teacher.ntw --init $(RP_OUT)/init.ntw --fresh-opt \
	    --env-steps $(RP_STEPS) --ns $(RP_NS) --seed $(RP_SEED) --out $(RP_OUT) --library $(RP_LIB) --student-zero ore $(RP_TONEF) \
	    $(if $(filter-out 0,$(RP_CURVE)),--eval-every $(RP_CURVE) --eval-runs 32 --eval-minutes $(RP_MIN) --eval-seed $(RP_EVAL_SEED) \
	      --eval-threads 8 --eval-extra "--dev --start $$(awk -F'\t' 'NR == 2 {print $$9}' $(RP_CURVE_LIB)/index.tsv) --library $(RP_CURVE_LIB) $(RP_TONEF)") \
	    $(RP_ARGS) > $(RP_OUT)/train.log 2>&1 || { tail -5 $(RP_OUT)/train.log; exit 1; }; \
	  t2=$$(date +%s.%N); \
	  for v in $(RP_EVALS); do for lib in $(RS_HELD) $(RP_HCAVE); do \
	    held0=$$(awk -F'\t' 'NR == 2 {print $$9}' $$lib/index.tsv); e=$(RP_OUT)/eval-$$v-$$(basename $$lib); \
	    $(NT_OUT)/$$v/ntloop --score --run $(RP_RUN) --dev --init $(RP_OUT)/model.ntw --start $$held0 --library $$lib \
	      --ns $(RP_EVAL_N) --minutes $(RP_MIN) --seed $(RP_EVAL_SEED) --out $$e --zero-sc ore $(RP_TONEF) \
	      $(if $(RP_VIEW),--view-books $(RP_VIEW) --task-hint 0) > $$e.log 2>&1 || { tail -5 $$e.log; exit 1; }; \
	  done; done; \
	  t3=$$(date +%s.%N); \
	  rate=$$(sed -n 's/.* \([0-9]*\) env-steps\/s (.*/\1/p' $(RP_OUT)/train.log | head -1); \
	  awk -v a=$$t0 -v b=$$t1 -v c=$$t2 -v d=$$t3 -v r="$$rate" 'BEGIN { \
	    printf "RAWPIX-TIME total %.2f s: teacher %.2f, student %.2f (%s env-steps/s), evals %.2f\n", d - a, b - a, c - b, r, d - c }'; \
	  for v in $(RP_EVALS); do for lib in $(RS_HELD) $(RP_HCAVE); do \
	    jq -s -r --arg lib $$(basename $$lib) --arg v $$v 'group_by(.env) | map(.[0]) | length as $$n | \
	      (map(select((.items["$(RP_ITEM)"] // 0) >= $(RP_NEED))) | length) as $$k | \
	      "RAWPIX pixels-only \($$v) \($$lib) success \($$k / $$n * 1000 | round / 1000) (\($$k) of \($$n)), deaths \(map(.deaths) | add)"' \
	      $(RP_OUT)/eval-$$v-$$(basename $$lib)/episodes.jsonl; \
	  $(if $(RP_VIEW),bash $(NT_DIR)/viewbooks.sh $(RP_OUT)/eval-$$v-$$(basename $$lib) $(RP_ITEM) "RAWPIX-VIEW $$v $$(basename $$lib)";) \
	  done; done
