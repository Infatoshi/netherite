#!/usr/bin/env bash
# lane/dleg: the known-answer check of a stage rollout's attempt (chain search): attempt I rerun alone (--n 1, sampler
# seed SEED + I) with --tape-attempt, its script played on the Java client from the start's checkpoint in the start's
# own world, the oracle's recording replayed by the C engine (test_snapshots: every row and field), and the Java
# client's last row held against the attempt's: one line, KA PASS or KA FAIL, exit 0 or 1 (2: refused).
#
#   bash csrc/runtime/train/ka.sh [-b CHAIN] -a I [-s 180000] [-t 60000] [-r dragon_dead] [-m MOPTS] [-w WORLD]
#        [-o DIR] [-x "EXTRA CHAIN ARGS"] RUN.yaml
#   make -C csrc/runtime chain-ka KA_RUN=RUN.yaml KA_ATTEMPT=I [KA_ARGS='-m FILE -x "..."']
#
# RUN.yaml: the leg's run file; its sim.pipeline.start is a library start (LIB/ID/start), whose checkpoint LIB/ID/cp the
# Java client starts from and whose world file (LIB/ID/args, line 2: the world.yaml the start was harvested in) the
# oracle plays (CONF). -w names the world file instead. Refused: no world file, or its difficulty is not the run
# file's. The world matters: without CONF the oracle plays config.yaml's (difficulty normal) while a leg's run file may
# be easy, a hit then takes other damage on Java, and test_snapshots' 0 mismatches is still printed (it checks the C
# engine against Java's own rows, not the sim's attempt); lane/dleg's first eight known answers were void that way.
# MOPTS: a file of one line, the macro options comma separated (each passed as --mopt); -x: the leg's other chain flags
# (they must be the leg's, or the attempt is another one); -t: the bound in ticks; -r: --target.
# -d (lane/rods2): a dev start, a WHAT-IF made the library's way (dev ops then a save as LIB/ID/cp, the join's Snapshot
# as LIB/ID/start; its checkpoint's manifest says dev true): the oracle plays it with DEV=1 and chain runs with --dev 1.
# Refused: -d on a checkpoint that is not dev, or a dev checkpoint without -d. Its line says WHAT-IF, never a benchmark.
# -f (lane/wood): a spawn start (fwd.sh start's: the oracle's new world of the seed at tick 2, no checkpoint): the oracle
# plays the seed from a new world at the run file's difficulty and render distance (SET), no FROM; refused unless the
# start is a tick-2, non-dev snapshot of the run file's seed whose tape header has the run file's difficulty and rd.
#
# Checked, each a FAIL: java's exit, the tape header's options.difficulty against the run file's, test_snapshots' field
# mismatches (0), the last row against the attempt's last state (chain's 'tape-attempt end' line, full precision):
# the tick (t = ticks + 2), the health exactly (the same float32), the food exactly, the feet's place to 0.001.
set -u
cd "$(dirname "$0")/../../.."
bin=out/runtime/train/chain att= seed=180000 ticks=60000 rung=dragon_dead mopts= world= out= xargs_= devst= fresh=
while getopts b:a:s:t:r:m:w:o:x:df o; do
  case $o in
    b) bin=$OPTARG ;; a) att=$OPTARG ;; s) seed=$OPTARG ;; t) ticks=$OPTARG ;; r) rung=$OPTARG ;; m) mopts=$OPTARG ;;
    w) world=$OPTARG ;; o) out=$OPTARG ;; x) xargs_=$OPTARG ;; d) devst=1 ;; f) fresh=1 ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
