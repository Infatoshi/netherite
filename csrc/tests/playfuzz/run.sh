#!/usr/bin/env bash
# playfuzz: sessions of human-like play from real starts, each played by
# out/native/play through its SDL input path (--input-script, the dummy video
# driver) and checked on the Java oracle by csrc/play/session.sh exactly as
# a human's session is (Java replays the inputs, the native whole-server check
# and play --check compare every row), one session per case: --no-chain skips
# the Save and Quit and the next start, which only a next session would use
# (csrc/tests/session-proof proves the chain).
#
#   bash csrc/tests/playfuzz/run.sh [-k] [-j N] CASE...     CASE is START:SEED (fresh:3 g12:40); -k keeps
#                                                             a PASS's run directories (for pin.sh)
#   bash csrc/tests/playfuzz/run.sh [-j N] csrc/tests/playfuzz/cases.txt
#   bash csrc/tests/playfuzz/run.sh [-j N] csrc/tests/playfuzz/pins.txt   (START:@SCRIPT: a kept script)
#
# START is fresh (out/java/snapshots/fresh-play-s1, the human's first
# session), gN for N in 1..29 (golden checkpoint 42/GN: the snapshot
# gold-g(N+1)-s42 starts there) or g30 (checkpoint 42/G30, back at spawn after
# the End: out/java/playfuzz/start-g30, which this script makes once); sN
# (1..22) is seed 1's checkpoint 1/SN (the snapshot s1chain-S(N+1)) and s23
# out/java/playfuzz/start-s23 (chain.sh makes it). The
# script is playfuzz_gen SEED (the same seed, the same script), written to
# out/playfuzz/scripts/; the run is out/human/pf-START-SEED (made anew each
# time; a PASS keeps only its tapes and log). Java is deterministic, so the
# rows of a tape it already replayed come from out/playfuzz/jcache (keyed by
# the tape's and the harness's hash) and only the native checks run again. One line per case, and every
# line in out/playfuzz/results.tsv:
#   CASE  PASS|FAIL  ticks  rows  mismatches  seconds  detail (a FAIL names the diverge file)
set -uo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
J=4 KEEP=0
[[ ${1:-} == -k ]] && { KEEP=1; shift; }
[[ ${1:-} == -j ]] && { J=$2; shift 2; }
[[ ${1:-} == -k ]] && { KEEP=1; shift; }
export KEEP
[[ $# -ge 1 ]] || { sed -n '2,19p' "$0"; exit 2; }

start_dir() {
    case $1 in
        fresh) echo out/java/snapshots/fresh-play-s1;;
        g30) echo out/java/playfuzz/start-g30;;
        g[0-9]*) local n=${1#g}; (( n >= 1 && n <= 29 )) && echo "out/java/snapshots/gold-g$((n + 1))-s42";;
        s23) echo out/java/playfuzz/start-s23;;
        s[0-9]*) local n=${1#s}; (( n >= 1 && n <= 22 )) && echo "out/java/snapshots/s1chain-S$((n + 1))";;
    esac
}

