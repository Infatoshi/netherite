# The saved start's gate (lane/savestart): start_gate.c, pipe_start_from_env
# held to the env it was saved from. Uses pool.mk's and pipeline.mk's
# variables (included first by the Makefile).
#
#   make -C csrc/runtime rl-start-gate [CUDA_DEV=D]
#          the speedrun config at 1 and 4 ticks a step (device meshing, the
#          HUD; its start under the seeded walk, and its own recording
#          sr-speedrun-s1 from its tape), then recordings saved mid-episode
#          at the default observation with the C frames drawn too: a chest
#          window open, the game-over screen with the drops in flight, the
#          crafting table with its grid filled, a trade, a fight, the
#          Nether portal both ways and a day in the Nether, the End with the
#          dragon. Every case's saved starts (and a start saved from one)
#          give the saving env's rows, results, frames and end state. Last,
#          the same through the binding (libnwrl.so from mcsr-speedrun's
#          mcsr/nwrl_save_gate.py: every result record, the reward's engine
#          state included, and every frame torch reads).
#   make -C csrc/runtime rl-start-gate SG_SR1="..." SG_SR4="..." SG_RECS="..."   other cases (NAME@S+M, walk:SEED@S+M)

SG_CONFIG ?= $(RT_ROOT)/configs/speedrun.yaml
SG_SR1    ?= walk:1@300+120 sr-speedrun-s1@400+160
SG_SR4    ?= walk:2@120+60 sr-speedrun-s1@100+40
SG_RECS   ?= chest-chain-s1@20+20 death-drops-s1@29+40 cat-recipes-s1@42+30 cov-tradelist-s1@3+30 fight-zombie-s1@60+40 \
             nether-trip-s1@23+30 nether-trip-s1@100+40 nether-trip-s1@197+20 end-dragon-s1@50+60

.PHONY: rl-start-gate

pipeline: $(RT_OUT)/start_gate

$(RT_OUT)/obj/start_gate.o: $(RT_DIR)/start_gate.c $(RT_DIR)/pipeline.h $(RT_DIR)/view.h $(RT_DIR)/pool.h \
  $(RT_DIR)/../tests/rowrec.h | pool-native
	@mkdir -p $(RT_OUT)/obj
	$(RT_CC) $(RT_CFLAGS) -DNETHERITE_DEV -MMD -MP -c -o $@ $<

$(RT_OUT)/start_gate: $(RT_OUT)/obj/start_gate.o $(RT_OUT)/libpipe.a $(RT_OUT)/libpool.a $(RT_NLIB) $(RT_DEVOBJ) $(PL_CUDA) $(RT_OUT)/playview.so
	$(RT_CC) $(RT_LDFLAGS) -o $@ $< $(PL_LINK)

# (the views' "play: into dimension" lines left out of the output)
SG_RUN = cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/start_gate $(1) 2>$(RT_OUT)/start_gate.err; rc=$$?; \
         grep -v '^play: into' $(RT_OUT)/start_gate.err >&2; exit $$rc

rl-start-gate: pipeline rlbind
	$(call SG_RUN,--config $(SG_CONFIG) $(SG_SR1))
	$(call SG_RUN,--config $(SG_CONFIG) --ticks 4 $(SG_SR4))
	$(call SG_RUN,--draw $(SG_RECS))
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) NETHERITE=$(RT_ROOT) PYTHONPATH=$(MCSR) UV_CACHE_DIR=$(HOME)/.cache/uv \
	  uv run --no-sync --frozen --project $(MCSR) python -m mcsr.nwrl_save_gate --config $(SG_CONFIG) --steps 200 --more 100 \
	  2>$(RT_OUT)/start_gate.err; rc=$$?; grep -v '^play: into' $(RT_OUT)/start_gate.err >&2; exit $$rc