run=${1:-}
[ -n "$run" ] && [ -f "$run" ] && [ -n "$att" ] || { echo "usage: ka.sh -a ATTEMPT [flags] RUN.yaml" >&2; exit 2; }
refuse() { echo "KA REFUSE: $*" >&2; exit 2; }
# the run file's world: sim.world.seed and difficulty, sim.pipeline.start (the first of each key: the sim part's)
key() { awk -v k="$1" '$1 == k ":" { print $2; exit }' "$2"; }
rseed=$(key seed "$run") rdiff=$(key difficulty "$run") start=$(key start "$run")
[ -n "$start" ] && [ -d "$start" ] || refuse "$run: sim.pipeline.start '$start' is not a directory"
lib=$(dirname "$start")
devmk=() devch=() tag= jfrom=() jworld=()
if [ -n "$fresh" ]; then
  # -f (lane/wood): a spawn start (fwd.sh start: the oracle's fresh world of the seed at tick 2, made by
  # tests/fresh-play-s1.jsonl, whose first line is the Snapshot the attempt's script also begins with): the oracle plays
  # the seed from a new world, no checkpoint, at the run file's difficulty and render distance (SET), and the start's
  # own tape header must say the same
  [ -n "$devst" ] && refuse "-f with -d"
  [ "$(jq -r '.tick' "$start/manifest.json" 2>/dev/null)" = 2 ] && [ "$(jq -r '.dev' "$start/manifest.json")" = false ] ||
    refuse "-f: $start is not a tick-2 spawn start"
  rrd=$(key render_distance "$run")
  [ "$(jq -r '.seed' "$start/manifest.json")" = "$rseed" ] || refuse "-f: $start is not seed $rseed's"
  case $rdiff in peaceful) dnum=0 ;; easy) dnum=1 ;; normal) dnum=2 ;; hard) dnum=3 ;; *) refuse "difficulty '$rdiff'" ;; esac
  [ "$(head -1 "$start/tape.jsonl" | jq -r '.options.difficulty')" = "$dnum" ] &&
    [ "$(head -1 "$start/tape.jsonl" | jq -r '.options.rd')" = "$rrd" ] || refuse "-f: the start's tape is not $rdiff, rd $rrd"
  jworld=(SET="difficulty=$rdiff render_distance=$rrd")
else
[ -d "$lib/cp" ] || refuse "no checkpoint $lib/cp beside the start"
jfrom=(FROM="$lib/cp")
cpdev=$(jq -r '.dev // false' "$lib/cp/manifest.json" 2>/dev/null)
if [ -n "$devst" ]; then
  [ "$cpdev" = true ] || refuse "-d: $lib/cp is not a dev checkpoint"
  devmk=(DEV=1) devch=(--dev 1) tag="WHAT-IF "
else
  [ "$cpdev" != true ] || refuse "$lib/cp is a dev checkpoint (a WHAT-IF start): -d"
fi
[ -n "$world" ] || world=$(sed -n 2p "$lib/args" 2>/dev/null)
[ -n "$world" ] && [ -f "$world" ] || refuse "no start world file (-w, or $lib/args line 2)"
wdiff=$(key difficulty "$world")
[ "$wdiff" = "$rdiff" ] || refuse "the world file's difficulty '$wdiff' is not the run file's '$rdiff' ($world)"
jworld=(CONF="$world")
fi
case $rdiff in peaceful) dnum=0 ;; easy) dnum=1 ;; normal) dnum=2 ;; hard) dnum=3 ;; *) refuse "difficulty '$rdiff'" ;; esac
name=$(basename "$run" .yaml)-a$att
[ -n "$out" ] || out=out/runtime/train/ka/$name
mkdir -p "$out"
out=$(cd "$out" && pwd)
mo=()
if [ -n "$mopts" ]; then IFS=, read -r -a ol < "$mopts"; for x in "${ol[@]}"; do mo+=(--mopt "$x"); done; fi
seg=$(awk -v t="$ticks" 'BEGIN { printf "%.6f", t / 20 }')
rm -rf "$out/dir" "$out/row.tsv" "$out/script.jsonl"
# shellcheck disable=SC2086
env CUDA_VISIBLE_DEVICES= "$bin" search --run "$run" --dir "$out/dir" --n 1 --groups 1 --seed $((seed + att)) \
  --seg "$seg" --target "$rung" --rollout-out "$out/row.tsv" \
  --tape-attempt "0:$out/script.jsonl" "${devch[@]}" "${mo[@]}" $xargs_ > "$out/chain.log" 2>&1
