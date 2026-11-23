# The native trainer's loop (lane/ntloop, SPEC.md item 6a, train/loop.c): libneth's vector env driven as the trainer
# drives it, groups pipelined on their own streams, a policy (train/loop.h) launched a group. Uses rlbind.mk's library.
#
#   make -C csrc/runtime train-loop          out/runtime/train/ntloop
#   make -C csrc/runtime train-loop-v NT_V=V out/runtime/train/V/ntloop at net.h's variant V (s1: 64x64 with the stem)
#   make -C csrc/runtime train-loop-check    the stand-in policy on the device: ntloop's trajectory digest equal to
#          vec_bench --digest's on the same config, envs, groups and steps, with the cast on the host (NTCONF) and on
#          the device (NTCONF_DEV), at depth 0 and 1 and served, and every run's frames equal (--frame-digest); the
#          same frames at 64x64 through V's ntloop; the actor's trajectory and frames equal on both casts (gpuq.sh check)
#   make -C csrc/runtime train-loop-bench    ntloop --ab on NTB_CONF (host or dev; in a bench window:
#          out/flywheel/bin/gpuq.sh bench -- make ...): one vector env, vec_bench's own loop (A)
#          and this one (B) in short stretches A B B A ... (NTB_LOOP=... --ab-swap: B A A B), env-steps/s by mode, the
#          quads' median at least NTB_MIN %, and the main thread a group step in each. Two processes measured the same
#          vec_bench at 81.6k and 118.4k in one window (the first's cold start), so the comparison is in one process
#
# NTCONF and NTCONF_DEV are the brute recipe's sim at 16x16 (the cast on the host, on the device): configs/tests/brute16.yaml
# and configs/tests/brute16dev.yaml (in ~/.cache/netherite-native until lane/polish, so a fresh clone had none). The ray cast takes
# its textures from the config's own directory (rlbind.c), so they are copied to out/runtime/train/cfg beside links to
# this tree's out/assets and out/java/render.
NT_DIR     := $(RT_DIR)/train
NT_OUT     := $(RT_OUT)/train
NT_CUDAINC := $(dir $(PL_NVCC))../include
NTCONF     ?= $(RT_ROOT)/configs/tests/brute16.yaml
NTCONF_DEV ?= $(RT_ROOT)/configs/tests/brute16dev.yaml
NT_CFG     := $(NT_OUT)/cfg
# the check: small, its six runs (three at a time, 4 workers each) under 10 s, a reset a group
NTC_ARGS   ?= --ns 64 --groups 4 --threads 4 --warm 10 --steps 110 --ep-len 100 --macros 63
# the bench: brute16's 512 envs in 4 groups, 30 workers (the orchestrator's vec_bench), 200 group steps a group of
# warm-up, then 24 stretches of 50 (6 quads), about 8 s with the make
NTB_ARGS   ?= --ns 512 --groups 4 --warm 200 --steps 50 --ab 12 --ep-len 3600 --macros 63
NTB_CONF   ?= host
NTB_LOOP   ?= --policy standin
NTB_MIN    ?= 97
NTL_OUT    := $(NT_OUT)/loopcheck
# ntloop's sources: loop.c (the command line) and its modules (train/neth_*.c), one compile with the variant's flags
NT_LOOP_C  := train/loop.c $(patsubst $(RT_DIR)/%,%,$(sort $(wildcard $(NT_DIR)/neth_*.c)))
NETH_NET_LOOP_H  := $(NT_DIR)/loop.h $(wildcard $(NT_DIR)/neth_*.h) $(NT_DIR)/net.h $(NT_DIR)/learn.h $(NT_DIR)/learn_io.h \
              $(RT_DIR)/vecenv.h $(NT_DIR)/rec.h $(NT_DIR)/distill.h $(NT_DIR)/curric.h $(NT_DIR)/stages.h
NT_LOOP_SRC := $(NT_DIR)/loop.c $(wildcard $(NT_DIR)/neth_*.c) $(NETH_NET_LOOP_H)

.PHONY: train-loop train-loop-cfg train-loop-check train-loop-bench

train-loop: $(NT_OUT)/ntloop $(NT_OUT)/ntrec train-loop-cfg

$(NT_OUT)/loop_dev.o: $(NT_DIR)/loop.cu $(NT_DIR)/loop.h $(NT_DIR)/net.h
	@mkdir -p $(NT_OUT)
	cd $(NT_DIR) && $(PL_NVCC) -O3 -arch=$(CUDA_ARCH) -Xcompiler -Wall -c -o $@ loop.cu

# lane/ntwire: the actor as the policy and its refresh (train/trainer.cu over actor.h's neth_actor_reload, lane/ntcast),
# the actor (actor.mk's actor.o) and the learner (learn.mk's learn.o)
$(NT_OUT)/trainer.o: $(NT_DIR)/trainer.cu $(NT_DIR)/actor.h $(NT_DIR)/learn.h $(NT_DIR)/loop.h $(NT_DIR)/net.h
	@mkdir -p $(NT_OUT)
	cd $(NT_DIR) && $(NTA_CCACHE)$(PL_NVCC) -O3 -std=c++17 -arch=$(CUDA_ARCH) -Xcompiler -Wall -Werror all-warnings \
	  -c -o $@ trainer.cu

# lane/ntbc: distillation to fewer macros (train/distill.h: --teacher, the labels, the cross-entropy's kernel)
$(NT_OUT)/distill.o: $(NT_DIR)/distill.cu $(NT_DIR)/distill.h $(NT_DIR)/learn.h $(NT_DIR)/net.h $(NT_DIR)/prec.cuh \
                     $(RT_DIR)/vecenv.h
	@mkdir -p $(NT_OUT)
	cd $(NT_DIR) && $(NTA_CCACHE)$(PL_NVCC) -O3 -std=c++17 -arch=$(CUDA_ARCH) -Xcompiler -Wall -Werror all-warnings \
	  -c -o $@ distill.cu

$(NT_OUT)/ntloop: $(NT_LOOP_SRC) $(NT_OUT)/rec.o $(NT_OUT)/loop_dev.o \
                  $(NT_OUT)/trainer.o $(NT_OUT)/distill.o $(NT_OUT)/actor.o $(NT_OUT)/learn.o $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) -I$(NT_CUDAINC) $(RT_LDFLAGS) -o $@ $(NT_LOOP_C) $(NT_OUT)/rec.o \
	  $(NT_OUT)/loop_dev.o $(NT_OUT)/trainer.o $(NT_OUT)/distill.o $(NT_OUT)/actor.o $(NT_OUT)/learn.o -L$(RT_OUT) \
	  -Wl,-rpath,$(RT_OUT) \
	  -lneth -L$(PL_CUDALIB) -Wl,-rpath,$(PL_CUDALIB) -lcublas -lcudart -lzstd -lstdc++ -lm

