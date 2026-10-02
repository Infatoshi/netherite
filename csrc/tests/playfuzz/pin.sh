#!/usr/bin/env bash
# pin: keep a fuzz case's divergence as a snapshot recording the suite runs.
#
#   bash csrc/tests/playfuzz/pin.sh CASE NAME [LAST]
#   bash csrc/tests/playfuzz/pin.sh --chain RUN SESSION NAME [LAST]
#
# CASE is START:SEED or START:@SCRIPT as run.sh ran it. The recording is the session's check
# directory: its start snapshot (copied, not linked) and the Java oracle's
# rows of the played inputs, cut after row LAST (default: the whole session).
# It goes to out/java/snapshots/NAME, where make -C csrc test replays it
# (test_snapshots, and play --check through the client's tick pair).
# --chain: a chain.sh session that failed (RUN pfc-START-BASE, its SESSION):
# a session after the first starts from the previous session's checkpoint,
# which the recording's header names by its path in out/human; it is copied
# to out/java/checkpoints/SEED/ under its own name, where a recording made in
# another tree finds it (session.c resolve_checkpoint).
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
[[ $# -ge 2 ]] || { sed -n '2,16p' "$0"; exit 2; }
if [[ $1 == --chain ]]; then
    [[ $# -ge 4 ]] || { sed -n '2,16p' "$0"; exit 2; }
    run=$2 sess=$3 name=$4 last=${5:-}
    chk=out/human/$run/play/$run-s$sess-1.check
else
    c=$1 name=$2 last=${3:-}
    run=pf-${c%%:*}-${c##*:}
    [[ $c == *:@* ]] && { s=${c##*@}; run=pf-${c%%:*}-$(basename "$s" .jsonl); }
    chk=out/human/$run/play/$run-s1-1.check
fi
[[ -f $chk/tape.jsonl ]] || { echo "pin: no check dir $chk (run the case first)"; exit 1; }
dst=out/java/snapshots/$name
[[ ! -e $dst ]] || { echo "pin: $dst exists"; exit 1; }
mkdir -p "$dst"
for f in "$chk"/*; do [[ $(basename "$f") == tape.jsonl ]] || cp -rL "$f" "$dst/"; done
if [[ -n $last ]]; then
    # the lines as Java wrote them (jq would reprint the numbers)
    awk -v l="$last" 'match($0, /^\{"t":-?[0-9]+/) && substr($0, 6, RLENGTH - 5) + 0 > l { exit } { print }' \
        "$chk/tape.jsonl" > "$dst/tape.jsonl"
else
    cp "$chk/tape.jsonl" "$dst/tape.jsonl"
fi
# a checkpoint start outside out/java/checkpoints travels with the recording
read -r kind sdir seed < <(head -1 "$dst/tape.jsonl" | jq -r '[.start.kind // "-", .start.dir // "-", .seed] | @tsv')
if [[ $kind == checkpoint && $(readlink -f "$sdir") != "$root"/out/java/checkpoints/* ]]; then
    cpd=out/java/checkpoints/$seed/$(basename "$sdir")
    # a checkpoint of that name from another round (another tree's save) is
    # another start: this one goes under the recording's name, and the
    # header's start.dir (its basename is what another tree resolves) follows
    if [[ -e $cpd ]] && ! diff -rq "$sdir" "$cpd" > /dev/null; then
        cpd=out/java/checkpoints/$seed/$name-start
        sed -i "1s|\"dir\":\"$sdir\"|\"dir\":\"$root/$cpd\"|" "$dst/tape.jsonl"
    fi
    [[ -e $cpd ]] || { mkdir -p "$(dirname "$cpd")"; cp -rL "$sdir" "$cpd"; echo "pin: $cpd"; }
fi
echo "pin: $dst, $(($(wc -l < "$dst/tape.jsonl") - 1)) rows"
