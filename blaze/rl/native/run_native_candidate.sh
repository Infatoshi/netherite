#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
binary="$repo/blaze/rl/native/build/ppo_native"
fixture="$repo/optloop_runs/ppo-native-bf16-d55-v2/native_oracle.fixture"

"$binary" --set native_oracle_bf16=1 \
  --set "native_oracle_fixture=$fixture"
"$binary" \
  --set n_envs=6144 --set t_chunk=32 --set epochs=2 --set mb=8192 \
  --set bench_warmup_chunks=2 --set bench_measure_chunks=1 \
  --set max_ticks=1000000000 --set max_wall=3600 --set rng_seed=0 \
  --set native_bf16=1 --set native_channels_last=0
