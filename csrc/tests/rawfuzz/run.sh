#!/usr/bin/env bash
# rawfuzz: generated sessions played on the real Java client and judged on
# the native client's own input path. Each case is a RawDrive step list from
# rawfuzz_gen (csrc/tests/rawfuzz/gen.c, a seeded stand-in person), played on the play-mode Java client
# under Xvfb (oracle/tests/rawplay.sh: every keyboard and mouse event recorded
# as LWJGL delivered it), then csrc/play/rawjudge.sh on it: rows, screen
# ops and frames (goldens every 250 ticks, budgets in
# out/java/rawfuzz/budget.txt, not the tracked file).
#
#   bash csrc/tests/rawfuzz/run.sh [-j N] [-F] CASE...
#
# -F judges rows and screen ops only (rawjudge --no-frames: no goldens
# replay; a new session has no reviewed frame budget, so every frame of it
# reads "no budget line"); the frames column is then -.
#
# CASE is START:SEED. START is fresh (seed 1's first join), fresh42 (seed
# 42's), or a checkpoint: SN (out/java/checkpoints/1/SN, the seed-1 golden
# chain) or GN (42/GN). The session is out/java/rawfuzz/rf-START-SEED
# (drive.jsonl, tape.jsonl, tape.raw.jsonl, the goldens, judge.log); a
# checkpoint's start snapshot is recorded once into
# out/java/rawfuzz/starts/START. One line per case, and each in
# out/java/rawfuzz/results.tsv:
#   NAME  rows gui frames (PASS|FAIL each)  ticks  seconds  the first FAIL line
set -uo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
J=4 NOFRAMES=0
while [[ ${1:-} == -* ]]; do
    case $1 in
        -j) J=$2; shift 2;;
        -F) NOFRAMES=1; shift;;
        *) break;;
    esac
done
[[ $# -ge 1 ]] || { sed -n '2,24p' "$0"; exit 2; }
make -s -C csrc "$root/out/native/rawfuzz_gen" > /dev/null || { echo "rawfuzz: native build failed"; exit 1; }
base=out/java/rawfuzz
mkdir -p "$base/starts"

# the start snapshot of a checkpoint start: the join a Java script makes
# from the checkpoint, as rawjudge.sh would record it, once per checkpoint
start_snap() {
    local st=$1 seed=$2 cp=$3 d=$base/starts/$1
    (
        flock 9
        [[ -s $d/manifest.json ]] && exit 0
        mkdir -p "$d"
        echo '{"cmd":"run","class":"Snapshot"}' > "$d/script.jsonl"
        make -s -C oracle script SET="seed=$seed" ${cp:+FROM="$(realpath "$cp")"} SCRIPT="../$d/script.jsonl" \
            TAPE="../$d/tape.jsonl" > "$d/java.log" 2>&1
        [[ -s $d/manifest.json ]]
    ) 9> "$base/starts/.$st.lock"
}

one() {
    local c=$1 st=${1%%:*} n=${1##*:} seed=1 cp= prof=cp
    case $st in
        fresh) prof=fresh;;
        fresh42) prof=fresh seed=42;;
        S[0-9]*) cp=out/java/checkpoints/1/$st;;
        G[0-9]*) cp=out/java/checkpoints/42/$st seed=42;;
        *) echo "rawfuzz: no start $st"; return 1;;
    esac
    local name=rf-$st-$n dir=$base/rf-$st-$n t0=$SECONDS
    rm -rf "$dir"; mkdir -p "$dir"
    out/native/rawfuzz_gen "$n" --profile "$prof" --salt "$st" > "$dir/drive.jsonl"
    # a client that never reached its display (Xvfb's start raced it: "Can't
    # connect to X11 window server") is started again, twice at most
    for try in 1 2 3; do
        (cd oracle && bash tests/rawplay.sh "../$dir/drive.jsonl" "../$dir/tape.jsonl" "$seed" ${cp:+"../$cp"}) > "$dir/play.log" 2>&1
        grep -q "Can't connect to X11" "$dir/tape.log" 2>/dev/null || break
    done
    local ticks
    ticks=$(tail -1 "$dir/tape.jsonl" 2>/dev/null | jq -r '.t // empty' 2>/dev/null)
    if [[ -z $ticks ]] || ! grep -q 'ORACLE DONE rc=0' "$dir/tape.log" 2>/dev/null; then
        printf '%s\tjava\t-\t-\t%s\t%s\tthe Java session failed: %s\n' "$name" "${ticks:--}" $((SECONDS - t0)) \
            "$(grep -a 'ORACLE' "$dir/tape.log" 2>/dev/null | tail -1)" | tee -a "$base/results.tsv"
        return 1
    fi
    if [[ -n $cp ]]; then
        start_snap "$st" "$seed" "$cp" || { printf '%s\tstart\t-\t-\t%s\t%s\tno start snapshot (%s)\n' "$name" "$ticks" $((SECONDS - t0)) "$base/starts/$st/java.log" | tee -a "$base/results.tsv"; return 1; }
        echo "rawfuzz/starts/$st" > "$dir/start"
    elif [[ $seed == 42 ]]; then
        start_snap fresh42 42 "" || return 1
        echo "rawfuzz/starts/fresh42" > "$dir/start"
    fi
    local nf=()
    ((NOFRAMES)) && nf=(--no-frames)
    bash csrc/play/rawjudge.sh "$dir" --every 250 --budgets "$base/budget.txt" ${nf[@]+"${nf[@]}"} > "$dir/judge.log" 2>&1
    local r g f first
    r=$(grep -q '^PASS rawjudge rows' "$dir/judge.log" && echo PASS || echo FAIL)
    g=$(grep -q '^PASS rawjudge gui' "$dir/judge.log" && echo PASS || echo FAIL)
    f=$(grep -q '^PASS rawjudge frames' "$dir/judge.log" && echo PASS || echo FAIL)
    ((NOFRAMES)) && f=-
    first=$(grep -m1 '^FAIL' "$dir/judge.log")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$r" "$g" "$f" "$ticks" $((SECONDS - t0)) "$first" | tee -a "$base/results.tsv"
    # a session that passes keeps what a rerun needs (the tapes and goldens), not the judge's work
    [[ $r$g$f == PASSPASSPASS || $r$g$f == PASSPASS- ]] && rm -rf "out/native/rawjudge/$name" "$dir"/*.make.log
    return 0
}
export -f one start_snap
export base NOFRAMES
printf '%s\n' "$@" | xargs -P "$J" -I{} bash -c 'one "$@"' _ {}
