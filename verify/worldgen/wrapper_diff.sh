#!/usr/bin/env bash
# Policy-pinned census: magma product populate WRAPPER vs blaze overworld_region
# reference, with fluid/shroomlight knobs matched to magma product defaults.
#
# Magma defaults: MAGMA_FLUID_CA unset (fluid OFF), MAGMA_SHROOMLIGHT unset (stale).
# Blaze owr_run always runs fluid ON and never attaches stale skylight (CA model).
# This tool pins both sides to fluid OFF + shroomlight stale so residual diffs are
# wrapper mechanics (donors, OOB spill, cascade, caps, multi-window apply, support
# sweep, st_map_features, load order) - not mixed policy.
#
# Diagnostic census (builds artifacts under verify/worldgen/out). The pinned gate
# is verify/worldgen/wrapper_gate.sh, which compares this census to
# verify/worldgen/known_divergences.json (blessed 2026-08-02: ore multi-window
# apply + load-order residuals are KNOWN wrapper mechanics). Use the gate for
# pass/fail; use this script for full reports and the load-order probe.
#
# Usage:
#   bash verify/worldgen/wrapper_diff.sh [OUTDIR] [seeds...]
#   bash verify/worldgen/wrapper_gate.sh              # pinned gate (rc=0/1)
# Defaults: OUTDIR=verify/worldgen/out  seeds=0 7 9 19
#
# Region: origin 2x2 chunks (bcx=0,bcz=0) - the owr_run window at the origin.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/verify/worldgen/out}"
shift || true
if [[ $# -gt 0 ]]; then
  SEEDS=("$@")
else
  SEEDS=(0 7 9 19)
fi

export UV_CACHE_DIR="${UV_CACHE_DIR:-/home/infatoshi/.cache/uv}"
export TMPDIR="${TMPDIR:-/home/infatoshi/dev/nw/.tmp}"
mkdir -p "$TMPDIR" "$OUT"

BLAZE="$ROOT/blaze"
MAGMA="$ROOT/magma"
BLAZE_DUMP="$BLAZE/build/bin/cpu/owr_policy_dump"
WORLD_DUMP="$MAGMA/trace/world_dump"
CRWD_PY="$ROOT/verify/worldgen/crwd_to_sparse.py"
CLASS_PY="$ROOT/verify/worldgen/classify_diff.py"
UV=(uv run --no-project --with numpy)
# crwd_to_sparse / classify need only stdlib; numpy not required but keep env consistent

REPORT="$OUT/report.txt"
: >"$REPORT"
log() { printf '%s\n' "$*" | tee -a "$REPORT"; }

log "=== wrapper_diff: magma product wrapper vs blaze owr reference ==="
log "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
log "root: $ROOT"
log "seeds: ${SEEDS[*]}"
log "region: chunks (0,0)+2x2  (== owr_run bcx=0 bcz=0 window)"
log "policy: fluid=OFF  shroomlight=stale  (magma product defaults)"
log ""

# ---------- build tools ----------
log "[build] blaze owr_policy_dump"
make -C "$BLAZE" build/bin/cpu/owr_policy_dump 2>&1 | tee -a "$REPORT" | tail -5

log "[build] magma world_dump (product wrapper path)"
# world_dump links game objects; build_world_dump.sh runs make game then links.
if [[ ! -x "$WORLD_DUMP" ]]; then
  bash "$MAGMA/trace/build_world_dump.sh" 2>&1 | tee -a "$REPORT" | tail -20
else
  # Rebuild if populate_mc.c newer than binary (safe no-op when current).
  if [[ "$MAGMA/world/populate_mc.c" -nt "$WORLD_DUMP" ]] || \
     [[ "$MAGMA/trace/world_dump.c" -nt "$WORLD_DUMP" ]]; then
    bash "$MAGMA/trace/build_world_dump.sh" 2>&1 | tee -a "$REPORT" | tail -20
  else
    log "  world_dump present and up to date"
  fi
fi

# ---------- acceptance (a): fluid ON == overworld_region seed 0 ----------
log ""
log "=== (a) fluid ON reproduces overworld_region seed 0 ==="
make -C "$BLAZE" build/bin/cpu/overworld_region 2>&1 | tee -a "$REPORT" | tail -3
REF_FULL="$OUT/ref_overworld_region_s0.full"
POL_FULL="$OUT/policy_fluid_on_s0.full"
"$BLAZE/build/bin/cpu/overworld_region" 0 0 0 >"$REF_FULL"
"$BLAZE_DUMP" --fluid on --shroomlight ca --format full 0 0 0 >"$POL_FULL"
if cmp -s "$REF_FULL" "$POL_FULL"; then
  REF_LINES=$(wc -l <"$REF_FULL")
  log "PASS: owr_policy_dump --fluid on --shroomlight ca --format full"
  log "      == overworld_region 0 0 0  ($REF_LINES lines, byte-identical)"
else
  log "FAIL: fluid-on policy dump diverges from overworld_region"
  diff -u "$REF_FULL" "$POL_FULL" | head -40 | tee -a "$REPORT" || true
  exit 1
fi

# ---------- per-seed census ----------
log ""
log "=== per-seed census (matched policy: fluid off, shroomlight stale) ==="
log ""
printf '%-6s %10s %10s %10s %8s %8s %8s\n' \
  SEED blaze_n magma_n diff fluid mush other | tee -a "$REPORT"
printf '%-6s %10s %10s %10s %8s %8s %8s\n' \
  ---- ------- ------- ---- ----- ---- ----- | tee -a "$REPORT"

TABLE_TMP="$OUT/.table_rows"
: >"$TABLE_TMP"

for S in "${SEEDS[@]}"; do
  SDIR="$OUT/seed_$S"
  mkdir -p "$SDIR"
  log ""
  log "--- seed $S ---"

  # Blaze reference under magma product policy
  "$BLAZE_DUMP" --fluid off --shroomlight stale --format sparse "$S" 0 0 \
    >"$SDIR/blaze_sparse.txt"

  # Magma product wrapper: world_dump through light/gen_chunk -> popmc_decorate_chunk
  # Unset fluid CA; leave SHROOMLIGHT unset (= stale).
  unset MAGMA_FLUID_CA || true
  unset MAGMA_SHROOMLIGHT || true
  BIN="$SDIR/magma_crwd.bin"
  "$WORLD_DUMP" --seed "$S" --cx0 0 --cz0 0 --ncx 2 --ncz 2 --out "$BIN" \
    2>"$SDIR/magma_world_dump.err" || {
      log "world_dump failed for seed $S:"; cat "$SDIR/magma_world_dump.err" | tee -a "$REPORT"
      exit 1
    }
  cat "$SDIR/magma_world_dump.err" >>"$REPORT"

  "${UV[@]}" python3 "$CRWD_PY" "$BIN" -o "$SDIR/magma_sparse.txt" 2>>"$REPORT"

  # Diff + classify
  "${UV[@]}" python3 "$CLASS_PY" "$SDIR/blaze_sparse.txt" "$SDIR/magma_sparse.txt" \
    --label "seed=$S" >"$SDIR/classify.txt"
  cat "$SDIR/classify.txt" >>"$REPORT"

  # Parse numbers for the table (portable sed)
  B=$(sed -n 's/.*blaze_cells=\([0-9]*\).*/\1/p' "$SDIR/classify.txt" | head -1)
  M=$(sed -n 's/.*magma_cells=\([0-9]*\).*/\1/p' "$SDIR/classify.txt" | head -1)
  D=$(sed -n 's/.*diff_cells=\([0-9]*\).*/\1/p' "$SDIR/classify.txt" | head -1)
  F=$(sed -n 's/.*fluid=\([0-9]*\).*/\1/p' "$SDIR/classify.txt" | head -1)
  U=$(sed -n 's/.*mushroom=\([0-9]*\).*/\1/p' "$SDIR/classify.txt" | head -1)
  O=$(sed -n 's/.*other=\([0-9]*\).*/\1/p' "$SDIR/classify.txt" | head -1)
  B=${B:-0}; M=${M:-0}; D=${D:-0}; F=${F:-0}; U=${U:-0}; O=${O:-0}
  printf '%-6s %10s %10s %10s %8s %8s %8s\n' "$S" "$B" "$M" "$D" "$F" "$U" "$O" \
    | tee -a "$REPORT" | tee -a "$TABLE_TMP"
done

# ---------- load-order probe (seed 9) ----------
log ""
log "=== load-order probe (seed 9, magma-vs-magma) ==="
# world_dump --prep-list builds windows in the listed order before ensure.
# Two different orders over the same base set: cheap, no new streaming machinery.
PROBE_DIR="$OUT/load_order_seed9"
mkdir -p "$PROBE_DIR"
# Bases that decorate chunks (0,0)..(1,1): bcx,bcz in {-1,0,1}^2
{
  for bcx in -1 0 1; do
    for bcz in -1 0 1; do
      echo "$bcx $bcz"
    done
  done
} >"$PROBE_DIR/order_raster.txt"
{
  for bcx in 1 0 -1; do
    for bcz in 1 0 -1; do
      echo "$bcx $bcz"
    done
  done
} >"$PROBE_DIR/order_rev.txt"

unset MAGMA_FLUID_CA || true
unset MAGMA_SHROOMLIGHT || true
for tag in raster rev; do
  "$WORLD_DUMP" --seed 9 --cx0 0 --cz0 0 --ncx 2 --ncz 2 \
    --prep-list "$PROBE_DIR/order_${tag}.txt" \
    --out "$PROBE_DIR/magma_${tag}.bin" \
    2>"$PROBE_DIR/magma_${tag}.err" || {
      log "load-order world_dump ($tag) failed:"; cat "$PROBE_DIR/magma_${tag}.err" | tee -a "$REPORT"
      exit 1
    }
  cat "$PROBE_DIR/magma_${tag}.err" >>"$REPORT"
  "${UV[@]}" python3 "$CRWD_PY" "$PROBE_DIR/magma_${tag}.bin" \
    -o "$PROBE_DIR/magma_${tag}.txt" 2>>"$REPORT"
done

"${UV[@]}" python3 "$CLASS_PY" "$PROBE_DIR/magma_raster.txt" "$PROBE_DIR/magma_rev.txt" \
  --label "load_order_seed9 raster_vs_rev" >"$PROBE_DIR/classify.txt"
cat "$PROBE_DIR/classify.txt" >>"$REPORT"
LO_DIFF=$(sed -n 's/.*diff_cells=\([0-9]*\).*/\1/p' "$PROBE_DIR/classify.txt" | head -1)
LO_DIFF=${LO_DIFF:-0}
log "load-order cell delta (raster vs rev prep-list, seed 9): $LO_DIFF"

# ---------- wrap-up ----------
log ""
log "=== summary table ==="
printf '%-6s %10s %10s %10s %8s %8s %8s\n' \
  SEED blaze_n magma_n diff fluid mush other | tee -a "$REPORT"
cat "$TABLE_TMP" | tee -a "$REPORT"

TOTAL_DIFF=0
while read -r _ _ _ d _ _ _; do
  TOTAL_DIFF=$((TOTAL_DIFF + d))
done <"$TABLE_TMP"

log ""
if [[ "$TOTAL_DIFF" -eq 0 ]]; then
  log "VERDICT: zero residual under matched policy across seeds ${SEEDS[*]}."
  log "         If the blessed sidecar still expects nonzero, update it via"
  log "         bash verify/worldgen/wrapper_gate.sh --update (maintainer only)."
else
  log "VERDICT: nonzero residual (total diff_cells summed over seeds = $TOTAL_DIFF)."
  log "         Pinned gate: bash verify/worldgen/wrapper_gate.sh"
  log "         Sidecar:     verify/worldgen/known_divergences.json"
  log "         load-order note: prep-list raster vs rev delta on seed 9 = $LO_DIFF"
fi

log ""
log "report: $REPORT"
log "artifacts under: $OUT"
echo "$REPORT"
