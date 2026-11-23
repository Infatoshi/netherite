# lane/rawtree: lane/rawpix's recipe (a seen-ore teacher by PPO, a pixels-only student by DAgger, the auto exposure)
# on harder starts, in one command:
#
#   make -C csrc/runtime rawtree [TR_TASK=ore] [RP_SEED=K] [RP_...: rawpix's knobs]
#     TR_TASK=ore: iron ore 5 to 12 blocks away, turned 90 to 180 degrees from the look (behind the player), on plain
#       spawns, and in dark caves (no torch) set into the cave's wall or floor with one face showing (rawstarts
#       --embed). The libraries (rawtree-starts, made where missing): TR_HP (plain, seeds RS_TRAIN_SEEDS), TR_HPW and
#       TR_HCW (plain and caves, seeds RP_WIDE_SEEDS) and their mix TR_HMIX, the held-out TR_HHP and TR_HHC (seeds
#       RS_HELD_SEEDS, rawstarts --seed 2, never trained on). The teacher on TR_HTMIX (plain and caves, 16 seeds, 1.6M),
#       the student on TR_HMIX (100 seeds, 1.6M), the evals on TR_HHP and TR_HHC; the lines RAWTREE-TIME and RAWTREE.
#     TR_TASK=log: a log (block 17) from a seed's spawn as it is (rawtree-logs' libraries: no dev setup, an empty
#       inventory), the teacher seeing the seen-log scalars (the .or build's, the ore's) on the spawns with a log in
#       view (TR_LVIEW, 800k, started from the ore teacher: TR_TINIT), the student pixels-only on the spawns with a log
#       within 16 blocks (TR_LNEAR, 1.6M); the evals on the held-out spawns with a log in view and on every one.
#     TR_TASK=search (lane/rawsearch): a log from any spawn in 60 s, the teacher with the hint (task.hint), below.
#   Each task's defaults are TR_*_ore, TR_*_log and TR_*_search: TR_TLIB the teacher's library, TR_LIB the student's, TR_H1 and
#   TR_H2 the two held-out evals' libraries, TR_RUN the run file (its episode_seconds trains), TR_MIN an episode's
#   minutes at the evals (rawpix's RP_MIN), TR_ITEM the item a success holds; any of them set alone overrides.
#   Inside a GPU window: ~/dev/netherite-v2/out/flywheel/bin/gpuq.sh check --mem 20000 -- make -C csrc/runtime rawtree
.PHONY: rawtree rawtree-starts rawtree-logs
TR_TASK     ?= ore
TR_HDIR     ?= $(RT_OUT)/rawtree
TR_HFLAGS   ?= --dmin 5 --dmax 12 --turn 180 --turn-min 90
TR_HCFLAGS  ?= --cave --no-torch --embed
TR_HP       ?= $(TR_HDIR)/hard
TR_HPW      ?= $(TR_HDIR)/hard100
TR_HCW      ?= $(TR_HDIR)/hard100-cave
TR_HMIX     ?= $(TR_HDIR)/hard100-mix
TR_HHP      ?= $(TR_HDIR)/heldout-hard
TR_HHC      ?= $(TR_HDIR)/heldout-hard-cave
TR_HC16     ?= $(TR_HDIR)/hard16-cave
TR_HTMIX    ?= $(TR_HDIR)/hmix16
TR_TLIB_ore ?= $(TR_HTMIX)
TR_TSTEPS_ore ?= 1600000
# the ore student: 3.2M env-steps at lr 3e-4 (at 1e-3 two of eight students collapsed; its training success levels
# off from 2.2M on)
TR_STEPS_ore  ?= 3200000
TR_SARGS_ore  ?= --beta 0.9 --lr 3e-4
TR_SARGS_log  ?= --beta 0.9
TR_SARGS      ?= $(TR_SARGS_$(TR_TASK))
TR_TSTEPS_log ?= 800000
# the log teacher starts from the ore teacher of the same seed (it already holds the attack while the crack scalar
# rises: the 60-tick punch found by 0.2 to 0.4M env-steps against 0.7 to 1.4M from a fresh net), when it is there
TR_MODELS   ?= $(RT_CACHE)/native/models/rawtree
TR_TINIT_log ?= $(TR_MODELS)/ore-teacher-hmix16-s$(RP_SEED).ntw
TR_TINIT_ore ?=
TR_TINIT    ?= $(TR_TINIT_$(TR_TASK))
TR_STEPS_log  ?= 1600000
TR_TSTEPS   ?= $(TR_TSTEPS_$(TR_TASK))
TR_STEPS    ?= $(TR_STEPS_$(TR_TASK))
TR_LIB_ore  ?= $(TR_HMIX)
TR_H1_ore   ?= $(TR_HHP)
TR_H2_ore   ?= $(TR_HHC)
TR_RUN_ore  ?= configs/rawtree-ore64.yaml
TR_MIN_ore  ?= 0.3333
TR_ITEM_ore ?= 15
TR_TLIB_log ?= $(TR_LVIEW)
TR_LIB_log  ?= $(TR_LNEAR)
TR_H1_log   ?= $(TR_LVIEW)-held
TR_H2_log   ?= $(TR_LANY)-held
TR_RUN_log  ?= configs/rawtree-log64.yaml
TR_MIN_log  ?= 0.3333
TR_ITEM_log ?= 17
# lane/rawsearch: TR_TASK=search, a log from any spawn (rawtree-logs' TR_LANY, 60 s episodes,
# configs/rawsearch-log64.yaml): the teacher sees the hint (task.hint: the nearest log's direction when none is in
# view, privileged), started from rawtree's in-view log teacher of the seed (TR_TINIT_search), PPO on every training
# spawn; the student pixels-only on every training spawn; the evals on every held-out spawn and on those with a log
# within 16 blocks. TR_VIEW (each task's: 0 iron ore, 2 logs) the evals' view books (rawpix RP_VIEW)
# the training spawns of seeds 1 to TR_LSRCH_MAX (TR_LSRCH; a 256-env run holds about 110 MB of host memory a start:
# 40 GB on the 356 ore starts, so not all 499)
TR_LSRCH_MAX    ?= 300
TR_LSRCH        ?= $(TR_HDIR)/logs-search
TR_TLIB_search  ?= $(TR_LSRCH)
TR_LIB_search   ?= $(TR_LSRCH)
TR_H1_search    ?= $(TR_LANY)-held
TR_H2_search    ?= $(TR_LNEAR)-held
TR_RUN_search   ?= configs/rawsearch-log64.yaml
TR_MIN_search   ?= 1
TR_ITEM_search  ?= 17
TR_TSTEPS_search ?= 1600000
TR_STEPS_search ?= 1600000
TR_SARGS_search ?= --beta 0.9
TR_TINIT_search ?= $(TR_MODELS)/log-final-teacher-s$(RP_SEED).ntw
TR_VAL_search   ?= $(TR_LANY)
# lane/rawsearch: TR_TASK=logs3, three logs (task.count 3) from a spawn with a log within 16 blocks in 30 s
# (configs/rawsearch-logs3.yaml), the teacher with the hint started from this lane's search teacher of the seed
# (TR_TINIT_logs3, ~/.cache/netherite-native/models/rawsearch), the student on the same spawns, the evals on the held-out
# spawns with a log within 16 blocks and in view; a success holds TR_NEED logs (rawpix RP_NEED)
TR_SMODELS      ?= $(RT_CACHE)/native/models/rawsearch
TR_TLIB_logs3   ?= $(TR_LNEAR)
TR_LIB_logs3    ?= $(TR_LNEAR)
TR_H1_logs3     ?= $(TR_LNEAR)-held
TR_H2_logs3     ?= $(TR_LVIEW)-held
TR_RUN_logs3    ?= configs/rawsearch-logs3.yaml
TR_MIN_logs3    ?= 0.5
TR_ITEM_logs3   ?= 17
TR_TSTEPS_logs3 ?= 1600000
TR_STEPS_logs3  ?= 1600000
TR_SARGS_logs3  ?= --beta 0.9
TR_TINIT_logs3  ?= $(TR_SMODELS)/search-teacher-h1-s$(RP_SEED).ntw
TR_VAL_logs3    ?= $(TR_LNEAR)
TR_VIEW_logs3   ?= 2
TR_NEED_logs3   ?= 3
TR_NEED         ?= $(or $(TR_NEED_$(TR_TASK)),1)
TR_VIEW_ore     ?= 0
TR_VIEW_log     ?= 2
TR_VIEW_search  ?= 2
TR_VIEW         ?= $(TR_VIEW_$(TR_TASK))
TR_TLIB     ?= $(TR_TLIB_$(TR_TASK))
TR_LIB      ?= $(TR_LIB_$(TR_TASK))
TR_H1       ?= $(TR_H1_$(TR_TASK))
TR_H2       ?= $(TR_H2_$(TR_TASK))
TR_RUN      ?= $(TR_RUN_$(TR_TASK))
TR_MIN      ?= $(TR_MIN_$(TR_TASK))
TR_ITEM     ?= $(TR_ITEM_$(TR_TASK))
TR_OUT      ?= $(TR_HDIR)/run-$(TR_TASK)-s$(RP_SEED)
# TR_TEACHERS=N (1, the default: one teacher, rawpix's own): N teachers trained (sampler seeds RP_SEED * 10 + k), each
# scored on TR_VAL (64 envs, with its scalars: never the held-out evals' starts), and the student distilled from the
# best; the teachers in TR_OUT-teachers/k. The ore's TR_VALO: hard plain and cave starts of seeds TR_VSEEDS placed
# anew (rawstarts --seed 3: worlds the student trains in, never the held-out seeds); the log's: the teacher's own
# in-view spawns
TR_TEACHERS ?= 1
TR_VSEEDS   ?= 17-32
TR_VALP     ?= $(TR_HDIR)/val-hard
TR_VALC     ?= $(TR_HDIR)/val-hard-cave
TR_VALO     ?= $(TR_HDIR)/val-hard-mix
TR_VAL_ore  ?= $(TR_VALO)
TR_VAL_log  ?= $(TR_LVIEW)
TR_VAL      ?= $(TR_VAL_$(TR_TASK))

