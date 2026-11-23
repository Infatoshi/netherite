#!/usr/bin/env bash
# The seed-42 golden chain played on the native client and checked on Java.
# For each segment gold-gN-s42 (G1 from the seed, GN from checkpoint 42/G<N-1>,
# the start its snapshot holds): out/native/play starts on the segment's
# snapshot, takes only the act stream of the segment's tape (its t and act
# fields, nothing the oracle computed) through play's tick path and writes its
# own tape; check.sh then has the Java oracle replay that tape's inputs and the
# native whole-server check compare every row of Java's replay. A segment lined
# up when that check has 0 field mismatches and Java's rows carry the golden
# recording's state digests at every tick.
#   csrc/play/chain.sh [--seed 1] [N ...]    (default 1 to 30; J=2 segments at once)
# --seed 1: the seed-1 chain, s1chain-SN (default 1 to 23).
# Tapes and logs: out/native/tapes/chain-gold-gN-s42.* (chain-s1chain-SN.*). Each job takes its
# memory from csrc/tests/memslot.sh's budget (play here, test_snapshots in
# check.sh, the oracle JVM in oracle/tests/headless.sh), never around a job that
# reserves again inside: a held outer reservation starves the inner one.
set -uo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"

seed=42
[[ ${1:-} == --seed ]] && { seed=$2; shift 2; }
chainname() { if [[ $seed == 1 ]]; then echo "s1chain-S$1"; else echo "gold-g$1-s42"; fi; }

if [[ ${1:-} == --one ]]; then
    n=$2
    name=$(chainname "$n")
    snap=out/java/snapshots/$name
    base=out/native/tapes/chain-$name
    inputs=$base.in.jsonl
    tape=$base.jsonl
    log=$base.log
    : > "$log"
    [[ -s $snap/tape.jsonl ]] || { echo "$name FAIL no snapshot at $snap"; exit 1; }
    t0=$(date +%s.%N)
    { printf '{"inputs":"%s"}\n' "$name"; jq -c 'select(.act) | {t, act}' "$snap/tape.jsonl"; } > "$inputs"
    bash csrc/tests/memslot.sh "$snap" out/native/play "$snap" --replay "$inputs" --tape "$tape" >> "$log" 2>&1 \
        || { echo "$name FAIL play (see $log)"; exit 1; }
    played=$(grep -o '[0-9]* ticks played' "$log" | cut -d' ' -f1)
    bash csrc/play/check.sh "$tape" "$snap" >> "$log" 2>&1
    rc=$?
    java=$(grep -o '^java rows: [0-9]*' "$log" | cut -d' ' -f3)
    line=$(grep '^replay: .* rows simulated' "$log" | tail -1)
    rows=$(sed -E 's/^replay: ([0-9]+) rows.*/\1/' <<< "$line")
    mism=$(sed -E 's/.*, ([0-9]+) field mismatch.*/\1/' <<< "$line")
    # Java's rows from the native tape against the golden recording's: the
    # same inputs are the same game, so every tick's state digests (d) and
    # counts (w) match; fields added to rows since the recording are left out.
    # This catches an input play applied one way and wrote another (the check
    # above replays Java's own reading of the tape on both sides).
    run=out/java/run/tapes-chain-$name.replay/replay.jsonl
    same=differ
    cmp -s <(jq -c '{t, w, d}' "$run" 2>/dev/null) <(jq -c '{t, w, d}' "$snap/tape.jsonl") && same=same
    secs=$(awk -v a="$t0" -v b="$(date +%s.%N)" 'BEGIN { printf "%.0f", b - a }')
    if [[ $rc == 0 ]] && grep -q ': OK: ' "$log" && [[ $mism == 0 ]] && [[ $same == same ]]; then
        printf '%-13s LINED UP  played %6s  java rows %6s  native check %6s rows, 0 mismatches  digests as the golden  %ss\n' \
            "$name" "$played" "$java" "$rows" "$secs"
    else
        printf '%-13s FAIL      played %6s  java rows %6s  native check: %s  golden digests: %s  (see %s)\n' \
            "$name" "${played:-?}" "${java:-?}" "$(grep -a 'FAIL\|mismatch\|ORACLE DIVERGE' "$log" | head -1)" "$same" "$log"
        exit 1
    fi
    exit 0
fi

segs=("$@")
[[ ${#segs[@]} == 0 ]] && segs=($(seq 1 $([[ $seed == 1 ]] && echo 23 || echo 30)))
make -s -C csrc all play >/dev/null || { echo "native build failed"; exit 1; }
make -s -C oracle build >/dev/null || { echo "java build failed"; exit 1; }
mkdir -p out/native/tapes
t0=$(date +%s)
out=$(printf '%s\n' "${segs[@]}" | xargs -P "${J:-2}" -I{} bash csrc/play/chain.sh --seed "$seed" --one {})
sort -V <<< "$out"
ok=$(grep -c ' LINED UP ' <<< "$out")
rows=$(awk '/ LINED UP / { s += $11 } END { print s + 0 }' <<< "$out")
echo "chain: $ok of ${#segs[@]} segments lined up, $rows rows checked with 0 mismatches, $(( $(date +%s) - t0 )) s"
[[ $ok == "${#segs[@]}" ]]
