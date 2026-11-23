# The judges' device frames (play/framedev.h): out/native/cuda/libframedev.so,
# the device renderer (cuda/render) behind the ABI play loads at run time.
# play draws every judged shot's world frame with it when it is there (the
# frame judges, rawjudge's frames), every 16th again with the C renderer to
# compare byte for byte; make test builds it wherever nvcc is (the dev host), and
# nothing else needs it (the Mac's play draws in C).
#   make -C csrc gpu-render-judge          the plugin
#   make -C csrc gpu-render-judge-check    the gate: every frame judge's shots at
#                                      JUDGE_SIZE (854x480) and render
#                                      distance JUDGE_RD (8), the frame and the
#                                      frame without the drops, each drawn by
#                                      both renderers, colour and depth byte
#                                      for byte (cuda/render/judge_gate.sh)
FRAMEDEV_SO := $(OUT)/cuda/libframedev.so
JUDGE_SIZE  ?= 854x480
JUDGE_RD    ?= 8
JUDGE_ARGS  ?=
# the renderer's precision (engine/raster_prec.h): exact, fast or fast:STAGE,...
JUDGE_PREC  ?= exact

.PHONY: gpu-render-judge gpu-render-judge-check
gpu-render-judge: $(FRAMEDEV_SO)
# framedev.cu links render.mk's objects, the host side's (host.cu) with
# the device units: position independent and hidden (the plugin exports
# framedev_api alone), device-linked by nvcc -shared; nomesher.c stands in
# for the device mesher the plugin does not run (render_no_mesher)
# (judge.mk is read before render.mk: its names written out)
JUDGE_ROBJ := $(patsubst %,$(OUT)/obj/cuda_render_%.o,host geom_quads geom_equads geom_gen geom_rec raster sort)
$(OUT)/obj/cuda_render_framedev.o: cuda/render/framedev.cu play/framedev.h cuda/render/dev.cuh cuda/render/render.h \
		engine/raster_obs.h engine/meshfeed.h cuda/meshing/meshing.h $(NVSTAMP)
	@mkdir -p $(dir $@)
	$(NVJOB) $(NVDEV) -rdc=true -Xcompiler -fPIC -Xcompiler -fvisibility=hidden -c -o $@ $<
$(OUT)/obj/cuda_render_nomesher.o: cuda/render/nomesher.c cuda/meshing/meshing.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -c -o $@ $<
$(OUT)/obj/raster_obs_pic.o: engine/raster_obs.c engine/raster_obs.h engine/raster_entity_quad.h
	@mkdir -p $(dir $@)
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fPIC -fvisibility=hidden -c -o $@ $<
$(FRAMEDEV_SO): $(OUT)/obj/cuda_render_framedev.o $(JUDGE_ROBJ) $(OUT)/obj/cuda_render_nomesher.o $(OUT)/obj/raster_obs_pic.o
	@mkdir -p $(dir $@)
	$(NVCC) -shared -rdc=true -arch=$(CUDA_ARCH) -Xcompiler -fPIC -o $@.tmp $^ -lz -lcuda && mv $@.tmp $@
gpu-render-judge-check: $(FRAMEDEV_SO) $(OUT)/play $(OUT)/play-dev
	CUDA_VISIBLE_DEVICES=$(CUDA_DEV) bash cuda/render/judge_gate.sh --size $(JUDGE_SIZE) --rd $(JUDGE_RD) --prec $(JUDGE_PREC) $(JUDGE_ARGS)

# the suite's frame judges draw on the device where it can be built
ifneq ($(wildcard $(NVCC)),)
test: $(FRAMEDEV_SO)
endif
