# The native trainer's actor (train/actor.h; lane/ntactor): net.h's forward and fastppo's gumbel sampler as CUDA
# kernels, its check against mcsr-speedrun's scripts/ntref.py fixture and its benchmark.
#
#   make -C csrc/runtime train-actor            out/runtime/train/actor.o, actor_check, actor_bench (the default net)
#   make -C csrc/runtime train-actor-check      [NTA_FIXTURE=dir]: fp32 against the fixture (actor_check's header)
#   make -C csrc/runtime train-actor-bench      [NTA_BENCH="--n 300 128 512"]: device time a call, samples a second
#
# net.h's variants (lane/ntwidth), one build each in out/runtime/train/V/: V is wN (16x16 at wm N) or sN (64x64 with
# the stem at wm N); NT_VARIANTS lists the gated ones. NT_V=V picks one for the targets below; the fixture is
# ntref.py's for that net, ~/.cache/netherite-native/actor-fresh-V (fixtures.sh in lane/ntwidth's notes made them).
#
#   make -C csrc/runtime train-actor-v NT_V=w2          out/runtime/train/w2/actor.o, actor_check, actor_bench
#   make -C csrc/runtime train-actor-v-check NT_V=w2    actor_check on the variant's fixture (fp32 and bf16)
#   make -C csrc/runtime train-actor-v-bench NT_V=w2    [NTA_BENCH=...] actor_bench on it
NTA_OUT     := $(OUT)/train
NTA_DIR     := $(RUNTIME)/train
NTA_NVCC    ?= $(or $(PL_NVCC),/usr/local/cuda/bin/nvcc)
NTA_CUDAINC := $(dir $(NTA_NVCC))../include
NTA_CUDALIB := $(dir $(NTA_NVCC))../lib64
NTA_CCACHE  := $(if $(shell command -v ccache 2>/dev/null),CCACHE_DIR=$(RT_CACHE)/ccache ccache )
NTA_NVFLAGS := -O3 -std=c++17 -arch=$(CUDA_ARCH) -Xcompiler -Wall -Werror all-warnings
NTA_CFLAGS  := -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -I$(NTA_CUDAINC)
NTA_FIXTURE ?= $(RT_CACHE)/native/actor-a05-070
NTA_BENCH   ?= --n 300 128 512

# the variants: V's -D flags (wN: NETH_NET_WM N; sN: NETH_NET_WM N, NETH_NET_STEM 1) and its fixtures; V.mP is V with net.h's NETH_NET_MMA P
# (lane/nth16, prec.cuh: 1 fp16 with fp32 accumulation, 2 fp16 with fp16, 3 fp16 with fp16 promoted to fp32), on V's
# fixtures: make train-actor-v NT_V=s1.m2. Lane/ntfc's fusions as further words, in any order after V: .fc the
# learner's MLP, heads and loss as one kernel (learn.cu NETH_LEARN_FC 1; .fcN with N warps a block, fc.cuh's FC_W; .bmN its
# rows a block, FC_BM), .af the
# actor's step fused (actor.cu NETH_ACTOR_FUSE 1; at 16x16 wm 1, elsewhere the per-layer actor): NT_V=s1.fc, w1.m2.fc16.af
NT_VARIANTS ?= w1 w2 w4 w8 s1 s2
NT_V        ?= w1
# and V.X... (lane/ntbytes) the learner's stem fusions X, after any mP: b NETH_LEARN_S1B, f NETH_LEARN_S1F (s1.b.f both, s1.m2.b.f
# with fp16 accumulation)
nt_vbase     = $(firstword $(subst ., ,$(1)))
nt_vwords    = $(wordlist 2,32,$(subst ., ,$(1)))
nt_vmma      = $(patsubst m%,%,$(filter m%,$(call nt_vwords,$(1))))
# V.rN (lane/ntgru): net.h's NETH_NET_RNN N, the minGRU after fc2 (w1.r1, s1.r1, s1.m2.r1); its fixtures are V's own
# (actor-fresh-w1.r1, learn-fresh-w1.r1: ntref.py fresh:...,rnn=1,hzero=0), NT_RVARIANTS the gated ones
nt_vrnn      = $(patsubst r%,%,$(filter r%,$(call nt_vwords,$(1))))
nt_vflags    = -DNETH_NET_WM=$(patsubst s%,%,$(patsubst w%,%,$(call nt_vbase,$(1)))) \
               -DNETH_NET_STEM=$(if $(filter s%,$(call nt_vbase,$(1))),1,0)$(if $(call nt_vmma,$(1)), -DNETH_NET_MMA=$(call nt_vmma,$(1)))$(if \
               $(call nt_vrnn,$(1)), -DNETH_NET_RNN=$(call nt_vrnn,$(1)))$(if \
               $(filter fc%,$(call nt_vwords,$(1))), -DNETH_LEARN_FC=1)$(if $(patsubst fc%,%,$(filter fc%,$(call nt_vwords,$(1)))), \
               -DFC_W=$(patsubst fc%,%,$(filter fc%,$(call nt_vwords,$(1)))))$(if $(filter bm%,$(call nt_vwords,$(1))), \
               -DFC_BM=$(patsubst bm%,%,$(filter bm%,$(call nt_vwords,$(1)))))$(if \
               $(filter af,$(call nt_vwords,$(1))), -DNETH_ACTOR_FUSE=1) \
               $(foreach x,$(call nt_vwords,$(1)),$(NT_X_$(x)))
