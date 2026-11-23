# Gate sets as make targets (lane/tooling, AGENTS.md "Tools, not scripts": 35 gate-bundle scripts in the lanes' out/
# trees, 2026-10-31). Uses pool.mk's, raycast.mk's, rlbind.mk's and train/loop.mk's variables (included first).
#
#   make -C csrc/runtime gates-env    the env's gates, for a change to csrc/engine or csrc/runtime:
#          digest-512x4, digest-128x2   vec_bench --digest --obs-digest (CPU, the trainer's 16x16 host cast config,
#                                       train-loop-cfg's host.yaml) against the digests pinned in gates.tsv
#          digest-512x4-f32, -128x2-f32 the same with the FP32 cast (hostf32.yaml, cast_precision 32; lane/hostf32)
#          digest-*-fx, digest-*-f16    the same with the fixed and the FP16 casts (hostfixed.yaml, host16.yaml;
#                                       lane/castexact)
#          digest-pearl-walk            the pearl night start with a path ask every step (vec_bench --walk-ask,
#                                       lane/divpearl): a privileged search leaves the trajectory as it was
#          pool-gate                   every pool_gate.list recording's rows against test_snapshots' (40)
#          raycast-gate                 the cast both ways after every tick, byte-equal
#          native-test                  make -s -C csrc test (the whole native suite, test selection on)
#          train-loop-check             ntloop against vec_bench on both casts (gpuq.sh check)
#          livguard                     a living's overwritten hook stops on the guard's report (livguard.mk, 3 s)
#   make -C csrc/runtime gates-cast   the cast's gates, for a change to csrc/cuda/raycast:
#          gpu-raycast-check            the device cast after every tick of raycast-gate's recordings (gpuq.sh check)
#          train-loop-check             as above
#          rcbench-dumps                rcbench on each stored dump (RCDUMPS): frames and seen records equal the dump's
#   make -C csrc/runtime gates-pin    vec_bench's digests at each gates.tsv shape printed as gates.tsv lines (the
#                                     reason column to fill), to pin a change that moves them on purpose
#
# First everything the gates use is built in one make (so no two gates build one file at once) and every pool
# reference made (so no two gates make one reference at once: they collide, `ref NAME: FAIL`). Then the gates run
# at once, GT_J at a time (4: every CPU gate of a set together; the suite's jobs and the references reserve memory
# through memslot.sh). Each gate that runs the oracle or the pool's references runs under setsid -w (they end
# their process group) and each GPU gate through gpuq.sh check (GPUQ, the flywheel's; run directly where there is
# none). One line a gate as it ends (NAME: PASS or FAIL, seconds, its summary line; the whole log in
# out/runtime/gates/NAME.log), then GATES-ENV PASS (GATES-CAST PASS), or GATES-ENV FAIL: and the gates that failed,
# exit 1. A gate that exits 0 with a `: FAIL` line in its log (a reference that failed) fails.
#
# gates.tsv pins each digest shape: name, vec_bench's arguments, the trajectory and observation digests, and the
# reason they last changed (the commit and why). A change that moves a digest on purpose pins the new one with its
# reason in the same commit.
GT_OUT   := $(RT_OUT)/gates
GT_PIN   := $(RT_DIR)/gates.tsv
GT_J     ?= 4
GPUQ     ?=
# A GPU gate first waits for GT_GPUFREE MiB free on the device, at most GT_GPUWAIT s, and only then takes a check slot:
# never inside one, since a running check holds the queue's shared lock and a check that waits there holds every
# window back (2026-10-31: cpuhot's check waited 35 minutes inside for memory the mirror nightly's tick_batch held,
# 14 to 17 GB of the 3090, and every lane's window waited behind it). train-loop-check's 15 runs met cudaMalloc
# with 9 GB free. The wait is one line in the gate's log and on its line; past GT_GPUWAIT the gate fails unrun.
GT_GPUFREE ?= 12000
GT_GPUWAIT ?= 1800
gt_memwait = { t0=$$(date +%s); while f=$$(nvidia-smi --query-gpu=memory.free --format=csv,noheader,nounits -i $(CUDA_DEV) \
  2>/dev/null | head -1); [ "$${f:-0}" -lt $(GT_GPUFREE) ] && [ $$(( $$(date +%s) - t0 )) -lt $(GT_GPUWAIT) ]; do sleep 10; done; \
  w=$$(( $$(date +%s) - t0 )); if [ "$${f:-0}" -ge $(GT_GPUFREE) ]; then echo "gpu memory: $$f MiB free after $$w s"; \
  else echo "gpu memory: $${f:-?} MiB free after $$w s, under $(GT_GPUFREE): not run"; false; fi; }
