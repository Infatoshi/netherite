#!/bin/bash
# raweval.sh (lane/eval1k): the frozen eval of a raw-skill model on about 1,000 held-out episodes, the same starts and
# the same sampler seeds for every model of a task, so two models compare start by start (rawstat pair). Called by
# `make -C csrc/runtime raweval` (raweval.mk); inside a GPU window: gpuq.sh check --mem 8000 -- make ... raweval
#
#   bash csrc/runtime/train/raweval.sh -w MODEL.ntw -t TASK -o OUT.tsv [-l LABEL] [-k student|teacher] [-a strip|van]
#        [-b BUILD] [-T TEMP] [-m MINUTES] [-K STARTS] [-R REPS] [-S STARTS_A_PROCESS] [-s SEED] [-L]
#
#   -t TASK  ore | orecave (rawpix's, 10 s) | hard | hardcave (rawtree's iron ore 5 to 12 blocks behind, 20 s) |
#            logview | logany | lognear (logs from a spawn, rawtree's 20 s run file for view, rawsearch's 60 s for the
#            rest) | logs3near | logs3view (three logs, 30 s); the held-out library of each is out/runtime/eval1k
#            (ore, made by rawstarts on world seeds 501 to 1000, never trained on) or out/runtime/rawtree (logs-*-held)
#   -k       student: pixels only (the ore scalars zeroed, drawn at temperature 0.5: ntloop --temp 0.5); teacher: its
#            scalars (and the run file's hint), sampled
#   -T       the draw's temperature over the kind's (ntloop --temp; 1 sampled; lane/audit-fixes: was the build's .tN)
#   -a       strip: the lanes' fidelity (no spawning, random ticks or weather); van: the run file without its
#            fidelity section, vanilla (mobs spawning)
#   -K -R    the first K starts of the library (512), each played R times (2: R episodes a start, N = K x R, 1,024);
#            a library with fewer than K starts is played whole, R raised to reach K x R >= 1,000. More starts beat more
#            plays of one start: the success of a start differs between starts far more than between its plays (the
#            intraclass correlation is about 0.5), so the standard error is sqrt((s_between^2 + s_within^2 / R) / K)
#   -S       starts a process loads (64); a process plays its starts R_PROC times (ns = S x R_PROC <= 256)
#   -L       no host-memory lock (the default holds ~/dev/nw/.tmp/gates-env.lock a process: the brief's rule for a job
#            over 4 GB)
# The start of env i is library start (i mod S) (ntloop --start-cycle on), its sampler stream the env index and
# --seed (SEED + the process number: one number, one stream), so a model meets the same starts and the same noise
# whatever it is. OUT.tsv: a row an episode (model, arm, shard, gstart, rep, env, success, deaths, steps); OUT.time: a
# row a process (starts, envs, load s, run s, env-steps, wall s). The last line: rawstat one OUT.tsv.
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
W= TASK= OUT= LABEL= KIND=student ARM=strip BUILD= TEMP= MIN= K=512 R=2 SC=64 SEED=7000 LOCK=1
while getopts w:t:o:l:k:a:b:T:m:K:R:S:s:L f; do
  case $f in
    w) W=$OPTARG;; t) TASK=$OPTARG;; o) OUT=$OPTARG;; l) LABEL=$OPTARG;; k) KIND=$OPTARG;; a) ARM=$OPTARG;;
    b) BUILD=$OPTARG;; T) TEMP=$OPTARG;; m) MIN=$OPTARG;; K) K=$OPTARG;; R) R=$OPTARG;; S) SC=$OPTARG;; s) SEED=$OPTARG;;
    L) LOCK=0;;
    *) exit 2;;
  esac
