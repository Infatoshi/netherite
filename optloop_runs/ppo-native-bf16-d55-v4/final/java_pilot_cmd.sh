#!/usr/bin/env bash
# Pilot: 3 seeds, 1 worker. Needs free displays :11 and qrl ports; paper-t3 clean at d55.
set -euo pipefail
export UV_CACHE_DIR=/home/infatoshi/.cache/uv
export TMPDIR=/home/infatoshi/dev/nw/.tmp
FINAL=/home/infatoshi/dev/nw/ppo-native-bf16/optloop_runs/ppo-native-bf16-d55-v4/final
PT=$FINAL/native_1p92b.pt
test -f "$PT"
# plan first
python3 /home/infatoshi/dev/nw/.tmp/parallel_java_eval.py \
  --root /home/infatoshi/dev/nw/paper-t3 \
  --state "$FINAL/java_pilot" \
  --output "$FINAL/java_pilot/java.json" \
  --sim /dev/null \
  --checkpoint "$PT" \
  --workers 1 \
  --seeds 2 3 10 \
  --base-port 25610 \
  --base-display 11
# then: add --run under a fresh overnight-compute lease if plan looks good
