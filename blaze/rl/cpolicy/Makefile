# Native cpolicy: cuDNN FP32 convs + cuBLAS FC/heads + sample kernels.
# Build for the host from BLAZE_SM (Anvil: BLAZE_SM=120).

CUDA_HOME ?= /usr/local/cuda
NVCC ?= $(CUDA_HOME)/bin/nvcc
BLAZE_SM ?= 120

# Accept 120 or sm_120.
SM_NUM := $(patsubst sm_%,%,$(BLAZE_SM))
NVARCH := -arch=sm_$(SM_NUM)

INCLUDES := -I. -I$(CUDA_HOME)/include
LDFLAGS  := -L$(CUDA_HOME)/lib64 -L$(CUDA_HOME)/targets/x86_64-linux/lib
LIBS     := -lcudnn -lcublas -lcudart

NVCCFLAGS := -O3 $(NVARCH) $(INCLUDES) -std=c++17

.PHONY: all clean test

all: test_cpolicy

test_cpolicy: test_cpolicy.cu cpolicy_fwd.cu cpolicy_fwd.h
	$(NVCC) $(NVCCFLAGS) test_cpolicy.cu cpolicy_fwd.cu -o $@ $(LDFLAGS) $(LIBS)

test: test_cpolicy
	./test_cpolicy --device 0

clean:
	rm -f test_cpolicy cpolicy_fwd.so *.o
