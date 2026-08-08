#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
binary="$repo/blaze/rl/native/build/ppo_native"
fixture="$repo/optloop_runs/ppo-native-bf16-d55-v2/native_oracle.fixture"

NATIVE_ORACLE_BF16=1 NATIVE_ORACLE_FIXTURE="$fixture" "$binary"
env N_ENVS=6144 T_CHUNK=32 EPOCHS=2 MB=8192 \
  BENCH_WARMUP_CHUNKS=2 BENCH_MEASURE_CHUNKS=1 \
  MAX_TICKS=1000000000 MAX_WALL=3600 RNG_SEED=0 \
  NATIVE_BF16=1 NATIVE_CHANNELS_LAST=0 \
  "$binary"
