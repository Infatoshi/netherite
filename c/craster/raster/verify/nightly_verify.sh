#!/usr/bin/env bash
# Full-sweep craster verify: gated replay of every canonical tape that has
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
cd "$(dirname "$0")"
TRACE=trace
TAPES=tapes
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
NIGHT_DIR=$TRACE/report/nightly
OUT_MD=$TRACE/report/nightly_${STAMP}.md
PAR=${NIGHTLY_PAR:-6}
mkdir -p "$NIGHT_DIR"

GPU1_MB=$(nvidia-smi --id=1 --query-gpu=memory.used --format=csv,noheader,nounits 2>/dev/null | head -1)
if [ -n "${GPU1_MB:-}" ] && [ "$GPU1_MB" -gt 4096 ]; then
    echo "[nightly] GPU1 busy (${GPU1_MB} MiB used) - skipping run" | tee "$OUT_MD"
    exit 0
fi

# rebuild so the replay always tests HEAD (stale-object trap: clean game/)
( cd ../.. && rm -f game/*.o && make craster_game_cuda -j"$(nproc)" >/dev/null ) || {
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
    echo "# Nightly craster verify - $STAMP"
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
rc_all=0
for base in "${TAPE_LIST[@]}"; do
    rc=$(cat "$NIGHT_DIR/${base}.rc" 2>/dev/null || echo 99)
    night_json=$NIGHT_DIR/${base}.gate.json
    {
        echo "## $base (replay rc=$rc)"
        echo
        if [ -f "$night_json" ]; then
            uv run --no-project python "$TRACE/gate_baseline_diff.py" \
                --baseline "$TRACE/baselines/${base}.gate.json" \
                --current "$night_json" || rc_all=1
        elif [ "$rc" -eq 0 ]; then
            # replay passed but ran no pixel gate: tape has no usable goldens
            # (pre-convention rows or resolution-skipped frames, e.g. the 12k
            # human tape) - physics-divergence verify only, by design.
            echo "replay PASS, no pixel gate (no usable goldens) - see $NIGHT_DIR/${base}.log"
        else
            echo "no gate.json produced (rc=$rc) - see $NIGHT_DIR/${base}.log"; rc_all=1
        fi
        echo
    } >> "$OUT_MD"
    # rc=3 = gate found clusters, which the committed baseline already prices
    # in - gate_baseline_diff above is the regression arbiter. Anything else
    # above 1 is a crash/refusal and fails the sweep outright.
    { [ "$rc" -gt 1 ] && [ "$rc" -ne 3 ]; } && rc_all=1
done

echo "[nightly] report -> $OUT_MD"
exit $rc_all