rawtree-starts: $(NT_OUT)/rawstarts
	@cd $(RT_ROOT) && set -e; \
	  for d in $(TR_HP):$(RS_TRAIN_SEEDS):1: $(TR_HPW):$(RP_WIDE_SEEDS):1: $(TR_HCW):$(RP_WIDE_SEEDS):1:c \
	           $(TR_HHP):$(RS_HELD_SEEDS):2: $(TR_HHC):$(RS_HELD_SEEDS):2:c \
	           $(TR_VALP):$(TR_VSEEDS):3: $(TR_VALC):$(TR_VSEEDS):3:c; do \
	    set -- $$(echo $$d | tr : ' '); \
	    [ -f $$1/index.tsv ] && continue; mkdir -p $$1; \
	    $(NT_OUT)/rawstarts --set $(RS_SET) --seeds $$2 --per 2 --seed $$3 $(TR_HFLAGS) $${4:+$(TR_HCFLAGS)} --out $$1 | tail -1; \
	  done; \
	  if [ ! -f $(TR_HMIX)/index.tsv ] || [ $(TR_HPW)/index.tsv -nt $(TR_HMIX)/index.tsv ] || [ $(TR_HCW)/index.tsv -nt $(TR_HMIX)/index.tsv ]; then \
	    mkdir -p $(TR_HMIX); { cat $(TR_HPW)/index.tsv; tail -n +2 $(TR_HCW)/index.tsv | sed 's/^s/c/'; } > $(TR_HMIX)/index.tsv; fi; \
	  if [ ! -f $(TR_HTMIX)/index.tsv ] || [ $(TR_HCW)/index.tsv -nt $(TR_HTMIX)/index.tsv ]; then \
	    mkdir -p $(TR_HC16) $(TR_HTMIX); awk -F'\t' 'NR == 1 || $$2 <= 16' $(TR_HCW)/index.tsv > $(TR_HC16)/index.tsv; \
	    { cat $(TR_HP)/index.tsv; tail -n +2 $(TR_HC16)/index.tsv | sed 's/^s/c/'; } > $(TR_HTMIX)/index.tsv; fi; \
	  if [ ! -f $(TR_VALO)/index.tsv ]; then \
	    mkdir -p $(TR_VALO); { cat $(TR_VALP)/index.tsv; tail -n +2 $(TR_VALC)/index.tsv | sed 's/^s/c/'; } > $(TR_VALO)/index.tsv; fi

