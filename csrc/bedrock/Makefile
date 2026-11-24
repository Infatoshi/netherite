# Netherite Bedrock: Blackwell sm_120a GPU-resident simulator + Square-NVFP4 PPO + Portal Speedrun Teacher Pipeline
CC       ?= cc
NVCC     ?= /usr/local/cuda/bin/nvcc
CUDA_DIR ?= /usr/local/cuda
OUT      := $(abspath ../../out/bedrock)
ENG_OUT  := $(abspath ../../out/native)
ENG_LIB  := $(ENG_OUT)/libnetherite.a

CFLAGS   := -O2 -std=c11 -Wall -Wextra -Werror -ffp-contract=off -I. -I..
NVFLAGS  := -O3 -gencode=arch=compute_120a,code=sm_120a --extended-lambda -I.
LDFLAGS  := -fuse-ld=mold -L$(CUDA_DIR)/lib64 -lcudart -lcublas -lcublasLt -lz -lstdc++ -lm -lpthread

.PHONY: all clean parity speed train portal

all: $(OUT)/bedrock_bench $(OUT)/br_harvest $(OUT)/br_portal_parity $(OUT)/br_teacher_eval $(OUT)/br_portal_train

$(OUT):
	mkdir -p $(OUT)

$(ENG_LIB):
	$(MAKE) -C .. $(ENG_LIB)

$(OUT)/br_sim.o: br_sim.cu bedrock.h | $(OUT)
	$(NVCC) $(NVFLAGS) -c $< -o $@

$(OUT)/br_train.o: br_train.cu bedrock.h | $(OUT)
	$(NVCC) $(NVFLAGS) -c $< -o $@

$(OUT)/br_bridge.o: br_bridge.c bedrock.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/br_bench.o: br_bench.c bedrock.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/bedrock_bench: $(OUT)/br_sim.o $(OUT)/br_train.o $(OUT)/br_bridge.o $(OUT)/br_bench.o $(ENG_LIB)
	$(CC) $^ -o $@ $(LDFLAGS)

# Lane A: Real 1.7.10 Lava-Pool Seed Harvester
$(OUT)/br_harvest: br_harvest.c br_contract.h $(ENG_LIB) | $(OUT)
	$(CC) $(CFLAGS) br_harvest.c $(ENG_LIB) -o $@ -lz -lm -lpthread

# Lane B: GPU 3D Portal-Casting Physics + Rung 3 CPU Parity
$(OUT)/br_portal_sim.o: br_portal_sim.cu br_portal_sim.h br_contract.h | $(OUT)
	$(NVCC) $(NVFLAGS) -c $< -o $@

$(OUT)/br_portal_parity.o: br_portal_parity.c br_portal_sim.h br_contract.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/br_portal_parity: $(OUT)/br_portal_sim.o $(OUT)/br_portal_parity.o $(ENG_LIB)
	$(CC) $^ -o $@ $(LDFLAGS)

# Lane C: Exact net.h Factored s1 NVFP4 Policy
$(OUT)/br_policy_s1.o: br_policy_s1.cu br_policy_s1.h br_contract.h | $(OUT)
	$(NVCC) $(NVFLAGS) -c $< -o $@

# Lane D: Live CPU World Obs Bridge + BRT1 Zero-Shot Replay + distill.cu Adapter
$(OUT)/br_obs_bridge.o: br_obs_bridge.c br_obs_bridge.h br_contract.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/br_teacher_eval.o: br_teacher_eval.c br_obs_bridge.h br_contract.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/br_teacher_eval: $(OUT)/br_obs_bridge.o $(OUT)/br_teacher_eval.o $(ENG_LIB)
	$(CC) $^ -o $@ -lz -lm -lpthread

# Integrated End-to-End Portal Speedrun Teacher Trainer (Lanes A+B+C+D)
$(OUT)/br_portal_train.o: br_portal_train.cu br_portal_sim.h br_policy_s1.h br_obs_bridge.h br_contract.h | $(OUT)
	$(NVCC) $(NVFLAGS) -c $< -o $@

$(OUT)/br_portal_train: $(OUT)/br_portal_train.o $(OUT)/br_portal_sim.o $(OUT)/br_policy_s1.o $(OUT)/br_obs_bridge.o $(ENG_LIB)
	$(CC) $^ -o $@ $(LDFLAGS)

parity: $(OUT)/bedrock_bench $(OUT)/br_portal_parity
	$(OUT)/bedrock_bench --mode parity
	$(OUT)/br_portal_parity

speed: $(OUT)/bedrock_bench
	$(OUT)/bedrock_bench --mode speed

train: $(OUT)/bedrock_bench
	$(OUT)/bedrock_bench --mode train --worlds 8192 --hidden 1024 --iters 30

portal: $(OUT)/br_portal_train $(OUT)/br_teacher_eval
	$(OUT)/br_portal_train --seeds $(OUT)/portal_seeds.bin --save $(OUT)/portal_teacher.brt1 --worlds 8192 --hidden 1024 --iters 40
	$(OUT)/br_teacher_eval --teacher $(OUT)/portal_teacher.brt1 --seeds $(OUT)/portal_seeds.bin --episodes 16 --ticks 200 --dump-rec $(OUT)/portal_distill_recs.bin

clean:
	rm -rf $(OUT)
