#!/usr/bin/env bash
# genahead_check.sh [-j N] BIN GEN NAME...: each snapshot recording NAME
# replayed by BIN (test_snapshots, or out/native/cuda/worldgen/test_snapshots) with --gen-ahead GEN,
# N at a time (default 8), each through tests/memslot.sh; one line per
# recording (OK or the first FAIL, rows, the hit rate and its counts, what
# the misses were), then the totals. Exit 1 when any recording fails.
# `make -C csrc gpu-worldgen-ahead-check` runs it (GPU plan L15, engine/genahead.h).
set -u
J=8
[ "${1:-}" = -j ] && { J=$2; shift 2; }
BIN=$1 GEN=$2; shift 2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SNAP=$ROOT/out/java/snapshots
mkdir -p "${TMPDIR:-$HOME/dev/nw/.tmp}" "$ROOT/out/native/gpu-worldgen-ahead"
tmp=$(mktemp -d "${TMPDIR:-$HOME/dev/nw/.tmp}/gacheck.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
export ROOT SNAP BIN GEN tmp
printf '%s\n' "$@" | xargs -P "$J" -I{} bash -c '
    n=$1; d=$SNAP/$n
    bash "$ROOT/csrc/tests/memslot.sh" --key test_snapshots "$d" "$BIN" --gen-ahead "$GEN" "$d" > "$tmp/$n.log" 2>&1; rc=$?
    cp "$tmp/$n.log" "$ROOT/out/native/gpu-worldgen-ahead/$n.log"
    rows=$(grep -a "rows simulated" "$tmp/$n.log" | tail -1 | sed "s/^replay: \([0-9]*\) rows.*/\1/")
    ga=$(grep -a "^gen-ahead: " "$tmp/$n.log" | sed "s/^gen-ahead: [a-z]* //")
    if [ "$rc" = 0 ] && grep -aq ": OK" "$tmp/$n.log"; then r="OK ${rows:-?} rows"
    else r="FAIL $(grep -a "^FAIL" "$tmp/$n.log" | head -1 | cut -c6-200)"; [ "$r" = "FAIL " ] && r="FAIL rc=$rc"; fi
    echo "$r | $ga" > "$tmp/$n.res"' _ {}
ok=0 all=0 H=0 M=0 OH=0 OM=0 low=
for n in "$@"; do
    r=$(cat "$tmp/$n.res" 2>/dev/null || echo "FAIL no result |")
    printf '%-34s %s\n' "$n" "$r"
    all=$((all + 1)); case $r in OK*) ok=$((ok + 1));; esac
    h=$(echo "$r" | sed -n "s/.*| hits \([0-9]*\) misses \([0-9]*\).*/\1/p")
    m=$(echo "$r" | sed -n "s/.*| hits \([0-9]*\) misses \([0-9]*\).*/\2/p")
    oh=$(echo "$r" | sed -n "s/.*owed \([0-9]*\)\/\([0-9]*\).*/\1/p")
    ot=$(echo "$r" | sed -n "s/.*owed \([0-9]*\)\/\([0-9]*\).*/\2/p")
    H=$((H + ${h:-0})); M=$((M + ${m:-0})); OH=$((OH + ${oh:-0})); OM=$((OM + ${ot:-0} - ${oh:-0}))
done
echo "gpu-worldgen-ahead-check ($GEN): $ok/$all recordings OK; generations served ahead $H, fell back to C $M ($(awk -v h=$H -v m=$M 'BEGIN { printf "%.2f", h + m ? 100 * h / (h + m) : 100 }')% hit); owed $OH served, $OM by C; logs out/native/gpu-worldgen-ahead/NAME.log"
[ "$ok" = "$all" ]
