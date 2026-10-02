#!/usr/bin/env bash
# The judges' device frames against the C renderer (make -C csrc
# gpu-render-judge-check): every shot of every frame judge's recording (out/java/
# clientframes/NAME, the ticks and partial ticks csrc/tests/
# frame_budget.txt lists) replayed by play at the judges' size with the
# device drawing the world frames (--frames device) and the C renderer
# drawing each of them too (--frames-check 1): the frame (the world, the
# weather, the hand, the overlays) and the world's depth under them, and
# the frame without the drops where the oracle drew one, byte for byte.
#
#   judge_gate.sh [--size WxH] [--rd N] [--jobs J] [--prec P] [NAME...]
#
# --size 854x480 and --rd 8 by default (render distance 8: the most the
# recordings play at); J plays at once (4: the device's judge slots, /tmp/
# netherite-gpu). --prec: play draws at that precision, the device and C
# alike (--render-prec: exact, the default, fast or fast:STAGE,...; engine/
# raster_prec.h). NAME... narrows the recordings. One line per recording,
# the FAIL lines, then "judge_gate: P of N frames bit-exact ...".
set -uo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
size=854x480 rd=8 jobs=4 prec=exact
names=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --size) size=$2; shift 2;;
        --rd) rd=$2; shift 2;;
        --jobs) jobs=$2; shift 2;;
        --prec) prec=$2; shift 2;;
        *) names+=("$1"); shift;;
    esac
done
out=$root/out/native
[[ -f $out/cuda/libframedev.so ]] || { echo "judge_gate: build the plugin first (make -C csrc gpu-render-judge)"; exit 2; }
budgets=$root/csrc/tests/frame_budget.txt
tmp=$(mktemp -d "${TMPDIR:-/tmp}/judgegate.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
if [[ ${#names[@]} == 0 ]]; then
    for d in out/java/clientframes/*/; do n=$(basename "$d"); grep -qE "^$n " "$budgets" && names+=("$n"); done
fi

# one recording's shots, both renderers; a line: NAME rc PASS FAIL SHOTS
gate_one() {
    local n=$1 dir=$root/out/java/clientframes/$1
    local start=$dir tape=$dir/tape.jsonl
    [[ -f $dir/start ]] && start=$root/out/java/$(cat "$dir/start")
    [[ -f $tape ]] || tape=$start/tape.jsonl
    grep -E "^$n " "$budgets" | awk '{print $2, $3}' > "$tmp/$n.shots"
    local bin=$out/play extra=()
    head -1 "$start/tape.jsonl" | grep -q '"dev":true' && bin=$out/play-dev
    [[ -f $dir/raw.jsonl ]] && extra+=(--gui-raw "$dir/raw.jsonl")
    SDL_VIDEODRIVER=dummy "$bin" "$start" --threads 2 --replay "$tape" --tape "$tmp/$n.tape" \
        --shots "$tmp/$n.shots" --shot-golden-dir "$dir" --shot-items "$dir/drops.jsonl" "${extra[@]}" \
        --size "$SIZE" --render-distance "$RD" --render-prec "$PREC" --frames device --frames-check 1 > "$tmp/$n.log" 2>&1
    local rc=$?
    rm -f "$tmp/$n.tape"
    echo "$n $rc $(grep -c '^PASS device frame' "$tmp/$n.log") $(grep -c '^FAIL device frame' "$tmp/$n.log") $(wc -l < "$tmp/$n.shots")"
}
export -f gate_one
export root tmp out budgets SIZE=$size RD=$rd PREC=$prec

printf '%s\n' "${names[@]}" | xargs -P "$jobs" -I{} bash -c 'gate_one {}' > "$tmp/results.txt"
# the device is shared: a recording the device turned away (no free slot or
# memory, a failed call) runs again alone, twice at most
for try in 1 2; do
    again=()
    while read -r n _; do
        grep -q 'no device renderer\|device renderer stopped' "$tmp/$n.log" && again+=("$n")
    done < "$tmp/results.txt"
    [[ ${#again[@]} == 0 ]] && break
    echo "judge_gate: ${#again[@]} recordings again, one at a time (the device turned them away)"
    for n in "${again[@]}"; do
        grep -v "^$n " "$tmp/results.txt" > "$tmp/results.new"
        gate_one "$n" >> "$tmp/results.new"
        mv "$tmp/results.new" "$tmp/results.txt"
    done
done
rc=0 pass=0 fail=0 shots=0 nrec=0
while read -r n prc p f s; do
    nrec=$((nrec + 1)); pass=$((pass + p)); fail=$((fail + f)); shots=$((shots + s))
    # play's rc 3 is a judgement (the budgets are not asked for here); any
    # other failure, a device refusal or a missing shot fails the gate
    frames=$(grep -c '^frame t=' "$tmp/$n.log")
    if [[ $f != 0 || ( $prc != 0 && $prc != 3 ) || $frames != "$s" ]] || grep -q 'no device renderer\|device renderer stopped' "$tmp/$n.log"; then
        echo "FAIL judge_gate $n: play rc $prc, $frames of $s shots drawn, $p device frames equal, $f differ"
        grep -E '^FAIL device frame|no device renderer|device renderer stopped' "$tmp/$n.log" | head -5 | sed 's/^/  /'
        rc=1
    else
        echo "PASS judge_gate $n: $s shots, $p frames and their world depth equal"
    fi
done < <(sort "$tmp/results.txt")
echo "judge_gate: $pass of $((pass + fail)) frames bit-exact (the frame and its world depth; the frame without the drops where the oracle drew one) at $size, render distance $rd, over $nrec recordings and $shots shots"
exit $rc