# lane/runrec: the recorder (train/rec.c, linked into ntloop) and its reader, out/runtime/train/ntrec (train/recread.c:
# list, show, check, stats, tape; the tape through libneth's moveact_script_line)
$(NT_OUT)/rec.o: $(NT_DIR)/rec.c $(NT_DIR)/rec.h $(RT_DIR)/vecenv.h $(RT_DIR)/rlbind.h
	@mkdir -p $(NT_OUT)
	cd $(RT_DIR) && $(RT_CCC) $(RT_CFLAGS) -c -o $@ train/rec.c

$(NT_OUT)/ntrec: $(NT_DIR)/recread.c $(NT_DIR)/rec.h $(NT_OUT)/rec.o $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ train/recread.c $(NT_OUT)/rec.o -L$(RT_OUT) \
	  -Wl,-rpath,$(RT_OUT) -lneth -L$(PL_CUDALIB) -Wl,-rpath,$(PL_CUDALIB) -lcudart -lzstd -lm

# lane/starts: the library of starts' tool (train/starts.c: stages, cut, entry, index) and its harvest (harvest.sh)
$(NT_OUT)/ntstarts: $(NT_DIR)/starts.c $(NT_DIR)/stages.h $(RT_DIR)/vecenv.h $(RL_LIB)
	@mkdir -p $(NT_OUT)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ train/starts.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth \
	  -L$(PL_CUDALIB) -Wl,-rpath,$(PL_CUDALIB) -lcudart -lm

# lane/ntcast: ntloop at net.h's variant V (actor.mk's NT_VARIANTS: wN 16x16 at wm N, sN 64x64 with the stem), its
# loop, actor, learner and trainer built with V's -D flags, in out/runtime/train/V/ntloop (make train-loop-v NT_V=s1)
.PHONY: train-loop-v
.PRECIOUS: $(NT_OUT)/%/loop_dev.o $(NT_OUT)/%/trainer.o $(NT_OUT)/%/actor.o $(NT_OUT)/%/learn.o $(NT_OUT)/%/distill.o
train-loop-v: $(NT_OUT)/$(NT_V)/ntloop train-loop-cfg

$(NT_OUT)/%/loop_dev.o: $(NT_DIR)/loop.cu $(NT_DIR)/loop.h $(NT_DIR)/net.h
	@mkdir -p $(@D)
	cd $(NT_DIR) && $(NTA_CCACHE)$(PL_NVCC) -O3 -arch=$(CUDA_ARCH) -Xcompiler -Wall $(call nt_vflags,$*) -c -o $@ loop.cu

$(NT_OUT)/%/trainer.o: $(NT_DIR)/trainer.cu $(NT_DIR)/actor.h $(NT_DIR)/learn.h $(NT_DIR)/loop.h $(NT_DIR)/net.h
	@mkdir -p $(@D)
	cd $(NT_DIR) && $(NTA_CCACHE)$(PL_NVCC) -O3 -std=c++17 -arch=$(CUDA_ARCH) -Xcompiler -Wall -Werror all-warnings \
	  $(call nt_vflags,$*) -c -o $@ trainer.cu

$(NT_OUT)/%/distill.o: $(NT_DIR)/distill.cu $(NT_DIR)/distill.h $(NT_DIR)/learn.h $(NT_DIR)/net.h $(NT_DIR)/prec.cuh \
                       $(RT_DIR)/vecenv.h
	@mkdir -p $(@D)
	cd $(NT_DIR) && $(NTA_CCACHE)$(PL_NVCC) -O3 -std=c++17 -arch=$(CUDA_ARCH) -Xcompiler -Wall -Werror all-warnings \
	  $(call nt_vflags,$*) -c -o $@ distill.cu

$(NT_OUT)/%/ntloop: $(NT_LOOP_SRC) $(NT_OUT)/rec.o $(NT_OUT)/%/loop_dev.o \
                    $(NT_OUT)/%/trainer.o $(NT_OUT)/%/distill.o $(NT_OUT)/%/actor.o $(NT_OUT)/%/learn.o $(RL_LIB)
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(call nt_vflags,$*) -I$(NT_CUDAINC) $(RT_LDFLAGS) -o $@ $(NT_LOOP_C) \
	  $(NT_OUT)/rec.o $(NT_OUT)/$*/loop_dev.o $(NT_OUT)/$*/trainer.o $(NT_OUT)/$*/distill.o $(NT_OUT)/$*/actor.o \
	  $(NT_OUT)/$*/learn.o \
	  -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -L$(PL_CUDALIB) -Wl,-rpath,$(PL_CUDALIB) -lcublas -lcudart -lzstd \
	  -lstdc++ -lm

train-loop-cfg:
	@mkdir -p $(NT_CFG)/out/java && cp $(NTCONF) $(NT_CFG)/host.yaml && cp $(NTCONF_DEV) $(NT_CFG)/dev.yaml && \
	  ln -sfn $(RT_ROOT)/out/assets $(NT_CFG)/out/assets && ln -sfn $(RT_ROOT)/out/java/render $(NT_CFG)/out/java/render

# lane/ntcast: every ntloop run also hashes what each env's segment holds (--frame-digest: the frames, the scalars,
# the reward, end and driver), so the device cast's frames are held to the host cast's byte for byte through the
# loop's own path, at depth 0 and 1 and served (--serve 1: the casts of the groups received together in one launch,
# at most one group a launch, held while a cast runs), and the copies' kernel (--stage 1) to the copies; and the
# frames at 64x64 through the s1 build (NTC_V64), the host cast against the device's
NTC_V64 ?= s1
# and the native actor (fixed weights, learning off) on the host and device casts: equal frames give equal actions,
# so the trajectory digests (the actions are in the scalars) and the frame digests match
NTC_ACTOR ?= $(NTW_ACTOR)/weights.ntw
NTC_RUNS := host:0:--depth=0 host:1:--depth=1 dev:0:--depth=0 dev:1:--depth=1 dev:s:--serve=1 dev:s1:--serve=1_--cast-max=1 \
            dev:h:--serve=1_--cast-hold=1 host:st:--depth=0_--stage=1 dev:st:--serve=1_--stage=1

