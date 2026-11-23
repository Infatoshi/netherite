# The native trainer's learner (train/learn.h; lane/ntlearn): GAE, the minibatch's forward, PPO loss and backward, and
# the clipped Adam step as CUDA kernels and cuBLAS GEMMs; its check against mcsr-speedrun's scripts/ntref.py learner
# fixture and its benchmark.
#
#   make -C csrc/runtime train-learn            out/runtime/train/learn.o, learn_check, learn_bench (the default net)
#   make -C csrc/runtime train-learn-check      [NTL_FIXTURE=dir]: fp32 against the fixture (learn_check's header)
#   make -C csrc/runtime train-learn-bench      [NTL_BENCH="--updates 20"]: device time an update, samples a second
#
# net.h's variants (lane/ntwidth; NT_V and NT_VARIANTS as actor.mk's), one build each in out/runtime/train/V/, the
# fixture ~/.cache/netherite-native/learn-fresh-V:
#
#   make -C csrc/runtime train-learn-v NT_V=w2          out/runtime/train/w2/learn.o, learn_check, learn_bench
#   make -C csrc/runtime train-learn-v-check NT_V=w2    learn_check on the variant's fixture, fp32 then bf16
#   make -C csrc/runtime train-learn-v-bench NT_V=w2    [NTL_BENCH=...] learn_bench on it
#   make -C csrc/runtime train-width                    every variant's actor and learner
#   make -C csrc/runtime train-width-check              every variant's actor_check and learn_check (fp32, bf16): the
#                                                       lane's gate, one line a check and TRAIN-WIDTH-CHECK PASS or FAIL
LRN_OUT     := $(OUT)/train
LRN_DIR     := $(RUNTIME)/train
NTL_NVCC    ?= $(or $(PL_NVCC),/usr/local/cuda/bin/nvcc)
NTL_CUDAINC := $(dir $(NTL_NVCC))../include
NTL_CUDALIB := $(dir $(NTL_NVCC))../lib64
NTL_CCACHE  := $(if $(shell command -v ccache 2>/dev/null),CCACHE_DIR=$(RT_CACHE)/ccache ccache )
NTL_NVFLAGS := -O3 -std=c++17 -arch=$(CUDA_ARCH) -Xcompiler -Wall -Werror all-warnings
NTL_CFLAGS  := -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Werror -I$(NTL_CUDAINC)
NTL_FIXTURE ?= $(RT_CACHE)/native/learn-a05-070
NTL_BENCH   ?= --updates 20

.PHONY: train-learn train-learn-check train-learn-bench train-learn-v train-learn-v-check train-learn-v-bench
.PHONY: train-width train-width-check
all: train-learn
train-learn: $(LRN_OUT)/learn_check $(LRN_OUT)/learn_bench

$(LRN_OUT)/learn.o: $(LRN_DIR)/learn.cu $(LRN_DIR)/learn.h $(LRN_DIR)/net.h $(LRN_DIR)/prec.cuh $(LRN_DIR)/fc.cuh \
    $(LRN_DIR)/rnn.cuh $(LRN_DIR)/critic.cuh
	@mkdir -p $(@D)
	$(NTL_CCACHE)$(NTL_NVCC) $(NTL_NVFLAGS) -c -o $@ $<

$(LRN_OUT)/learn_%: $(LRN_DIR)/learn_%.c $(LRN_DIR)/learn_io.h $(LRN_OUT)/learn.o $(LRN_DIR)/learn.h $(LRN_DIR)/net.h
	@mkdir -p $(@D)
	$(NTL_CCACHE)cc $(NTL_CFLAGS) -c -o $@.o $<
	cc -o $@ $@.o $(LRN_OUT)/learn.o -L$(NTL_CUDALIB) -Wl,-rpath,$(NTL_CUDALIB) -lcublas -lcudart -lstdc++ -lm

$(LRN_OUT)/%/learn.o: $(LRN_DIR)/learn.cu $(LRN_DIR)/learn.h $(LRN_DIR)/net.h $(LRN_DIR)/prec.cuh $(LRN_DIR)/fc.cuh \
    $(LRN_DIR)/rnn.cuh $(LRN_DIR)/critic.cuh
	@mkdir -p $(@D)
	$(NTL_CCACHE)$(NTL_NVCC) $(NTL_NVFLAGS) $(call nt_vflags,$*) -c -o $@ $<

$(LRN_OUT)/%/learn_check $(LRN_OUT)/%/learn_bench: $(LRN_DIR)/learn_check.c $(LRN_DIR)/learn_bench.c \
    $(LRN_DIR)/learn_io.h $(LRN_OUT)/%/learn.o $(LRN_DIR)/learn.h $(LRN_DIR)/net.h
	for p in learn_check learn_bench; do \
	  $(NTL_CCACHE)cc $(NTL_CFLAGS) $(call nt_vflags,$*) -c -o $(@D)/$$p.o $(LRN_DIR)/$$p.c && \
	  cc -o $(@D)/$$p $(@D)/$$p.o $(@D)/learn.o -L$(NTL_CUDALIB) -Wl,-rpath,$(NTL_CUDALIB) -lcublas -lcudart -lstdc++ -lm \
	    || exit 1; \
	done

train-learn-check: train-learn
	$(LRN_OUT)/learn_check $(NTL_FIXTURE)

