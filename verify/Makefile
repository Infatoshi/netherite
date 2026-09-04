# verify gates. Binaries land under repo-root out/verify/.
CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror
ROOT    := $(abspath ..)
OUT     := $(ROOT)/out/verify
CANON   := $(ROOT)/verify/tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl

.PHONY: all env_knob_gate env_knob_gate-selftest env_knob_gate-check \
	public_export public-export public-export-selftest tape-info \
	replay test-replay test-replay-contract test-dimensions clean

all: env_knob_gate public_export tape-info replay

$(OUT):
	mkdir -p $@

$(OUT)/env_knob_gate: env_knob_gate.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $<

env_knob_gate: $(OUT)/env_knob_gate

env_knob_gate-selftest: $(OUT)/env_knob_gate
	$(OUT)/env_knob_gate --selftest

env_knob_gate-check: $(OUT)/env_knob_gate
	$(OUT)/env_knob_gate $(ROOT)

$(OUT)/public_export: public_export.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $<

public_export: $(OUT)/public_export

public-export: $(OUT)/public_export
ifeq ($(DEST),)
	$(error usage: make -C verify public-export DEST=/absolute/path)
endif
	$(OUT)/public_export --repo '$(ROOT)' --dest '$(DEST)'

public-export-selftest: $(OUT)/public_export
	'$(OUT)/public_export' --selftest

$(OUT)/tape_info: tape/tape_info.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $<

tape-info: $(OUT)/tape_info
ifeq ($(TAPE),)
	$(error usage: make -C verify tape-info TAPE=path/to/tape.jsonl)
endif
	@if [ -f '$(TAPE)' ]; then \
		$(OUT)/tape_info '$(TAPE)'; \
	elif [ -f '$(ROOT)/$(TAPE)' ]; then \
		$(OUT)/tape_info '$(ROOT)/$(TAPE)'; \
	else \
		echo "tape not found: $(TAPE)" >&2; exit 1; \
	fi

$(OUT)/replay: tape/replay.c tape/mca.c tape/mca.h | $(OUT)
	$(CC) $(CFLAGS) -O2 -DREPO_ROOT='"$(ROOT)"' -o $@ tape/replay.c tape/mca.c -lz -lm

replay: $(OUT)/replay

$(OUT)/test_replay: tape/test_replay.c | $(OUT)
	$(CC) $(CFLAGS) -O2 -o $@ $<

test-replay-contract: $(OUT)/replay $(OUT)/test_replay
	$(OUT)/test_replay $(OUT)/replay

# MAGMA is the magma_game binary. Default: this worktree, then fail.
MAGMA ?= $(ROOT)/magma/magma_game

test-replay: $(OUT)/replay $(OUT)/tape_info
	$(OUT)/tape_info '$(CANON)'
	@if [ ! -x '$(MAGMA)' ]; then \
		echo "magma_game missing: $(MAGMA) (build with make -C magma game)" >&2; \
		exit 1; \
	fi
	$(OUT)/replay --tape '$(CANON)' --ticks 8 --out '$(OUT)/replay_test' \
		--magma '$(MAGMA)'

DIMENSION_SO ?= $(ROOT)/blaze/env/blaze_cpu.so
DIMENSION_SNAPSHOT ?= $(ROOT)/verify/fixtures/port/s10_t0_r64_portals.bsnp
DL_LIB := $(if $(filter Darwin,$(shell uname -s)),,-ldl)

$(OUT)/test_dimension_lifecycle: $(ROOT)/blaze/env/test_dimension_lifecycle.c \
        $(ROOT)/blaze/env/blaze_abi.h $(ROOT)/blaze/core/port_parity.h | $(OUT)
	$(CC) $(CFLAGS) -O2 -I$(ROOT)/blaze/env -I$(ROOT)/blaze/core -o $@ $< $(DL_LIB)

# The selected library is explicit so this same ABI test runs on CPU/CUDA.
test-dimensions: $(OUT)/test_dimension_lifecycle
	$(OUT)/test_dimension_lifecycle '$(DIMENSION_SO)' '$(DIMENSION_SNAPSHOT)'

clean:
	rm -f $(OUT)/env_knob_gate $(OUT)/public_export $(OUT)/tape_info \
		$(OUT)/replay $(OUT)/test_replay $(OUT)/test_dimension_lifecycle
