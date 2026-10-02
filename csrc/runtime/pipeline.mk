# The pipeline (lane/pipeline): the env pool joined to the device renderer
# (pipeline.h), the per-env view (view.h: play's frame, out/runtime/
# playview.so), the gate and the benchmark. The dev host only (Linux, CUDA); never
# part of native's `all`, clangcheck or the Mac build. Uses pool.mk's
# variables (included first by the Makefile).
#
#   make -C csrc/runtime pipeline        playview.so, libpipe.a, pipe_gate, pipe_bench
#   make -C csrc/runtime pipe-gate [N=16] [J=16] [STREAMS=2] [BATCH=] [GROUP=] [DEPTH=2]
#                                    [PRECS="a b"] [EPISODES=] [DRAW=1] [PLAY=DIR] [MESH=1|2] [MESHTAB=1] [LIGHT=1] [MESHCHECK=1]
#                                    [RPREC=exact|fast|fast:STAGE,...]  (the renderer's precision, engine/raster_prec.h)
#                                    [GEN=cuda|srv]  (raw generation on the device, pipeline.h gen_device;
#                                                 srv: through the generation server's clients' path, gensrv.h)
#                                    [NOWAIT=1]  (a step never waits for its generation round, pool.h gen_nowait)
#                                    [AHEAD=R]  (the generation prediction R chunk rings wider, pool.h gen_ahead)
#                                    [SIZE=WxH]  (the observation's size, 128x128 by default; with PLAY=,
#                                                 play's frames made at it: pipe_play.sh -s WxH)
#                                    [RD=R]  (the frames at render distance R, 2 to 16; 4 by default;
#                                             the rows keep the tapes' own)
#                                    [CONFIG=FILE]  (a config.yaml: its observation, render distance and
#                                                 device switches, every recording held to its world;
#                                                 CONFIG=configs/headline.yaml PRECS=headline-s1)
#                                          every row against test_snapshots' and every
#                                          frame the device drew against the C renderer's
#   make -C csrc/runtime pipe-refs [PRECS=...]   the gate's references (pool-refs)
#   make -C csrc/runtime pipe-bench ARGS=... [SIZE=WxH] [RD=R]   env-steps per second with observations
#
# playview.so is play.c built with -DPLAY_VIEW and the renderer's own files
# (the ones csrc/tests/globals.sh leaves out of the tick's no-globals rule:
# their statics are play's frame state), position independent with hidden
# symbols, so each env's private copy keeps its own statics; everything
# else it calls is the engine in the program, which links the archive whole
# and exports it (-rdynamic).
PL_RENDER := $(sort $(wildcard $(addprefix $(RT_NATIVE)/engine/,raster*.c render*.c texanim.c font.c pngread.c \
               gui_screens.c block_item_model.c item_color.c hud_live.c)))
PL_PVOBJ  := $(RT_OUT)/obj/pv/play.o $(patsubst $(RT_NATIVE)/engine/%.c,$(RT_OUT)/obj/pv/%.o,$(PL_RENDER))
PL_SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
PL_SDL_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)
PL_PVFLAGS := $(RT_CFLAGS) -fPIC -fvisibility=hidden -DPLAY_VIEW -DNW_ENV_TLS_MODEL='"initial-exec"'
PL_NVCC   ?= /usr/local/cuda/bin/nvcc
PL_CUDA   := $(RT_NOUT)/obj/cuda_render.o $(RT_NOUT)/obj/cuda_lighting.o $(RT_NOUT)/obj/cuda_worldgen.o
PL_LIBOBJ := $(RT_OUT)/obj/pipeline.o $(RT_OUT)/obj/view.o $(RT_OUT)/obj/devbusy.o $(RT_OUT)/obj/gensrv.o
# the program's link: the engine whole and exported, the device renderer
# (a host link: nvcc would reorder the archive around --whole-archive)
PL_CUDALIB := $(dir $(PL_NVCC))../lib64
PL_LINK   = -rdynamic $(RT_OUT)/libpipe.a $(RT_OUT)/libpool.a $(RT_DEVOBJ) \
            -Wl,--whole-archive $(RT_NLIB) -Wl,--no-whole-archive $(PL_CUDA) \
            -L$(PL_CUDALIB) -Wl,-rpath,$(PL_CUDALIB) -lcupti -lcudart -lcuda -lstdc++ -ldl -lz -lm -lpthread
# the gate's recordings
PL_LIST   := $(RT_DIR)/pipeline_gate.list
PL_CHAIN  := $(RT_DIR)/pipeline_gate_chain.list
# CHAIN=1 adds the seed-1 chain (minutes: kept out of the per-lane gate, 2026-09-30)
PRECS    ?= $(shell grep -hv '^\#' $(PL_LIST) $(if $(filter 1,$(CHAIN)),$(PL_CHAIN)) | awk 'NF {print $$1}')
STREAMS  ?= 2

all: pipeline

.PHONY: pipeline pipe-gate pipe-refs pipe-bench pipe-cuda

