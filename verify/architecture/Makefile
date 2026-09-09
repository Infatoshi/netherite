# Independent, small observation build. No simulation CUDA compilation.
NVCC ?= nvcc
CC ?= cc
CUDA_HOME ?= /usr/local/cuda
BLAZE_SM ?= sm_86
OUT ?= ../../out/verify/architecture
.PHONY: all
all: $(OUT)/env_cuda_obs.so $(OUT)/measure
$(OUT)/env_cuda_obs.so: env_cuda_obs.cu env_cuda_obs.h ../../blaze/core/obs_camera.h ../../blaze/core/mc_math.h ../../blaze/core/mc.h
	mkdir -p $(OUT)
	$(NVCC) -O3 -std=c++14 -arch=$(BLAZE_SM) --fmad=false -Xcompiler=-fPIC,-ffp-contract=off -shared env_cuda_obs.cu -o $@

NN_OUT := ../../out/blaze/nn/cuda
NN_OBJS := $(addprefix $(NN_OUT)/,nn.o cpu.o fixture.o cuda.o cuda_ws.o cuda_conv_graph.o cuda_lt_gemm.o cuda_layout.o)
$(OUT)/measure: measure.c env_cuda_obs.h ../../blaze/rl/obs_pack.h ../../blaze/rl/env_cuda_stage.c $(NN_OBJS)
	mkdir -p $(OUT)
	$(CC) -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Wno-unused-function -fopenmp -I../../blaze/nn -I../../blaze/rl -I$(CUDA_HOME)/include measure.c ../../blaze/rl/env_cuda_stage.c $(NN_OBJS) -L$(CUDA_HOME)/lib64 -lcudnn -lcublas -lcublasLt -lcudart -lstdc++ -lm -ldl -o $@

$(NN_OBJS):
	$(MAKE) -C ../../blaze/nn cuda BLAZE_SM=$(BLAZE_SM) CUDA_HOME=$(CUDA_HOME)
$(OUT)/test_cuda_obs: test_cuda_obs.c env_cuda_obs.h $(OUT)/env_cuda_obs.so
	$(CC) -O3 -ffp-contract=off test_cuda_obs.c -L$(OUT) -Wl,-rpath,$(abspath $(OUT)) -l:env_cuda_obs.so -lm -o $@
.PHONY: check
check: $(OUT)/test_cuda_obs
	$(OUT)/test_cuda_obs