GT_GPU    = $(gt_memwait) && $(if $(GPUQ),$(GPUQ) check --)
# a check slot without the wait: the native suite's frame judges draw on the device (and fall back to C when it is
# busy), which the lanes' make shim queues as a check; the gates' sub-makes pass the shim, so they queue here
GT_GPUQ   = $(if $(GPUQ),$(GPUQ) check --)
# the stored casts are keyed by the sources of their format (the head, the device world's layout, the writer, the
# reader): a dump made before lane/casthot's Morton section order and lane/castf32's precision field still read as
# RCDUMP1 and failed rcbench-dumps on master (2026-10-31); a change to any of these makes new dumps
RCFMT    := $(firstword $(shell cat $(RT_ROOT)/csrc/runtime/rchead.h $(RT_ROOT)/csrc/cuda/raycast/rcdev.h \
              $(RT_ROOT)/csrc/cuda/raycast/host.cu $(RT_ROOT)/csrc/cuda/raycast/bench.cu 2>/dev/null | cksum))
RCDUMPS  ?= $(RT_CACHE)/native/rcdumps-$(RCFMT)
RCDUMPN  ?= c64-64 c16-128
GT_ENV   := digest-512x4 digest-128x2 digest-512x4-f32 digest-128x2-f32 digest-512x4-fx digest-128x2-fx digest-512x4-f16 \
            digest-128x2-f16 digest-pearl-walk pool-gate raycast-gate native-test train-loop-check livguard
GT_CAST  := gpu-raycast-check train-loop-check rcbench-dumps

# vec_bench at gates.tsv's shape $(1), both digests against the pin
gt_digest = pin=$$(awk -F'\t' -v n=$(1) '$$1 == n' $(GT_PIN)); [ -n "$$pin" ] || { echo "$(1): no line in $(GT_PIN)"; exit 1; }; \
  args=$$(printf '%s\n' "$$pin" | cut -f2); want=$$(printf '%s\n' "$$pin" | cut -f3,4 | tr '\t' ' '); \
  CUDA_VISIBLE_DEVICES= $(RT_OUT)/vec_bench --config $(NT_CFG)/host.yaml $$args --digest --obs-digest > $(GT_OUT)/$(1).out 2>&1; \
  rc=$$?; cat $(GT_OUT)/$(1).out; [ $$rc = 0 ] || exit 1; \
  got=$$(awk '/^trajectory digest/ {t = $$3} /^observation digest/ {o = $$3} END {print t " " o}' $(GT_OUT)/$(1).out); \
  echo "$(1): got $$got, pinned $$want ($$(printf '%s\n' "$$pin" | cut -f5))"; [ "$$got" = "$$want" ]