crc=$?
rm -rf "$out/dir"
[ $crc = 0 ] && [ -s "$out/script.jsonl" ] || { echo "KA FAIL $name: chain rc $crc, no script ($out/chain.log)"; exit 1; }
row=$(awk -F'\t' 'NR == 2' "$out/row.tsv")
tape=out/java/try/ka-$name
rm -rf "$tape"
make -s -C oracle script "${devmk[@]}" SEED="$rseed" "${jworld[@]}" SCRIPT="$out/script.jsonl" TAPE="../$tape/tape.jsonl" \
  "${jfrom[@]}" CACHE=0 POOL=0 > "$out/java.log" 2>&1
jrc=$?
out/native/test_snapshots "$tape" > "$out/ts.log" 2>&1
trc=$?
rows=$(grep -o '[0-9]* rows simulated' "$out/ts.log" | head -1 | awk '{print $1}')
mism=$(grep -o '[0-9]* field mismatches' "$out/ts.log" | head -1 | awk '{print $1}')
jdiff=$(head -1 "$tape/tape.jsonl" 2>/dev/null | jq -r '.options.difficulty')
jend=$(tail -1 "$tape/tape.jsonl" 2>/dev/null | jq -r '[.t, .sp.hp, .sp.food, .sp.x, .sp.y, .sp.z] | @tsv')
# the attempt's last state at full precision (chain's own line: health %.9g, the place %.17g)
send=$(awk '/^tape-attempt end:/ { print $6 "\t" $8 "\t" $10 "\t" $12 "\t" $14 "\t" $16 }' "$out/chain.log" | tail -1)
[ -n "$send" ] || { echo "KA FAIL $name: no 'tape-attempt end' line in $out/chain.log (an older chain?)"; exit 1; }
# the health exact: both sides rounded to the float32 they print (Java's Float.toString and a 9-digit print each name
# one float), then compared with ==; the food and the tick exact; the place to 0.001 (and said whether exact)
endv=$(awk -F'\t' -v j="$jend" -v s="$send" '
  function f32(x,   sg, e, u, v, r, fr) { if (x == 0) return 0; sg = x < 0 ? -1 : 1; x *= sg; e = int(log(x) / log(2))
      while (2 ^ e > x) --e; while (2 ^ (e + 1) <= x) ++e; u = 2 ^ (e - 23); v = x / u; r = int(v); fr = v - r
      if (fr > 0.5 || (fr == 0.5 && r % 2 == 1)) ++r; return sg * r * u }
  BEGIN { split(j, b, "\t"); split(s, a, "\t"); ok = (b[1] == a[1] + 2) && (f32(b[2] + 0) == f32(a[2] + 0)) && (b[3] == a[3])
    ex = 1
    for (k = 4; k <= 6; ++k) { d = b[k] - a[k]; if (d < 0) d = -d; if (d >= 0.001) ok = 0; if (b[k] + 0 != a[k] + 0) ex = 0 }
    printf "%s: java t %s health %s food %s at %s %s %s / attempt ticks %s health %s food %s at %s %s %s (place %s)",
      ok ? "END EQUAL" : "END DIFF", b[1], b[2], b[3], b[4], b[5], b[6], a[1], a[2], a[3], a[4], a[5], a[6],
      ex ? "exact" : "to 0.001" }')
res=$(echo "$row" | cut -f6)
ok=1
[ $jrc = 0 ] && [ $trc = 0 ] && [ "$mism" = 0 ] && [ "$jdiff" = "$dnum" ] && [[ $endv == END\ EQUAL* ]] || ok=0
echo "${tag}KA $([ $ok = 1 ] && echo PASS || echo FAIL) $name: attempt $res at $(echo "$row" | cut -f7) ticks; java rc $jrc," \
  "difficulty $jdiff (run file $rdiff = $dnum); test_snapshots rc $trc, $rows rows, $mism field mismatches; $endv"
[ $ok = 1 ]
