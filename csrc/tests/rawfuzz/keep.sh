#!/usr/bin/env bash
# Keep a fuzz session as a rawjudge test directory (make -C csrc test runs
# every out/java/rawjudge directory): its tape, raw events, stand-in script
# and goldens go to out/java/rawjudge/NAME, and its start snapshot with it
# (DIR/snap, or SHARE's when another kept session has the same start).
#   bash csrc/tests/rawfuzz/keep.sh SRC NAME [SHARE]
# SRC is out/java/rawfuzz/rf-...; the frames' budget lines are added to
# csrc/tests/rawjudge_budget.txt by rawjudge.sh --accept after a review.
set -uo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
src=${1%/} name=$2 share=${3:-}
dst=out/java/rawjudge/$name
[[ -s $src/tape.jsonl && -s $src/tape.raw.jsonl ]] || { echo "keep: $src has no session"; exit 1; }
[[ -e $dst ]] && { echo "keep: $dst exists"; exit 1; }
mkdir -p "$dst"
cp "$src/tape.jsonl" "$src/tape.raw.jsonl" "$src/drive.jsonl" "$dst/"
cp "$src"/f_*.png "$dst/" 2>/dev/null
[[ -f $src/pins ]] && cp "$src/pins" "$dst/"
st=$(cat "$src/start" 2>/dev/null || echo snapshots/fresh-play-s1)
if [[ $st == rawfuzz/* ]]; then
    if [[ -n $share ]]; then
        echo "rawjudge/$share/snap" > "$dst/start"
    else
        cp -r "out/java/$st" "$dst/snap"
        rm -f "$dst/snap/java.log" "$dst/snap/script.jsonl"
        echo "rawjudge/$name/snap" > "$dst/start"
    fi
else
    echo "$st" > "$dst/start"
fi
echo "kept $src as $dst (start $(cat "$dst/start"))"