# the log task's libraries (rawstarts --logs: the seed set's spawns unchanged, never a dev start): TR_LALL every spawn
# of seeds 1 to 1000 with its nearest log in sight, then by awk: TR_LVIEW a log within 16 blocks and within 35
# degrees of the spawn's look (trees in view), TR_LNEAR within 16 in any direction, TR_LANY every spawn; seeds
# TR_LTRAIN to train, TR_LHELD held out (never trained on), each library's held-out twin NAME-held
TR_LALL     ?= $(TR_HDIR)/logs-all
TR_LVIEW    ?= $(TR_HDIR)/logs-view
TR_LNEAR    ?= $(TR_HDIR)/logs-near
TR_LANY     ?= $(TR_HDIR)/logs-any
TR_LTRAIN   ?= 1 500
TR_LHELD    ?= 501 1000
TR_LSEL_view = $$7 != "rawtree:log,none" && d <= 16 && a < 35 && a > -35 && p < 35 && p > -35
TR_LSEL_near = $$7 != "rawtree:log,none" && d <= 16
TR_LSEL_any  = 1
rawtree-logs: $(NT_OUT)/rawstarts
	@cd $(RT_ROOT) && set -e; \
	  [ -f $(TR_LALL)/index.tsv ] || $(NT_OUT)/rawstarts --logs --set $(RS_SET) --seeds 1-1000 --out $(TR_LALL) | tail -1; \
	  set -- $(TR_LTRAIN) $(TR_LHELD); \
	  for k in view:$(TR_LVIEW) near:$(TR_LNEAR) any:$(TR_LANY); do \
	    n=$${k%%:*} d=$${k#*:}; \
	    for part in "$$d $$1 $$2" "$$d-held $$3 $$4"; do set -- $$part $(TR_LTRAIN) $(TR_LHELD); \
	      [ -f $$1/index.tsv ] && [ $$1/index.tsv -nt $(TR_LALL)/index.tsv ] && continue; mkdir -p $$1; \
	      awk -F'\t' -v lo=$$2 -v hi=$$3 -v sel=$$n 'NR == 1 { print; next } $$2 < lo || $$2 > hi { next } \
	        { d = 99; a = 999; p = 999; m = split($$7, f, /[,=]/); \
	          for (i = 1; i < m; ++i) { if (f[i] == "d") d = f[i + 1]; if (f[i] == "a") a = f[i + 1]; if (f[i] == "p") p = f[i + 1] } \
	          if ((sel == "view" && $(TR_LSEL_view)) || (sel == "near" && $(TR_LSEL_near)) || sel == "any") print }' \
	        $(TR_LALL)/index.tsv > $$1/index.tsv; \
	      echo "$$1: $$(($$(wc -l < $$1/index.tsv) - 1)) spawns"; \
	      set -- $(TR_LTRAIN) $(TR_LHELD); done; \
	  done; \
	  if [ ! -f $(TR_LSRCH)/index.tsv ] || [ $(TR_LANY)/index.tsv -nt $(TR_LSRCH)/index.tsv ]; then mkdir -p $(TR_LSRCH); \
	    awk -F'\t' 'NR == 1 || $$2 <= $(TR_LSRCH_MAX)' $(TR_LANY)/index.tsv > $(TR_LSRCH)/index.tsv; \
	    echo "$(TR_LSRCH): $$(($$(wc -l < $(TR_LSRCH)/index.tsv) - 1)) spawns"; fi

# lane/rawsearch: TR_PPO_TASK more teacher flags by task: logs3's PPO at lr 1e-3 from the search teacher collapsed (a
# log in 177 of 256 episodes, then 9 of 256 by 244k env-steps, none by 384k); at lr 3e-4 with the KL to its start
# (ntloop --ref-kl 1) it holds
TR_PPO_logs3 ?= --lr 3e-4 --ref-kl 1
TR_TARGS     = $(RP_TARGS) $(TR_PPO_$(TR_TASK)) $(if $(wildcard $(TR_TINIT)),--init $(TR_TINIT))
rawtree: rawtree-starts rawtree-logs
	@cd $(RT_ROOT) && set -e; t0=$$(date +%s.%N); t="$(RP_TEACHER)"; \
	  if [ -z "$$t" ] && [ $(TR_TEACHERS) -gt 1 ]; then \
	    rm -rf $(TR_OUT)-teachers; best=-1; \
	    for k in $$(seq 1 $(TR_TEACHERS)); do \
	      v=$$($(MAKE) --no-print-directory -s -C $(RT_DIR) rawskill RS_ITEM=$(TR_ITEM) RS_MIN=$(TR_MIN) RS_V=$(RP_V) \
	        RS_RUN=$(TR_RUN) RS_SEED=$$(($(RP_SEED) * 10 + k)) RS_STEPS=$(TR_TSTEPS) RS_NS=$(RP_NS) \
	        RS_OUT=$(TR_OUT)-teachers/$$k RS_HELD=$(TR_VAL) RS_ARGS='--library $(TR_TLIB) $(TR_TARGS)' RS_TASK='$(RP_TONEF)' | \
	        sed -n 's/^RAWSKILL held-out success \([0-9.]*\) .*/\1/p'); \
	      echo "RAWTREE $(TR_TASK) teacher $$k of $(TR_TEACHERS): validation success $$v"; \
	      if awk -v a="$$v" -v b="$$best" 'BEGIN { exit !(a > b) }'; then best=$$v; t=$(TR_OUT)-teachers/$$k/model.ntw; fi; \
	    done; \
	    echo "RAWTREE $(TR_TASK) teachers: the best $$best, $$t, $$(awk -v a=$$t0 -v b=$$(date +%s.%N) 'BEGIN {printf "%.1f", b - a}') s"; \
	  fi; \
	  $(MAKE) --no-print-directory -s -C $(RT_DIR) rawpix RP_RUN=$(TR_RUN) RP_TLIB=$(TR_TLIB) RP_LIB=$(TR_LIB) \
	  RS_HELD=$(TR_H1) RP_HCAVE=$(TR_H2) RP_MIN=$(TR_MIN) RP_ITEM=$(TR_ITEM) RP_OUT=$(TR_OUT) \
	  RP_TSTEPS=$(TR_TSTEPS) RP_STEPS=$(TR_STEPS) RP_TARGS='$(TR_TARGS)' RP_TEACHER=$$t RP_VIEW=$(TR_VIEW) RP_NEED=$(TR_NEED) \
	  RP_ARGS='$(TR_SARGS)' | sed 's/^RAWPIX/RAWTREE $(TR_TASK)/'; \
	  echo "RAWTREE $(TR_TASK) teacher: $${t:-trained from $(if $(wildcard $(TR_TINIT)),$(TR_TINIT),a fresh net)}"
