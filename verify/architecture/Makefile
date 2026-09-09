# Independent, small observation build. No simulation CUDA compilation.
NVCC ?= nvcc
BLAZE_SM ?= sm_86
OUT ?= ../../out/verify/architecture
.PHONY: all
all: $(OUT)/env_cuda_obs.so
$(OUT)/env_cuda_obs.so: env_cuda_obs.cu env_cuda_obs.h ../../blaze/core/obs_camera.h ../../blaze/core/mc_math.h ../../blaze/core/mc.h
	mkdir -p $(OUT)
	$(NVCC) -O3 -std=c++14 -arch=$(BLAZE_SM) --fmad=false -Xcompiler=-fPIC,-ffp-contract=off -shared env_cuda_obs.cu -o $@
$(OUT)/test_cuda_obs: test_cuda_obs.c env_cuda_obs.h $(OUT)/env_cuda_obs.so
	$(CC) -O3 -ffp-contract=off test_cuda_obs.c -L$(OUT) -Wl,-rpath,$(abspath $(OUT)) -l:env_cuda_obs.so -lm -o $@
.PHONY: check
check: $(OUT)/test_cuda_obs
	$(OUT)/test_cuda_obs
