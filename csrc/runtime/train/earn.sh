#!/usr/bin/env bash
# lane/armed: an EARNED START: a stage rollout's attempt (chain search) made into a library start the bot reached
# itself, through the Java client, the library's way (harvest.sh's layout, train/starts.c): not a dev start.
#
#   bash csrc/runtime/train/earn.sh -l LIB -i ID -a I -t BOUND -T "LEG TARGET ARGS" -P "PAST TARGET ARGS"
#        [-x "THE LEG'S OTHER CHAIN FLAGS"] [-m MOPTS] [-s 180000] [-r dragon_dead] [-p 400] [-q 200] [-b CHAIN]
#        [-g STAGE] [-k TRIES] [-K SKIP] RUN.yaml
#   make -C csrc/runtime chain-earn EARN_RUN=RUN.yaml EARN_ARGS='-l LIB -i ID -a I -t BOUND -T "..." -P "..." ...'
#
# 1. attempt I of the leg alone (--n 1, sampler seed SEED + I, RUN, MOPTS, -x and -T: the leg's own flags), its
#    script (--tape-attempt); it must end "reached", at N steps
# 2. the same attempt with the bound N + PAST (-p) and -P's target in place of -T's (one the attempt does not reach,
#    so it plays on past its arrival), as ka.sh does it: the script played on the Java client from the start's
#    checkpoint in the start's own world (CACHE=0 POOL=0), the recording replayed by the C engine, the last row held
#    against the attempt's (KA PASS required); the two scripts must be equal through step 1's (the target does not
#    shape the trajectory), so Java's rows through the arrival are the leg's attempt's
# 3. the save tick: the first quiet row at or after the arrival's row (N + 2) within -q ticks (ntstarts quiet: no
#    screen on the server or, as the script's acts show it, the client; on the ground); none is a FAIL. A checkpoint
#    whose manifest's quiescence is not all true (the rows carry no portal state: a Nether arrival still in the
#    portal) is made again from the next quiet row -K ticks on (20), at most -k times (4), within the -q window
# 4. LIB/ID/cp: the script cut there with a save (ntstarts cut), played by the oracle from the same checkpoint
# 5. LIB/ID/start: the oracle's Snapshot of the checkpoint's join (snap1.jsonl), replayed by the C engine (0 field
#    mismatches); LIB/ID/args (seed, world file, the checkpoint the attempt began at), prov, stage (-g, else none),
#    the cut script gzipped into cp/, the recordings deleted
# 6. one line: EARN OK or FAIL, the attempt, the arrival and save ticks, and what the start holds (ntstarts kit)
# Refused: a dev checkpoint (a WHAT-IF start is not earned), an attempt that does not reach, an existing LIB/ID.
set -u
cd "$(dirname "$0")/../../.."
ROOT=$PWD
bin=out/runtime/train/chain lib= id= att= bound= tgt= past_tgt= xargs_= mopts= seed=180000 rung=dragon_dead past=400
quiet=200 stage= TRIES=4 SKIP=20
while getopts l:i:a:t:T:P:x:m:s:r:p:q:b:g:k:K: o; do
  case $o in
    l) lib=$OPTARG ;; i) id=$OPTARG ;; a) att=$OPTARG ;; t) bound=$OPTARG ;; T) tgt=$OPTARG ;; P) past_tgt=$OPTARG ;;
    x) xargs_=$OPTARG ;; m) mopts=$OPTARG ;; s) seed=$OPTARG ;; r) rung=$OPTARG ;; p) past=$OPTARG ;; q) quiet=$OPTARG ;;
    b) bin=$OPTARG ;; g) stage=$OPTARG ;; k) TRIES=$OPTARG ;; K) SKIP=$OPTARG ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
run=${1:-}
refuse() { echo "EARN REFUSE: $*" >&2; exit 2; }
[ -n "$run" ] && [ -f "$run" ] && [ -n "$lib" ] && [ -n "$id" ] && [ -n "$att" ] && [ -n "$bound" ] && [ -n "$tgt" ] &&
  [ -n "$past_tgt" ] || refuse "usage: earn.sh -l LIB -i ID -a I -t BOUND -T TARGET -P PASTTARGET [flags] RUN.yaml"