done
[ -f "$W" ] || { echo "raweval: model $W is missing" >&2; exit 2; }
[ -n "$OUT" ] || { echo "raweval: -o OUT.tsv" >&2; exit 2; }
EVL=out/runtime/eval1k RTL=out/runtime/rawtree
ITEM=15 NEED=1
case $TASK in
  ore)       RUN=configs/tests/rawpix64.yaml;        LIB=$EVL/ore-plain;  M=0.1667;;
  orecave)   RUN=configs/tests/rawpix64.yaml;        LIB=$EVL/ore-cave;   M=0.1667;;
  hard)      RUN=configs/tests/rawtree-ore64.yaml;   LIB=$EVL/hard-plain; M=0.3333;;
  hardcave)  RUN=configs/tests/rawtree-ore64.yaml;   LIB=$EVL/hard-cave;  M=0.3333;;
  logview)   RUN=configs/tests/rawtree-log64.yaml;   LIB=$RTL/logs-view-held; M=0.3333; ITEM=17;;
  logany)    RUN=configs/tests/rawsearch-log64.yaml; LIB=$RTL/logs-any-held;  M=1;      ITEM=17;;
  lognear)   RUN=configs/tests/rawsearch-log64.yaml; LIB=$RTL/logs-near-held; M=1;      ITEM=17;;
  logs3near) RUN=configs/tests/rawsearch-logs3.yaml; LIB=$RTL/logs-near-held; M=0.5;    ITEM=17; NEED=3;;
  logs3view) RUN=configs/tests/rawsearch-logs3.yaml; LIB=$RTL/logs-view-held; M=0.5;    ITEM=17; NEED=3;;
  *) echo "raweval: task $TASK" >&2; exit 2;;
esac
MIN=${MIN:-$M}
[ -n "$LABEL" ] || LABEL=$(basename "$W" .ntw)
BUILD=${BUILD:-s1.dk.or}
# lane/audit-fixes: the student's temperature is ntloop's --temp on the base build (was the s1.dk.or.t5 build)
[ -n "$TEMP" ] || { [ "$KIND" = teacher ] && TEMP=1 || TEMP=0.5; }
BIN=out/runtime/train/$BUILD/ntloop
[ -x "$BIN" ] || { echo "raweval: $BIN is not built (make -C csrc/runtime train-loop-v NT_V=$BUILD)" >&2; exit 2; }
ZERO=; [ "$KIND" = teacher ] || ZERO="--zero-sc ore"
if [ "$ARM" = van ]; then
  mkdir -p out/e/run
  RUNF=out/e/run/van-$(basename "$RUN")
  # the run file without its fidelity section: natural spawning, random ticks and weather as vanilla has them
  awk '/^  fidelity:/ { skip = 1; next } skip && /^    / { next } { skip = 0; print }' "$RUN" > "$RUNF"
else
  RUNF=$RUN