NT_X_b      := -DNETH_LEARN_S1B=1
NT_X_f      := -DNETH_LEARN_S1F=1
# lane/nthotbar: .hb, net.h's hotbar head (NETH_NET_HOTBAR 1; s1.hb, w1.hb), its fixtures actor-fresh-V.hb, learn-fresh-V.hb
NT_X_hb     := -DNETH_NET_HOTBAR=1
# lane/starts: .ls, the late game's scalars (net.h NETH_NET_LATE 1: 155 + 14; s1.ls), an old blob widened with zeros
NT_X_ls     := -DNETH_NET_LATE=1
# lane/ladder2 + lane/hill-rnn-hotbar: .hs, legal 9-slot HUD hotbar scalars (net.h NETH_NET_HSEE 1: + 108; s1.hb.hs), an old blob widened with zeros
NT_X_hs     := -DNETH_NET_HSEE=1
# lane/ladder4: .dk, the dig the screen shows as scalars (net.h NETH_NET_DIG 1: + 2, the crack stage and the hit; s1.hb.dk)
NT_X_dk     := -DNETH_NET_DIG=1
# lane/ladder5: .or, the seek's view as scalars (net.h NETH_NET_ORE 1: + 12, iron ore, diamond ore and logs in view; s1.hb.dk.or)
NT_X_or     := -DNETH_NET_ORE=1
# lane/ladder10: .gv, what the open container screen shows as scalars (net.h NETH_NET_GV 1: + 708; s1.hb.dk.gv), for a student
# that clicks (actions.gui cursor)
NT_X_gv     := -DNETH_NET_GV=1
# lane/acrl: .tk, the task as the policy is told it (net.h NETH_NET_TASK 1: + 2, the one-hot of task_item and task_item2;
# s1.dk.or.tk), an old blob widened with zeros
NT_X_tk     := -DNETH_NET_TASK=1
# lane/ladder2: .gk, .gc, .gkc, the actor's keys, camera, or both drawn greedily (net.h NETH_ACTOR_GREEDY; frozen evals only)
NT_X_gk     := -DNETH_ACTOR_GREEDY=1
NT_X_gc     := -DNETH_ACTOR_GREEDY=2
NT_X_gkc    := -DNETH_ACTOR_GREEDY=3
# lane/rawpix: .t3, .t5, .t7, the keys and camera drawn at temperature 0.3, 0.5, 0.7 (net.h NETH_ACTOR_TEMP; frozen evals only)
NT_X_t3     := -DNETH_ACTOR_TEMP=3
NT_X_t5     := -DNETH_ACTOR_TEMP=5
NT_X_t7     := -DNETH_ACTOR_TEMP=7
# lane/neon-prec-sim-buf: .sb (BF16 rollout/H2D scalars + zero-copy psc alias when --zero-sc is off), .pa (psc alias alone)
NT_X_sb     := -DNETH_SC_BF16=1 -DNETH_PSC_ALIAS=1
NT_X_pa     := -DNETH_PSC_ALIAS=1
NT_RVARIANTS ?= w1.r1 s1.r1
NT_FIXDIR   ?= $(RT_CACHE)/native

