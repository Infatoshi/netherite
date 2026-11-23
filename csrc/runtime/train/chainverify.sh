#!/bin/bash
# chainverify.sh RUN DIR NODE OUT [LIBDIR]  (lane/chain): a search node's chain proved row for row against the Java client.
#   1. chain stitch: the chain as the oracle's script and the run file's world section (OUT/script.jsonl, OUT/world.yaml)
#   2. the oracle plays it (make -C oracle script, CACHE=0) into OUT/java (its tape and end snapshot)
#   3. test_snapshots replays Java's recording in the C engine (every field of every row) and dumps Java's rows
#   4. chain check --rows: the chain played again by libneth (each seam's state against the search's), its rows
#   5. the two row files compared line for line (the entity digest d.ents included); the first differing row named
# Last line: VERIFY EQUAL NODE ROWS or VERIFY DIFF NODE at t=T (FIELD) or VERIFY FAIL NODE (step). LIBDIR is the
# libneth directory the search ran with (LD_LIBRARY_PATH; the default out/runtime). FROM, a checkpoint directory: for a
# run file whose pipeline.start was made from that checkpoint (one Snapshot step after its join: the stitched script
# opens the same way), the oracle plays the script from it (make script FROM=), the second part of a proof whose first
# is the checkpoint's own chain.
# CHAIN_VERIFY_BIN and CHAIN_TEST_SNAPSHOTS_BIN select separate builds for a fixed-engine proof; defaults unchanged.
set -u
run=$1 dir=$2 node=$3 out=$4 lib=${5:-} from=${6:-}
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 2
mkdir -p "$out"
out=$(cd "$out" && pwd)
fail() { echo "VERIFY FAIL $node ($1)"; exit 1; }
test -f "$run" || fail "run file missing: $run"
run_from=$(awk '$1 == "#" && $2 == "oracle_from:" {print $3; exit}' "$run")
run_start=$(awk '$1 == "start:" {print $2; exit}' "$run")
if [ -n "$run_from" ]; then
    case $run_from in /*) ;; *) fail "run file oracle_from must be absolute: $run_from" ;; esac
    run_from=$(realpath -e "$run_from") || fail "run file oracle_from missing: $run_from"
    if [ -n "$from" ]; then
        from=$(realpath -e "$from") || fail "FROM missing: $from"
        [ "$from" = "$run_from" ] || fail "FROM differs from run file oracle_from: $from"
    fi
    from=$run_from
elif [[ $run_start == */out/chain/starts/*/start ]]; then
    fail "checkpoint-derived pipeline.start has no # oracle_from in the run file"
fi
if [ -n "$from" ]; then
    from=$(realpath -e "$from") || fail "FROM missing: $from"
    [ -f "$from/manifest.json" ] && [ -f "$from/save/level.dat" ] ||
        fail "FROM is not a complete checkpoint: $from"
fi
ch=${CHAIN_VERIFY_BIN:-out/runtime/train/chain}
[ -z "${CHAIN_VERIFY_BIN:-}" ] && [ -n "$lib" ] && [ -x "$lib/chain" ] && ch=$lib/chain  # a variant may add a rung
[ -n "$lib" ] && export LD_LIBRARY_PATH=$lib
snapbin=${CHAIN_TEST_SNAPSHOTS_BIN:-out/native/test_snapshots}
[ -x "$snapbin" ] || fail "native replay missing: $snapbin"
export CUDA_VISIBLE_DEVICES=
$ch stitch "$dir" "$node" -o "$out/script.jsonl" --run "$run" --world "$out/world.yaml" > "$out/stitch.log" 2>&1 ||
    fail "stitch: $(tail -1 "$out/stitch.log")"
seed=$(awk '$1 == "seed:" {print $2; exit}' "$out/world.yaml")
rm -rf "$out/java"
make -s -C oracle script SEED="$seed" CONF="$out/world.yaml" SCRIPT="$out/script.jsonl" TAPE="$out/java/tape.jsonl" \
    ${from:+FROM="$from"} CACHE=0 > "$out/java.log" 2>&1 || fail "oracle: $(tail -1 "$out/java.log")"
if ! "$snapbin" --row-dump "$out/java.rows.all" "$out/java" > "$out/replay.log" 2>&1; then
    # the C engine's replay of Java's recording differs: the first differing row and field (out/native/diverge NAME ROW
    # has the rest)
    d=$(grep -o 'first differing row [0-9]* field [^ ]* want [^ ]* got [^ ]*' "$out/replay.log" | head -1)
    [ -n "$d" ] || fail "test_snapshots: $(tail -1 "$out/replay.log")"
    echo "VERIFY DIFF $node at row $(awk '{print $4}' <<< "$d") (the C engine's replay of Java's rows: ${d#first differing row })"
    exit 1
fi
$ch check --run "$run" "$dir" "$node" --rows "$out/sim.rows" > "$out/check.log" 2>&1 ||
    fail "check: $(tail -1 "$out/check.log")"
grep -v '^end ' "$out/java.rows.all" > "$out/java.rows"
n=$(wc -l < "$out/java.rows")
if cmp -s "$out/sim.rows" "$out/java.rows" && [ "$n" -gt 0 ]; then
    echo "VERIFY EQUAL $node $n rows (the sim's rows equal the Java client's, every field; C's replay of Java: $(tail -1 "$out/replay.log" | cut -c1-80))"
    exit 0
fi
# the first differing row and its first differing field
line=$(cmp "$out/sim.rows" "$out/java.rows" 2>/dev/null | awk '{print $NF}')
[ -n "$line" ] || line=$(( $(wc -l < "$out/sim.rows") + 1 ))
a=$(sed -n "${line}p" "$out/sim.rows") b=$(sed -n "${line}p" "$out/java.rows")
field=$(paste <(tr ' ' '\n' <<< "$a") <(tr ' ' '\n' <<< "$b") | awk -F'\t' '$1 != $2 {print $1 " vs " $2; exit}')
echo "VERIFY DIFF $node at row $line ${a%% *}: ${field:-length} (sim $(wc -l < "$out/sim.rows") rows, java $n)"
exit 1