fi
NLIB=$(($(wc -l < "$LIB/index.tsv") - 1))
[ "$K" -gt "$NLIB" ] && K=$NLIB
[ $((K * R)) -lt 1000 ] && R=$(((1000 + K - 1) / K))
TMP=out/e/tmp/$$; mkdir -p "$TMP" "$(dirname "$OUT")"
trap 'rm -rf "$TMP"' EXIT
printf "model\tarm\tshard\tgstart\trep\tenv\tsuccess\tdeaths\tsteps\tcount\n" > "$OUT"
printf "shard\tstarts\tenvs\tload_s\trun_s\tenv_steps\twall_s\tlock_wait_s\trss_mb\n" > "$OUT.time"
t_all=$(date +%s.%N)
shard=0 off=0
while [ $off -lt "$K" ]; do
  S=$SC; [ $((off + S)) -gt "$K" ] && S=$((K - off))
  # a library of a few starts (the tree-in-view one) is cut so that one process has about 256 envs at most
  RP=$((256 / S)); [ "$RP" -gt "$R" ] && RP=$R; [ "$RP" -lt 1 ] && RP=1
  # a lone start chunk: split the reps into processes of RP each
  roff=0
  while [ $roff -lt "$R" ]; do
    r=$RP; [ $((roff + r)) -gt "$R" ] && r=$((R - roff))
    NS=$((S * r)); G=1; for g in 8 7 6 5 4 3 2; do [ $((NS % g)) -eq 0 ] && { G=$g; break; }; done
    D=$TMP/$shard; mkdir -p "$D/lib"
    { head -1 "$LIB/index.tsv"; tail -n +2 "$LIB/index.tsv" | sed -n "$((off + 1)),$((off + S))p"; } > "$D/lib/index.tsv"
    held0=$(awk -F'\t' 'NR == 2 {print $9}' "$D/lib/index.tsv")
    tq=$(date +%s.%N)
    LK=; [ "$LOCK" = 1 ] && { mkdir -p "${NETHER_CACHE:-.cache}/tmp"; LK="flock ${NETHER_CACHE:-.cache}/tmp/gates-env.lock"; }
    # t0 is written once the lock is held: another lane's job holds it for a minute at a time, and that wait is not the eval's
    $LK bash -c 'date +%s.%N > "$0"; exec /usr/bin/time -f %M -o "$0.rss" "$@"' "$D/start" "$BIN" --score --run "$RUNF" --dev --init "$W" --start "$held0" --library "$D/lib" --start-cycle on \
      --ns $NS --groups $G --minutes "$MIN" --seed $((SEED + shard)) --out "$D/out" --obs-tone auto --temp "$TEMP" $ZERO > "$D/eval.log" 2>&1 \
      || { tail -5 "$D/eval.log" >&2; echo "raweval: shard $shard failed" >&2; exit 1; }
    t1=$(date +%s.%N)
    t0=$(cat "$D/start")
    n=$(jq -s -r --arg item $ITEM --argjson need $NEED --argjson S "$S" --argjson off "$off" --argjson roff "$roff" \
      --arg model "$LABEL" --arg arm "$ARM" --arg shard "$shard" \
      'group_by(.env) | map(.[0]) | .[] | [$model, $arm, $shard, ($off + .start - 1), ($roff + (.env / $S | floor)), .env,
        (if (.items[$item] // 0) >= $need then 1 else 0 end), .deaths, .len, (.items[$item] // 0)] | @tsv' "$D/out/episodes.jsonl" | tee -a "$OUT" | wc -l)
    [ "$n" -eq "$NS" ] || { echo "raweval: shard $shard has $n first episodes for $NS envs" >&2; exit 1; }
    load=$(sed -n 's/.*made in \([0-9.]*\) s.*/\1/p' "$D/eval.log" | head -1)
    run=$(sed -n 's/.*env-steps in \([0-9.]*\) s).*/\1/p' "$D/eval.log" | head -1)
    steps=$(sed -n 's/^N [0-9]*, [0-9]* groups: [0-9]* env-steps\/s (\([0-9]*\) env-steps.*/\1/p' "$D/eval.log" | head -1)
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%.1f\t%.1f\t%d\n" $shard "$S" $NS "${load:-0}" "${run:-0}" "${steps:-0}" "$(echo "$t1 - $t0" | bc)" \
      "$(echo "$t0 - $tq" | bc)" "$(( $(cat "$D/start.rss") / 1024 ))" >> "$OUT.time"
    rm -rf "$D"
    shard=$((shard + 1)); roff=$((roff + r))
  done
  off=$((off + S))
done
t_end=$(date +%s.%N)
# w: the wall less the waits for the lock (another lane's job holds it a minute at a time)
awk -F'\t' -v w="$(echo "$t_end - $t_all" | bc)" 'NR > 1 { l += $4; r += $5; s += $6; n += $3; q += $8; if ($9 > m) m = $9 } END {
  w -= q
  printf "RAWEVAL-TIME %s: %d episodes in %.1f s (starts loaded %.1f s, env steps %.1f s for %d: %.0f env-steps/s, the rest %.1f s; lock wait %.1f s not counted; peak %d MB resident a process)\n",
    "'"$LABEL"'", n, w, l, r, s, (r > 0 ? s / r : 0), w - l - r, q, m }' "$OUT.time" | tee -a "$OUT.time"
"$ROOT/out/runtime/train/rawstat" one "$OUT"
