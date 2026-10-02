#!/usr/bin/env bash
# The playable client's own tick pair over a snapshot recording:
#   tests/play_check.sh SNAPSHOT_DIR [OUT_DIR]
# runs OUT_DIR/play SNAPSHOT_DIR --check SNAPSHOT_DIR/tape.jsonl, or
# OUT_DIR/play-dev for a dev recording (the product refuses a dev tape).
# make test runs it on every snapshot directory beside test_snapshots, so the
# client cannot fall behind the replay the gate checks. OUT_DIR defaults to
# the checkout's out/native.
# A recording with a cwhash.txt (the oracle's "ORACLE CWHASH" lines without
# the prefix, java --cw-diff T,...) also checks the client world's own light
# and blocks: play --cw-hash prints the same section hashes at those ticks.
# A recording listed in tests/play_check.list with +live then plays its
# whole tape the live way too: the replay loop with the renderer on two
# threads and a frame after its last row (and after each row T of
# +live:T,T...) dumped at the render gate's observation
# (cuda/render/gate.sh), where --check does not go
# (lane/playarena: the live replay freed no dead living, and the arena of
# 65,536 filled in moon-swamp-s2 and swamp-night-s2).
set -uo pipefail
dir=${1:?usage: play_check.sh SNAPSHOT_DIR [OUT_DIR]}
dir=${dir%/}
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${2:-$root/out/native}
bin=$out/play
head -1 "$dir/tape.jsonl" | grep -q '"dev":true' && bin=$out/play-dev
live() {
    local tag tmp last rc
    tag=$(sed 's/#.*//' "$root/csrc/tests/play_check.list" | awk -v n="${dir##*/}" '$1 == n && $2 ~ /^\+live/ {print $2}')
    [ -n "$tag" ] || return 0
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/playlive.XXXXXX")
    last=$(tail -1 "$dir/tape.jsonl" | grep -o '"t":[0-9]*' | head -1 | cut -d: -f2)
    { echo "$last"; [ "$tag" = +live ] || tr , '\n' <<< "${tag#+live:}"; } | sort -un | sed 's/$/ 1.0/' > "$tmp/shots"
    (cd "$root" && SDL_VIDEODRIVER=dummy "$bin" "$dir" --threads 2 --replay "$dir/tape.jsonl" --tape "$tmp/tape.jsonl" \
        --shots "$tmp/shots" --obs-dump "$tmp" --size 128x128 --hide-gui --render-distance 4) > "$tmp/log" 2>&1
    rc=$?
    [ $rc != 0 ] || [ -f "$tmp/o_$(printf %06d "$last").p1000.obs" ] || rc=1
    if [ $rc != 0 ]; then echo "FAIL play_check: $dir: the live replay (rc $rc):"; tail -3 "$tmp/log"
    else echo "play_check: $dir: the live replay reached row $last and drew its frame"; fi
    rm -rf "$tmp"
    return $rc
}
if [ ! -f "$dir/cwhash.txt" ]; then
    "$bin" "$dir" --check "$dir/tape.jsonl" || exit $?
    live
    exit $?
fi
ticks=$(sed 's/^t=\([0-9]*\) .*/\1/' "$dir/cwhash.txt" | uniq | paste -sd,)
got=$("$bin" "$dir" --check "$dir/tape.jsonl" --cw-hash "$ticks") || exit $?
d=$(diff <(sed 's/^t=/CWHASH t=/' "$dir/cwhash.txt") <(grep '^CWHASH ' <<< "$got") | head -4)
if [ -n "$d" ]; then
    echo "play_check: $dir: the client world's light differs from the oracle's:"
    echo "$d"
    exit 1
fi
echo "play_check: $dir: $(wc -l < "$dir/cwhash.txt") client sections match the oracle's"
live
