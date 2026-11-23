#!/usr/bin/env bash
# Observation parity (lane/parity, GOAL.md "Done means" 4): the ray cast the policy sees (raycast.c's FP64 reference,
# raycast.h) drawn by the Java client from its own world, held to C's row for row. Each recording is replayed by
# the oracle with --raycast SIZE (RayCast.java: each row's d.rc, the FNV-1a 64 of its frame; a recording made with
# client.camera raycast at SIZE carries its own) and by pool_gate with --raycast SIZE --raycast-rows (C's cast of the
# same row, the same digest); one line a recording and size: the rows compared, or the first row whose digests
# differ. For that row both sides draw the frame and trace the cast again (the oracle's --raycast-dump DIR:@T,
# pool_gate's --raycast-dump DIR:SIZE:@T: the setup's reals as bits, the entity boxes, each pixel's walk as it ended)
# into DIR/diff-NAME-SIZE: the pixels that differ (pxdiff) and the trace's first differing lines. Last line
# RCPARITY PASS or FAIL.
#
#   csrc/runtime/rcparity.sh [-s "16x16 64x64"] [-j J] [-k KEEP] [-n] DIR NAME...
#     -n: no diagnosis of the failures (their first rows only)
#     -k KEEP: the oracle's digests of an earlier run (KEEP/j-SIZE/NAME.rc) where it has them, no replay: a C change
#     checked against an unchanged oracle in the time C takes
#   csrc/runtime/rcparity.sh -d SIZE DIR NAME:ROW...     the diagnosis alone, those rows (in parallel)
#   (make -C csrc/runtime raycast-parity [RPRECS="a b"] [RPSIZES="16x16 64x64"] [RPJ=8])
set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
sizes="16x16 64x64"
j=8
diag_only=
keep=
nodiag=
while [[ ${1:-} == -* ]]; do
    case $1 in
    -s) sizes=$2; shift 2 ;;
    -j) j=$2; shift 2 ;;
    -k) keep=$(cd "$2" && pwd); shift 2 ;;
    -n) nodiag=1; shift ;;
    -d) diag_only=1; sizes=$2; shift 2 ;;
    *) echo "rcparity: unknown option $1"; exit 2 ;;
    esac
done
dir=$(mkdir -p "$1" && cd "$1" && pwd); shift
gate=out/runtime/pool_gate

# diag NAME SIZE ROW: both frames and traces of row ROW, the differing pixels and the traces' first differing lines
diag() {
    local r=$1 s=$2 t=$3 d=$dir/diff-$1-$2
    rm -rf "$d" && mkdir -p "$d"
    POOL=0 CACHE=0 make -s -C oracle replay REF=../out/java/snapshots/$r/tape.jsonl ARGS="--raycast $s --raycast-dump $d:@$t --until $((t + 1))" > "$d/oracle.log" 2>&1
    "$gate" -n 1 -j 1 --ticks 1 --episodes 1 --raycast-dump "$d:$s:@$t" "$r" > "$d/c.log" 2>&1
    local a b ta tb
    a=$(printf "%s/r_%06d.png" "$d" "$t") b=$(printf "%s/%s.r_%06d.png" "$d" "$r" "$t")
    ta=${a%.png}.txt tb=${b%.png}.txt
    if [[ ! -s $ta || ! -s $tb ]]; then
        echo "  $r $s row $t: not traced ($d)"
        return
    fi
    diff "$ta" "$tb" > "$d/trace.diff"
    out/native/pxdiff stats --a "$a" --b "$b" > "$d/stats.json" 2>&1
    echo "  $r $s row $t: $(grep -o '"exact_pixels": [0-9]*' "$d/stats.json") of $(( ${s%x*} * ${s#*x} )); the traces (< oracle, > C, $d/trace.diff):"
    grep -m 8 '^[<>]' "$d/trace.diff" | cut -c1-200 | sed 's/^/    /'
}

if [[ -n $diag_only ]]; then
    for a in "$@"; do diag "${a%%:*}" "$sizes" "${a##*:}" > "$dir/diag-${a%%:*}.out" 2>&1 & done
    wait
    for a in "$@"; do cat "$dir/diag-${a%%:*}.out"; done
    exit 0
fi

