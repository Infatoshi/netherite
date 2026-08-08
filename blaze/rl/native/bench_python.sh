#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
metric_file=${PPO_METRIC_FILE:-"$repo/optloop_runs/ppo-native-bf16-d55/python_metric.txt"}
log_file=$(mktemp "$repo/optloop_runs/ppo-native-bf16-d55/python-bench.XXXXXX.log")
trap 'unlink "$log_file" 2>/dev/null || true' EXIT

nvidia-smi
overnight-compute run \
  --agent "ppo-bf16-baseline-$$" --resource gpu0 --ttl 10m --poll 15s -- \
  env CUDA_VISIBLE_DEVICES=0 \
  UV_CACHE_DIR=/home/infatoshi/.cache/uv \
  TMPDIR=/home/infatoshi/dev/nw/.tmp \
  N_ENVS=6144 T_CHUNK=32 EPOCHS=2 MB=8192 \
  BENCH_WARMUP_CHUNKS=2 BENCH_MEASURE_CHUNKS=1 \
  MAX_TICKS=1e12 MAX_WALL=3600 PYTHONHASHSEED=0 RNG_SEED=0 \
  uv run --no-project --with numpy==2.5.1 --with torch==2.13.0 \
  python "$repo/blaze/env/ppo_chain_cu.py" | tee "$log_file"

awk '
  /^BENCH / {
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