train-loop-check: rlbind $(RT_OUT)/vec_bench $(NT_OUT)/ntloop $(NT_OUT)/$(NTC_V64)/ntloop train-loop-cfg
	@cd $(RT_ROOT) && rm -rf $(NTL_OUT) && mkdir -p $(NTL_OUT) && \
	  fail() { echo "$$1"; echo "TRAIN-LOOP-CHECK FAIL"; exit 1; }; \
	  for k in host host64 dev64; do sed 's/observation: 16x16/observation: 64x64/' $(NT_CFG)/$${k%64}.yaml > $(NT_CFG)/$${k%64}64.yaml; done; \
	  for k in host dev; do \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/vec_bench --config $(NT_CFG)/$$k.yaml $(NTC_ARGS) --digest > $(NTL_OUT)/vec-$$k.log 2>&1 & \
	  done; \
	  for r in $(NTC_RUNS); do \
	    k=$${r%%:*}; d=$${r#*:}; d=$${d%%:*}; a=$$(echo $${r##*:} | tr '_=' '  '); \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(NT_OUT)/ntloop --config $(NT_CFG)/$$k.yaml $(NTC_ARGS) --frame-digest --policy standin \
	      $$a > $(NTL_OUT)/loop-$$k-$$d.log 2>&1 & \
	  done; \
	  wait; \
	  for k in host dev; do \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(NT_OUT)/ntloop --config $(NT_CFG)/$$k.yaml $(NTC_ARGS) --frame-digest --policy actor \
	      --init $(NTC_ACTOR) --learn 0 > $(NTL_OUT)/actor-$$k.log 2>&1 & \
	  done; \
	  for k in host64:0:--depth=0 dev64:s:--serve=1; do \
	    c=$${k%%:*}; d=$${k#*:}; d=$${d%%:*}; a=$$(echo $${k##*:} | tr '_=' '  '); \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(NT_OUT)/$(NTC_V64)/ntloop --config $(NT_CFG)/$$c.yaml $(NTC_ARGS) --frame-digest \
	      --policy standin $$a > $(NTL_OUT)/loop-$$c-$$d.log 2>&1 & \
	  done; \
	  wait; \
	  for k in host dev; do \
	    grep -q '^trajectory digest' $(NTL_OUT)/vec-$$k.log || fail "vec_bench $$k: $$(tail -1 $(NTL_OUT)/vec-$$k.log)"; \
	  done; \
	  want=$$(grep '^trajectory digest' $(NTL_OUT)/vec-host.log); \
	  [ "$$want" = "$$(grep '^trajectory digest' $(NTL_OUT)/vec-dev.log)" ] || fail "vec_bench's digests differ, host and dev"; \
	  fw=$$(grep '^frame digest' $(NTL_OUT)/loop-host-0.log); \
	  for r in $(NTC_RUNS); do \
	    k=$${r%%:*}; d=$${r#*:}; d=$${d%%:*}; L=$(NTL_OUT)/loop-$$k-$$d.log; \
	    got=$$(grep '^trajectory digest' $$L) || fail "ntloop $$k $$d: $$(tail -1 $$L)"; f=$$(grep '^frame digest' $$L); \
	    echo "$$k cast, $$(echo $${r##*:} | tr '_=' '  '): vec_bench $${want#trajectory digest }, ntloop $${got#trajectory digest }, frames $${f#frame digest }$$(grep -o ' ([0-9.]* groups a cast' $$L | tr -d '(' | sed 's/^/,/')"; \
	    [ -n "$$want" ] && [ "$$want" = "$$got" ] || fail "the digests differ"; \
	    [ -n "$$fw" ] && [ "$$fw" = "$$f" ] || fail "the frames differ"; \
	  done; \
	  f64=$$(grep '^frame digest' $(NTL_OUT)/loop-host64-0.log); \
	  for k in host64-0 dev64-s; do \
	    L=$(NTL_OUT)/loop-$$k.log; f=$$(grep '^frame digest' $$L) || fail "ntloop $(NTC_V64) $$k: $$(tail -1 $$L)"; \
	    echo "$(NTC_V64) at 64x64, $$k: $$(grep '^trajectory digest' $$L), frames $${f#frame digest }"; \
	    [ -n "$$f64" ] && [ "$$f64" = "$$f" ] || fail "the 64x64 frames differ"; \
	  done; \
	  for k in host dev; do \
	    L=$(NTL_OUT)/actor-$$k.log; grep -q '^frame digest' $$L || fail "the actor, $$k cast: $$(tail -1 $$L)"; \
	    echo "the actor (fixed weights, learning off), $$k cast: $$(grep -h 'digest' $$L | tr '\n' ' ')"; \
	  done; \
	  [ "$$(grep -h digest $(NTL_OUT)/actor-host.log)" = "$$(grep -h digest $(NTL_OUT)/actor-dev.log)" ] || \
	    fail "the actor's trajectory or frames differ between the host and device casts"; \
	  for k in host dev host64 dev64; do \
	    sed 's/^\(    camera: raycast\)$$/\1\n    cast_precision: 32/' $(NT_CFG)/$$k.yaml > $(NT_CFG)/$${k}f32.yaml; \
	  done; \
	  for k in hostf32:$(NT_OUT)/ntloop:--depth=0 devf32:$(NT_OUT)/ntloop:--serve=1 host64f32:$(NT_OUT)/$(NTC_V64)/ntloop:--depth=0 \
	           dev64f32:$(NT_OUT)/$(NTC_V64)/ntloop:--serve=1; do \
	    c=$${k%%:*}; b=$${k#*:}; b=$${b%%:*}; a=$$(echo $${k##*:} | tr '_=' '  '); \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$b --config $(NT_CFG)/$$c.yaml $(NTC_ARGS) --frame-digest --policy standin $$a \
	      > $(NTL_OUT)/loop-$$c.log 2>&1 & \
	  done; \
	  wait; \
	  for p in "hostf32 devf32 host-0" "host64f32 dev64f32 host64-0"; do \
	    set -- $$p; L=$(NTL_OUT)/loop-$$1.log; M=$(NTL_OUT)/loop-$$2.log; \
	    f=$$(grep '^frame digest' $$L) || fail "ntloop FP32 $$1: $$(tail -1 $$L)"; \
	    g=$$(grep '^frame digest' $$M) || fail "ntloop FP32 $$2: $$(tail -1 $$M)"; \
	    echo "the FP32 cast (client.cast_precision 32), $$1 and $$2: $$(grep '^trajectory digest' $$L), frames $${f#frame digest }"; \
	    [ "$$f" = "$$g" ] || fail "the FP32 frames differ between the host and device casts"; \
	    [ "$$(grep '^trajectory digest' $$L)" = "$$(grep '^trajectory digest' $$M)" ] && \
	      [ "$$(grep '^trajectory digest' $$L)" = "$$(grep '^trajectory digest' $(NTL_OUT)/loop-$$3.log)" ] || \
	      fail "the stand-in's trajectory differs under FP32 (its actions read no frame)"; \
	  done; \
	  echo "TRAIN-LOOP-CHECK PASS"

train-loop-bench: rlbind $(RT_OUT)/vec_bench $(NT_OUT)/ntloop train-loop-cfg
	@cd $(RT_ROOT) && mkdir -p $(NTL_OUT) && \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(NT_OUT)/ntloop --config $(NT_CFG)/$(NTB_CONF).yaml $(NTB_ARGS) $(NTB_LOOP) \
	    > $(NTL_OUT)/bench.log 2>&1; rc=$$?; grep '^ab' $(NTL_OUT)/bench.log; \
	  [ $$rc = 0 ] || { tail -1 $(NTL_OUT)/bench.log; echo "TRAIN-LOOP-BENCH FAIL"; exit 1; }; \
	  awk -v min=$(NTB_MIN) '/^ab: / {for (i = 1; i < NF; ++i) if ($$i == "median") m = $$(i + 1) + 0} \
	    END {if (m == "") {print "TRAIN-LOOP-BENCH FAIL: no median"; exit 1} \
	    if (m < min) {print "TRAIN-LOOP-BENCH FAIL: the median under " min "%"; exit 1} print "TRAIN-LOOP-BENCH PASS"}' \
	    $(NTL_OUT)/bench.log

# lane/ladder2, the hotbar's tools as scalars (net.h NETH_NET_HSEE, the .hs builds; gpuq.sh check), about 20 s:
#   make -C csrc/runtime train-hsee-check    a fresh s1.hb net (ntinit, seed 3) frozen at 64x64 (NTCONF_DEV's world,
#          the device cast), the L2 student's macros (8187) with the hotbar, through the s1.hb build and the s1.hb.hs
#          build: the blob's 155 scalars load into the wider fc1 with zeros on the 27 added, so the actions and the
#          trajectory digests (the 155 scalars, rewards and ends) are equal, and equal again with policy_tool=off
#          given (the option's default); lane/ladder4: and the s1.hb.dk build (the crack stage and the hit, 2 added);
#          lane/ladder5: and the s1.hb.dk.or build (the seek's view, 12 more)
NTH_ARGS ?= --ns 64 --groups 4 --threads 8 --warm 10 --steps 400 --ep-len 300 --macros 8187

.PHONY: train-hsee-check
train-hsee-check: rlbind $(NT_OUT)/s1.hb/ntloop $(NT_OUT)/s1.hb.hs/ntloop $(NT_OUT)/s1.hb.dk/ntloop \
    $(NT_OUT)/s1.hb.dk.or/ntloop $(NT_OUT)/s1.hb/ntinit train-loop-cfg
	@cd $(RT_ROOT) && mkdir -p $(NTL_OUT) && fail() { echo "$$1"; echo "TRAIN-HSEE-CHECK FAIL"; exit 1; }; \
	  sed 's/observation: 16x16/observation: 64x64/' $(NT_CFG)/dev.yaml > $(NT_CFG)/dev64.yaml; \
	  $(NT_OUT)/s1.hb/ntinit $(NTL_OUT)/hsee-fresh.ntw 3 > /dev/null || fail "ntinit"; \
	  for r in s1.hb: s1.hb.hs: s1.hb.hs:--mopt+policy_tool=off s1.hb.dk: s1.hb.dk.or:; do \
	    v=$${r%%:*}; a=$$(echo $${r#*:} | tr '+' ' '); \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(NT_OUT)/$$v/ntloop --config $(NT_CFG)/dev64.yaml $(NTH_ARGS) --policy actor \
	      --init $(NTL_OUT)/hsee-fresh.ntw --learn 0 $$a --digest > $(NTL_OUT)/hsee-$$v$${a:+-off}.log 2>&1; \
	  done; \
	  for r in s1.hb s1.hb.hs s1.hb.hs-off s1.hb.dk s1.hb.dk.or; do \
	    d=$$(grep '^trajectory digest' $(NTL_OUT)/hsee-$$r.log) || fail "$$r: $$(tail -1 $(NTL_OUT)/hsee-$$r.log)"; \
	    echo "$$r: $$d"; \
	    [ "$$d" = "$$(grep '^trajectory digest' $(NTL_OUT)/hsee-s1.hb.log)" ] || fail "$$r plays another trajectory"; \
	  done; \
	  echo "TRAIN-HSEE-CHECK PASS"

# lane/ntwire, the trainer's gates without the env (gpuq.sh check), about 2 s:
#   make -C csrc/runtime train-wire-check    gate 1: learn_check's fixture segment through the loop's learner path
#          (--check-learn, fp32 and bf16, NTCONF's ppo recipe) equal bit for bit to the learner called as learn_check
#          calls it, the refreshed actor's image equal to a fresh load's; gate 2: actor_check's fixture batch through
#          the loop's launch() (--check-actor), every action equal, loaded and refreshed
NTW_LEARN ?= $(RT_CACHE)/native/learn-a05-070
NTW_ACTOR ?= $(RT_CACHE)/native/actor-a05-070

.PHONY: train-wire-check
train-wire-check: $(NT_OUT)/ntloop
	@cd $(RT_ROOT) && mkdir -p $(NTL_OUT) && fail=0; \
	  for p in fp32 bf16; do \
	    CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(NT_OUT)/ntloop --check-learn $(NTW_LEARN) --run $(NTCONF) --prec $$p \
	      > $(NTL_OUT)/wire-learn-$$p.log 2>&1 || fail=1; grep -v 'skipped' $(NTL_OUT)/wire-learn-$$p.log; \
	  done; \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(NT_OUT)/ntloop --check-actor $(NTW_ACTOR) > $(NTL_OUT)/wire-actor.log 2>&1 || fail=1; \
	  cat $(NTL_OUT)/wire-actor.log; \
	  [ $$fail = 0 ] && echo "TRAIN-WIRE-CHECK PASS" || { echo "TRAIN-WIRE-CHECK FAIL"; exit 1; }

# lane/runrec, the recording's gate (gpuq.sh check), about 10 s:
#   make -C csrc/runtime rec-gate     ntloop --rec twice at once with the stand-in on the device: (a) the run-to-tape
#          world (configs/tests/tape-gate.yaml at 16x16, vanilla fidelity), 8 envs, every episode kept, with env 0's rows
#          (neth_rl_rows) and its second episode's script as neth_vec_tape writes it; (b) brute16 (train-fast fidelity),
#          64 envs, the gain rule and half the rest sampled; (c) brute16 with the cast on the device (the served loop:
#          several groups received before one is launched), every episode kept. Then: every kept episode of each
#          read back equal to the
#          --rec-raw copy of what was recorded (ntrec check: every field of every step, the records, the digests);
#          (a)'s env 0 episode 1 turned into a script by ntrec tape, byte for byte neth_vec_tape's; that script run on
#          the oracle from the config's world and test_snapshots' rows of the recording equal to the episode's, row for
#          row (rl-tape-gate's route); and ntrec tape refusing (b)'s episodes (not vanilla). The trajectory digests of
#          both equal to the same runs without --rec (the recording changes nothing)
#          REC_TPS=T (lane/ticks-per-step): (a) at client.ticks_per_step T, a step of T ticks (rec version 3's subs: the
#          later ticks a macro drove), its script T lines a step
REC_OUT ?= $(NT_OUT)/recgate
REC_EP  ?= 300
REC_TPS ?= 1
REC_A   := --ns 8 --groups 2 --threads 4 --warm 0 --steps $$((2 * $(REC_EP) + 2)) --ep-len $(REC_EP) --macros 63 \
           --policy standin --digest
REC_B   := --ns 64 --groups 4 --threads 8 --warm 0 --steps 410 --ep-len 200 --macros 63 --policy standin --digest

.PHONY: rec-gate
rec-gate: rlbind pool-native $(NT_OUT)/ntloop $(NT_OUT)/ntrec train-loop-cfg
	@cd $(RT_ROOT) && rm -rf $(REC_OUT) out/java/try/recgate && mkdir -p $(REC_OUT) && \
	  fail() { echo "$$1"; echo "REC-GATE FAIL"; exit 1; }; \
	  sed -e 's/observation: 32x32/observation: 16x16/' -e 's/ticks_per_step: 1$$/ticks_per_step: $(REC_TPS)/' \
	    configs/tests/tape-gate.yaml > $(NT_CFG)/tapegate.yaml; \
	  grep -q 'ticks_per_step: $(REC_TPS)$$' $(NT_CFG)/tapegate.yaml || fail "REC_TPS=$(REC_TPS): no ticks_per_step line"; \
	  L=$(NT_OUT)/ntloop; R=$(NT_OUT)/ntrec; O=$(REC_OUT); \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --config $(NT_CFG)/tapegate.yaml $(REC_A) --rec $$O/a --rec-keep all \
	    --rec-raw $$O/a.raw --rec-rows 0:$$O/vec.rows --tape 0:$$O/vectape.jsonl > $$O/a.log 2>&1 & \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --config $(NT_CFG)/tapegate.yaml $(REC_A) > $$O/a0.log 2>&1 & \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --config $(NT_CFG)/host.yaml $(REC_B) --rec $$O/b --rec-keep gain \
	    --rec-sample 0.5 --rec-raw $$O/b.raw > $$O/b.log 2>&1 & \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --config $(NT_CFG)/host.yaml $(REC_B) > $$O/b0.log 2>&1 & \
	  wait; \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --config $(NT_CFG)/dev.yaml $(REC_B) --rec $$O/c --rec-keep all \
	    --rec-raw $$O/c.raw > $$O/c.log 2>&1 & \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --config $(NT_CFG)/dev.yaml $(REC_B) > $$O/c0.log 2>&1 & \
	  wait; \
	  for k in a b c; do \
	    d=$$(grep '^trajectory digest' $$O/$$k.log) || fail "ntloop $$k: $$(tail -1 $$O/$$k.log)"; \
	    [ "$$d" = "$$(grep '^trajectory digest' $$O/$${k}0.log)" ] || fail "$$k: the digest differs without --rec"; \
	    echo "$$k: $$d with and without --rec; $$(grep '^rec:' $$O/$$k.log | cut -d';' -f1-2)"; \
	    $$R check $$O/$$k/episodes.nrec $$O/$$k.raw > $$O/$$k.check 2>&1 || fail "$$k: $$(tail -2 $$O/$$k.check | head -1)"; \
	    echo "$$k: $$(head -1 $$O/$$k.check)"; \
	  done; \
	  $$R tape $$O/b/episodes.nrec 0 -o $$O/b.jsonl > $$O/b.tape 2>&1; rc=$$?; \
	  [ $$rc = 3 ] && grep -q '^REFUSED: .*not vanilla' $$O/b.tape || fail "b: ntrec tape did not refuse train-fast (rc $$rc)"; \
	  echo "b: $$(cut -c1-120 $$O/b.tape)..."; \
	  $$R tape $$O/a/episodes.nrec 0:1 -o $$O/script.jsonl --world $$O/world.yaml > $$O/a.tape 2>&1 || \
	    fail "a: ntrec tape: $$(tail -1 $$O/a.tape)"; \
	  cmp -s $$O/script.jsonl $$O/vectape.jsonl || fail "a: ntrec's script differs from neth_vec_tape's"; \
	  echo "a: $$(head -1 $$O/a.tape); byte for byte neth_vec_tape's"; \
	  seed=$$(awk '$$1 == "seed:" {print $$2; exit}' $$O/world.yaml); \
	  $(MAKE) --no-print-directory -s -C oracle script SEED=$$seed CONF=$$O/world.yaml SCRIPT=$$O/script.jsonl \
	    TAPE=../out/java/try/recgate/tape.jsonl CACHE=0 > $$O/java.log 2>&1 || fail "oracle: $$(tail -1 $$O/java.log)"; \
	  $(RT_NOUT)/test_snapshots --row-dump $$O/java.rows out/java/try/recgate > $$O/replay.log 2>&1 || \
	    fail "test_snapshots: $$(tail -1 $$O/replay.log)"; \
	  awk '/^t=2 /{k++} k == 1' $$O/vec.rows > $$O/vec.ep.rows; \
	  grep -v '^end ' $$O/java.rows > $$O/java.ep.rows; \
	  n=$$(wc -l < $$O/java.ep.rows); \
	  [ $$n -gt 0 ] && cmp -s $$O/vec.ep.rows $$O/java.ep.rows || \
	    fail "rows differ: $$(diff $$O/vec.ep.rows $$O/java.ep.rows | head -2 | cut -c1-200 | tr '\n' ' ')"; \
	  echo "a: the oracle ran ntrec's script: $$n rows equal to the episode's"; \
	  echo "REC-GATE PASS"
# lane/ladder, the fidelity ladder's gate (gpuq.sh check), about 20 s:
#   make -C csrc/runtime ladder-gate   (a) a ladder of one rung, fast, on the vanilla sim against the flat train-fast
#          sim (brute16, its fidelity in the start's image), 64 envs, every episode kept: each env's episodes after its
#          first (the make's, at the sim's own fidelity) byte for byte the flat run's (ntrec's digests), so a rung put
#          on at a reset is the fidelity the image holds; (b) the run-to-tape world with a fixed 50/50 ladder (group 0
#          vanilla, group 1 fast), 8 envs: the records' rungs and fidelities by group, env 0's second episode (vanilla)
#          by ntrec tape byte for byte neth_vec_tape's and run on the oracle, its rows equal to the episode's row for
#          row; ntrec tape refusing env 4's (fast); --tape on a fast env refused by ntloop; (c) the milestone schedule
#          (the wooden pickaxe at rate 0 over 16 episodes): vanilla opens once rung 0's window is full, its episodes
#          booked, episodes.jsonl's rungs -1, 0 and 1
LAD_OUT ?= $(NT_OUT)/laddergate
LAD_A   := --ns 64 --groups 4 --threads 8 --warm 0 --steps 410 --ep-len 100 --macros 63 --policy standin --digest
.PHONY: ladder-gate
ladder-gate: rlbind pool-native $(NT_OUT)/ntloop $(NT_OUT)/ntrec train-loop-cfg
	@cd $(RT_ROOT) && rm -rf $(LAD_OUT) out/java/try/laddergate && mkdir -p $(LAD_OUT) && \
	  fail() { echo "$$1"; echo "LADDER-GATE FAIL"; exit 1; }; \
	  L=$(NT_OUT)/ntloop; R=$(NT_OUT)/ntrec; O=$(LAD_OUT); C=$(NT_CFG); \
	  sed 's/^    archive: ms .*/    archive: none/' $(NTCONF) > $$C/lad-flat.yaml; \
	  awk 'BEGIN{s=0} /^  fidelity:/{s=1; next} s && /^    /{next} {s=0; print}' $$C/lad-flat.yaml > $$C/lad-van.yaml; \
	  { cat $$C/lad-van.yaml; printf '  ladder:\n    rungs: fast\n'; } > $$C/lad-one.yaml; \
	  { echo sim:; sed 's/observation: 32x32/observation: 16x16/' configs/tests/tape-gate.yaml | grep -v '^#' | sed 's/^/  /'; \
	    printf 'policy:\n  ladder:\n    rungs: fast,vanilla\n    schedule: fixed\n    shares: 0.5,0.5\n'; } > $$C/lad-mix.yaml; \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --run $$C/lad-one.yaml $(LAD_A) --rec $$O/one --rec-keep all > $$O/one.log 2>&1 & \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --run $$C/lad-flat.yaml $(LAD_A) --rec $$O/flat --rec-keep all > $$O/flat.log 2>&1 & \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --run $$C/lad-mix.yaml $(REC_A) --rec $$O/mix --rec-keep all --rec-raw $$O/mix.raw \
	    --rec-rows 0:$$O/vec.rows --tape 0:$$O/vectape.jsonl > $$O/mix.log 2>&1 & \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --run $$C/lad-mix.yaml $(REC_A) --tape 4:$$O/fast.jsonl > $$O/fasttape.log 2>&1 & \
	  { cat $$C/lad-van.yaml; printf '  ladder:\n    rungs: fast,vanilla\n    schedule: milestone\n    milestone: wooden_pickaxe\n    at: 0\n    window: 16\n'; } \
	    > $$C/lad-ms.yaml; \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $$L --run $$C/lad-ms.yaml $(LAD_A) --out $$O/ms > $$O/ms.log 2>&1 & \
	  wait; \
	  grep -q '^ladder: rung 1 (vanilla) opens at .* in 0.0% of rung 0.s last 16 episodes' $$O/ms.log && \
	    grep -q '^ladder rung 1 vanilla: [1-9]' $$O/ms.log && [ "$$(jq -s 'map(.rung) | unique' -c $$O/ms/episodes.jsonl)" = "[-1,0,1]" ] || \
	    fail "c: the milestone schedule: $$(grep '^ladder' $$O/ms.log | tail -3 | tr '\n' ' ')"; \
	  echo "c: $$(grep '^ladder: rung 1' $$O/ms.log | cut -c1-150); episodes.jsonl's rungs $$(jq -s 'map(.rung) | unique' -c $$O/ms/episodes.jsonl)"; \
	  for k in one flat mix; do grep -q '^trajectory digest' $$O/$$k.log || fail "ntloop $$k: $$(tail -1 $$O/$$k.log)"; done; \
	  for k in one flat; do $$R list $$O/$$k/episodes.nrec | awk '$$1 ~ /^[0-9]+$$/ && $$4 > 0 {print $$3 ":" $$4, $$12}' | LC_ALL=C sort \
	    > $$O/$$k.eps; done; \
	  n=$$(wc -l < $$O/one.eps); [ $$n -gt 0 ] && cmp -s $$O/one.eps $$O/flat.eps || \
	    fail "a: the rung put on at the resets differs from the image's fidelity: $$(diff $$O/one.eps $$O/flat.eps | head -2 | tr '\n' ' ')"; \
	  echo "a: one rung (fast) on the vanilla sim: $$n episodes after the make's, each byte for byte the flat train-fast run's"; \
	  $$R check $$O/mix/episodes.nrec $$O/mix.raw > $$O/mix.check 2>&1 || fail "b: $$(tail -2 $$O/mix.check | head -1)"; \
	  $$R list $$O/mix/episodes.nrec | awk '$$1 ~ /^[0-9]+$$/ {g = $$3 < 4 ? 0 : 1; k = g " " $$10 " " $$11; c[k]++} END {for (k in c) print k, c[k]}' \
	    | LC_ALL=C sort > $$O/mix.rungs; \
	  want=$$(printf '0 -1 vanilla 4\n0 1 vanilla 4\n1 -1 vanilla 4\n1 0 no-spawning+no-random_ticks+no-weather 4\n'); \
	  [ "$$(cat $$O/mix.rungs)" = "$$want" ] || fail "b: the records' rungs by group: $$(tr '\n' ';' < $$O/mix.rungs)"; \
	  echo "b: group, rung, fidelity, episodes: $$(tr '\n' ';' < $$O/mix.rungs)"; \
	  $$R tape $$O/mix/episodes.nrec 4:1 -o $$O/fastep.jsonl > $$O/fastep.tape 2>&1; rc=$$?; \
	  [ $$rc = 3 ] && grep -q "^REFUSED: episode .* rung 0, fidelity .* not vanilla" $$O/fastep.tape || \
	    fail "b: ntrec tape did not refuse a fast rung's episode (rc $$rc): $$(head -1 $$O/fastep.tape)"; \
	  echo "b: $$(cut -c1-110 $$O/fastep.tape)..."; \
	  grep -q '^FAIL --tape 4: refused' $$O/fasttape.log || fail "b: --tape on a fast env was not refused: $$(tail -1 $$O/fasttape.log)"; \
	  echo "b: $$(grep '^FAIL --tape' $$O/fasttape.log | cut -c1-110)..."; \
	  $$R tape $$O/mix/episodes.nrec 0:1 -o $$O/script.jsonl --world $$O/world.yaml > $$O/a.tape 2>&1 || \
	    fail "b: ntrec tape: $$(tail -1 $$O/a.tape)"; \
	  cmp -s $$O/script.jsonl $$O/vectape.jsonl || fail "b: ntrec's script differs from neth_vec_tape's"; \
	  seed=$$(awk '$$1 == "seed:" {print $$2; exit}' $$O/world.yaml); \
	  $(MAKE) --no-print-directory -s -C oracle script SEED=$$seed CONF=$$O/world.yaml SCRIPT=$$O/script.jsonl \
	    TAPE=../out/java/try/laddergate/tape.jsonl CACHE=0 > $$O/java.log 2>&1 || fail "oracle: $$(tail -1 $$O/java.log)"; \
	  $(RT_NOUT)/test_snapshots --row-dump $$O/java.rows out/java/try/laddergate > $$O/replay.log 2>&1 || \
	    fail "test_snapshots: $$(tail -1 $$O/replay.log)"; \
	  awk '/^t=2 /{k++} k == 1' $$O/vec.rows > $$O/vec.ep.rows; \
	  grep -v '^end ' $$O/java.rows > $$O/java.ep.rows; \
	  n=$$(wc -l < $$O/java.ep.rows); \
	  [ $$n -gt 0 ] && cmp -s $$O/vec.ep.rows $$O/java.ep.rows || \
	    fail "b: rows differ: $$(diff $$O/vec.ep.rows $$O/java.ep.rows | head -2 | cut -c1-200 | tr '\n' ' ')"; \
	  echo "b: the vanilla rung's episode (env 0's second) run on the oracle from ntrec's script: $$n rows equal to the episode's"; \
	  echo "LADDER-GATE PASS"

# lane/ladder10, the policy's GUI clicks on the Java client (gpuq.sh check), about a minute:
#   make -C csrc/runtime gui-tape-gate   run-to-tape's world (configs/tests/tape-gate.yaml at 16x16), 8 envs, macros 63 with the
#          screen macro off (NETH_VEC_NOSCREEN) and actions.gui cursor, the stand-in policy with key 12 at 2%
#          (standin-gui: it opens the inventory, clicks the cells its camera bins name with its mine, place and sneak keys,
#          closes it again; the craft macro's plans one op a step): env 0's second episode written by neth_vec_tape, run
#          on the oracle, and its rows equal to the episode's row for row; the script's clicks and closes counted (a
#          gate that clicked nothing fails)
GUI_OUT ?= $(NT_OUT)/guigate
GUI_EP  ?= 600
GUI_A   := --ns 8 --groups 2 --threads 4 --warm 0 --steps $$((2 * $(GUI_EP) + 2)) --ep-len $(GUI_EP) --macros 33554495 \
           --mopt gui=cursor --policy standin-gui --digest

.PHONY: gui-tape-gate
gui-tape-gate: rlbind pool-native $(NT_OUT)/ntloop train-loop-cfg
	@cd $(RT_ROOT) && rm -rf $(GUI_OUT) out/java/try/guigate && mkdir -p $(GUI_OUT) && \
	  fail() { echo "$$1"; echo "GUI-TAPE-GATE FAIL"; exit 1; }; \
	  sed 's/observation: 32x32/observation: 16x16/' configs/tests/tape-gate.yaml > $(NT_CFG)/guigate.yaml; \
	  O=$(GUI_OUT); \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(NT_OUT)/ntloop --config $(NT_CFG)/guigate.yaml $(GUI_A) \
	    --rec-rows 0:$$O/vec.rows --tape 0:$$O/script.jsonl > $$O/a.log 2>&1 || fail "ntloop: $$(tail -1 $$O/a.log)"; \
	  grep -q '^trajectory digest' $$O/a.log || fail "ntloop: $$(tail -1 $$O/a.log)"; \
	  nc=$$(grep -o '\["click",' $$O/script.jsonl | wc -l); nx=$$(grep -o '\["close"' $$O/script.jsonl | wc -l); \
	  [ $$nc -gt 0 ] && [ $$nx -gt 0 ] || fail "the episode clicked $$nc times and closed $$nx: nothing to check"; \
	  awk '/^world:/ {w = 1; print; next} w && /^[^ #]/ {w = 0} w' $(NT_CFG)/guigate.yaml > $$O/world.yaml; \
	  seed=$$(awk '$$1 == "seed:" {print $$2; exit}' $$O/world.yaml); \
	  $(MAKE) --no-print-directory -s -C oracle script SEED=$$seed CONF=$$O/world.yaml SCRIPT=$$O/script.jsonl \
	    TAPE=../out/java/try/guigate/tape.jsonl CACHE=0 > $$O/java.log 2>&1 || fail "oracle: $$(tail -1 $$O/java.log)"; \
	  $(RT_NOUT)/test_snapshots --row-dump $$O/java.rows out/java/try/guigate > $$O/replay.log 2>&1 || \
	    fail "test_snapshots: $$(tail -1 $$O/replay.log)"; \
	  awk '/^t=2 /{k++} k == 1' $$O/vec.rows > $$O/vec.ep.rows; \
	  grep -v '^end ' $$O/java.rows > $$O/java.ep.rows; \
	  n=$$(wc -l < $$O/java.ep.rows); \
	  [ $$n -gt 0 ] && cmp -s $$O/vec.ep.rows $$O/java.ep.rows || \
	    fail "rows differ: $$(diff $$O/vec.ep.rows $$O/java.ep.rows | head -2 | cut -c1-200 | tr '\n' ' ')"; \
	  echo "env 0's second episode ($$nc clicks, $$nx closes, the screen macro off) run on the oracle: $$n rows equal"; \
	  echo "GUI-TAPE-GATE PASS"

# lane/ntboot, the frozen eval as one command (a long GPU job: run it as gpuq.sh check --mem 12000 -- make ...):
#   make -C csrc/runtime train-score RUN=FILE W=W.ntw [V=s1] [RUNS=512] [MIN=60] [SEED=1000] [GROUPS=4] [CLOCK=wrap]
#        [SCORE_ARGS=...]
# ntloop --score: W's actor frozen (nothing learned), RUNS envs from RUN's start, one MIN-minute episode each with RUN's
# macros and fidelity, the actor's sampler seeded by SEED (every env starts at the same spawn, so one SEED is one set of
# trajectories: two models on one SEED meet the same sampler stream), the time scalar read modulo 1 (CLOCK raw: as it
# grows); prints mcsr's fromspawn.py's table (each timed item's first held, each milestone's share at the end, deaths a
# run, cells, depth with its standard error, env-steps/s) and a JSON line; the episodes and score.json in
# out/runtime/train/score/NAME-SEED (NAME W's file name, SCORE_DIR another). V is net.h's variant W was exported for
# (s1: 64x64 with the stem; none: 16x16 wm 1)
RUNS   ?= 512
MIN    ?= 60
SEED   ?= 1000
GROUPS ?= 4
CLOCK  ?= wrap
V      ?=
SCORE_DIR ?= $(NT_OUT)/score/$(basename $(notdir $(W)))-$(SEED)
NT_SCORE_BIN := $(if $(V),$(NT_OUT)/$(V)/ntloop,$(NT_OUT)/ntloop)
# a path as given (absolute or ~), else the tree root's (make -C csrc/runtime from the root: RUN=configs/NAME.yaml)
nt_root_path = $(if $(filter /% ~%,$(1)),$(1),$(RT_ROOT)/$(1))

.PHONY: train-score
train-score: $(NT_SCORE_BIN)
	@[ -n "$(RUN)" ] && [ -n "$(W)" ] || { echo "train-score: RUN=FILE W=W.ntw [V=s1] [RUNS MIN SEED GROUPS CLOCK]"; exit 2; }; \
	  cd $(RT_ROOT) && rm -rf $(SCORE_DIR) && mkdir -p $(SCORE_DIR) && \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(NT_SCORE_BIN) --run $(call nt_root_path,$(RUN)) --init $(call nt_root_path,$(W)) --score --ns $(RUNS) \
	    --groups $(GROUPS) --minutes $(MIN) --seed $(SEED) --clock $(CLOCK) --out $(SCORE_DIR) \
	    --score-json $(SCORE_DIR)/score.json $(SCORE_ARGS) > $(SCORE_DIR)/score.log 2>&1; rc=$$?; \
	  grep '^score' $(SCORE_DIR)/score.log | sed 's/^score: //'; grep '^FAIL' $(SCORE_DIR)/score.log; \
	  [ $$rc = 0 ] && echo "TRAIN-SCORE OK $(SCORE_DIR)" || { tail -3 $(SCORE_DIR)/score.log; echo "TRAIN-SCORE FAIL"; exit 1; }

# lane/ntboot: several models at several eval seeds, one table (a long run's checkpoints, a fine-tune's arms):
#   make -C csrc/runtime train-score-sweep RUN=FILE WS="A.ntw B.ntw ..." | WDIR=DIR [SEEDS="1000 1001"] [V RUNS MIN ...]
# WDIR scores every DIR/model*.ntw (ntloop --save-every's model-MMM.ntw and the final model.ntw). Each (model, seed) is
# train-score's, its directory SWEEP_DIR/NAME-SEED; then a line each: model, seed, depth (SE), iron pickaxe, bucket,
# flint and steel, deaths a run, and a line a model of its seeds pooled (the mean depth: one model's two eval seeds
# moved by 0.25 to 0.35 at 512 runs, more than one score's SE says, so rank on pooled seeds)
SEEDS     ?= 1000 1001
SWEEP_DIR ?= $(NT_OUT)/score/sweep
SW_DIR     = $(call nt_root_path,$(SWEEP_DIR))
.PHONY: train-score-sweep
train-score-sweep: $(NT_SCORE_BIN)
	@ws="$(WS)"; [ -n "$(WDIR)" ] && ws="$$(ls $(call nt_root_path,$(WDIR))/model*.ntw 2>/dev/null)"; \
	  [ -n "$(RUN)" ] && [ -n "$$ws" ] || { echo "train-score-sweep: RUN=FILE WS=\"A.ntw ...\" | WDIR=DIR [SEEDS=...]"; exit 2; }; \
	  mkdir -p $(SW_DIR); bad=0; \
	  for w in $$ws; do for k in $(SEEDS); do \
	    nm=$$(basename $$(dirname $$w))-$$(basename $$w .ntw); [ -n "$(WDIR)" ] || nm=$$(basename $$w .ntw); \
	    $(MAKE) --no-print-directory -s train-score RUN=$(RUN) W=$$w SEED=$$k SCORE_DIR=$(SW_DIR)/$$nm-$$k \
	      > $(SW_DIR)/$$nm-$$k.out 2>&1 || { bad=1; echo "$$nm $$k: $$(tail -1 $(SW_DIR)/$$nm-$$k.out)"; }; \
	  done; done; \
	  printf '%-34s %5s %6s %6s %6s %6s %6s %6s %6s %6s\n' model seed depth se iron_p bucket flint deaths end dragon; \
	  for w in $$ws; do nm=$$(basename $$(dirname $$w))-$$(basename $$w .ntw); [ -n "$(WDIR)" ] || nm=$$(basename $$w .ntw); \
	    for k in $(SEEDS); do f=$(SW_DIR)/$$nm-$$k/score.json; [ -f $$f ] || continue; \
	      jq -r --arg m $$nm '[$$m, .seed, .depth, .depth_se, .end.iron_pickaxe, .end.bucket, .end.flint_and_steel, .deaths, (.end.end // 0), (.end.dragon // 0)] | @tsv' $$f; \
	    done | awk -F'\t' '{printf "%-34s %5s %6.3f %6.3f %6.3f %6.3f %6.3f %6.3f %6.3f %6.3f\n", $$1, $$2, $$3, $$4, $$5, $$6, $$7, $$8, $$9, $$10; \
	      n++; d += $$3; ip += $$5; de += $$8; en += $$9; dr += $$10} END {if (n > 1) printf "%-34s %5s %6.3f %6s %6.3f %6s %6s %6.3f %6.3f %6.3f\n", $$1, "pool", d / n, "", ip / n, "", "", de / n, en / n, dr / n}'; \
	  done; [ $$bad = 0 ] && echo "TRAIN-SCORE-SWEEP OK $(SW_DIR)" || { echo "TRAIN-SCORE-SWEEP FAIL"; exit 1; }

# lane/dbench (2026-11-09): the dragon benchmark, the measuring stick of the macro route (Elliot, 2026-11-09 3:25 pm: a
# policy over the macros that kills the dragon from spawn). One command over train-score (ntloop --score, no second eval
# path), a table and a TSV:
#   make -C csrc/runtime dragon-bench W=MODEL.ntw [V=s1.ls] [DB_RUNS=256] [DB_MIN=120] [DB_SEEDS="spawn 101-164/4"]
#        [DB_LRUNS=128] [DB_LINKS="STAGE:MIN ..."] [DB_LIB=LIB] [DB_PRE="gpuq.sh check --mem 12000 --"] [DB_DIR=DIR] [DB_REDO=1]
# From spawn (configs/tests/dbench.yaml: every chain macro, vanilla fidelity, master's defaults), for each DB_SEEDS entry
# (spawn: seed 42's spawn; A-B,C: the captured starts of those seeds, starts.seeds): each milestone of the chain in
# order (wooden pickaxe .. the stronghold's room, the End, the dragon): the share of runs that reached it, the median
# minute it was first held, the conversion from the milestone before (with its standard error), the deaths a run and what
# killed them and which macro drove, from ntloop --score-books (vecenv.h neth_vec_db: counting only). Then link by link:
# the library's start of each stage (--library --stages, DB_LINKS: the stage and its length in minutes; stagelen's
# lengths where there are, 10 minutes for the early stages and 20 to 30 for the late), the next milestone's rate, and for
# every link under 0.5 where the runs that fail stop: died (what, driving macro, dimension), or timed out (who drove
# its steps; the link's macros never starting; the kit held). DB_DIR/dragon-bench.txt and .tsv; each run's directory
# (score.json, episodes.jsonl, books.jsonl) is kept and a finished one is skipped (DB_REDO=1 runs it again).
DB_DIR    ?= $(NT_OUT)/dbench/$(basename $(notdir $(W)))
DB_RUN    ?= configs/tests/dbench.yaml
DB_RUNS   ?= 256
DB_MIN    ?= 120
DB_SEEDS  ?= spawn 101-164/4
DB_LRUNS  ?= 128
DB_LINKS  ?= spawn:10 wooden_pickaxe:10 stone_pickaxe:10 furnace:10 iron_ingot:10 iron_pickaxe:10 bucket:10 flint_and_steel:20 diamond_pickaxe:20 obsidian:20 nether:20 blaze_rod:20 ender_pearl:30 ender_eye:30 end:30
DB_LIB    ?= out/library-train
DB_PRE    ?=
DB_REDO   ?=
# DB_SET="difficulty=easy keep_inventory=on": world keys set in the run file's sim world for every job (a copy of DB_RUN
# in DB_DIR/run.yaml); the start snapshots are recorded at normal difficulty and no keep_inventory (see DEVLOG 2026-11-09
# lane/dbench for how the engine takes a different one)
DB_SET    ?=
.PHONY: dragon-bench dragon-bench-report
dragon-bench: $(NT_SCORE_BIN)
	@[ -n "$(W)" ] || { echo "dragon-bench: W=MODEL.ntw [V=s1.ls] [DB_RUNS DB_MIN DB_SEEDS DB_LRUNS DB_LINKS DB_PRE DB_DIR]"; exit 2; }; \
	  bash $(CURDIR)/train/dbench.sh run -m "$(MAKE) --no-print-directory" -w "$(call nt_root_path,$(W))" -v "$(V)" \
	    -r "$(DB_RUN)" -o "$(call nt_root_path,$(DB_DIR))" -n "$(DB_RUNS)" -t "$(DB_MIN)" -s "$(DB_SEEDS)" -L "$(DB_LRUNS)" \
	    -l "$(DB_LINKS)" -B "$(DB_LIB)" -p "$(DB_PRE)" -k "$(SEED)" -S "$(DB_SET)" $(if $(DB_REDO),-f,) && $(MAKE) --no-print-directory dragon-bench-report W=$(W) DB_DIR=$(DB_DIR) DB_LIB=$(DB_LIB)

dragon-bench-report:
	@bash $(CURDIR)/train/dbench.sh report -o "$(call nt_root_path,$(DB_DIR))" -w "$(W)" -B "$(DB_LIB)"