pipeline: $(RT_OUT)/playview.so $(RT_OUT)/libpipe.a $(RT_OUT)/pipe_gate $(RT_OUT)/pipe_bench

pipe-cuda:
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) $(PL_CUDA)

$(PL_CUDA): pipe-cuda

$(RT_OUT)/obj/pv/play.o: $(RT_NATIVE)/play/play.c $(RT_NATIVE)/play/clientents.c $(RT_NATIVE)/play/playview.h | pool-native
	@mkdir -p $(dir $@)
	$(RT_CC) $(PL_PVFLAGS) $(PL_SDL_CFLAGS) -MMD -MP -c -o $@ $<

$(RT_OUT)/obj/pv/%.o: $(RT_NATIVE)/engine/%.c | pool-native
	@mkdir -p $(dir $@)
	$(RT_CC) $(PL_PVFLAGS) -MMD -MP -c -o $@ $<

$(RT_OUT)/playview.so: $(PL_PVOBJ)
	$(RT_CC) -shared -o $@ $^ $(PL_SDL_LIBS) -lz -lm

$(RT_OUT)/obj/pipeline.o $(RT_OUT)/obj/view.o $(RT_OUT)/obj/gensrv.o: $(RT_OUT)/obj/%.o: $(RT_DIR)/%.c $(RT_DIR)/pipeline.h $(RT_DIR)/view.h $(RT_DIR)/gensrv.h \
  $(RT_DIR)/pool.h $(RT_NATIVE)/play/playview.h $(RT_NATIVE)/cuda/render/render.h | pool-native
	@mkdir -p $(RT_OUT)/obj
	$(RT_CC) $(RT_CFLAGS) -MMD -MP -c -o $@ $<

# pipe_bench --profile's device timeline (CUPTI's activity records)
$(RT_OUT)/obj/devbusy.o: $(RT_DIR)/devbusy.c $(RT_DIR)/devbusy.h | pool-native
	@mkdir -p $(RT_OUT)/obj
	$(RT_CC) $(RT_CFLAGS) -I$(dir $(PL_NVCC))../include -MMD -MP -c -o $@ $<

$(RT_OUT)/libpipe.a: $(PL_LIBOBJ)
	@rm -f $@
	ar rcs $@ $^

$(RT_OUT)/obj/pipe_gate.o $(RT_OUT)/obj/pipe_bench.o: $(RT_OUT)/obj/%.o: $(RT_DIR)/%.c $(RT_DIR)/pipeline.h $(RT_DIR)/view.h $(RT_DIR)/devbusy.h $(RT_DIR)/semcam.h \
  $(RT_DIR)/pool.h $(RT_DIR)/../tests/rowrec.h | pool-native
	@mkdir -p $(RT_OUT)/obj
	$(RT_CC) $(RT_CFLAGS) -DNETHERITE_DEV -MMD -MP -c -o $@ $<

$(RT_OUT)/pipe_gate $(RT_OUT)/pipe_bench: $(RT_OUT)/%: $(RT_OUT)/obj/%.o $(RT_OUT)/libpipe.a $(RT_OUT)/libpool.a $(RT_NLIB) $(RT_DEVOBJ) $(PL_CUDA) $(RT_OUT)/playview.so
	$(RT_CC) $(RT_LDFLAGS) -o $@ $< $(PL_LINK)

-include $(wildcard $(RT_OUT)/obj/pv/*.d)

pipe-refs:
	@$(MAKE) --no-print-directory pool-refs RECS="$(PRECS)"

pipe-gate: pipeline pipe-refs
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/pipe_gate $(if $(CONFIG),--config $(abspath $(CONFIG))) -n $(N) -j $(J) --ticks $(TICKS) \
	  --streams $(STREAMS) $(if $(BATCH),--batch $(BATCH)) $(if $(GROUP),--group $(GROUP)) $(if $(DEPTH),--depth $(DEPTH)) \
	  $(if $(EPISODES),--episodes $(EPISODES)) $(if $(DRAW),--draw) $(if $(PLAY),--play $(PLAY)) \
	  $(if $(filter 1,$(LIGHT)),--device-light) $(if $(MESH),--device-mesh $(MESH)) $(if $(filter 1,$(MESHTAB)),--mesh-tab) $(if $(filter 1,$(USUALTRIP)),--usual-trip) $(if $(filter 1,$(MESHCHECK)),--mesh-check) \
	  $(if $(RPREC),--render-prec $(RPREC)) $(if $(filter cuda,$(GEN)),--gen-device) $(if $(filter srv,$(GEN)),--gen-serve) $(if $(NOWAIT),--gen-nowait) $(if $(AHEAD),--gen-ahead $(AHEAD)) $(if $(SIZE),--size $(SIZE)) $(if $(RD),--rd $(RD)) $(PRECS)

pipe-bench: pipeline
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES=$(CUDA_DEV) $(RT_OUT)/pipe_bench $(if $(SIZE),--size $(SIZE)) $(if $(RD),--rd $(RD)) $(ARGS)
