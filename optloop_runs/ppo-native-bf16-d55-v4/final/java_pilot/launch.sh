#!/usr/bin/env bash
set -uo pipefail
export UV_CACHE_DIR=/home/infatoshi/.cache/uv
export TMPDIR=/home/infatoshi/dev/nw/.tmp
export CUDA_VISIBLE_DEVICES=0
cd /home/infatoshi/dev/nw/ppo-native-bf16
(cd /home/infatoshi/dev/nw/paper-t3 && git update-index --refresh >/dev/null 2>&1 || true)
AGENT="ppo-native-jpilot-1785621597"
exec overnight-compute run --agent "$AGENT" --resource gpu0 --ttl 180m --poll 30s -- \
  env CUDA_VISIBLE_DEVICES=0 UV_CACHE_DIR=/home/infatoshi/.cache/uv TMPDIR=/home/infatoshi/dev/nw/.tmp \
  uv run --no-project --with pyyaml --with torch==2.13.0 --with numpy \
  python /home/infatoshi/dev/nw/.tmp/parallel_java_eval.py \
    --root /home/infatoshi/dev/nw/paper-t3 \
    --state /home/infatoshi/dev/nw/ppo-native-bf16/optloop_runs/ppo-native-bf16-d55-v4/final/java_pilot \
    --output /home/infatoshi/dev/nw/ppo-native-bf16/optloop_runs/ppo-native-bf16-d55-v4/final/java_pilot/java.json \
    --sim /home/infatoshi/dev/nw/ppo-native-bf16/optloop_runs/ppo-native-bf16-d55-v4/final/java_pilot/sim_stub.json \
    --checkpoint /home/infatoshi/dev/nw/ppo-native-bf16/optloop_runs/ppo-native-bf16-d55-v4/final/native_1p92b.pt \
    --workers 1 \
    --seeds 2 3 10 \
    --base-port 25610 \
    --base-display 11 \
    --run
