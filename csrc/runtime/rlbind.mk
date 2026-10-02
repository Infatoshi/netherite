# The trainer's binding (lane/rlbind): rlbind.[ch], the pipeline as a shared
# library a Python trainer loads in its own process. Linux and CUDA only;
# never part of native's `all`, clangcheck or the Mac build. Uses pool.mk's
# and pipeline.mk's variables (included first by the Makefile).
#
#   make -C csrc/runtime rlbind      out/runtime/libnwrl.so (and playview.so beside it)
#   make -C csrc/runtime rl-gate [CONFIG=configs/speedrun.yaml] [N=16] [WARM=20] [STEPS=200] [MCSR=~/dev/mcsr-speedrun]
#          the binding's gate: mcsr-speedrun's Python binding (mcsr/nwrl.py) runs pipe_bench's
#          corpus through libnwrl.so (its seeded walk, drawn the same way) and its trajectory and
#          frame digests (the frames read by torch from the device buffer, DLPack) must equal
#          pipe_bench --frame-digest's on the same corpus
#   make -C csrc/runtime rl-gui-gate [GUIRECS="..."]   (lane/invact, rlgui.h)
#          the gui ops through the binding: each recording's script (agent acts and gui ops, oracle/tests/NAME.jsonl)
#          stepped through libnwrl.so (mcsr/nwrl_gui_gate.py), every tick's row record equal to test_snapshots'
#          (pool-refs), the role table checked against every open screen's slots
#
# The library is the engine, the pool and the pipeline built position
# independent (out/native/pic, out/runtime/pic: the same sources, -fPIC, and
# the thread-local env pointer at the initial-exec model a loaded library
# may use: its thread-locals, about 200 bytes, fit the static TLS glibc
# keeps for loaded libraries), the device objects of pipeline.mk, and CUDA's
# shared runtime (the static one's thread-locals are page aligned, which a
# loaded library's static TLS cannot be). One libcudart.so.13 serves the
# process: load the library before torch (mcsr/nwrl.py does at import), so
# torch runs on this toolkit's runtime, which is newer than its own.
RL_PICN   := $(RT_ROOT)/out/native/pic
RL_PICR   := $(RT_OUT)/pic
RL_PICDEF := -fPIC -DNW_ENV_TLS_MODEL='"initial-exec"'
# native's own CFLAGS default and the two (quoted for the sub-make's shell)
RL_NCFLAGS := -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Werror -fPIC -DNW_ENV_TLS_MODEL=\"initial-exec\"
RL_SRC    := pool.c feed.c agent.c pipeline.c view.c gensrv.c rlbind.c rlgui.c moveact.c rlmove.c rlloop.c rlact.c semcam.c
RL_OBJ    := $(patsubst %.c,$(RL_PICR)/%.o,$(RL_SRC))
RL_LIB    := $(RT_OUT)/libnwrl.so
# pipeline.mk's device objects, their host code position independent (nvcc
# takes -fPIC from its environment, so ccache, which does not hash it, is off)
RL_CUDA   := $(RL_PICN)/obj/cuda_render.o $(RL_PICN)/obj/cuda_lighting.o $(RL_PICN)/obj/cuda_worldgen.o
CONFIG_RL ?= $(RT_ROOT)/configs/speedrun.yaml
MCSR      ?= $(HOME)/dev/mcsr-speedrun
WARM      ?= 20
STEPS     ?= 200

.PHONY: rlbind rl-pic rl-cuda rl-gate rl-gui-gate rl-act-gate

# not in `all`: the PIC device objects build without ccache (minutes); make rlbind

rlbind: $(RL_LIB) $(RT_OUT)/playview.so

rl-pic:
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) OUT=$(RL_PICN) CFLAGS='$(RL_NCFLAGS)' $(RL_PICN)/libnetherite.a $(RL_PICN)/obj/dev.o

$(RL_PICN)/libnetherite.a $(RL_PICN)/obj/dev.o: rl-pic

rl-cuda:
	@NVCC_APPEND_FLAGS="-Xcompiler -fPIC" $(MAKE) --no-print-directory -s -C $(RT_NATIVE) OUT=$(RL_PICN) CCACHE= CFLAGS='$(RL_NCFLAGS)' $(RL_CUDA)

$(RL_CUDA): rl-cuda

