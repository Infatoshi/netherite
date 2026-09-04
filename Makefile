# Root command surface. Forwards to owners. See SPEC.md.
# Do not invent verify/train here until those binaries exist.

UNAME_S := $(shell uname -s)
CANON_TAPE := verify/tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl

.PHONY: all assets test play clean magma-game magma-metal

all: magma-game
ifeq ($(UNAME_S),Darwin)
all: magma-metal
endif

magma-game:
	$(MAKE) -C magma game

magma-metal:
	$(MAKE) -C magma game-metal

assets:
	$(MAKE) -C magma assets

# Short native unit tests only. Must finish under 180s.
test:
	$(MAKE) -C magma test
	$(MAKE) -C magma test-config
	$(MAKE) -C magma test-asset-build
	$(MAKE) -C blaze/nn test
ifeq ($(UNAME_S),Darwin)
	$(MAKE) -C blaze/nn test-metal
endif
	$(MAKE) -C blaze/rl test-recipe
	$(MAKE) -C blaze/rl test-world
	$(MAKE) -C blaze/rl test-capture
	$(MAKE) -C verify env_knob_gate-selftest
	$(MAKE) -C verify public-export-selftest
	$(MAKE) -C verify test-replay-contract
	$(MAKE) -C verify tape-info TAPE=$(CANON_TAPE)

play:
	$(MAKE) -C magma game
	@echo "binary: magma/magma_game"

clean:
	$(MAKE) -C magma clean
	$(MAKE) -C verify clean
	$(MAKE) -C blaze/nn clean
