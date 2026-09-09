# Invoke from repository root: make -f verify/architecture/pipeline.mk
CUDA_HOME ?= /usr/local/cuda
CC ?= cc
NN_OUT ?= out/blaze/nn/cuda
OUT ?= out/verify/architecture
NN_OBJS := $(addprefix $(NN_OUT)/,nn.o cpu.o fixture.o cuda.o cuda_ws.o cuda_conv_graph.o cuda_lt_gemm.o cuda_layout.o)
$(OUT)/pipeline_probe: verify/architecture/pipeline_probe.c verify/architecture/env_cuda_obs.h $(NN_OBJS)
	mkdir -p $(OUT)
	$(CC) -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Wno-unused-function -fopenmp -pthread -Iblaze/nn -Iblaze/rl -I$(CUDA_HOME)/include $< $(NN_OBJS) -L$(CUDA_HOME)/lib64 -lcudnn -lcublas -lcublasLt -lcudart -lstdc++ -lm -ldl -o $@