train-learn-bench: train-learn
	$(LRN_OUT)/learn_bench --fixture $(NTL_FIXTURE) $(NTL_BENCH)

train-learn-v: $(LRN_OUT)/$(NT_V)/learn_check

train-learn-v-check: train-learn-v
	$(LRN_OUT)/$(NT_V)/learn_check $(NT_FIXDIR)/learn-fresh-$(call nt_vbase,$(NT_V))
	$(LRN_OUT)/$(NT_V)/learn_check --prec bf16 $(NT_FIXDIR)/learn-fresh-$(call nt_vbase,$(NT_V))

train-learn-v-bench: train-learn-v
	$(LRN_OUT)/$(NT_V)/learn_bench --fixture $(NT_FIXDIR)/learn-fresh-$(call nt_vbase,$(NT_V)) $(NTL_BENCH)

# NT_WPREC=P (lane/nth16): every check below on the variants' NETH_NET_MMA P builds (V.mP; the a05 fixtures on w1.mP), its
# logs in widthcheck.mP; the 16-bit path (--prec bf16, the check names' "bf16") is then fp16. NT_WX=X (lane/ntbytes):
# s1's learner checks on s1[.mP].X (b, b.f: the stem fusions), the logs in widthcheck[.mP].X
# NT_WFUSE=.W... (lane/ntfc): actor.mk's fusion words after it, on every variant (.fc16.af: V.fc16.af; a variant a
# fusion does not reach builds its per-layer path)
NT_WSUF := $(if $(NT_WPREC),.m$(NT_WPREC))$(NT_WFUSE)
NT_WA05 := $(if $(NT_WSUF),$(NTA_OUT)/w1$(NT_WSUF),$(NTA_OUT))
NT_WX   ?=
ntl_wv   = $(1)$(NT_WSUF)$(if $(and $(NT_WX),$(filter s1,$(1))),.$(NT_WX))
# lane/nthotbar: the hotbar head's builds (actor.mk's .hb), each on its own fixtures (ntref.py fresh:...,hot=1:
# actor-fresh-V.hb, learn-fresh-V.hb and learn-fresh-V.hb-s1)
NT_HBVARIANTS ?= w1.hb s1.hb
train-width: train-actor train-learn $(NT_WA05)/actor_check $(NT_WA05)/learn_check \
    $(foreach v,$(NT_VARIANTS) $(NT_RVARIANTS),$(NTA_OUT)/$(v)$(NT_WSUF)/actor_check $(LRN_OUT)/$(call ntl_wv,$(v))/learn_check) \
    $(foreach v,$(NT_HBVARIANTS),$(NTA_OUT)/$(v)/actor_check $(LRN_OUT)/$(v)/learn_check)

# every check runs (a failure does not stop the rest); each one's last line, then the verdict. lane/ntactor's and
# lane/ntlearn's own fixtures (round 10's a05 weights) run too, on the default build, and each variant's learner on a
# second fixture (learn-fresh-V-s1: ntref.py learner --seed 1).
train-width-check: train-width
	@fail=0; d=$(LRN_OUT)/widthcheck$(NT_WSUF)$(if $(NT_WX),.$(NT_WX)); mkdir -p $$d; \
	run() { n=$$1; shift; "$$@" > $$d/$$n.log 2>&1; rc=$$?; \
	  printf '%-18s %s\n' "$$n" "$$(grep -E 'actor-check:|learn_check: (PASS|FAIL)' $$d/$$n.log | tail -1) (rc $$rc)"; \
	  [ $$rc = 0 ] || fail=1; }; \
	run a05-actor $(NT_WA05)/actor_check $(NTA_FIXTURE); \
	run a05-learn-fp32 $(NT_WA05)/learn_check $(NTL_FIXTURE); \
	run a05-learn-bf16 $(NT_WA05)/learn_check --prec bf16 $(NTL_FIXTURE); \
	for v in $(NT_VARIANTS) $(NT_RVARIANTS); do \
	  run $$v-actor $(NTA_OUT)/$$v$(NT_WSUF)/actor_check $(NT_FIXDIR)/actor-fresh-$$v; \
	  for sd in "" -s1; do \
	    lv=$$v$(NT_WSUF); [ -n "$(NT_WX)" ] && [ $$v = s1 ] && lv=$$lv.$(NT_WX); \
	    run $$v$$sd-learn-fp32 $(LRN_OUT)/$$lv/learn_check $(NT_FIXDIR)/learn-fresh-$$v$$sd; \
	    run $$v$$sd-learn-bf16 $(LRN_OUT)/$$lv/learn_check --prec bf16 $(NT_FIXDIR)/learn-fresh-$$v$$sd; \
	  done; \
	done; \
	for v in $(NT_HBVARIANTS); do \
	  run $$v-actor $(NTA_OUT)/$$v/actor_check $(NT_FIXDIR)/actor-fresh-$$v; \
	  for sd in "" -s1; do \
	    run $$v$$sd-learn-fp32 $(LRN_OUT)/$$v/learn_check $(NT_FIXDIR)/learn-fresh-$$v$$sd; \
	    run $$v$$sd-learn-bf16 $(LRN_OUT)/$$v/learn_check --prec bf16 $(NT_FIXDIR)/learn-fresh-$$v$$sd; \
	  done; \
	done; \
	[ $$fail = 0 ] && echo TRAIN-WIDTH-CHECK PASS || { echo TRAIN-WIDTH-CHECK FAIL; exit 1; }
