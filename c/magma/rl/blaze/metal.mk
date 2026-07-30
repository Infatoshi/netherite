# Additive Darwin build fragment for the correctness-first Blaze Metal dylib.
# The kernel is compiled at runtime with newLibraryWithSource; no offline
# Metal compiler or generated metallib is required.

BLAZE_METAL_DIR := rl/blaze
BLAZE_METAL_BUILD_DIR ?= .build/blaze-metal
BLAZE_METAL_DYLIB := $(BLAZE_METAL_DIR)/blaze_metal.dylib
BLAZE_METAL_CC ?= xcrun --sdk macosx clang
BLAZE_METAL_CXX ?= xcrun --sdk macosx clang++
BLAZE_METAL_INCLUDES := -I$(BLAZE_METAL_DIR) -I../mc-sim/core
BLAZE_METAL_COMMON := -O2 -ffp-contract=off -fPIC $(BLAZE_METAL_INCLUDES)
BLAZE_METAL_SNAPSHOT_OBJ := $(BLAZE_METAL_BUILD_DIR)/blaze_snapshot.o

ifeq ($(shell uname -s),Darwin)
# Apple clang does not ship libomp. blaze_cpu.c has a serial source fallback.
BLAZE_OMP :=
endif

.PHONY: blaze_metal_dylib blaze_metal_check clean-blaze-metal

blaze_metal_dylib: $(BLAZE_METAL_DYLIB)

$(BLAZE_METAL_BUILD_DIR):
	mkdir -p $@

$(BLAZE_METAL_SNAPSHOT_OBJ): $(BLAZE_METAL_DIR)/blaze_snapshot.c \
                              $(BLAZE_METAL_DIR)/blaze_snapshot.h | $(BLAZE_METAL_BUILD_DIR)
	$(BLAZE_METAL_CC) $(BLAZE_METAL_COMMON) -std=c11 -c $< -o $@

$(BLAZE_METAL_DYLIB): $(BLAZE_METAL_DIR)/blaze_metal.mm \
                       $(BLAZE_METAL_DIR)/blaze_metal.h \
                       $(BLAZE_METAL_DIR)/blaze_metal_shared.h \
                       $(BLAZE_METAL_DIR)/blaze_metal.metal \
                       $(BLAZE_METAL_DIR)/blaze_core.h \
                       $(BLAZE_METAL_DIR)/blaze_snapshot.h \
                       $(BLAZE_METAL_SNAPSHOT_OBJ)
	$(BLAZE_METAL_CXX) $(BLAZE_METAL_COMMON) -std=c++17 -fobjc-arc -fblocks \
		-Wno-deprecated-declarations -dynamiclib \
		$(BLAZE_METAL_DIR)/blaze_metal.mm $(BLAZE_METAL_SNAPSHOT_OBJ) \
		-framework Foundation -framework Metal \
		-Wl,-install_name,@rpath/blaze_metal.dylib -o $@

blaze_metal_check: $(BLAZE_METAL_DYLIB)
	@BLAZE_METAL_SOURCE=$(abspath $(BLAZE_METAL_DIR)/blaze_metal.metal) \
	 BLAZE_METAL_SHARED=$(abspath $(BLAZE_METAL_DIR)/blaze_metal_shared.h) \
	 $(BLAZE_METAL_CXX) -fobjc-arc -fblocks -std=c++17 -fsyntax-only \
	 -Wno-deprecated-declarations $(BLAZE_METAL_INCLUDES) \
	 $(BLAZE_METAL_DIR)/blaze_metal.mm
	@echo "Blaze Metal dylib ready: $(BLAZE_METAL_DYLIB)"

# The parent Makefile includes this fragment, so extend its existing clean
# target without duplicating or replacing that recipe.
clean: clean-blaze-metal
clean-blaze-metal:
	rm -f $(BLAZE_METAL_DYLIB)
	rm -rf $(BLAZE_METAL_BUILD_DIR)
