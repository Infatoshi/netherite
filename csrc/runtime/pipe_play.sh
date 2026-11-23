#!/usr/bin/env bash
# Play's own frames at the pipeline's frame ticks, for pipe_gate --play DIR:
# each recording replayed by out/native/play from its start with --shots at
# the tick every step of n rows ends on (pipe_gate's schedule: frames after
# ticks t0 + n - 1, t0 + 2n - 1, ..., and the last row), at the observation's
# shape (128x128 or -s WxH, F1, render distance 4), dumped with --obs-dump into
# DIR/NAME/o_TTTTTT.p1000.obs (the frame data and play's C frame of it).
# play draws a frame only at a shot, and a frame reads what the frame before
# it left (the particles' interpolation), so the shots are exactly the
# pipeline's.
#
#   csrc/runtime/pipe_play.sh [-n TICKS] [-j JOBS] [-p PREC] [-s WxH] DIR NAME...
# -p: play's --render-prec (engine/raster_prec.h; exact by default); -s: the
# observation's size (pipe_gate --size; 128x128 by default)
set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
ticks=4 jobs=8 prec=exact size=128x128
while [[ $# -gt 0 ]]; do
    case $1 in
        -n) ticks=$2; shift 2;;
        -j) jobs=$2; shift 2;;
        -p) prec=$2; shift 2;;
        -s) size=$2; shift 2;;
        *) break;;
    esac
done
dir=$1; shift
mkdir -p "$dir"
one() {
    local name=$1 snap=out/java/snapshots/$1 out=$DIR/$1
    rm -rf "$out"; mkdir -p "$out"
    local t0 last
    t0=$(sed -n 2p "$snap/tape.jsonl" | grep -o '"t":[0-9]*' | head -1 | cut -d: -f1 --complement)
    local st
    st=$(grep -o '"tick": *[0-9]*' "$snap/manifest.json" | head -1 | grep -o '[0-9]*$')
    [[ -n $st && $st -gt $t0 ]] && t0=$st
    last=$(( $(wc -l < "$snap/tape.jsonl") - 2 ))
    for ((t = t0 + TICKS - 1; t < last; t += TICKS)); do echo "$t 1.0"; done > "$out.shots"
    echo "$last 1.0" >> "$out.shots"
    local bin=out/native/play
    head -1 "$snap/tape.jsonl" | grep -q '"dev":true' && bin=out/native/play-dev
    SDL_VIDEODRIVER=dummy "$bin" "$snap" --threads 1 --replay "$snap/tape.jsonl" --tape "$out.tape" \
        --shots "$out.shots" --obs-dump "$out" --size "$SIZE" --hide-gui --render-distance 4 --render-prec "$PREC" > "$out.log" 2>&1
    local rc=$?
    rm -f "$out.tape"
    echo "$name rc $rc: $(ls "$out" | wc -l) frames of $(wc -l < "$out.shots")"
}
export -f one
export DIR=$dir TICKS=$ticks PREC=$prec SIZE=$size
printf '%s\n' "$@" | xargs -P "$jobs" -I{} bash -c 'one {}'
