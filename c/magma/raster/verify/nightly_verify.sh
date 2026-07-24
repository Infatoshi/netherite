#!/usr/bin/env bash
# Full-sweep magma verify: gated replay of every canonical tape that has
# oracle golden frames, then per-class cluster-baseline diff against the
# committed .gate.json. New UNEXPLAINED growth = regression; shrinkage =
# progress. On-demand, not cron: a session that touches render/sim code ends
# by kicking this off in the background (it self-defers if GPU1 is busy) -
# inline work only replays the one relevant tape, this catches cross-tape
# regressions the session didn't sweep.
#
# GPU policy: replays run on GPU1 (3090) only; GPU0 is the shared big card.
# Skips the run entirely if GPU1 already has >4GB in use (someone's job).
#
# Parallel: replays run NIGHTLY_PAR at a time (default 6). Each replay is
# single-core CPU-bound at ~1.8 GB GPU1 (measured 2026-07-13; GPU sat at 32%
# util serial), so 6 fits 24 GB with slack. Report assembly stays serial and
# tape-ordered so the .md is deterministic.
#
# Output: trace/report/nightly_<date>.md (+ per-tape .gate.json refreshed
# under trace/report/nightly/, the committed baselines are never touched).
set -u
cd "$(dirname "$0")" || exit 1
TRACE=trace
TAPES=tapes
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
NIGHT_DIR=$TRACE/report/nightly
OUT_MD=$TRACE/report/nightly_${STAMP}.md
PAR=${NIGHTLY_PAR:-6}
mkdir -p "$NIGHT_DIR"

GPU1_MB=$(nvidia-smi --id=1 --query-gpu=memory.used --format=csv,noheader,nounits 2>/dev/null | head -1)
if [ -n "${GPU1_MB:-}" ] && [ "$GPU1_MB" -gt 4096 ]; then
    # Explicit SKIP, not green: callers must not treat exit 0 as a passed gate.
    {
        echo "# Nightly magma verify - $STAMP"
        echo
        echo "STATUS: SKIP"
        echo "reason: GPU1 busy (${GPU1_MB} MiB used)"
        echo
        echo "This run did not verify any tape. Do not report as green."
    } | tee "$OUT_MD"
    exit 2
fi

# rebuild so the replay always tests HEAD (stale-object trap: clean game/)
( cd ../.. && rm -f game/*.o && make magma_game_cuda -j"$(nproc)" >/dev/null ) || {
    echo "[nightly] BUILD FAILED" | tee "$OUT_MD"; exit 1; }

# warm the uv venv once so the parallel workers all hit the resolver cache
# (nbt: replay_tape refuses tapes with .snapshot_patch.jsonl sidecars without it)
uv run --no-project --with numpy,scipy,pillow,nbt python -c "" >/dev/null 2>&1

# verifiable tapes, in name (= date) order
TAPE_LIST=()
for tape in "$TAPES"/*.jsonl; do
    case "$tape" in *worldpatch*|*snapshot_patch*) continue;; esac
    base=$(basename "$tape" .jsonl)
    [ -d "$TAPES/${base}_frames" ] || continue   # no goldens -> not verifiable
    TAPE_LIST+=("$base")
done

{
    echo "# Nightly magma verify - $STAMP"
    echo
    echo "${#TAPE_LIST[@]} tapes, $PAR replays in parallel on GPU1"
    echo
} > "$OUT_MD"

# ---- phase 1: parallel replays (each tape fully independent: own out/ dir,
# own log, own gate.json, rc dropped in a marker file) --------------------
replay_one() {
    base=$1
    log=$NIGHT_DIR/${base}.log
    ( cd "$TRACE" && CUDA_VISIBLE_DEVICES=1 uv run --no-project \
        --with numpy,scipy,pillow,nbt python replay_tape.py \
        "../$TAPES/${base}.jsonl" --cuda --report ) > "$log" 2>&1
    echo $? > "$NIGHT_DIR/${base}.rc"
    new_json=$TRACE/report/tape_${base}.gate.json
    [ -f "$new_json" ] && cp "$new_json" "$NIGHT_DIR/${base}.gate.json"
    echo "[nightly] done $base (rc $(cat "$NIGHT_DIR/${base}.rc"))"
}

running=0
for base in "${TAPE_LIST[@]}"; do
    echo "[nightly] replaying $base"
    replay_one "$base" &
    running=$((running + 1))
    if [ "$running" -ge "$PAR" ]; then wait -n; running=$((running - 1)); fi
done
wait

# ---- phase 2: serial, tape-ordered report + baseline diff ---------------
# Required baselines: every verifiable tape that has goldens must either have
# a committed baseline or fail loudly. Missing baseline was previously silent
# green (gate_baseline_diff returned 0); that is a visible failure now.
# rc=3 (pixel gate fail) is only non-fatal when a baseline exists and
# gate_baseline_diff reports no regression.
rc_all=0
for base in "${TAPE_LIST[@]}"; do
    rc=$(cat "$NIGHT_DIR/${base}.rc" 2>/dev/null || echo 99)
    night_json=$NIGHT_DIR/${base}.gate.json
    baseline_json=$TRACE/baselines/${base}.gate.json
    {
        echo "## $base (replay rc=$rc)"
        echo
        if [ ! -f "$baseline_json" ]; then
            echo "FAIL: missing required baseline $baseline_json"
            echo "commit a .gate.json baseline before this tape can green nightly"
            rc_all=1
        elif [ -f "$night_json" ]; then
            if ! uv run --no-project python "$TRACE/gate_baseline_diff.py" \
                --baseline "$baseline_json" \
                --current "$night_json"; then
                echo "FAIL: baseline regression for $base"
                rc_all=1
            fi
        elif [ "$rc" -eq 0 ]; then
            # baseline exists but no gate.json: goldens unusable this run
            echo "replay PASS, no pixel gate (no usable goldens) - see $NIGHT_DIR/${base}.log"
        else
            echo "no gate.json produced (rc=$rc) - see $NIGHT_DIR/${base}.log"; rc_all=1
        fi
        echo
    } >> "$OUT_MD"
    # Crash/refusal (rc not in {0,3}) always fails. rc=3 is priced by baseline
    # diff above; without a baseline it already set rc_all=1.
    if [ "$rc" -ne 0 ] && [ "$rc" -ne 3 ]; then
        rc_all=1
    fi
    # rc=3 with a baseline that did not absorb the failure (no night_json or
    # baseline_diff already failed) is not silent green.
    if [ "$rc" -eq 3 ] && [ ! -f "$night_json" ]; then
        echo "FAIL: rc=3 without gate.json for $base" >> "$OUT_MD"
        rc_all=1
    fi
done

echo "[nightly] report -> $OUT_MD"
if [ "$rc_all" -ne 0 ]; then
    echo "[nightly] RESULT: FAIL" | tee -a "$OUT_MD"
else
    echo "[nightly] RESULT: PASS" | tee -a "$OUT_MD"
fi
exit $rc_all
