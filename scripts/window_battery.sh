#!/usr/bin/env bash
# window_battery.sh - build magma_game{,_cuda} and run ranks 4-15 battery.
#
# Cache: $HOME/dev/nw/.tmp/window_battery_<UTCdate>/
# GPU dumps: overnight-compute gpu1 + CUDA_VISIBLE_DEVICES=1 (never GPU0).
#
# Usage:
#   bash scripts/window_battery.sh
#   bash scripts/window_battery.sh --reuse /path/to/window_battery_YYYYMMDD
#   bash scripts/window_battery.sh --only XB-STILL-CPU-CUDA
#   bash scripts/window_battery.sh --skip-gpu
#   bash scripts/window_battery.sh --selftest
#
# Env:
#   SKIP_BUILD=1   skip make (use existing binaries)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}"
export TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"
mkdir -p "$TMPDIR" "$UV_CACHE_DIR"

SKIP_BUILD="${SKIP_BUILD:-0}"
# --reuse / --selftest still benefit from current binaries for meta hash.
if [[ "$SKIP_BUILD" != "1" ]]; then
  echo "[window_battery] building magma_game magma_game_cuda (-j16)"
  make -C magma magma_game magma_game_cuda -j16
fi

GAME_CPU="$ROOT/magma/magma_game"
GAME_CUDA="$ROOT/magma/magma_game_cuda"
if [[ ! -x "$GAME_CPU" ]]; then
  echo "ERROR: $GAME_CPU missing after build" >&2
  exit 2
fi

command -v uv >/dev/null || { echo "ERROR: uv required" >&2; exit 2; }

# GPU path needs overnight-compute + cuda binary unless --skip-gpu
SKIP_GPU=0
for a in "$@"; do
  case "$a" in
    --skip-gpu) SKIP_GPU=1 ;;
  esac
done
if [[ "$SKIP_GPU" != "1" ]]; then
  if [[ ! -x "$GAME_CUDA" ]]; then
    echo "ERROR: $GAME_CUDA missing (use --skip-gpu or fix CUDA build)" >&2
    exit 2
  fi
  command -v overnight-compute >/dev/null || {
    echo "ERROR: overnight-compute required for GPU jobs" >&2
    exit 2
  }
fi

echo "[window_battery] running battery"
export PYTHONUNBUFFERED=1
exec uv run --no-project --with numpy,pillow \
  python "$ROOT/scripts/window_battery.py" \
  --game "$GAME_CPU" \
  --game-cuda "$GAME_CUDA" \
  "$@"
