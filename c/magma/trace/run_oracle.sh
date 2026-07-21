#!/usr/bin/env bash
# run_oracle.sh - the full tick-trace STATE-VECTOR flywheel, one command.
#
#   1. build the C tracer               (bash trace/build_c_tracer.sh)
#   2. gen a scripted action tape       (trace/gen_tape.py)
#   3. C side  -> c_phys.csv + c_state.jsonl + c_spawn.txt   (headless, no GPU)
#   4. Java side (if the qrl bridge is up on :25575): teleport to the C spawn pose, replay
#      the SAME tape -> java_phys.csv + java_state.jsonl
#   5. per-feature diff (trace/diff_trace.py) on the JSONL state vectors
#
# If the Java bridge is NOT up, step 4/5-Java are skipped and a SELF-DIFF (c vs copy == 0)
# proves the harness. To bring the game up first (anvil, headless :1):
#   cd java && setsid nohup bash start_vnc_client.sh >/tmp/mc_launch.out 2>&1 &
#   # wait for a TCP connect to 127.0.0.1:25575, then re-run this script.
set -uo pipefail
MAGMA=/home/infatoshi/dev/minecraft/mc-1.11.2-env/c/magma
cd "$MAGMA"
TICKS="${TICKS:-300}"
SEED="${SEED:-0}"
OUT=trace/out

echo "[1/5] build C tracer"; bash trace/build_c_tracer.sh >/tmp/trace_build.log 2>&1 || { echo FAIL; tail /tmp/trace_build.log; exit 1; }
echo "[2/5] gen tape ($TICKS ticks, seed $SEED)"
uv run --no-project python trace/gen_tape.py --ticks "$TICKS" --seed "$SEED" --out $OUT/tape.txt
echo "[3/5] C tracer"
./trace_game --tape $OUT/tape.txt --seed "$SEED" --out $OUT/c_phys.csv \
    --state $OUT/c_state.jsonl --spawn-out $OUT/c_spawn.txt --render 0

# is the qrl bridge up?
if timeout 2 bash -c 'exec 3<>/dev/tcp/127.0.0.1/25575' 2>/dev/null; then
    echo "[4/5] Java tracer (bridge up) -- spawn-aligned to C pose"
    # PLATFORM=N fills an NxN stone pad under the spawn so the Java player is GROUNDED at C's y
    # (the two worldgens differ at a shared column; without it Java free-falls + dies, cascading
    # into every feature). Keep N small: a big fill in one server tick can time the socket out.
    PLATFORM="${PLATFORM:-21}"
    uv run --no-project python trace/trace_java.py \
        --tape $OUT/tape.txt --seed "$SEED" \
        --out $OUT/java_phys.csv --state $OUT/java_state.jsonl \
        --spawn-file $OUT/c_spawn.txt --platform "$PLATFORM"
    echo "[5/5] PER-FEATURE STATE DIFF (java vs c)"
    uv run --no-project python trace/diff_trace.py \
        --java $OUT/java_state.jsonl --c $OUT/c_state.jsonl
else
    echo "[4/5] qrl bridge DOWN on 127.0.0.1:25575 -- skipping live Java run."
    echo "      launch: (cd java && setsid nohup bash start_vnc_client.sh >/tmp/mc_launch.out 2>&1 &)"
    echo "[5/5] SELF-DIFF (harness proof: c vs copy must be ZERO divergence)"
    cp $OUT/c_state.jsonl /tmp/c_selfcopy.jsonl
    uv run --no-project python trace/diff_trace.py \
        --java $OUT/c_state.jsonl --c /tmp/c_selfcopy.jsonl
fi