n=$#
gj=$(( n < 16 ? n : 16 ))
bad=0
for s in $sizes; do
    rm -rf "$dir/c-$s" "$dir/j-$s"
    mkdir -p "$dir/c-$s" "$dir/j-$s"
    # C's digests, every recording in one pool (beside the oracle's replays)
    "$gate" -n $gj -j $gj --ticks 1 --episodes $n --raycast "$s" --raycast-rows "$dir/c-$s" "$@" > "$dir/c-$s.log" 2>&1 &
    cpid=$!
    # the oracle's: a recording made with this cast (client.camera raycast: its header's options.raycast) carries
    # its d.rc already; the others a replay each, J at a time, each tape's d.rc kept as "t digest". Each in a fresh
    # JVM, not the cache's or a warm pool member's: a pool member replayed gold-g15-s42 with other client light on
    # 1,549 rows than a fresh JVM, every row field equal (divergences/parity.tsv parity-poollight)
    replay=()
    for r in "$@"; do
        tape=out/java/snapshots/$r/tape.jsonl
        if [[ -n $keep && -s $keep/j-$s/$r.rc ]]; then
            cp "$keep/j-$s/$r.rc" "$dir/j-$s/$r.rc"
        elif [[ $(head -1 "$tape" | jq -r '.options.raycast // ""') == "$s" ]]; then
            jq -r 'select(.d.rc) | "\(.t) \(.d.rc)"' "$tape" > "$dir/j-$s/$r.rc"
        else
            replay+=("$r")
        fi
    done
    [[ ${#replay[@]} -gt 0 ]] && out/native/par -j "$j" -g 'ORACLE (REPLAY|DONE|DIVERGE)' \
        "POOL=0 CACHE=0 make -s -C oracle replay REF=../out/java/snapshots/{}/tape.jsonl ARGS='--raycast $s' > $dir/j-$s/{}.log 2>&1; rc=\$?; \
         jq -r 'select(.d.rc) | \"\\(.t) \\(.d.rc)\"' out/java/run/{}-tape.replay/replay.jsonl > $dir/j-$s/{}.rc; tail -2 $dir/j-$s/{}.log; exit \$rc" \
        "${replay[@]}" > "$dir/j-$s.log" 2>&1
    wait $cpid || { grep -E "FAIL" "$dir/c-$s.log" | head -5; echo "rcparity: FAIL: pool_gate at $s ($dir/c-$s.log)"; bad=1; }
    failed=()
    for r in "$@"; do
        c=$dir/c-$s/$r.rc jf=$dir/j-$s/$r.rc
        if [[ ! -s $c || ! -s $jf ]]; then
            echo "FAIL $r $s: no digests (C $(wc -l < "$c" 2>/dev/null || echo 0) rows, oracle $(wc -l < "$jf" 2>/dev/null || echo 0); $dir/j-$s/$r.log)"
            bad=1
            continue
        fi
        line=$(awk -v r="$r" -v s="$s" 'NR == FNR { c[$1] = $2; next }
            ($1 in c) { n++; if (c[$1] != $2) { d++; if (f == "") { f = $1; fc = c[$1]; fj = $2 } } delete c[$1] }
            END { m = 0; for (k in c) m++
                  if (d) printf "FAIL %s %s: %d of %d rows differ, first row %s: oracle %s, C %s\n", r, s, d, n, f, fj, fc
                  else if (m) printf "FAIL %s %s: %d rows equal, %d of C'"'"'s rows the oracle did not cast\n", r, s, n, m
                  else printf "%s %s: OK %d rows equal\n", r, s, n }' "$c" "$jf")
        echo "$line"
        if [[ $line == FAIL*"first row"* ]]; then
            bad=1
            failed+=("$r:$(sed 's/.*first row \([0-9]*\):.*/\1/' <<< "$line")")
        fi
    done
    # the failures' first rows, J at a time (-n: none)
    [[ -n $nodiag ]] && failed=()
    for ((i = 0; i < ${#failed[@]}; i += j)); do
        for a in "${failed[@]:i:j}"; do diag "${a%%:*}" "$s" "${a##*:}" > "$dir/diag-${a%%:*}-$s.out" 2>&1 & done
        wait
    done
    for a in "${failed[@]}"; do cat "$dir/diag-${a%%:*}-$s.out"; done
done
[[ $bad == 0 ]] && echo "RCPARITY PASS" || echo "RCPARITY FAIL"
exit $bad
