#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAGMA="$(cd "$HERE/.." && pwd)"
OUT="${1:-$HERE/out/redstone_moving_piston_checkpoint_regression}"
MATRIX="$OUT/oracle"

cd "$MAGMA"
ORACLE_MATRIX_INSTANCES=3 \
ORACLE_MATRIX_CASE_TIMEOUT=240 \
bash "$HERE/run_oracle_matrix_pool.sh" \
    --out "$MATRIX" \
    --case redstone_piston_east_single_stone_push_start_seed_0 \
    --case redstone_piston_east_single_stone_push_progress_seed_0 \
    --case redstone_piston_east_single_stone_push_settled_seed_0

UV_CACHE_DIR=/home/jawaugh/.cache/uv \
TMPDIR="$(cd "$MAGMA/.." && pwd)/.tmp" \
uv run --no-project python "$HERE/verify_moving_piston_checkpoint.py" \
    --start "$MATRIX/redstone_piston_east_single_stone_push_start_seed_0" \
    --progress "$MATRIX/redstone_piston_east_single_stone_push_progress_seed_0" \
    --settled "$MATRIX/redstone_piston_east_single_stone_push_settled_seed_0" \
    --out "$OUT/resume"
