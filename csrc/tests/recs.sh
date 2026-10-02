#!/usr/bin/env bash
# recs: replay several recordings at once, one line each.
#
#   out/native/recs [-j N] NAME|GLOB|DIR... [-- test_snapshots flags...]
#
# NAME is a recording under out/java/snapshots, GLOB a quoted pattern over
# those names ('gold-g2*', 'end-*'), DIR a recording directory. All of them
# replay natively at once (J at a time, default 16), each through
# tests/memslot.sh so the machine-wide memory budget holds, and print in the
# order given:
#   NAME  OK 13180 rows 5.9s
#   NAME  FAIL replay: first differing row 150 field d.sw want ... got ...
# then "recs: P/N OK"; the exit status is 1 when any fails. Flags after --
# go to every test_snapshots run (--rows N, --from-row N, --fault KIND@ROW).
# A failing run's whole log is kept in out/native/recs-logs/NAME.log (out/native/recs
# is this script, installed); then
# out/native/diverge NAME explains it. Lanes wrote this as a serial for loop
# 229 times by 2026-09-26 (116 minutes of replays run one at a time).
set -u
shopt -s nullglob
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ -f "$ROOT/oracle/Makefile" ] || { echo "recs: run it as out/native/recs in a netherite-v2 tree"; exit 2; }
NATIVE=$ROOT/out/native/test_snapshots
SNAP=$ROOT/out/java/snapshots
J=16
[ "${1:-}" = -j ] && { J=$2; shift 2; }
names=(); flags=()
while [ $# -gt 0 ]; do
    [ "$1" = -- ] && { shift; flags=("$@"); break; }
    names+=("$1"); shift
done
[ ${#names[@]} -gt 0 ] || { sed -n '3,14p' "$0"; exit 2; }
[ -x "$NATIVE" ] || { echo "recs: build $NATIVE first (make -C csrc)"; exit 2; }

dirs=()
seen=" "
for a in "${names[@]}"; do
    if [ -d "$a" ] && [ -f "$a/tape.jsonl" ]; then
        m=("$(cd "$a" && pwd)")
    else
        m=()
        for d in $SNAP/$a/; do [ -f "$d/tape.jsonl" ] && m+=("${d%/}"); done
        [ ${#m[@]} -gt 0 ] || { echo "recs: no recording matches $a"; exit 2; }
    fi
    for d in "${m[@]}"; do
        case "$seen" in *" $d "*) continue;; esac
        seen="$seen$d "; dirs+=("$d")
    done
done

LOG=$ROOT/out/native/recs-logs
mkdir -p "$LOG" "${TMPDIR:-$HOME/dev/nw/.tmp}"
tmp=$(mktemp -d "${TMPDIR:-$HOME/dev/nw/.tmp}/recs.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
export ROOT NATIVE LOG tmp
for i in "${!dirs[@]}"; do echo "$i ${dirs[$i]}"; done | xargs -P "$J" -I{} bash -c '
    i=${1%% *}; d=${1#* }; n=$(basename "$d"); shift
    s=$(date +%s%N)
    bash "$ROOT/csrc/tests/memslot.sh" --t "$tmp/$i.acq" "$d" "$NATIVE" "$@" "$d" > "$tmp/$i.log" 2>&1; rc=$?
    e=$(date +%s%N); a=$(cat "$tmp/$i.acq" 2>/dev/null || echo "$s")
    ds=$(( (e - a) / 100000000 )); run="$((ds / 10)).$((ds % 10))s"
    dw=$(( (a - s) / 1000000000 )); [ "$dw" -ge 1 ] && run="$run (+${dw}s waiting for memory)"
    if [ "$rc" = 0 ] && grep -aq ": OK" "$tmp/$i.log"; then
        rows=$(grep -a "rows simulated" "$tmp/$i.log" | tail -1 | sed "s/^replay: \([0-9]*\) rows.*/\1/")
        echo "OK ${rows:-?} rows $run" > "$tmp/$i.res"
    else
        f=$(grep -a "^FAIL" "$tmp/$i.log" | head -1 | cut -c6-220)
        [ -n "$f" ] || f="rc=$rc: $(tail -1 "$tmp/$i.log" | cut -c1-180)"
        echo "FAIL $f ($run)" > "$tmp/$i.res"
        cp "$tmp/$i.log" "$LOG/$n.log"
    fi' _ {} "${flags[@]}"

ok=0; w=0
for d in "${dirs[@]}"; do n=$(basename "$d"); [ ${#n} -gt $w ] && w=${#n}; done
for i in "${!dirs[@]}"; do
    r=$(cat "$tmp/$i.res" 2>/dev/null || echo "FAIL no result")
    printf '%-*s  %s\n' "$w" "$(basename "${dirs[$i]}")" "$r"
    case $r in OK*) ok=$((ok + 1));; esac
done
echo "recs: $ok/${#dirs[@]} OK"
[ "$ok" = "${#dirs[@]}" ]
