#!/usr/bin/env bash
# S6 on the device over many recordings (GPU plan L17), in batches: each
# recording's C phase trace (out/native/cuda/tick/test_snapshots without the
# device; kept under OUT/cuda/tick/traces, keyed by that build's hash, so an
# unchanged build traces nothing again), then the
# recordings in groups, each group one tick_batch process (cuda/tick/batch.c: its
# replays interleaved, S6 of all of them in one launch per step), one group
# at a time. A group holds at most GROUP recordings and at most BUDGET MB of
# their measured peaks (the memory budget's peaks file).
#   [PHASES=S6,...] bash cuda/tick/sweep.sh OUT GROUP BUDGET_MB NAME...
# Prints "NAME rc=N: ..." per recording (rc 0: every row exact and every
# row's S6 digests equal the trace's), then a total. Logs:
# OUT/cuda/tick/sweep.gK.txt per group.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2
out=$1 group=$2 budget=$3
shift 3
data=$(cd ../out/java/snapshots && pwd)
S=$out/cuda/tick
mkdir -p "$S/traces"
# the trace comes from the same host build the device run uses (C in both,
# one compiler): gcc's and clang's traces differ where a digest hashes an
# address outside the image (phase.c hashes client_out's bytes, which hold
# pointers into the harness's own session since lane/playfuzz)
hash=$(sha1sum "$S/test_snapshots" | cut -c1-16)
# the traces the C build has not made yet
for r in "$@"; do [ -s "$S/traces/$r.$hash.ref" ] || echo "$r"; done |
  xargs -r -P "${TESTJ:-8}" -I{} sh -c "bash tests/memslot.sh $data/{} $S/test_snapshots --phase-trace $S/traces/{}.$hash.tmp $data/{} \
    > $S/traces/{}.log 2>&1 && mv $S/traces/{}.$hash.tmp $S/traces/{}.$hash.ref || echo '{} rc=1 (the C trace; $S/traces/{}.log)'"
# the groups: biggest peaks first, first fit (every recording's peak in one
# pass over the peaks file: 871k lines, an awk per recording took minutes)
list=$(for r in "$@"; do [ -s "$S/traces/$r.$hash.ref" ] && echo "$r"; done |
  awk 'NR == FNR {want[$1] = 1; next} $2 == "test_snapshots" && ($3 in want) && $1 + 0 > m[$3] {m[$3] = $1 + 0}
       END {for (r in want) print (m[r] ? m[r] : 300), r}' - /tmp/netherite-mem/peaks 2>/dev/null | sort -rn)
declare -a gm gn gl
ng=0
while read -r mb r; do
  [ -n "$r" ] || continue
  placed=0
  for ((k = 0; k < ng; ++k)); do
    if [ "${gn[k]}" -lt "$group" ] && [ $((gm[k] + mb)) -le "$budget" ]; then
      gm[k]=$((gm[k] + mb)); gn[k]=$((gn[k] + 1)); gl[k]="${gl[k]} $r"; placed=1; break
    fi
  done
  [ $placed = 1 ] || { gm[ng]=$mb; gn[ng]=1; gl[ng]=$r; ng=$((ng + 1)); }
done <<< "$list"
group_run() {
  local k=$1 r first log brc
  local -a args=()
  local members=${gl[k]}
  for r in $members; do args+=("$data/$r" "$S/traces/$r.$hash.ref"); done
  log=$S/sweep.g$k.txt
  first=${members%% *}
  bash tests/memslot.sh --key tick_batch "$data/$first" "$S/tick_batch" ${PHASES:+--phases "$PHASES"} "${args[@]}" > "$log" 2>&1
  brc=$?
  for r in $members; do
    runs=$(grep -m1 "^phase-kernel cuda $r: " "$log" | sed "s/^phase-kernel cuda $r: //")
    dev=$(grep -m1 "^phase-kernel cuda $r per row: " "$log" | sed 's/.*device time ms \(mean [^;]*\);.*/\1/')
    if grep -q "^$data/$r: OK: " "$log" && grep -q "every row's digests equal the trace's ($S/traces/$r\.$hash\.ref)" "$log"; then
      echo "$r rc=0: $runs; device ms per row $dev; batch of ${gn[k]}"
    elif [ -z "${RETRY:-}" ] && grep -q "out of memory" "$log"; then
      # the device ran out of memory (another process shares it): again
      # alone at the end
      echo "$r" >> "$S/retry.list"
    else
      echo "$r rc=1: ${runs:-no summary}; $(grep -m1 -E "FAIL.*$r|$r.*FAIL" "$log" || grep -m1 -E 'FAIL|child|arena' "$log") (group rc $brc, $log)"
    fi
  done
}
rm -f "$S/retry.list"
# SWEEP_J groups at once (each process's context reserves the device stack
# for a full device, 4 GB at 32 KB a thread on the 3090, 8.7 GB on the GPU host's
# cards, besides its environments)
# the groups alternate from the largest and the smallest end, so the
# processes at once are not all large (each holds its environments'
# granules besides the stack reserve: a large recording alone is 7 GB)
running=0
lo=0 hi=$((ng - 1))
for ((i = 0; i < ng; ++i)); do
  if ((i % 2 == 0)); then k=$lo; lo=$((lo + 1)); else k=$hi; hi=$((hi - 1)); fi
  group_run "$k" &
  running=$((running + 1))
  if [ "$running" -ge "${SWEEP_J:-3}" ]; then wait -n; running=$((running - 1)); fi
done
wait
# the groups the device's memory refused: each recording alone, one at a time
if [ -s "$S/retry.list" ]; then
  RETRY=1
  while read -r r; do
    gl[ng]=$r; gn[ng]=1
    group_run "$ng" < /dev/null
    ng=$((ng + 1))
  done < "$S/retry.list"
fi