GT_CMD_digest-512x4      = $(call gt_digest,digest-512x4)
GT_CMD_digest-128x2      = $(call gt_digest,digest-128x2)
GT_CMD_digest-512x4-f32  = $(call gt_digest,digest-512x4-f32)
GT_CMD_digest-128x2-f32  = $(call gt_digest,digest-128x2-f32)
GT_CMD_digest-512x4-fx   = $(call gt_digest,digest-512x4-fx)
GT_CMD_digest-128x2-fx   = $(call gt_digest,digest-128x2-fx)
GT_CMD_digest-512x4-f16  = $(call gt_digest,digest-512x4-f16)
GT_CMD_digest-128x2-f16  = $(call gt_digest,digest-128x2-f16)
GT_CMD_digest-pearl-walk = $(call gt_digest,digest-pearl-walk)
GT_CMD_pool-gate        = setsid -w $(MAKE) --no-print-directory -s -C $(RT_DIR) pool-gate
GT_CMD_raycast-gate      = setsid -w $(MAKE) --no-print-directory -s -C $(RT_DIR) raycast-gate
GT_CMD_livguard          = $(MAKE) --no-print-directory -s -C $(RT_DIR) livguard-check
GT_CMD_native-test       = $(GT_GPUQ) setsid -w $(MAKE) --no-print-directory -s -C $(RT_NATIVE) test
GT_CMD_train-loop-check  = $(GT_GPU) setsid -w $(MAKE) --no-print-directory -s -C $(RT_DIR) train-loop-check
GT_CMD_gpu-raycast-check = $(GT_GPU) setsid -w $(MAKE) --no-print-directory -s -C $(RT_NATIVE) gpu-raycast-check
GT_CMD_rcbench-dumps     = $(GT_GPU) bash -c 'f=0; for d in $(RCDUMPN); do \
  l=$$($(RT_NOUT)/cuda/rcbench $(RCDUMPS)/$$d.dump 2>&1 | tail -1); echo "$$d: $$l"; \
  case "$$l" in *"frames equal, seen equal"*) ;; *) f=1;; esac; done; exit $$f'
# the line a gate is summed up by (default its last line)
GT_SUM_pool-gate         = pool_gate: (OK|FAIL)
GT_SUM_raycast-gate      = pool_gate: (OK|FAIL)
GT_SUM_gpu-raycast-check = pool_gate: (OK|FAIL)
GT_SUM_native-test       = FAILED job|replayed from the test cache
GT_SUM_train-loop-check  = TRAIN-LOOP-CHECK
GT_SUM_rcbench-dumps     = .

.PHONY: gates-env gates-cast gates-pin gates-build gates-build-cast gates-dumps gates-cfg32

# the FP32 cast's config beside host.yaml (lane/hostf32): gates.tsv's *-f32 lines name it with a second --config (the
# last one wins), so the FP32 host cast's own frames are pinned beside FP64's
gates-cfg32: train-loop-cfg
	@sed 's/^\(    camera: raycast\)$$/\1\n    cast_precision: 32/' $(NT_CFG)/host.yaml > $(NT_CFG)/hostf32.yaml
	@sed 's/^\(    camera: raycast\)$$/\1\n    cast_precision: fixed/' $(NT_CFG)/host.yaml > $(NT_CFG)/hostfixed.yaml
	@sed 's/^\(    camera: raycast\)$$/\1\n    cast_precision: 16/' $(NT_CFG)/host.yaml > $(NT_CFG)/host16.yaml

# everything a gate builds, built here once; the references of both pool gates made once
gates-build:
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) all assets
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool rlbind $(RT_OUT)/vec_bench $(RT_OUT)/livguard $(NT_OUT)/ntloop $(NT_OUT)/$(NTC_V64)/ntloop train-loop-cfg
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) gates-cfg32
	@setsid -w $(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(sort $(RECS) $(RCRECS))" | grep -v ': OK: every row replayed$$' || true

gates-build-cast: gates-build
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) $(RT_RCDOBJ) $(RT_NOUT)/cuda/rcbench
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) $(RT_OUT)/pool_gate_rcd
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) gates-dumps

