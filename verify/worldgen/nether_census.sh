#!/usr/bin/env bash
# Diagnostic census: Nether + End terrain across backends.
#   blaze CPU vs blaze CUDA  (nf_run / cpe_provide_chunk)
#   blaze CPU vs magma       (world_dump --world-type 2|3 --states)
#
# NOT a frozen gate. Pattern mirrors wrapper_diff.sh (sparse lines, class
# breakdown, first-diff cell, content sha). Does not touch known_divergences.json
# or wrapper_gate.sh.
#
# Regions (see nether_census.py):
#   nether origin 4x4 chunks around (0,0)
#   nether fortress 3x3 around first ft_can_spawn per seed
#   end main island 8x8 around (0,0)
#
# GPU: nvidia-smi preflight; CUDA under flock gpu0.lock, CUDA_VISIBLE_DEVICES=0,
# SM=sm_120 (Blackwell). Short holds only.
#
# Usage:
#   bash verify/worldgen/nether_census.sh [OUTDIR] [seeds...]
#   bash verify/worldgen/nether_census.sh --skip-cuda [OUTDIR]
#   bash verify/worldgen/nether_census.sh --skip-magma [OUTDIR]
# Defaults: OUTDIR=verify/worldgen/out_nether  seeds=0 2 3 7 9 10 19
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=""
SKIP_CUDA=0
SKIP_MAGMA=0
SEEDS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-cuda) SKIP_CUDA=1; shift ;;
    --skip-magma) SKIP_MAGMA=1; shift ;;
    -h|--help)
      sed -n '2,28p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      if [[ -z "$OUT" && "$1" != [0-9]* ]]; then
        OUT="$1"
      else
        SEEDS+=("$1")
      fi
      shift
      ;;
  esac
done

OUT="${OUT:-$ROOT/verify/worldgen/out_nether}"
if [[ ${#SEEDS[@]} -eq 0 ]]; then
  SEEDS=(0 2 3 7 9 10 19)
fi

export UV_CACHE_DIR="${UV_CACHE_DIR:-/home/infatoshi/.cache/uv}"
export TMPDIR="${TMPDIR:-/home/infatoshi/dev/nw/.tmp}"
export CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-0}"
export SM="${SM:-sm_120}"
export MC_SM="${MC_SM:-$SM}"
mkdir -p "$TMPDIR" "$OUT"

echo "=== nether_census.sh ==="
echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "root: $ROOT"
echo "out:  $OUT"
echo "seeds: ${SEEDS[*]}"
echo "skip_cuda=$SKIP_CUDA skip_magma=$SKIP_MAGMA"
echo ""

if [[ "$SKIP_CUDA" -eq 0 ]]; then
  echo "[gpu] nvidia-smi preflight"
  nvidia-smi --query-gpu=index,name,memory.used,memory.total,utilization.gpu --format=csv
  echo ""
fi

PY_ARGS=(--out "$OUT" --seeds "${SEEDS[@]}")
[[ "$SKIP_CUDA" -eq 1 ]] && PY_ARGS+=(--skip-cuda)
[[ "$SKIP_MAGMA" -eq 1 ]] && PY_ARGS+=(--skip-magma)

uv run --no-project --with numpy python \
  "$ROOT/verify/worldgen/nether_census.py" "${PY_ARGS[@]}"

echo ""
echo "done. report: $OUT/report.txt"
echo "      json:   $OUT/census.json"
