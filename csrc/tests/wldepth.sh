#!/usr/bin/env bash
# The worklists' depth check (make -C csrc worklist-depth): runs every test
# built with -DNETHERITE_WL_DEPTH over its data directories, as make test
# maps them, and prints for each explicit stack that replaced a recursion in
# the tick (csrc/engine/blockwl.h) the deepest point any run reached and the
# run that reached it.
#   bash tests/wldepth.sh BUILD_DIR DATA_DIR
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
bin=$1 data=$2
t=$(mktemp -d)
for b in "$bin"/test_*; do
  x=${b##*/test_}
  case $x in worldgen) d=chunks;; probe|feature) d=probes;; structures) d=structures;; move) d=moves;; *) d=$x;; esac
  if [ -d "$data/$d" ]; then
    for s in "$data/$d"/*/; do
      if [ "$x" = feature ] || [ "$x" = probe ]; then
        k=$(jq -r '.kind // "setblock"' "$s/manifest.json")
        [ "$x" != feature ] || [ "$k" = feature ] || continue
        [ "$x" != probe ] || [ "$k" != feature ] || continue
      fi
      echo "$b ${s%/}"
    done
  else echo "$b -"; fi
done > "$t/jobs"
T=$t xargs -P "${WLJ:-24}" -L1 sh -c '
  n=$(basename "$0")_$(basename "$1")
  if [ "$1" = - ]; then "$0" > /dev/null 2> "$T/$n"; else bash tests/memslot.sh "$1" "$0" "$1" > /dev/null 2> "$T/$n"; fi
  echo $? > "$T/$n.rc"' < "$t/jobs"
fail=0
for f in "$t"/*.rc; do [ "$(cat "$f")" = 0 ] || { echo "FAILED ${f##*/}"; fail=1; }; done
for f in "$t"/test_*; do
  case $f in *.rc) continue;; esac
  l=$(grep -o 'wldepth: .*' "$f" | tail -1)
  [ -n "$l" ] && echo "${f##*/} ${l#wldepth: }"
done | awk '{t = $1 ~ /^test_snapshots_/; for (i = 2; i <= NF; ++i) { split($i, kv, "=");
    if (!(kv[1] in best) || kv[2] + 0 > best[kv[1]] + 0) { best[kv[1]] = kv[2]; who[kv[1]] = $1 }
    if (t && (!(kv[1] in tb) || kv[2] + 0 > tb[kv[1]] + 0)) { tb[kv[1]] = kv[2]; tw[kv[1]] = $1 } } ++runs; tr += t}
  END {printf "%d runs, %d of them snapshot replays (the tick)\n", runs, tr;
       for (k in best) printf "%-11s deepest %4d (%s); in a replay %4d (%s)\n", k, best[k], who[k], tb[k], tw[k]}' | sort
rm -rf "$t"
exit $fail