.PHONY: train-actor train-actor-check train-actor-bench train-actor-v train-actor-v-check train-actor-v-bench
all: train-actor
train-actor: $(NTA_OUT)/actor_check $(NTA_OUT)/actor_bench

$(NTA_OUT)/actor.o: $(NTA_DIR)/actor.cu $(NTA_DIR)/actor.h $(NTA_DIR)/net.h $(NTA_DIR)/learn.h $(NTA_DIR)/prec.cuh \
    $(NTA_DIR)/actorfused.cuh $(NTA_DIR)/rnn_actor.cuh
	@mkdir -p $(@D)
	$(NTA_CCACHE)$(NTA_NVCC) $(NTA_NVFLAGS) -c -o $@ $<

$(NTA_OUT)/%: $(NTA_DIR)/%.c $(NTA_OUT)/actor.o $(NTA_DIR)/actor.h $(NTA_DIR)/net.h $(NTA_DIR)/learn.h
	$(NTA_CCACHE)cc $(NTA_CFLAGS) -c -o $@.o $<
	cc -o $@ $@.o $(NTA_OUT)/actor.o -L$(NTA_CUDALIB) -Wl,-rpath,$(NTA_CUDALIB) -lcudart -lstdc++ -lm

$(NTA_OUT)/%/actor.o: $(NTA_DIR)/actor.cu $(NTA_DIR)/actor.h $(NTA_DIR)/net.h $(NTA_DIR)/learn.h $(NTA_DIR)/prec.cuh \
    $(NTA_DIR)/actorfused.cuh $(NTA_DIR)/rnn_actor.cuh
	@mkdir -p $(@D)
	$(NTA_CCACHE)$(NTA_NVCC) $(NTA_NVFLAGS) $(call nt_vflags,$*) -c -o $@ $<

$(NTA_OUT)/%/actor_check $(NTA_OUT)/%/actor_bench: $(NTA_DIR)/actor_check.c $(NTA_DIR)/actor_bench.c $(NTA_OUT)/%/actor.o \
    $(NTA_DIR)/actor.h $(NTA_DIR)/net.h $(NTA_DIR)/learn.h
	for p in actor_check actor_bench; do \
	  $(NTA_CCACHE)cc $(NTA_CFLAGS) $(call nt_vflags,$*) -c -o $(@D)/$$p.o $(NTA_DIR)/$$p.c && \
	  cc -o $(@D)/$$p $(@D)/$$p.o $(@D)/actor.o -L$(NTA_CUDALIB) -Wl,-rpath,$(NTA_CUDALIB) -lcudart -lstdc++ -lm || exit 1; \
	done

train-actor-check: train-actor
	$(NTA_OUT)/actor_check $(NTA_FIXTURE)

train-actor-bench: train-actor
	$(NTA_OUT)/actor_bench --fixture $(NTA_FIXTURE) $(NTA_BENCH)

train-actor-v: $(NTA_OUT)/$(NT_V)/actor_check

train-actor-v-check: train-actor-v
	$(NTA_OUT)/$(NT_V)/actor_check $(NT_FIXDIR)/actor-fresh-$(NT_V)

train-actor-v-bench: train-actor-v
	$(NTA_OUT)/$(NT_V)/actor_bench --fixture $(NT_FIXDIR)/actor-fresh-$(NT_V) $(NTA_BENCH)
