# Independent, small observation build. No simulation CUDA compilation.
NVCC ?= $(CUDA_HOME)/bin/nvcc
CC ?= cc
CXX ?= c++
CUDA_HOME ?= /usr/local/cuda
BLAZE_SM ?= sm_86
OUT ?= ../../out/verify/architecture
.PHONY: all
all: $(OUT)/env_cuda_obs.so $(OUT)/measure $(OUT)/prepare $(OUT)/driver_dispatch.so $(OUT)/action_rows

$(OUT)/action_rows: action_rows.c
	mkdir -p $(OUT)
	$(CC) -O2 -std=c11 -Wall -Wextra $< -o $@

$(OUT)/hold_jobs: hold_jobs.c
	mkdir -p $(OUT)
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror $< -o $@
$(OUT)/env_cuda_obs.so: env_cuda_obs.cu env_cuda_obs.h ../../blaze/core/obs_camera.h ../../blaze/core/mc_math.h ../../blaze/core/mc.h
	mkdir -p $(OUT)
	$(NVCC) -O3 -std=c++14 -arch=$(BLAZE_SM) --fmad=false -Xcompiler=-fPIC,-ffp-contract=off -shared env_cuda_obs.cu -o $@

NN_OUT := ../../out/blaze/nn/cuda
NN_OBJS := $(addprefix $(NN_OUT)/,nn.o cpu.o fixture.o cuda.o cuda_ws.o cuda_conv_graph.o cuda_lt_gemm.o cuda_layout.o)
$(OUT)/measure: measure.c env_cuda_obs.h ../../blaze/rl/obs_pack.h ../../blaze/rl/env_cuda_stage.c $(wildcard ../../blaze/env/*.h ../../blaze/core/*.h) $(NN_OBJS)
	mkdir -p $(OUT)
	$(CC) -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Wno-unused-function -fopenmp -I../../blaze/core -I../../blaze/env -I../../blaze/nn -I../../blaze/rl -I$(CUDA_HOME)/include measure.c ../../blaze/rl/env_cuda_stage.c $(NN_OBJS) -L$(CUDA_HOME)/lib64 -lcudnn -lcublas -lcublasLt -lcudart -lstdc++ -lm -ldl -o $@

$(NN_OBJS):
	$(MAKE) -C ../../blaze/nn cuda BLAZE_SM=$(BLAZE_SM) CUDA_HOME=$(CUDA_HOME)

$(OUT)/prepare: prepare.c ../../blaze/env/blaze_snapshot.c ../../blaze/env/blaze_snapshot.h
	mkdir -p $(OUT)
	$(CC) -O2 -std=c11 -ffp-contract=off prepare.c ../../blaze/env/blaze_snapshot.c -lm -o $@

$(OUT)/driver_dispatch.so: driver_dispatch.cpp $(wildcard ../../blaze/env/*.h ../../blaze/core/*.h)
	mkdir -p $(OUT)
	$(CXX) -O2 -std=c++17 -fPIC -shared -ffp-contract=off -I../../blaze/core -I../../blaze/env -I$(CUDA_HOME)/include $< -L$(CUDA_HOME)/lib64 -lcuda -lcudart -o $@

.PHONY: fine
fine: $(OUT)/fine/pre_actions.cubin $(OUT)/fine/pure_player.cubin $(OUT)/fine/after_player.cubin
$(OUT)/fine/%.cubin: %.cu $(wildcard ../../blaze/env/*.h ../../blaze/core/*.h)
	@test "$$(sha256sum ../../blaze/env/blaze_core.h | cut -d' ' -f1)" = 925f5018c71b36e128d61c12fefcd5a2b6c84272295364d3eda3838198b56e21 || { echo 'copied phase source requires review against changed core' >&2; exit 1; }
	mkdir -p $(OUT)/fine
	$(NVCC) -O2 --fmad=false -arch=$(BLAZE_SM) -I../../blaze/core -I../../blaze/env -Xptxas -v -cubin $< -o $@
$(OUT)/test_cuda_obs: test_cuda_obs.c env_cuda_obs.h $(OUT)/env_cuda_obs.so
	$(CC) -O3 -ffp-contract=off test_cuda_obs.c -L$(OUT) -Wl,-rpath,$(abspath $(OUT)) -l:env_cuda_obs.so -lm -o $@
.PHONY: check
check: $(OUT)/test_cuda_obs
	$(OUT)/test_cuda_obs
