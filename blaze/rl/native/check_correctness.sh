#!/usr/bin/env bash
# Formal correctness for native BF16 PPO (production contract).
# Must execute the native BF16 oracle itself under a GPU0 lease.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$repo"

sha256sum --check blaze/rl/native/fp32_reference.sha256
sha256sum --check blaze/rl/native/native_fixture.sha256
git merge-base --is-ancestor \
  d55c7f01c1139299be3f7fa0b98ef11b82c3b473 HEAD
uv run --no-project python -m py_compile blaze/env/ppo_chain_cu.py

binary="$repo/blaze/rl/native/build/ppo_native"
fixture="$repo/optloop_runs/ppo-native-bf16-d55-v2/native_oracle.fixture"
test -x "$binary"
test -f "$fixture"
nvidia-smi
overnight-compute run \
  --agent "ppo-native-correctness-$$" --resource gpu0 --ttl 10m --poll 15s -- \
  env CUDA_VISIBLE_DEVICES=0 NATIVE_ORACLE_BF16=1 \
  NATIVE_ORACLE_FIXTURE="$fixture" "$binary"
