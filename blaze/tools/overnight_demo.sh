#!/usr/bin/env bash
# Overnight verification + demo report for blaze. Run from repo root.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/java"
REPORT="$ROOT/demo/OVERNIGHT_REPORT.txt"
mkdir -p demo

log() { echo "$@" | tee -a "$REPORT"; }

: > "$REPORT"
log "blaze overnight report — $(date -Is)"
log "host: $(hostname)"
log ""

pass() { log "  PASS  $1"; }
fail() { log "  FAIL  $1"; return 1; }

log "=== Wave 14 CPU verify (fast) ==="
for k in smoke tick_compose_full cuda_batch_tick cuda_batch_worldgen py_gym_env_smoke sps_benchmark; do
  if make "verify-cpu-$k" >>"$REPORT" 2>&1; then
    pass "verify-cpu-$k"
  else
    fail "verify-cpu-$k" || true
  fi
done

log ""
log "=== SPS benchmark (CPU + CUDA if nvcc) ==="
if command -v nvcc >/dev/null 2>&1; then
  make cpu-sps_benchmark >/dev/null 2>&1
  make -s build/bin/cuda/sps_benchmark 2>/dev/null || true
  log "CPU:"
  build/bin/cpu/sps_benchmark 2>>"$REPORT" | head -2 >>"$REPORT"
  build/bin/cpu/sps_benchmark 2>&1 | rg 'sps_benchmark cpu' >>"$REPORT" || true
  if [[ -x build/bin/cuda/sps_benchmark ]]; then
    log "CUDA:"
    build/bin/cuda/sps_benchmark 2>>"$REPORT" | head -2 >>"$REPORT"
    build/bin/cuda/sps_benchmark 2>&1 | rg 'sps_benchmark cuda' >>"$REPORT" || true
  fi
  if python3 oracle/runner.py sps_benchmark >>"$REPORT" 2>&1; then
    pass "oracle sps_benchmark (CPU==CUDA hash)"
  else
    fail "oracle sps_benchmark" || true
  fi
else
  log "  (nvcc missing — skipped CUDA SPS)"
fi

log ""
log "=== Full CPU==CUDA worldgen oracle (each kernel compiles standalone in ~7-30s) ==="
if command -v nvcc >/dev/null 2>&1; then
  for k in chunk_provider overworld_full nether_full end_full cuda_batch_worldgen; do
    if python3 oracle/runner.py "$k" >>"$REPORT" 2>&1; then
      pass "full CPU==CUDA $k"
    else
      fail "full CPU==CUDA $k" || true
    fi
  done
else
  log "  (nvcc missing)"
fi

log ""
log "=== Quick stats ==="
log "  cpu kernels: $(ls cpu/*.c 2>/dev/null | wc -l)"
log "  core headers: $(ls core/*.h 2>/dev/null | wc -l)"
log ""
log "Done. Pull demo/OVERNIGHT_REPORT.txt on the Mac to read results."
log "Try: make help | make verify-cpu-tick_compose_full | make batch"