# the stored casts rcbench-dumps replays, made once as lane/casthot made its (group 0's cast 600 of the trainer: the s1
# net at 64x64 with 64 envs, the w1 net at 16x16 with 128), one group each (a third of 512x4's device memory: 512x4
# met cudaMalloc beside the mirror nightly's tick_batch, 2026-10-31); RCDUMPS=dir to use others
gates-dumps: $(foreach d,$(RCDUMPN),$(RCDUMPS)/$(d).dump)
gt_dump = cd $(RT_ROOT) && $(GT_GPU) $(1) --config $(2) --ns $(3) --groups 1 --warm 600 --steps 200 --ep-len 3600 --macros 63 \
  --policy actor --init $(RT_CACHE)/native/$(4)/weights.ntw --dump-cast 600:$@.tmp > $@.log 2>&1; \
  grep -E 'dump-cast|FAIL' $@.log; grep -q '^--dump-cast: ' $@.log && mv $@.tmp $@
$(RCDUMPS)/c64-64.dump:
	@mkdir -p $(@D) && sed 's/observation: 16x16/observation: 64x64/' $(NT_CFG)/dev.yaml > $(NT_CFG)/dev64.yaml
	@$(call gt_dump,$(NT_OUT)/s1/ntloop,$(NT_CFG)/dev64.yaml,64,actor-fresh-s1)
$(RCDUMPS)/c16-128.dump:
	@mkdir -p $(@D)
	@$(call gt_dump,$(NT_OUT)/ntloop,$(NT_CFG)/dev.yaml,128,actor-a05-070)

# one gate: its command from the root, its log, its line
gt-%:
	@mkdir -p $(GT_OUT); t0=$$(date +%s); \
	( cd $(RT_ROOT) && $(GT_CMD_$*) ) > $(GT_OUT)/$*.log 2>&1; rc=$$?; \
	sum=$$(grep -E '$(or $(GT_SUM_$*),.)' $(GT_OUT)/$*.log | grep -v -e '^[[:space:]]*$$' -e '^gpu memory: ' | tail -1 | cut -c1-170); \
	[ -n "$$sum" ] || sum=$$(grep -E 'FAIL|rror' $(GT_OUT)/$*.log | grep -v '^make' | tail -1 | cut -c1-170); \
	if [ $$rc = 0 ] && grep -qE '^ref [^ ]*: FAIL' $(GT_OUT)/$*.log; then rc=fail-line; fi; \
	if [ "$$rc" = 0 ]; then r=PASS; else r="FAIL (rc $$rc)"; fi; \
	w=$$(grep -m1 '^gpu memory: ' $(GT_OUT)/$*.log | sed 's/^gpu memory: //'); \
	echo "$*: $$r in $$(( $$(date +%s) - t0 )) s$${w:+ (gpu memory $$w)}: $$sum" | tee $(GT_OUT)/$*.line

# a set: the build, its gates at once, the verdict
define gt_set
	@rm -f $(foreach g,$(2),$(GT_OUT)/$(g).line); echo "$(1): $(words $(2)) gates, $(GT_J) at a time, logs in $(GT_OUT)"
	-@$(MAKE) --no-print-directory -k -j$(GT_J) $(addprefix gt-,$(2))
	@fail=; for g in $(2); do grep -q ': PASS in' $(GT_OUT)/$$g.line 2>/dev/null || fail="$$fail $$g"; done; \
	  if [ -z "$$fail" ]; then echo "$(1) PASS"; else echo "$(1) FAIL:$$fail"; exit 1; fi
endef

gates-env: gates-build
	$(call gt_set,GATES-ENV,$(GT_ENV))

gates-cast: gates-build-cast
	$(call gt_set,GATES-CAST,$(GT_CAST))

gates-pin: rlbind $(RT_OUT)/vec_bench gates-cfg32
	@awk -F'\t' '!/^#/ && NF >= 2 {print $$1 "\t" $$2}' $(GT_PIN) | while IFS='	' read -r n a; do \
	  d=$$(cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/vec_bench --config $(NT_CFG)/host.yaml $$a --digest --obs-digest 2>&1 | \
	    awk '/^trajectory digest/ {t = $$3} /^observation digest/ {o = $$3} END {print t "\t" o}'); \
	  printf '%s\t%s\t%s\t%s\n' "$$n" "$$a" "$$d" "$$(git -C $(RT_ROOT) log -1 --format=%h) (the reason)"; done
