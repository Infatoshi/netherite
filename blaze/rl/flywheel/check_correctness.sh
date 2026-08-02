#!/usr/bin/env bash
# Formal correctness C for the flywheelopt lane. GPU0 exclusive, under the
# same flock every timed run uses.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$repo"

# the .so must be alive before anything else is believed
UV_CACHE_DIR=/home/infatoshi/.cache/uv TMPDIR=/home/infatoshi/dev/nw/.tmp \
  uv run --no-project python -m py_compile blaze/env/ppo_chain_cu.py

nvidia-smi --query-gpu=index,name,utilization.gpu,memory.used \
  --format=csv,noheader

flock /home/infatoshi/dev/nw/.tmp/gpu0.lock -c "
  CUDA_VISIBLE_DEVICES=0 \
  UV_CACHE_DIR=/home/infatoshi/.cache/uv \
  TMPDIR=/home/infatoshi/dev/nw/.tmp \
  PYTHONHASHSEED=0 \
  uv run --no-project --with numpy==2.5.1 --with torch==2.13.0 \
    python '$repo/blaze/rl/flywheel/so_sanity.py'
  CUDA_VISIBLE_DEVICES=0 \
  UV_CACHE_DIR=/home/infatoshi/.cache/uv \
  TMPDIR=/home/infatoshi/dev/nw/.tmp \
  PYTHONHASHSEED=0 \
  uv run --no-project --with numpy==2.5.1 --with torch==2.13.0 \
    python '$repo/blaze/rl/flywheel/check_correctness.py'
"