one() {
    local c=$1 start=${1%%:*} seed=${1##*:}
    local snap run t0=$SECONDS script
    snap=$(start_dir "$start")
    if [[ $seed == @* ]]; then
        # a kept script (csrc/tests/playfuzz/pins): START:@FILE
        script=${seed#@}
        run=pf-$start-$(basename "$script" .jsonl)
        [[ -n $snap && -f $script ]] || { printf '%s\tFAIL\t0\t0\t-\t0\tno such case\n' "$c"; return; }
    else
        run=pf-$start-$seed
        [[ -n $snap && $seed =~ ^[0-9]+$ ]] || { printf '%s\tFAIL\t0\t0\t-\t0\tno such case\n' "$c"; return; }
        script=out/playfuzz/scripts/$run.jsonl
        # the player at the start (row 1: the join's last), for the kit's scene
        local pose
        pose=$(sed -n 3p "$snap/tape.jsonl" | jq -r '[.sp.x, .sp.y, .sp.z, .cp.yaw, .cp.pitch, .sp.dim] | map(tostring) | join(" ")')
        out/native/playfuzz_gen "$seed" --pose "$pose" \
            --kit "${script%.jsonl}.kit.jsonl" --actions "${script%.jsonl}.acts.tsv" > "$script"
    fi
    # a kit (Dev header entries) plays on play-dev, which folds them into the tape
    local kit=${script%.jsonl}.kit.jsonl bin=out/native/play devops=()
    [[ -s $kit ]] && { bin=out/native/play-dev; devops=(--dev-ops "$kit"); }
    rm -rf "out/human/$run"
    local st=out/human/$run res ticks rows mism det o
    mkdir -p "$st/play" out/playfuzz/jcache
    # play through the SDL input path, in the memory budget
    local tape=$st/play/$run.played.jsonl
    # (a session the script leaves paused cannot tick: the timeout ends it)
    if ! o=$(bash csrc/tests/memslot.sh --key play "$snap" timeout 600 "$bin" "$snap" --tape "$tape" \
                 --no-draw --input-script "$script" ${devops[@]+"${devops[@]}"} 2>&1); then
        printf '%s\tFAIL\t0\t0\t-\t%s\tplay exited: %s\n' "$c" "$((SECONDS - t0))" "$(tail -1 <<< "$o")"
        return
    fi
    # Java is deterministic: a tape it already replayed has its rows cached
    local key cache chk=$st/play/$run-s1-1.check
    key=$(cat "$tape" <(echo "$HARNESS") | sha1sum | cut -c1-20)
    cache=out/playfuzz/jcache/$key.jsonl.gz
    if [[ -s $cache ]]; then
        local f
        # the cached rows' start.dir names the checkpoint through the
        # oracle's replay directory, which a PASS removed
        mkdir -p "$chk" "out/java/run/play-$run-s1-1.replay"
        for f in "$(readlink -f "$snap")"/*; do [[ $(basename "$f") == tape.jsonl ]] || ln -s "$f" "$chk/"; done
        gzip -dc "$cache" > "$chk/tape.jsonl"
        # the run that stored the rows named its own replay directory (a
        # pins.txt run of the same script has another run name)
        local sd
        sd=$(head -1 "$chk/tape.jsonl" | jq -r '.start.dir // empty')
        [[ $sd == */out/java/run/*.replay/* ]] && mkdir -p "${sd%%.replay/*}.replay"
        o=$(bash csrc/tests/memslot.sh "$snap" out/native/test_snapshots "$chk" 2>&1)
        local pc
        pc=$(bash csrc/tests/memslot.sh "$snap" "$bin" "$chk" --check "$chk/tape.jsonl" 2>&1); local prc=$?
        local line
        line=$(grep -a '^replay: .* rows simulated' <<< "$o" | tail -1)
        rows=$(sed -E 's/^replay: ([0-9]+) rows.*/\1/' <<< "$line")
        mism=$(sed -E 's/.*, ([0-9]+) field mismatch.*/\1/' <<< "$line")
        ticks=$(( $(grep -c '"act"' "$chk/tape.jsonl") + 2 ))
        if [[ -n $line && $mism == 0 && $prc == 0 ]] && grep -aq ': OK: ' <<< "$o"; then res=PASS det="java rows cached"
        else res=FAIL det="$(grep -a '^FAIL' <<< "$o$pc" | head -1) (cached java rows; out/native/diverge $chk)"; fi
    else
        o=$(bash csrc/play/session.sh --run "$run" --from "$snap" --no-chain --check "$tape" 2>&1)
        local line
        line=$(tail -1 "$st/sessions.tsv" 2>/dev/null)
        IFS=$'\t' read -r _ _ _ ticks rows mism res det <<< "$line"
        [[ -s $chk/tape.jsonl && $res =~ ^(PASS|FAIL)$ && ${rows:-0} != 0 ]] &&
            gzip -c "$chk/tape.jsonl" > "$cache"
        if [[ $res == PASS ]]; then
            det=
        else
            res=FAIL
            [[ -n ${det:-} ]] || det=$(grep -a 'session:\|FAIL\|play:' <<< "$o" | tail -1)
            [[ -f $st/play/$run-s1-1.diverge ]] && det="$det [$st/play/$run-s1-1.diverge]"
        fi
    fi
    rm -rf "$st/cp" "$st/start/$run-s2"
    # the oracle's game directories are not read once the case is judged; a
    # PASS keeps only what playfuzz_coverage reads (each checked row's dimension,
    # inventory and window), a FAIL its whole out/human directory and its
    # replay's directory, emptied: the check tape's start.dir names the
    # checkpoint through it
    local jr=out/java/run/play-$run-s1-1.replay
    rm -rf out/java/run/"$run"-s1-1.check-tape.replay
    if [[ $res == PASS && $KEEP == 1 ]]; then :
    elif [[ $res == PASS ]]; then rm -rf "$jr"; elif [[ -d $jr ]]; then find "$jr" -mindepth 1 -delete; fi
    if [[ $res == PASS && -s $chk/tape.jsonl ]]; then
        mkdir -p out/playfuzz/cov
        tail -n +2 "$chk/tape.jsonl" | jq -c 'select(.sp != null) | {t, sp: {dim: .sp.dim, inv: .sp.inv, win: .sp.win}}' \
            > out/playfuzz/cov/"$run".jsonl
        [[ $KEEP == 1 ]] || rm -rf "$st"
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$c" "$res" "${ticks:-0}" "${rows:-0}" "${mism:--}" "$((SECONDS - t0))" "$det"
}
export -f one start_dir

cases=()
for a in "$@"; do
    if [[ -f $a ]]; then while read -r l; do [[ $l =~ ^[a-z] ]] && cases+=("${l%%[[:space:]#]*}"); done < "$a"
    else cases+=("$a"); fi
done

make -s -C csrc play play-dev "$root/out/native/test_snapshots" "$root/out/native/playfuzz_gen" > /dev/null || { echo "playfuzz: native build failed"; exit 1; }
make -s -C oracle build > /dev/null 2>&1 || { echo "playfuzz: java build failed"; exit 1; }
# the oracle's rows depend on the harness too: its hash is part of the cache key
HARNESS=$(cd oracle && find src harness -name '*.java' | LC_ALL=C sort | xargs cat | sha1sum | cut -c1-12)
export HARNESS
mkdir -p out/playfuzz/scripts
if printf '%s\n' "${cases[@]}" | grep -q '^g30:' && [[ ! -s out/java/playfuzz/start-g30/manifest.json ]]; then
    mkdir -p out/java/playfuzz/start-g30
    echo '{"cmd":"run","class":"Snapshot"}' > out/playfuzz/snapshot.jsonl
    set=$(jq -r '.world | to_entries | map("\(.key)=\(.value)") | join(" ")' out/java/checkpoints/42/G30/manifest.json)
    make -s -C oracle script SET="$set" FROM=../out/java/checkpoints/42/G30 SCRIPT=../out/playfuzz/snapshot.jsonl \
        TAPE=../out/java/playfuzz/start-g30/tape.jsonl > out/playfuzz/start-g30.log 2>&1 \
        || { echo "playfuzz: the g30 start was not made (out/playfuzz/start-g30.log)"; exit 1; }
fi
printf '%s\n' "${cases[@]}" | nice -n 10 xargs -P "$J" -I{} bash -c 'one "$1"' _ {} | tee -a out/playfuzz/results.tsv \
    | awk -F'\t' '{print} $2=="PASS"{p++; r+=$4} END{printf "playfuzz: %d/%d PASS, %d rows checked\n", p, NR, r; exit p != NR}'