$(RL_PICR)/%.o: $(RT_DIR)/%.c $(RT_DIR)/pipeline.h $(RT_DIR)/pool.h $(RT_DIR)/view.h $(RT_DIR)/rlbind.h $(RT_DIR)/rlgui.h $(RT_DIR)/rlgui_int.h $(RT_DIR)/moveact.h $(RT_DIR)/rlmove.h \
  $(RT_DIR)/rlloop.h $(RT_DIR)/rlact.h $(RT_DIR)/rlact_int.h $(RT_DIR)/semcam.h | pool-native
	@mkdir -p $(RL_PICR)
	cd $(RT_DIR) && $(RT_CCC) $(RT_CFLAGS) $(RL_PICDEF) -MMD -MP -c -o $@ $(notdir $<)

# the dev ops (engine/dev.c) too, for nwrl_make_ex's NWRL_DEV (lane/invact: the gui gate's dev recordings and the
# scenario starts; nwrl_make refuses a dev start as the product does)
$(RL_LIB): $(RL_OBJ) $(RL_PICN)/libnetherite.a $(RL_PICN)/obj/dev.o $(RL_CUDA)
	$(RT_CC) -shared $(RT_LDFLAGS) -Wl,-Bsymbolic-functions -o $@ $(RL_OBJ) $(RL_PICN)/obj/dev.o -Wl,--whole-archive $(RL_PICN)/libnetherite.a \
	  -Wl,--no-whole-archive $(RL_CUDA) -L$(PL_CUDALIB) -Wl,-rpath,$(PL_CUDALIB) -lcudart -lcuda -lstdc++ -ldl -lrt -lz -lm -lpthread

-include $(wildcard $(RL_PICR)/*.d)

rl-gate: rlbind pipeline
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/pipe_bench --config $(CONFIG_RL) --ns $(N) --warm $(WARM) --steps $(STEPS) \
	  --frame-digest > $(RT_OUT)/rl-gate.bench.txt && grep -E "digest|FAIL" $(RT_OUT)/rl-gate.bench.txt
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) NETHERITE=$(RT_ROOT) PYTHONPATH=$(MCSR) UV_CACHE_DIR=$(HOME)/.cache/uv uv run --no-sync --frozen --project $(MCSR) \
	  python -m mcsr.nwrl_gate --lib $(RL_LIB) --config $(CONFIG_RL) --n $(N) --warm $(WARM) --steps $(STEPS) --bench $(RT_OUT)/rl-gate.bench.txt

# the gui gate's recordings: crafting (2x2 and 3x3, every recipe), chests, furnaces, drops and drags (lane/invact)
GUIRECS ?= cat-recipes-s1 craft-spam-s1 chest-toggle-s1 cov-containers-s1 cat-clicks-s1 cont-wbfull-s1 furnace-break-s1 \
  chest-break-s1 cat-charcoal-s1 achieve-s1 container-break-s1 containers2-s42
rl-gui-gate: rlbind
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(GUIRECS)"
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) NETHERITE=$(RT_ROOT) PYTHONPATH=$(MCSR) UV_CACHE_DIR=$(HOME)/.cache/uv uv run --no-sync --frozen --project $(MCSR) \
	  python -m mcsr.nwrl_gui_gate --lib $(RL_LIB) --config $(RT_ROOT)/config.yaml --refdir $(RT_OUT)/ref --work $(RT_OUT)/guigate $(GUIRECS)

# the action compiler's levers (lane/guiact, rlact.h): each lever script (guiact/NAME.jsonl: the compiler's ops and
# its switches) stepped through libnwrl.so; the ops it gives each tick equal to the recording's expanded script
# (oracle/tests/NAME.jsonl, recorded by Java) and every tick's row record equal to test_snapshots' (mcsr/nwrl_act_gate.py)
ACTRECS ?= guiact-move-s1 guiact-item-s1 guiact-recipe-s1 guiact-mask-s1
rl-act-gate: rlbind
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) pool-refs RECS="$(ACTRECS)"
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) NETHERITE=$(RT_ROOT) PYTHONPATH=$(MCSR) UV_CACHE_DIR=$(HOME)/.cache/uv uv run --no-sync --frozen --project $(MCSR) \
	  python -m mcsr.nwrl_act_gate --lib $(RL_LIB) --config $(RT_ROOT)/configs/speedrun.yaml --refdir $(RT_OUT)/ref --work $(RT_OUT)/actgate $(ACTRECS)
