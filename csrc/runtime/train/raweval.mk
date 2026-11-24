# lane/eval1k: the raw skills' eval at about 1,000 held-out episodes a model (standard error about 0.015 at 0.7), the
# same starts and sampler seeds for every model of a task, so two models compare start by start.
#
#   make -C csrc/runtime raweval RE_W=MODEL.ntw RE_TASK=ore [RE_KIND=student|teacher] [RE_ARM=strip|van] [RE_OUT=FILE.tsv]
#                                [RE_LABEL=NAME] [RE_BUILD=s1.dk.or.r1] [RE_TEMP=T] [RE_MIN=M] [RE_K=256] [RE_R=4]
#       (lane/audit-fixes: RE_TEMP is ntloop's --temp, the student's 0.5 and the teacher's 1 by default; the build is
#       the net alone, no .tN word)
#       one model on one task: raweval.sh's header has every knob and the tasks (ore, orecave, hard, hardcave,
#       logview, logany, lognear, logs3near, logs3view); RE_OUT (out/e/NAME-TASK-ARM.tsv) a row an episode, and the
#       line RAWEVAL-TIME (starts loaded, env steps, the rest) and rawstat's one line: success, its standard error
#       (start-cluster, and the binomial one beside it), deaths, the variance between starts and within a start.
#   make -C csrc/runtime raweval-starts     the ore libraries the evals draw from (out/runtime/eval1k/{ore-plain,
#       ore-cave,hard-plain,hard-cave}: rawstarts on world seeds 501 to 1000, placement seed 2, two starts a seed: never
#       the training seeds 1 to 100 or the validation 17 to 32), and rawtree-logs' held-out log libraries
#   make -C csrc/runtime rawstat            out/runtime/train/rawstat (train/rawstat.c: one, pair, table)
#   out/runtime/train/rawstat pair A.tsv B.tsv   A - B start by start, a 95% interval, the starts A won and lost
#   out/runtime/train/rawstat table MANIFEST     every eval, recipe (the spread of its runs less the evals' noise),
#       pair and recipe comparison of a manifest into tables (rawstat.c's header has the lines)
#   Inside a GPU window: ~/dev/netherite-v2/out/flywheel/bin/gpuq.sh check --mem 8000 -- make -C csrc/runtime raweval ...
#   A process holds about 7.5 GB + 0.09 GB a start + 0.058 GB an env of host memory (28 GB at 64 starts and 256 envs),
#   so each runs under the host-memory lock (raweval.sh -L: none).
.PHONY: raweval raweval-starts rawstat
RE_TASK  ?= ore
RE_KIND  ?= student
RE_ARM   ?= strip
RE_W     ?=
RE_LABEL ?= $(basename $(notdir $(RE_W)))
RE_OUT   ?= $(RT_ROOT)/out/e/$(RE_LABEL)-$(RE_TASK)-$(RE_ARM).tsv
RE_EXTRA ?=

rawstat: $(NT_OUT)/rawstat
$(NT_OUT)/rawstat: $(NT_DIR)/rawstat.c
	@mkdir -p $(NT_OUT)
	cd $(RT_DIR) && cc -O2 -Wall -Wextra -o $@ train/rawstat.c -lm

raweval: $(NT_OUT)/rawstat
	@test -n "$(RE_W)" || { echo "raweval: RE_W=MODEL.ntw"; exit 2; }
	@cd $(RT_ROOT) && bash $(NT_DIR)/raweval.sh -w $(RE_W) -t $(RE_TASK) -k $(RE_KIND) -a $(RE_ARM) -l $(RE_LABEL) -o $(RE_OUT) \
	  $(if $(RE_BUILD),-b $(RE_BUILD)) $(if $(RE_TEMP),-T $(RE_TEMP)) $(if $(RE_MIN),-m $(RE_MIN)) $(if $(RE_K),-K $(RE_K)) $(if $(RE_R),-R $(RE_R)) $(RE_EXTRA)

RE_SET   ?= $(RS_SET)
RE_LIBS  ?= $(RT_OUT)/eval1k
RE_SEEDS ?= 501-1000
raweval-starts: $(NT_OUT)/rawstarts
	@cd $(RT_ROOT) && set -e; mkdir -p $(RE_LIBS); \
	  for d in ore-plain: ore-cave:--cave,--no-torch hard-plain:--dmin,5,--dmax,12,--turn,180,--turn-min,90 \
	           hard-cave:--dmin,5,--dmax,12,--turn,180,--turn-min,90,--cave,--no-torch,--embed; do \
	    n=$${d%%:*}; f=$$(echo $${d#*:} | tr , ' '); [ -f $(RE_LIBS)/$$n/index.tsv ] && continue; mkdir -p $(RE_LIBS)/$$n; \
	    $(NT_OUT)/rawstarts --set $(RE_SET) --seeds $(RE_SEEDS) --per 2 --seed 2 $$f --out $(RE_LIBS)/$$n | tail -1; \
	  done
	@mkdir -p $(RT_OUT)/rawtree/logs-all; $(MAKE) --no-print-directory -s -C $(RT_DIR) rawtree-logs
