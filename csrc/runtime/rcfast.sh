#!/usr/bin/env bash
# fast's cast gate (lane/fastcast): engine.tick_scope fast skips the client's
# draw-side calls, cp_world_tick among them, and with it the client world's
# per-tick light check (session.c session_client_blocks), so its client world
# keeps light full's would have rechecked, and the ray cast (the trainer's
# observation) reads that light. Each recording is replayed twice by pool_gate
# with the cast after every row, full and then fast (its rows held to
# test_snapshots' but for its three declared fields, d.cseed, d.cw and
# d.cmath), and each row's cast digest compared (--raycast-rows): one line a
# recording, the rows, the casts that differ, the first and the longest run of
# consecutive ones; PASS when none does. (pipe_gate's frames are the device's
# against the C renderer of the same env, so they cannot see this.)
#
#   csrc/runtime/rcfast.sh [-s WxH] DIR NAME...
set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
spec=32x32
if [[ ${1:-} == -s ]]; then spec=$2; shift 2; fi
dir=$1; shift
n=$#
j=$(( n < 16 ? n : 16 ))
gate=out/runtime/pool_gate
rm -rf "$dir/full" "$dir/fast"
mkdir -p "$dir/full" "$dir/fast"
# the two runs at once (a recording's rows are one env's, so each run is its longest tape)
"$gate" -n $j -j $j --ticks 1 --episodes $n --raycast "$spec" --raycast-rows "$dir/full" "$@" > "$dir/full.log" 2>&1 &
full=$!
"$gate" -n $j -j $j --ticks 1 --episodes $n --fast --mask d.cseed,d.cw,d.cmath --raycast "$spec" \
    --raycast-rows "$dir/fast" "$@" > "$dir/fast.log" 2>&1
frc=$?
wait $full || { grep -E "FAIL" "$dir/full.log" | head -5; echo "rcfast: FAIL: the full run"; exit 1; }
[[ $frc -eq 0 ]] || { grep -E "FAIL" "$dir/fast.log" | head -5; echo "rcfast: FAIL: the fast run"; exit 1; }
bad=0 rows=0 diff=0
for r in "$@"; do
    b=${r##*/}
    line=$(paste "$dir/full/$b.rc" "$dir/fast/$b.rc" | awk -v r="$b" '
        $1 != $3 { printf "FAIL %s: row %s against %s\n", r, $1, $3; bad = 1; exit }
        $2 != $4 { n++; if (f == "") f = $1; run = (p == $1 - 1) ? run + 1 : 1; if (run > m) m = run; p = $1 }
        END { if (bad) exit 1
              printf "%s %s: %d rows, %d casts differ from full'\''s (first row %s, longest run %d)\n",
                     n ? "FAIL" : "PASS", r, NR, n, f == "" ? "-" : f, m }')
    echo "$line"
    [[ $line == PASS* ]] || bad=$((bad + 1))
    rows=$((rows + $(wc -l < "$dir/full/$b.rc")))
    diff=$((diff + $(echo "$line" | sed -nE 's/.* ([0-9]+) casts differ.*/\1/p')))
done
if [[ $bad -gt 0 ]]; then echo "rcfast: FAIL: $diff of $rows casts at $spec differ from full's, in $bad of $n recordings"; exit 1; fi
echo "rcfast: PASS: $n recordings, $rows rows at $spec, every cast equal to full's"
