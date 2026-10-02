#!/usr/bin/env bash
# rlfuzz: sessions of RL-policy input (csrc/tests/playfuzz/agentgen.c, out/native/playfuzz_agentgen)
# recorded on the Java oracle through the agent input (make -C oracle script),
# each a snapshot recording, and checked row by row natively: test_snapshots
# (the whole-server replay) and csrc/tests/play_check.sh (play's own tick
# pair). One recording per case.
#
#   bash csrc/tests/playfuzz/agentrun.sh [-j N] CASE...        CASE is START:SEED (s1:3 g12:40)
#   bash csrc/tests/playfuzz/agentrun.sh [-j N] FILE           one CASE per line (# comments)
#
# START is s1 (a fresh seed-1 world, 16 ticks in), a seed-1 checkpoint by
# name (cp-mobs-s1, cp-nether-s1, cp-end-s1, cp-far-s1, snap-afternether-s1:
# out/java/checkpoints/1/NAME), or gN for N in 1..30 (seed 42's golden
# checkpoint GN). SEED is playfuzz_agentgen's seed; a case is a dev case (a kit, a
# portal, mobs: playfuzz_agentgen --dev 1) on a dev checkpoint, and otherwise when
# SEED % 3 != 0 from s1 and SEED % 3 == 0 elsewhere. START:@FILE runs a kept
# script (csrc/tests/playfuzz/agentpins/).
# The script is out/rlfuzz/scripts/rf-START-SEED.jsonl, the recording
# out/java/rlfuzz/rf-START-SEED (deleted with the oracle's run directory
# once judged PASS; a FAIL keeps it for out/native/diverge and pin). One line
# per case, and every line in out/rlfuzz/results.tsv:
#   CASE  PASS|FAIL  ticks  rows  mismatches  refusals  seconds  detail
set -uo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
J=6
[[ ${1:-} == -j ]] && { J=$2; shift 2; }
[[ $# -ge 1 ]] || { sed -n '2,23p' "$0"; exit 2; }
export TMPDIR=${TMPDIR_RL:-$HOME/dev/nw/.tmp}
mkdir -p "$TMPDIR"

one() {
    local c=$1 start=${1%%:*} seed=${1#*:} t0=$SECONDS
    local cp= pos=161,67,253 dim=0 dev=0 fresh=0 run script
    case $start in
        s1) fresh=1;;
        g[0-9]*) cp=out/java/checkpoints/42/G${start#g};;
        *) cp=out/java/checkpoints/1/$start;;
    esac
    if [[ -n $cp ]]; then
        [[ -f $cp/manifest.json ]] || { printf '%s\tFAIL\t0\t0\t-\t0\t0\tno such start\n' "$c"; return; }
        pos=$(jq -r '.position | map(floor) | join(",")' "$cp/manifest.json")
        dim=$(jq -r '.dimension // 0' "$cp/manifest.json")
        [[ $(jq -r '.dev' "$cp/manifest.json") == true ]] && dev=1
    fi
    if [[ $seed == @* ]]; then
        script=${seed#@}
        run=rf-$start-$(basename "$script" .jsonl)
        [[ -f $script ]] || { printf '%s\tFAIL\t0\t0\t-\t0\t0\tno such script\n' "$c"; return; }
        grep -q '"cmd":"dev"' "$script" && dev=1
    else
        [[ $seed =~ ^[0-9]+$ ]] || { printf '%s\tFAIL\t0\t0\t-\t0\t0\tno such case\n' "$c"; return; }
        run=rf-$start-$seed
        if (( dev == 0 )); then
            if (( fresh )); then (( seed % 3 != 0 )) && dev=1; else (( seed % 3 == 0 )) && dev=1; fi
        fi
        script=out/rlfuzz/scripts/$run.jsonl
        { (( fresh )) && echo '{"n":16,"act":{}}'
          out/native/playfuzz_agentgen "$seed" --pos "$pos" --dim "$dim" --dev "$dev"; } > "$script"
    fi
    local dir=out/java/rlfuzz/$run log=out/rlfuzz/logs/$run.log args=()
    rm -rf "$dir"
    mkdir -p "$dir"
    (( dev )) && args+=(DEV=1)
    if [[ -n $cp ]]; then
        args+=(FROM="../$cp" SET="$(jq -r '.world | to_entries | map("\(.key)=\(.value)") | join(" ")' "$cp/manifest.json")")
    else
        args+=(SEED=1)
    fi
    if ! make -s -C oracle script "${args[@]}" SCRIPT="../$script" TAPE="../$dir/tape.jsonl" > "$log" 2>&1 \
            || ! grep -q '^ORACLE DONE rc=0' "$log"; then
        printf '%s\tFAIL\t0\t0\t-\t0\t%s\toracle: %s\n' "$c" "$((SECONDS - t0))" "$(grep -a 'Exception\|ORACLE DONE\|Error' "$log" | head -1)"
        rm -rf "out/java/run/$run-tape"
        return
    fi
    local refusals o pc prc line rows mism ticks res det
    refusals=$(grep -c '^ORACLE REFUSE' "$log")
    ticks=$(grep -a '^ORACLE DONE' "$log" | sed -E 's/.*ticks=([0-9]+).*/\1/')
    o=$(bash csrc/tests/memslot.sh "$dir" out/native/test_snapshots "$dir" 2>&1)
    pc=$(bash csrc/tests/memslot.sh --key play "$dir" bash csrc/tests/play_check.sh "$dir" 2>&1); prc=$?
    line=$(grep -a '^replay: .* rows simulated' <<< "$o" | tail -1)
    rows=$(sed -E 's/^replay: ([0-9]+) rows.*/\1/' <<< "$line")
    mism=$(sed -E 's/.*, ([0-9]+) field mismatch.*/\1/' <<< "$line")
    if [[ -n $line && $mism == 0 && $prc == 0 ]] && grep -aq ': OK: ' <<< "$o"; then
        res=PASS det=
        rm -rf "$dir"
    else
        res=FAIL
        det="$(grep -a 'FAIL\|mismatch\|error' <<< "$o" | grep -v '^replay:' | head -1)"
        [[ $prc == 0 ]] || det="$det | play_check: $(grep -a 'FAIL\|mismatch' <<< "$pc" | head -1)"
        det="$det [$dir]"
    fi
    rm -rf "out/java/run/$run-tape"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$c" "$res" "${ticks:-0}" "${rows:-0}" "${mism:--}" "$refusals" "$((SECONDS - t0))" "$det"
}
export -f one

cases=()
for a in "$@"; do
    if [[ -f $a ]]; then while read -r l; do [[ $l =~ ^[a-z] ]] && cases+=("${l%%[[:space:]#]*}"); done < "$a"
    else cases+=("$a"); fi
done

make -s -C csrc all play play-dev > /dev/null || { echo "rlfuzz: native build failed"; exit 1; }
make -s -C oracle build > /dev/null 2>&1 || { echo "rlfuzz: java build failed"; exit 1; }
mkdir -p out/rlfuzz/scripts out/rlfuzz/logs out/java/rlfuzz
printf '%s\n' "${cases[@]}" | nice -n 10 xargs -P "$J" -I{} bash -c 'one "$1"' _ {} | tee -a out/rlfuzz/results.tsv \
    | awk -F'\t' '{print} $2=="PASS"{p++; r+=$4} END{printf "rlfuzz: %d/%d PASS, %d rows checked\n", p, NR, r; exit p != NR}'
