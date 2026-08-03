#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
metric_file=${PPO_METRIC_FILE:-"$repo/optloop_runs/ppo-native-bf16-d55-v3/native_fp32_metric.txt"}
log_file=$(mktemp "$repo/optloop_runs/ppo-native-bf16-d55-v3/native-fp32-bench.XXXXXX.log")
trap 'unlink "$log_file" 2>/dev/null || true' EXIT

nvidia-smi
overnight-compute run \
  --agent "ppo-native-fp32-$$" --resource gpu0 --ttl 15m --poll 15s -- \
  env CUDA_VISIBLE_DEVICES=0 \
  "$repo/blaze/rl/native/run_native_candidate.sh" | tee "$log_file"

awk '
  /^NATIVE_BENCH / {
    for (i = 1; i <= NF; i++) {
      if ($i ~ /^wall_ms=/) {
        sub(/^wall_ms=/, "", $i)
        value = $i
      }
    }
  }
  END {
    if (value == "") exit 1
    printf "{\"chunk_wall_ms\": %.6f}\n", value
  }
' "$log_file" >"$metric_file"
