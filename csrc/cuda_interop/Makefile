# CUDA-GL interop PoC + future production library.
# Linux + CUDA only. Not built on macOS.

CUDA_HOME ?= /usr/local/cuda-13.2
NVCC      ?= $(CUDA_HOME)/bin/nvcc
CC        ?= cc

BUILD_DIR := build

CFLAGS    := -O2 -Wall -Wextra -Wno-unused-parameter -fPIC
INCLUDES  := -I$(CUDA_HOME)/include
LIBS      := -L$(CUDA_HOME)/lib64 \
             -lcudart -lcuda \
             -lglfw -lGLEW -lGL -ldl -lpthread -lm

POC_SRC   := poc.c
POC_BIN   := $(BUILD_DIR)/poc

.PHONY: all poc clean

all: poc

poc: $(POC_BIN)

$(POC_BIN): $(POC_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(POC_SRC) -o $@ $(LIBS)

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
