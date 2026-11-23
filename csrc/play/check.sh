#!/usr/bin/env bash
# Check a tape played on the native client (out/native/tapes) against the Java
# oracle, on the canonical host (the dev host): the Java client replays the tape's inputs (its header says
# inputs_only) and records its own rows, then the native whole-server check
# replays those rows from the same snapshot and compares every field, tick by
# tick. The session lined up when test_snapshots passes.
#   csrc/play/check.sh TAPE [SNAPSHOT_DIR]
# On that host it runs in the tree this script is in; elsewhere (the Mac) it copies
# the tape to its checkout and runs there.
set -euo pipefail
tape=${1:?usage: check.sh TAPE [SNAPSHOT_DIR]}
snap=${2:-out/java/snapshots/fresh-play-s1}
[[ -f $tape ]] && [[ $(wc -l < "$tape") -ge 2 ]] || { echo "FAIL: tape has zero input rows: $tape" >&2; exit 1; }
name=$(basename "$tape" .jsonl)

# $1 the tape's name under out/native/tapes, $2 the snapshot, from the repo root
body='
set -euo pipefail
name=$1; snap=$2
[[ -s $snap/manifest.json ]] && [[ -s $snap/tape.jsonl ]] || { echo "FAIL: snapshot golden missing: $snap"; exit 1; }
run=out/java/run/tapes-$name.replay
make -s -C oracle replay REF=../out/native/tapes/$name.jsonl > out/native/tapes/$name.java.log 2>&1 || {
    tail -20 out/native/tapes/$name.java.log; exit 1;
}
grep -a "ORACLE" out/native/tapes/$name.java.log | tail -3
[ -s $run/replay.jsonl ] || { echo "no Java rows at $run/replay.jsonl"; exit 1; }
[[ $(wc -l < "$run/replay.jsonl") -ge 2 ]] || { echo "no Java tick rows at $run/replay.jsonl"; exit 1; }
chk=out/native/tapes/$name.check
rm -rf $chk; mkdir -p $chk
for f in $snap/*; do [ "$(basename $f)" = tape.jsonl ] || ln -s "$(readlink -f $f)" $chk/; done
cp $run/replay.jsonl $chk/tape.jsonl
echo "java rows: $(($(wc -l < $chk/tape.jsonl) - 1))"
bash csrc/tests/memslot.sh $snap out/native/test_snapshots $chk | tail -4
'

source "$(dirname "${BASH_SOURCE[0]}")/host.sh"
# any Linux host with the full tree (the dev host, the GPU host) runs it here;
# the Mac sends the tape to $home (csrc/play/host.sh)
if [[ $(uname -s) == Linux || -z $home ]]; then
    [[ -e $snap ]] && snap=$(readlink -f "$snap")
    root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
    mkdir -p "$root/out/native/tapes"
    src=$(readlink -f "$tape")
    [[ $src == "$root/out/native/tapes/$name.jsonl" ]] || cp "$tape" "$root/out/native/tapes/$name.jsonl"
    cd "$root"
    exec bash -c "$body" check "$name" "$snap"
fi

ssh $home "mkdir -p $R/out/native/tapes"
rsync -a "$tape" "$home:$R/out/native/tapes/$name.jsonl"
ssh $home "cd $R && bash -c $(printf %q "$body") check $(printf %q "$name") $(printf %q "$snap")"