run=$(realpath "$run")
[ -z "$mopts" ] || mopts=$(realpath "$mopts")
case $bin in /*) ;; *) bin=$ROOT/$bin ;; esac
mkdir -p "$lib" && lib=$(realpath "$lib")
D=$lib/$id
[ ! -e "$D" ] || refuse "$D exists"
key() { awk -v k="$1" '$1 == k ":" { print $2; exit }' "$2"; }
rseed=$(key seed "$run") start=$(key start "$run")
src=$(dirname "$start")
[ -d "$src/cp" ] || refuse "no checkpoint $src/cp beside the run file's start"
[ "$(jq -r '.dev // false' "$src/cp/manifest.json" 2>/dev/null)" != true ] || refuse "$src/cp is a dev checkpoint"
world=$(sed -n 2p "$src/args" 2>/dev/null)
[ -n "$world" ] && [ -f "$world" ] || refuse "no start world file ($src/args line 2)"
fail() { echo "EARN FAIL $id: $*"; exit 1; }
W=$D.work
rm -rf "$W"; mkdir -p "$W"
mo=()
if [ -n "$mopts" ]; then IFS=, read -r -a ol < "$mopts"; for x in "${ol[@]}"; do mo+=(--mopt "$x"); done; fi
# 1: the leg's attempt alone
seg=$(awk -v t="$bound" 'BEGIN { printf "%.6f", t / 20 }')
# shellcheck disable=SC2086
env CUDA_VISIBLE_DEVICES= "$bin" search --run "$run" --dir "$W/dir" --n 1 --groups 1 --seed $((seed + att)) \
  --seg "$seg" --target "$rung" --rollout-out "$W/row.tsv" --tape-attempt "0:$W/leg.jsonl" "${mo[@]}" $xargs_ $tgt \
  > "$W/leg.log" 2>&1 || fail "chain rc $? ($W/leg.log)"
rm -rf "$W/dir"
res=$(awk -F'\t' 'NR == 2 { print $6 }' "$W/row.tsv") N=$(awk -F'\t' 'NR == 2 { print $7 }' "$W/row.tsv")
[ "$res" = reached ] || fail "attempt $att ends $res at $N steps, not reached"
# 2: on past the arrival, on the Java client (ka.sh), the scripts equal through the arrival
bash csrc/runtime/train/ka.sh -b "$bin" -a "$att" -s "$seed" -t $((N + past)) -r "$rung" ${mopts:+-m "$mopts"} \
  -o "$W/ka" -x "$xargs_ $past_tgt" "$run" > "$W/ka.log" 2>&1
krc=$?
kline=$(grep -h 'KA ' "$W/ka.log" | tail -1)
[ $krc = 0 ] || fail "the known answer past the arrival: $kline"
n=$(wc -c < "$W/leg.jsonl")
cmp -s -n "$n" "$W/leg.jsonl" "$W/ka/script.jsonl" || fail "the script past the arrival differs from the leg's within its first $n bytes"
rows=$ROOT/out/java/try/ka-$(basename "$run" .yaml)-a$att/tape.jsonl
[ -s "$rows" ] || fail "no Java rows at $rows"
# 3: the save tick; 4: the checkpoint. The rows hold no portal or client ground state, so a checkpoint whose manifest's
# quiescence is not all true (a Nether arrival still in the portal) is made again at the next quiet row SKIP ticks on,
# at most TRIES times, all within -q ticks of the arrival
arrive=$(out/runtime/train/ntstarts kit "$rows" $((N + 2)))
from=$((N + 2)) tries=0 qz=
while :; do
  left=$((N + 2 + quiet - from))
  [ "$left" -gt 0 ] || fail "no quiet checkpoint within $quiet ticks of the arrival's row $((N + 2)) (last: $qz)"
  q=$(out/runtime/train/ntstarts quiet "$rows" "$from" --within "$left" --script "$W/ka/script.jsonl") ||
    fail "ntstarts quiet"
  T=$(echo "$q" | cut -f1)
  [ "$(echo "$q" | cut -f2)" = 1 ] || fail "no quiet row (no screen, on the ground) within $quiet ticks of the arrival's row $((N + 2))"
  rm -rf "$D"; mkdir -p "$D"
  out/runtime/train/ntstarts cut "$W/ka/script.jsonl" "$T" "$D/cut.jsonl" "$D/cp" > /dev/null || fail "ntstarts cut at $T"
  make -s -C oracle script SEED="$rseed" CONF="$world" SCRIPT="$D/cut.jsonl" CACHE=0 POOL=0 \
    TAPE="../out/java/try/earn-$id/tape.jsonl" FROM="$src/cp" > "$D/cp.log" 2>&1 || fail "cp: $(tail -1 "$D/cp.log")"
  rm -rf "out/java/try/earn-$id"
  [ -s "$D/cp/manifest.json" ] || fail "no checkpoint manifest after the save"
  [ "$(jq -r '.dev // false' "$D/cp/manifest.json")" != true ] || fail "the checkpoint says dev"
  qz=$(jq -c .quiescence "$D/cp/manifest.json")
  [ "$(jq -r '.quiescence | .guiClosed and .onGround and .noPortal and .noItemUse' "$D/cp/manifest.json")" = true ] && break
  tries=$((tries + 1))
  [ "$tries" -lt "$TRIES" ] || fail "the checkpoint is not quiet after $tries tries (row $T): $qz"
  from=$((T + SKIP))
done
rm -rf "$(dirname "$rows")"
# 5: the start
mkdir -p "$D/start"
make -s -C oracle script SEED="$rseed" CONF="$world" FROM="$D/cp" SCRIPT="$ROOT/csrc/runtime/train/snap1.jsonl" CACHE=0 \
  POOL=0 TAPE="$D/start/tape.jsonl" > "$D/start.log" 2>&1 || fail "start: $(tail -1 "$D/start.log")"
out/native/test_snapshots "$D/start" > "$D/start.c.log" 2>&1
trc=$?
mism=$(grep -o '[0-9]* field mismatches' "$D/start.c.log" | head -1 | awk '{print $1}')
srows=$(grep -o '[0-9]* rows simulated' "$D/start.c.log" | head -1 | awk '{print $1}')
[ $trc = 0 ] && [ "$mism" = 0 ] || fail "the start's C replay: rc $trc, $mism field mismatches ($D/start.c.log)"
rm -rf "$D/cp/pre" "$D/cp/tape.jsonl"; gzip -f "$D/cut.jsonl"; mv "$D/cut.jsonl.gz" "$D/cp/script.jsonl.gz"
printf '%s\n' "$rseed" "$world" "$src/cp" > "$D/args"
[ -z "$stage" ] || echo "$stage" > "$D/stage"
echo "earned $(basename "$run" .yaml) attempt $att (sampler seed $((seed + att))), reached at $N steps, saved at row $T" > "$D/prov"
kit=$(out/runtime/train/ntstarts kit "$D/start")
rm -rf "$W"
echo "EARN OK $id: $(cat "$D/prov"); start replayed by C: $srows rows, 0 field mismatches; $kline" | tr -s ' '
echo "  at the arrival: $arrive"
echo "  the start holds: $kit"
