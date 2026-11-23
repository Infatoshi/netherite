#!/usr/bin/env bash
# lane/proom: an attempt is the same attempt alone and in a batch of any size (chain search's rollouts: every attempt
# from one start, attempt I seeded SEED + I). For each run file: N attempts (64), batches of each size K (4 and 32) and
# attempts I... alone (--n 1 --seed SEED+I), TICKS ticks each, every step traced (--shold-trace: place, look, health,
# the owner, the macros' state), attempt I's trace in each against the batch of N's row for row; one line a (run,
# attempt, size), ALONE-GATE PASS or FAIL last.
#
#   bash csrc/runtime/train/alone_gate.sh [-b CHAIN] [-n 64] [-k "4 32"] [-t 400] [-a "3 17 40"] [-s 180000] [-m MOPTS]
#        [-x "EXTRA CHAIN ARGS"] [-T shold|nether] RUN.yaml...
#   -T nether (lane/ghast): every step's --nether-trace instead of --shold-trace, which writes no row in the Nether
#   -T end (lane/dleg): every End step's --end-trace with --end-trace-hunt 1 (the crystal hunt's state), for an End start
#   make -C csrc/runtime chain-alone-gate AG_RUNS="A.yaml B.yaml C.yaml" [AG_ARGS='-m FILE -t 400']
#
# EXTRA: {} in it is each run's own file prefix (lane/pdig: -x '--dig-log {}.dig.tsv', a log file a run).
# MOPTS: a file of one line, the macro options comma separated (each passed as --mopt). The rollout flags are the
# stage legs' (--start-latched 1, --target dragon_dead, --toward locate, the end, death and census logs). Found by
# lane/proom: the census switch (neth_rl_far_census) turned on lazily by the first env's books gave s42 blaze_powder
# attempt 3 one trajectory at 1, 8, 16 and 64 attempts and another at 4 and 32 (f2d7ab5d); a batch size counts only
# where the attempt is in it (I < K).
set -u
cd "$(dirname "$0")/../../.."
bin=out/runtime/train/chain n=64 sizes="4 32" ticks=400 atts="3 17 40" seed=180000 mopts=
xargs_= trace=shold
while getopts b:n:k:t:a:s:m:x:T: o; do
  case $o in
    b) bin=$OPTARG ;; n) n=$OPTARG ;; k) sizes=$OPTARG ;; t) ticks=$OPTARG ;; a) atts=$OPTARG ;; s) seed=$OPTARG ;;
    m) mopts=$OPTARG ;; x) xargs_=$OPTARG ;; T) trace=$OPTARG ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || { echo "alone_gate: no run file" >&2; exit 2; }
mo=()
if [ -n "$mopts" ]; then
  IFS=, read -r -a ol < "$mopts"
  for x in "${ol[@]}"; do mo+=(--mopt "$x"); done
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/alone_gate.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
seg=$(awk -v t="$ticks" 'BEGIN {printf "%.6f", t / 20}')
run1() {  # RUN NAME N SEED TRACE-ATTEMPT
  CUDA_VISIBLE_DEVICES= "$bin" search --run "$1" --dir "$tmp/$2" --n "$3" --groups "$3" --threads 12 --seed "$4" \
    --seg "$seg" --target dragon_dead --rollout-out "$tmp/$2.tsv" --start-latched 1 --toward locate \
    --end-log "$tmp/$2.end.tsv" --death-log "$tmp/$2.deaths.tsv" --owner-census "$tmp/$2.census" \
    $(case $trace in nether) echo --nether-trace "$5:$tmp/$2.sh.tsv" ;;
        end) echo --end-trace "$5:$tmp/$2.sh.tsv" --end-trace-hunt 1 ;; *) echo --shold-trace "$5/1:$tmp/$2.sh.tsv" ;; esac) \
    ${xargs_//\{\}/$tmp/$2} "${mo[@]}" > "$tmp/$2.log" 2>&1
}
acol=2; [ "$trace" = end ] && acol=13   # the end trace's attempt is its column 13
cmp1() {  # BATCH-TRACE ATTEMPT-IN-IT OTHER-TRACE ATTEMPT-IN-IT: column 1 the step, acol the attempt (blanked)
  awk -F'\t' -v a="$2" -v c="$4" -v ac="$acol" '
    NR == FNR { if (FNR > 1 && $ac == a) { k = $1; $ac = ""; B[k] = $0; nb++ } next }
    FNR > 1 && $ac == c { k = $1; $ac = ""; na++; if (!(k in B)) { if (!miss) miss = k; next } if (B[k] != $0 && !first) first = k }
    END { if (first) printf "FAIL first differing step %s", first; else if (miss || na != nb || !na) printf "FAIL steps %d here, %d in the batch", na, nb; else printf "EQUAL %d steps", na }' \
    "$1" "$3"
}
fail=0
k=0
for run in "$@"; do
  k=$((k + 1))
  if ! run1 "$run" b$k "$n" "$seed" -1; then echo "$run: FAIL the batch ($(tail -1 "$tmp/b$k.log"))"; fail=1; continue; fi
  for K in $sizes; do
    if ! run1 "$run" b$k-$K "$K" "$seed" -1; then echo "$run: FAIL the batch of $K ($(tail -1 "$tmp/b$k-$K.log"))"; fail=1; fi
  done
  for a in $atts; do
    if ! run1 "$run" a$k-$a 1 $((seed + a)) 0; then echo "$run attempt $a: FAIL alone ($(tail -1 "$tmp/a$k-$a.log"))"; fail=1; continue; fi
    r=$(cmp1 "$tmp/b$k.sh.tsv" "$a" "$tmp/a$k-$a.sh.tsv" 0)
    echo "$run attempt $a alone: $r"
    case $r in EQUAL*) ;; *) fail=1 ;; esac
    for K in $sizes; do
      [ "$a" -lt "$K" ] && [ -e "$tmp/b$k-$K.sh.tsv" ] || continue
      r=$(cmp1 "$tmp/b$k.sh.tsv" "$a" "$tmp/b$k-$K.sh.tsv" "$a")
      echo "$run attempt $a in $K: $r"
      case $r in EQUAL*) ;; *) fail=1 ;; esac
    done
  done
done
[ $fail = 0 ] && echo "ALONE-GATE PASS" || echo "ALONE-GATE FAIL"
exit $fail
