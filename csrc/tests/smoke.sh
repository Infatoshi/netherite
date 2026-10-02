#!/usr/bin/env bash
# make smoke: the inner-loop check a refactor lane runs after each edit, the
# full suite (make test) staying the check before a commit and in the merge
# trial. It runs the globals and order checks and the jobs of tests/smoke.list
# (written from measurements by tests/smoke_select.c: every test binary,
# every (phase, state class) pair the phase check saw in any recording, then
# the jobs that add the most engine/ lines the full suite runs per second), all
# at once, each through tests/memslot.sh like make test's; with PLAYCHECK_ON=1
# also play_check.sh on the list's three shortest whole recordings.
#   bash tests/smoke.sh OUT DATA TESTJ PLAYCHECK_ON [LIST]    (from csrc/, after make)
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
out=$1 data=$2 testj=$3 pc=${4:-0} list=${5:-tests/smoke.list}
start=$(date +%s%N)
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
n=0
add() { n=$((n + 1)); printf '%s %s\n' "$n" "$*" >> "$t/order"; }
add - "$out/order_test"
add - "$out/scratch_check"
add - bash tests/globals.sh
grep -v '^#' "$list" | sed 's/ *#.*//' | awk NF > "$t/list"
while read -r x d flags; do
  if [ "$d" = - ]; then add - "$out/test_$x" $flags
  else add "$data/$d" "$out/test_$x" $flags "$data/$d"; fi
done < "$t/list"
if [ "$pc" = 1 ]; then
  awk '$1 == "snapshots" && NF == 2 {print $2}' "$t/list" | tail -3 | while read -r d; do
    add "$data/$d" bash tests/play_check.sh "$data/$d" "$out"; done
fi
# memslot.sh keys a job's memory by its command's basename and directory
# each job's running and waiting milliseconds and its start (ms after smoke's)
# go to OUT/smoke-times.txt
T=$t S=$start xargs -P "$testj" -L1 sh -c 'n=$1; d=$2; shift 2; s=$(date +%s%N)
  if [ "$d" = - ]; then "$@" > "$T/$n" 2>&1; else bash tests/memslot.sh --t "$T/$n.acq" "$d" "$@" > "$T/$n" 2>&1; fi
  echo $? > "$T/$n.rc"; e=$(date +%s%N); a=$(cat "$T/$n.acq" 2>/dev/null || echo $s)
  echo "$(( (e - a) / 1000000 )) $(( (a - s) / 1000000 )) $(( (s - S) / 1000000 )) $*" > "$T/$n.ms"' _ < "$t/order"
cat "$t"/*.ms 2>/dev/null | sort -rn > "$out/smoke-times.txt"
fail=0
for i in $(seq 1 $n); do
  [ "$(cat "$t/$i.rc" 2>/dev/null)" = 0 ] && continue
  fail=$((fail + 1)); job=$(grep "^$i " "$t/order" | cut -d' ' -f3-)
  cat "$t/$i"; echo "FAILED smoke job $job rc=$(cat "$t/$i.rc" 2>/dev/null)"; echo
done
secs=$(( ($(date +%s%N) - start) / 1000000 ))
echo "smoke: $((n - fail)) of $n jobs pass in $((secs / 1000)).$(( secs % 1000 / 100 )) s ($list; make test before a commit)"
exit $((fail > 0))
