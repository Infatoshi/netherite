#!/bin/bash
# rawskill_sum.sh OUT T0 T1 T2 (lane/rawskill; make -C csrc/runtime rawskill runs it): the loop's lines from OUT's logs
# and episodes. T0 the start, T1 the training's end, T2 the eval's end (date +%s.%N).
#   RAWSKILL-TIME  the wall's parts: train = start (the process, CUDA, the net) + starts (the make: the starts loaded)
#                  + steps (the measured window: env steps with the learner beside them) + end (saves, exit); eval the
#                  same three for the frozen run
#   RAWSKILL-CURVE the training episodes in tenths of the run: env-steps at the tenth's end, wall seconds into the
#                  window, the share that picked the ore up
#   RAWSKILL       held-out: each eval env's first episode (one random held-out start each): the success share, the
#                  median and mean steps to the pickup, deaths a run, the episodes counted
set -e
out=$1 t0=$2 t1=$3 t2=$4
item=${RS_ITEM:-15}
made() { sed -n 's/.*made in \([0-9.]*\) s.*/\1/p' "$1" | head -1; }
win() { sed -n 's/.*env-steps in \([0-9.]*\) s).*/\1/p' "$1" | head -1; }
rate() { sed -n 's/.* \([0-9]*\) env-steps\/s (.*/\1/p' "$1" | head -1; }
tm=$(made "$out/train.log"); tw=$(win "$out/train.log"); em=$(made "$out/eval.log"); ew=$(win "$out/eval.log")
awk -v t0="$t0" -v t1="$t1" -v t2="$t2" -v tm="${tm:-0}" -v tw="${tw:-0}" -v em="${em:-0}" -v ew="${ew:-0}" \
    -v r="$(rate "$out/train.log")" 'BEGIN {
  tr = t1 - t0; ev = t2 - t1
  printf "RAWSKILL-TIME total %.2f s: train %.2f (start+end %.2f, starts %.2f, steps %.2f at %s env-steps/s), eval %.2f (start+end %.2f, starts %.2f, steps %.2f)\n",
    t2 - t0, tr, tr - tm - tw, tm, tw, r, ev, ev - em - ew, em, ew }'
jq -r --arg it "$item" '[.env_steps, .t, ((.items[$it] // 0) > 0 | if . then 1 else 0 end)] | @tsv' \
    "$out/episodes.jsonl" | awk -F'\t' '{ s[NR] = $1; t[NR] = $2; w[NR] = $3 } END {
  if (NR == 0) { print "RAWSKILL-CURVE no episodes"; exit }
  printf "RAWSKILL-CURVE"
  for (k = 1; k <= 10; ++k) {
    lo = int((k - 1) * NR / 10) + 1; hi = int(k * NR / 10); n = 0; ok = 0
    for (i = lo; i <= hi; ++i) { n++; ok += w[i] }
    printf " %.2fM:%.1fs:%.3f", s[hi] / 1e6, t[hi] - t[1], n ? ok / n : 0
  }
  printf " (%d episodes)\n", NR }'
if ls "$out"/ckpt-*.ntw.eval/episodes.jsonl > /dev/null 2>&1; then
  # RAWSKILL-HELDOUT: each checkpoint's held-out success (env-steps from its name: ckpt-NNNNNM, M a million)
  printf "RAWSKILL-HELDOUT"
  for f in "$out"/ckpt-*.ntw.eval/episodes.jsonl; do
    m=$(basename "$(dirname "$f")"); m=${m#ckpt-}; m=${m%%.ntw.eval}
    jq -s -r --arg it "$item" --arg m "$m" 'group_by(.env) | map(.[0]) |
      "\($m):\((map(select((.items[$it] // 0) > 0)) | length) / length * 1000 | round / 1000)"' "$f" | tr '\n' ' '
  done
  echo
fi
jq -s -r --arg it "$item" 'group_by(.env) | map(.[0]) |
  [length, (map(select((.items[$it] // 0) > 0)) | length), (map(.deaths) | add),
   (map(select((.items[$it] // 0) > 0) | .len) | sort | if length > 0 then .[length / 2 | floor] else -1 end),
   (map(select((.items[$it] // 0) > 0) | .len) | if length > 0 then add / length else -1 end)] | @tsv' \
    "$out/${RS_EVAL:-eval}/episodes.jsonl" | awk -F'\t' '{
  printf "RAWSKILL held-out success %.3f (%d of %d), steps to the pickup median %d mean %.1f, deaths %.3f a run\n",
    $1 ? $2 / $1 : 0, $2, $1, $4, $5, $1 ? $3 / $1 : 0 }'
