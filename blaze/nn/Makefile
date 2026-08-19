# Blaze native policy. Build output is disposable and stays under root out/.
CC ?= cc
METAL_CC ?= clang
CUDA_HOME ?= /usr/local/cuda
NVCC ?= $(CUDA_HOME)/bin/nvcc
BLAZE_SM ?= sm_120

OUT := ../../out/blaze/nn
CPU_OUT := $(OUT)/cpu
SAN_OUT := $(OUT)/sanitize
METAL_OUT := $(OUT)/metal
CUDA_OUT := $(OUT)/cuda

CPPFLAGS := -I.
CFLAGS := -std=c11 -O2 -ffp-contract=off -Wall -Wextra \
	-Werror=implicit-function-declaration
SAN_CFLAGS := -std=c11 -O1 -g -ffp-contract=off -Wall -Wextra \
	-Werror=implicit-function-declaration -fsanitize=address,undefined \
	-fno-omit-frame-pointer
METAL_CFLAGS := $(CFLAGS) -DNN_HAVE_METAL=1
METAL_OBJCFLAGS := -fobjc-arc -O2 -ffp-contract=off -Wall -Wextra \
	-DNN_HAVE_METAL=1
METAL_LIBS := -lm -framework Metal -framework Foundation \
	-framework MetalPerformanceShadersGraph -framework MetalPerformanceShaders

SM_NUM := $(patsubst sm_%,%,$(BLAZE_SM))
CUDA_CPPFLAGS := $(CPPFLAGS) -I$(CUDA_HOME)/include -DNN_HAVE_CUDA=1
NVCCFLAGS := -O2 -arch=sm_$(SM_NUM) $(CUDA_CPPFLAGS) -std=c++17 \
	--fmad=false -Xcompiler -ffp-contract=off
CUDA_LIBS := -L$(CUDA_HOME)/lib64 \
	-L$(CUDA_HOME)/targets/x86_64-linux/lib -lcudnn -lcublas -lcudart -lm

CPU_OBJS := $(CPU_OUT)/nn.o $(CPU_OUT)/cpu.o $(CPU_OUT)/fixture.o
METAL_OBJS := $(METAL_OUT)/nn.o $(METAL_OUT)/cpu.o \
	$(METAL_OUT)/fixture.o $(METAL_OUT)/metal.o
CUDA_OBJS := $(CUDA_OUT)/nn.o $(CUDA_OUT)/cpu.o \
	$(CUDA_OUT)/fixture.o $(CUDA_OUT)/cuda.o

CPU_BIN := $(CPU_OUT)/test_cpu
SAN_BIN := $(SAN_OUT)/test_cpu
METAL_BIN := $(METAL_OUT)/test_metal
CUDA_BIN := $(CUDA_OUT)/test_cuda

.PHONY: all cpu metal cuda test test-sanitize test-metal test-cuda clean

all: cpu
cpu: $(CPU_BIN)
metal: $(METAL_BIN)
cuda: $(CUDA_BIN)

$(CPU_OUT) $(SAN_OUT) $(METAL_OUT) $(CUDA_OUT):
	mkdir -p $@

$(CPU_OUT)/nn.o: nn.c nn.h cpu.h | $(CPU_OUT)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CPU_OUT)/cpu.o: cpu.c cpu.h nn.h model.h fixture.h | $(CPU_OUT)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CPU_OUT)/fixture.o: fixture.c fixture.h model.h nn.h | $(CPU_OUT)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CPU_BIN): test_cpu.c $(CPU_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ test_cpu.c $(CPU_OBJS) -lm

$(SAN_BIN): test_cpu.c nn.c cpu.c fixture.c nn.h cpu.h model.h fixture.h | $(SAN_OUT)
	$(CC) $(SAN_CFLAGS) $(CPPFLAGS) -o $@ test_cpu.c nn.c cpu.c fixture.c \
		-fsanitize=address,undefined -lm

$(METAL_OUT)/nn.o: nn.c nn.h cpu.h metal.h | $(METAL_OUT)
	$(METAL_CC) $(METAL_CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(METAL_OUT)/cpu.o: cpu.c cpu.h nn.h model.h fixture.h | $(METAL_OUT)
	$(METAL_CC) $(METAL_CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(METAL_OUT)/fixture.o: fixture.c fixture.h model.h nn.h | $(METAL_OUT)
	$(METAL_CC) $(METAL_CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(METAL_OUT)/metal.o: metal.m metal.h nn.h model.h fixture.h | $(METAL_OUT)
	$(METAL_CC) $(METAL_OBJCFLAGS) $(CPPFLAGS) -c -o $@ $<

$(METAL_BIN): test_metal.m $(METAL_OBJS)
	$(METAL_CC) $(METAL_OBJCFLAGS) $(CPPFLAGS) -o $@ test_metal.m \
		$(METAL_OBJS) $(METAL_LIBS)

$(CUDA_OUT)/nn.o: nn.c nn.h cpu.h cuda.h | $(CUDA_OUT)
	$(CC) $(CFLAGS) $(CUDA_CPPFLAGS) -c -o $@ $<

$(CUDA_OUT)/cpu.o: cpu.c cpu.h nn.h model.h fixture.h | $(CUDA_OUT)
	$(CC) $(CFLAGS) $(CUDA_CPPFLAGS) -c -o $@ $<

$(CUDA_OUT)/fixture.o: fixture.c fixture.h model.h nn.h | $(CUDA_OUT)
	$(CC) $(CFLAGS) $(CUDA_CPPFLAGS) -c -o $@ $<

$(CUDA_OUT)/cuda.o: cuda.cu cuda.h nn.h model.h fixture.h | $(CUDA_OUT)
	$(NVCC) $(NVCCFLAGS) -c -o $@ $<

$(CUDA_BIN): test_cuda.cu $(CUDA_OBJS)
	$(NVCC) $(NVCCFLAGS) -o $@ test_cuda.cu $(CUDA_OBJS) $(CUDA_LIBS)

test: $(CPU_BIN)
	$(CPU_BIN)

test-sanitize: $(SAN_BIN)
	$(SAN_BIN)

test-metal: $(METAL_BIN)
	$(METAL_BIN)

test-cuda: $(CUDA_BIN)
	$(CUDA_BIN)

clean:
	rm -rf $(OUT)
	rm -f nn.o cpu.o fixture.o metal.o cuda.o test_cpu test_cpu_sanitize \
		test_metal test_cuda
	rm -rf test_cpu_sanitize.dSYM test_metal.dSYM
