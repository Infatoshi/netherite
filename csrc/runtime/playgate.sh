#!/bin/bash
# playgate.sh (lane/divlib; vecenv.mk's rl-play-gate): a checkpoint start played by the vector env and by the pool
# alone, row for row, and the same start refused when its checkpoint's save is not there.
#
#   playgate.sh CONF START OUT [ACTS] [SEEDS]
#
# 1. equal: for each seed, vecplay's ACTS random acts (default 2600) from START through the vector env and through the
#    pool's plain replay (vecplay --pool): the row records (every field test_snapshots compares, d.cw aside: "-" on both
#    sides) byte for byte equal. A difference prints the first differing row and field.
# 2. refused: START copied with its tape header's checkpoint pointed at a directory holding the checkpoint's manifest
#    and (a) no save at all, (b) a packed save (save.tar.zst, the library's archive form): the vector env and the pool
#    must both refuse it, the error naming the missing save (before lane/divlib both played it, every saved chunk
#    generated afresh: seed 42's End start left the Java client at its 95th tick).
# Last line RL-PLAY-GATE PASS or FAIL.
set -u
CONF=$1 START=$2 OUT=$3 ACTS=${4:-2600} SEEDS=${5:-"1 2"}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VP=$ROOT/out/runtime/vecplay
fail() { echo "$1"; echo "RL-PLAY-GATE FAIL"; exit 1; }
[ -s "$START/manifest.json" ] && [ -s "$START/tape.jsonl" ] || fail "no start at $START (the kills' library: PLG_START=LIB/ID/start)"
CP=$(head -1 "$START/tape.jsonl" | jq -r '.start.dir // empty')
[ -n "$CP" ] && [ -s "$CP/manifest.json" ] || fail "$START is not a checkpoint start with its checkpoint in place"
[ -d "$CP/save" ] || fail "$CP/save missing: unpack the library first (harvest.sh -l LIB --unpack-all)"
rm -rf "$OUT" && mkdir -p "$OUT" && ln -s "$(realpath "$START")" "$OUT/start"   # (play-gate.yaml's pipeline.start)
for s in $SEEDS; do
  "$VP" --config "$CONF" --start "$START" --random "$s" --acts "$ACTS" --rows "$OUT/vec-$s.rows" > "$OUT/vec-$s.log" 2>&1 ||
    fail "vector env, seed $s: $(tail -1 "$OUT/vec-$s.log")"
  "$VP" --config "$CONF" --start "$START" --random "$s" --acts "$ACTS" --pool --rows "$OUT/pool-$s.rows" > "$OUT/pool-$s.log" 2>&1 ||
    fail "pool, seed $s: $(tail -1 "$OUT/pool-$s.log")"
  n=$(wc -l < "$OUT/pool-$s.rows")
  [ "$n" -eq $((ACTS + 1)) ] || fail "seed $s: the pool wrote $n rows, not $((ACTS + 1))"
  if ! cmp -s "$OUT/vec-$s.rows" "$OUT/pool-$s.rows"; then
    first=$(awk 'NR == FNR {a[FNR] = $0; next} a[FNR] != $0 {n = split(a[FNR], x, " "); split($0, y, " ");
                 for (i = 1; i <= n; ++i) if (x[i] != y[i]) {split(x[i], p, "="); print $1, p[1]; exit} print $1, "(length)"; exit}' \
            "$OUT/pool-$s.rows" "$OUT/vec-$s.rows")
    fail "seed $s: the vector env leaves the pool's replay at $first"
  fi
  echo "seed $s: $n rows, the vector env's equal to the pool's"
done
# 2: the refusals
for kind in nosave packed; do
  C=$OUT/cp-$kind S=$OUT/start-$kind
  mkdir -p "$C" && cp "$CP/manifest.json" "$C/"
  [ $kind = packed ] && printf 'not a save\n' > "$C/save.tar.zst"
  mkdir -p "$S" && for f in "$START"/*; do [ "$(basename "$f")" = tape.jsonl ] || ln -s "$(realpath "$f")" "$S/"; done
  { head -1 "$START/tape.jsonl" | jq -c --arg d "$(realpath "$C")" '.start.dir = $d'; tail -n +2 "$START/tape.jsonl"; } > "$S/tape.jsonl"
  for side in vec pool; do
    arg=; [ $side = pool ] && arg=--pool
    if "$VP" --config "$CONF" --start "$S" --random 1 --acts 20 $arg --rows "$OUT/$kind-$side.rows" > "$OUT/$kind-$side.log" 2>&1; then
      fail "$kind, $side: a start whose checkpoint has no save was played, not refused"
    fi
    grep -q "save missing" "$OUT/$kind-$side.log" || fail "$kind, $side: refused, but not for the save: $(tail -1 "$OUT/$kind-$side.log")"
    [ $kind = packed ] && ! grep -q "packed" "$OUT/$kind-$side.log" && fail "packed, $side: the refusal does not say packed"
  done
  echo "$kind: refused by the vector env and the pool ($(grep -o 'save missing[^)]*)\?' "$OUT/$kind-vec.log" | head -1))"
done
echo "RL-PLAY-GATE PASS"
