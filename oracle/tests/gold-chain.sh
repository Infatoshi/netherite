#!/bin/bash
# Re-record the golden chain on seed 42: G1 from the seed, then each segment
# from the previous segment's checkpoint (GoldBot plays; the tapes are dev
# false). Old recordings of these names must be removed first: a checkpoint
# refuses to overwrite itself. Run from oracle/: bash tests/gold-chain.sh [LAST [FIRST]]
# (FIRST > 1 re-records only FIRST..LAST, each from the checkpoint before it)
# --seed 1: the seed-1 chain instead, S1 from the fresh seed-1 world, then
# tests/s1chain-sN.jsonl from checkpoint 1/S<N-1> into snapshots/s1chain-SN.
# --root DIR: record into DIR instead (DIR/config.yaml, a copy of the tree's,
# puts the checkpoints under DIR/out/java/checkpoints and the tapes go to
# DIR/out/java/snapshots/NAME), the recorded chain untouched; a range's first
# segment starts on DIR's checkpoint before it when DIR has one, else on the
# tree's. Then each tape is compared with the tree's recording of that name
# (rows, and the header but for the harness hash and the start checkpoint's
# path and hash; a stored tape without the "s02" header flag predates the
# rows' S02 chat, one without start.spawnerPin the spawner pin, and the
# compare then drops them; the runs are fresh, CACHE=0 POOL=0): one SAME or
# DIFF line per segment, rc 1 on any DIFF.
set -e
cd "$(dirname "$0")/.."
seed=42
root=""
while [ $# -gt 0 ]; do
  case "$1" in
    --seed) seed=$2; shift 2 ;;
    --root) root=$(mkdir -p "$2" && cd "$2" && pwd); shift 2 ;;
    *) break ;;
  esac
done
last=${1:-11}
first=${2:-1}
conf=""
out=../out/java
if [ -n "$root" ]; then
  cp ../config.yaml "$root/config.yaml"
  # a proof runs fresh: no result cache, no warm pool
  conf="CONF=$root/config.yaml CACHE=0 POOL=0 POOLAUTO=0"
  out=$root/out/java
fi
diffs=0
for g in $(seq "$first" "$last"); do
  if [ "$seed" = 1 ]; then
    prev=S$((g - 1)); name=s1chain-S$g; script=tests/s1chain-s$g.jsonl
  else
    prev=G$((g - 1)); name=gold-g$g-s42; script=tests/gold-g$g-s42.jsonl
  fi
  from=""
  if [ "$g" -gt 1 ]; then
    from="FROM=../out/java/checkpoints/$seed/$prev"
    [ -n "$root" ] && [ -d "$out/checkpoints/$seed/$prev" ] && from="FROM=$out/checkpoints/$seed/$prev"
  fi
  mkdir -p "$out/snapshots/$name"
  make -s script $conf SEED=$seed $from SCRIPT=$script TAPE="$out/snapshots/$name/tape.jsonl"
  [ -n "$root" ] || continue
  # the harness hash and the start checkpoint's path and hash (its manifest
  # names the harness) aside; a stored tape from before the rows carried S02
  # chat (no "s02" header flag) is compared without it, one from before the
  # spawner pin without the start's spawnerPin
  ref=../out/java/snapshots/$name/tape.jsonl
  drop='.harness, .start.dir, .start.manifestSha256'
  head -1 "$ref" | grep -q '"s02":1' || drop="$drop, .s02"
  head -1 "$ref" | grep -q '"spawnerPin"' || drop="$drop, .start.spawnerPin"
  norm() { jq -c "del($drop)" "$1"; }
  if cmp -s <(norm "$out/snapshots/$name/tape.jsonl") <(norm "$ref"); then
    echo "$name SAME"
  else
    echo "$name DIFF $(cmp <(norm "$out/snapshots/$name/tape.jsonl") <(norm "$ref") 2>&1 | head -1)"
    diffs=1
  fi
done
exit $diffs
