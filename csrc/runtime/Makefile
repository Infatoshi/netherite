# csrc/runtime: the scheduler of the batched RL product (SPEC.md, the GPU
# section; D1, the hybrid, Elliot 2026-09-28): the C engine's tick (csrc/engine)
# stepped as a pool of worlds on the host's threads, the batches, streams and
# buffers that feed the device renderer (csrc/cuda/render), and the API the
# trainer calls. The dev host only (Linux, CUDA): never part of native's `all`,
# clangcheck or the Mac's build.
#
# Each part keeps its rules in its own fragment, so lanes never edit this file:
#   pool.mk      the env pool (lane/envpool)
#   pipeline.mk  batches, streams, buffering, the startup sweep (later)
#   rlbind.mk    the trainer's binding: the pipeline as a shared library (lane/rlbind)
#   moveact.mk   the action compiler's gate (lane/moveact)
#   startgate.mk the saved start's gate (lane/savestart)
#   semcam.mk    the semantic camera's gate (lane/semteach)
# A fragment adds its targets to `all` (all: target) and builds into $(OUT).
ROOT      := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
RUNTIME   := $(ROOT)/csrc/runtime
NATIVE    := $(ROOT)/csrc
OUT       ?= $(ROOT)/out/runtime
# objects from before the native/ to csrc/ rename name the old paths: they go once (csrc/Makefile)
$(shell [ -f $(OUT)/.layout-csrc ] || { find $(OUT) \( -name '*.d' -o -name '*.o' -o -name '*.a' -o -name '*.so' \) -delete 2>/dev/null; mkdir -p $(OUT); touch $(OUT)/.layout-csrc; })
CUDA_DEV  ?= 0
CUDA_ARCH ?= $(eval CUDA_ARCH := sm_$(or $(shell nvidia-smi --query-gpu=compute_cap --format=csv,noheader -i $(CUDA_DEV) 2>/dev/null | tr -d .),86))$(CUDA_ARCH)

all:

-include $(RUNTIME)/pool.mk
-include $(RUNTIME)/pipeline.mk
-include $(RUNTIME)/rlbind.mk
-include $(RUNTIME)/moveact.mk
-include $(RUNTIME)/startgate.mk
-include $(RUNTIME)/semcam.mk

.PHONY: all
