#!/usr/bin/env bash
# diverge: a recording's first native divergence, explained on one screen.
#
#   out/native/diverge RECORDING [ROW] [test_snapshots flags...]
#
# RECORDING is a name under out/java/snapshots or a recording directory. The
# replay starts at the keyframe at or before ROW (test_snapshots --from-row);
# with no ROW every keyframe segment runs at once and the earliest mismatch
# wins (the whole replay when there are no keyframes). Extra flags go to
# test_snapshots: --keyframe DIR starts from any snapshot of the run (an
# any-tick sweep's tT), --fault KIND@ROW plants a divergence.
#
# At the first mismatching row R it prints the field with both values, the
# rows before it (each row's differing server fields, want/got, and both
# sides' server entity id counters), then the structural diff of the whole
# native state after row R against the oracle's state at the head of tick
# R+1 (a keyframe when one is at R+1, else the oracle replays the tape with
# a snapshot there and --detail rows over the window; cached in
# out/java/diverge/NAME), grouped by world scalars, entities, chunks and
# pending ticks, and for the first entity that differs its NBT on each side
# row by row and the rows in which the oracle's copy changed.
#
#   W=8 (rows of history)  J=8 (segments at once)  JAVA=0 (no oracle run: the diff only when a keyframe is at R+1)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ -f "$ROOT/oracle/Makefile" ] || { echo "diverge: run it as out/native/diverge in a netherite-v2 tree"; exit 2; }
NATIVE=$ROOT/out/native/test_snapshots
OUTJ=$ROOT/out/java
W=${W:-8}
J=${J:-8}
JAVA=${JAVA:-1}
[ $# -ge 1 ] || { sed -n '3,14p' "$0"; exit 2; }
rec=$1; shift
[ -d "$rec" ] || rec=$OUTJ/snapshots/$rec
rec=${rec%/}
[ -f "$rec/tape.jsonl" ] || { echo "diverge: $rec has no tape.jsonl"; exit 2; }
name=$(basename "$rec")
row=
case "${1:-}" in [0-9]*) row=$1; shift;; esac
extra=("$@")
has_keyframe=0
for a in "${extra[@]}"; do [ "$a" = --keyframe ] && has_keyframe=1; done
tmp=$(mktemp -d "${TMPDIR:-$HOME/dev/nw/.tmp}/diverge.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

first_of() { grep -a "first differing row" "$1" | head -1 | sed 's/.*first differing row //; s/ (.*//'; }

# ---- 1. the first mismatch
start=()
if [ "$has_keyframe" = 1 ]; then :
elif [ -n "$row" ]; then start=(--from-row "$row")
elif bash "$ROOT/csrc/tests/segments.sh" "$rec" > "$tmp/segs"; then
    # every segment at once, rows only; the earliest first mismatch wins
    i=0
    while read -r rows tag flags; do
        i=$((i + 1))
        f=$(echo "$flags" | sed 's/ --state-diff [^ ]*//')
        echo "$i $f"
    done < "$tmp/segs" > "$tmp/jobs"
    xargs -P "$J" -L1 bash -c 'i=$1; shift; "$0" --stop-first "$@" > '"$tmp"'/seg.$i 2>&1' "$NATIVE" < <(while read -r i f; do echo "$i $f ${extra[*]} $rec"; done < "$tmp/jobs")
    best=; bestrow=
    while read -r i f; do
        r=$(first_of "$tmp/seg.$i" | cut -d' ' -f1)
        [ -n "$r" ] || continue
        if [ -z "$bestrow" ] || [ "$r" -lt "$bestrow" ]; then bestrow=$r; best=$f; fi
    done < "$tmp/jobs"
    if [ -z "$bestrow" ]; then
        echo "diverge $name: no divergence; every keyframe segment replays exactly ($(wc -l < "$tmp/jobs") segments)"
        exit 0
    fi
    read -r -a start <<< "$best"
fi
"$NATIVE" --stop-first "${start[@]}" "${extra[@]}" "$rec" > "$tmp/first" 2>&1
first=$(first_of "$tmp/first")
if [ -z "$first" ]; then
    if grep -aq ": OK:" "$tmp/first"; then
        echo "diverge $name: no divergence from $(grep -a '^keyframe ' "$tmp/first" | cut -c10- || echo "the recording's start")$( [ -n "$row" ] && echo " (row $row)")"
        grep -a "rows simulated" "$tmp/first"
        exit 0
    fi
    echo "diverge $name: the replay failed before any row mismatch:"
    grep -a "^FAIL" "$tmp/first" | head -5
    exit 1
fi
R=${first%% *}
kf=$(grep -a '^keyframe ' "$tmp/first" | head -1 | cut -c10-)
if [ -n "$kf" ]; then kf=$(realpath -sm --relative-to="$ROOT" "$kf"); else kf="the recording's own start"; fi
echo "diverge $name: first mismatch at row $R: ${first#* field }"
shown=(); skip=0
for a in "${extra[@]}"; do [ "$skip" = 1 ] && { skip=0; continue; }; [ "$a" = --keyframe ] && { skip=1; continue; }; shown+=("$a"); done
echo "  replayed from $kf$( [ ${#shown[@]} -gt 0 ] && echo " with ${shown[*]}")"
grep -a "^fault:" "$tmp/first" | sed 's/^/  /'
grep -a "^first entity mismatch\|^entity count want" "$tmp/first" | sed 's/^/  tape order: /' | head -2

# ---- 2. the oracle's state at the head of tick R+1
T=$((R + 1))
set_dir=$OUTJ/keyframes/$name
jdir=; detail=
if [ -f "$set_dir/t$T/manifest.json" ] && jq -e --argjson t "$T" '.keyframes | index($t)' "$set_dir/set.json" > /dev/null 2>&1; then
    jdir=$set_dir/t$T
fi
cache=$OUTJ/diverge/$name
if [ -f "$cache/t$T/manifest.json" ] && [ -f "$cache/t$T/tape.detail.jsonl" ] \
    && [ "$(tail -n 1 "$rec/tape.jsonl" | sha1sum | cut -c1-40)" = "$(cat "$cache/t$T/tape.sha1" 2>/dev/null)" ]; then
    jdir=$cache/t$T
    detail=$cache/t$T/tape.detail.jsonl
elif [ "$JAVA" = 1 ]; then
    lo=$((R - W)); [ "$lo" -ge 0 ] || lo=0
    rm -rf "$cache/t$T"
    mkdir -p "$cache"
    s0=$(date +%s)
    make -s -C "$ROOT/oracle" replay REF="$rec/tape.jsonl" SNAPAT=$T SNAPDIR="$cache" \
        ARGS="--detail-from $lo --until $((T + 1))" EVERY= FRAMES= > "$tmp/java.log" 2>&1
    if grep -aq "ORACLE REPLAY UNTIL\|ORACLE REPLAY OK" "$tmp/java.log" && [ -f "$cache/t$T/manifest.json" ]; then
        cp "$OUTJ/run/$name-tape.replay/replay.jsonl" "$cache/t$T/tape.detail.jsonl"
        tail -n 1 "$rec/tape.jsonl" | sha1sum | cut -c1-40 > "$cache/t$T/tape.sha1"
        rm -rf "$OUTJ/run/$name-tape.replay"
        jdir=$cache/t$T
        detail=$cache/t$T/tape.detail.jsonl
        echo "  oracle: replayed to tick $T for its snapshot and detail rows from $lo ($(( $(date +%s) - s0 )) s; cached in ${cache#$ROOT/})"
    else
        echo "  oracle: the replay for a snapshot at $T failed (make -C oracle replay; see below)"
        grep -a "ORACLE DIVERGE\|Exception" "$tmp/java.log" | head -3
    fi
fi

# ---- 3. the rows before it and the state after it, natively
lo=$((R - W)); [ "$lo" -ge 0 ] || lo=0
args=(--to-row "$T" --trace-rows "$((R - 3))")
[ -z "$detail" ] || args+=(--tape "$detail")
[ -z "$jdir" ] || args+=(--state-diff "$jdir")
"$NATIVE" "${start[@]}" "${extra[@]}" "${args[@]}" "$rec" > "$tmp/diff" 2>&1
echo "rows before it (server fields that differ, want/got; the server entity id counter, oracle/native):"
grep -a "^row [0-9]*:" "$tmp/diff" | sed 's/^/  /'
grep -a "^FAIL replay" "$tmp/diff" | grep -v "first differing row" | head -2 | sed 's/^/  /'
if [ -n "$jdir" ]; then
    echo "state after row $R, native against the oracle's (${jdir#$ROOT/}):"
    sed -n '/^state at the head of tick/,/^\(FAIL \)\{0,1\}state diff/p' "$tmp/diff" | sed '1d; $d' | sed 's/^/  /'
    grep -a "state diff" "$tmp/diff" | tail -1 | sed 's/^FAIL //; s/^/  /'
else
    echo "state diff: no oracle snapshot at tick $T (JAVA=0, or the oracle replay failed)"
fi

# ---- 4. the first entity that differs, row by row
eid=$(sed -n '/^state at the head of tick/,$p' "$tmp/diff" | sed -n '/^entities:/,/^chunks:/p' | grep -ao "id [0-9][0-9]*" | head -1 | cut -c4-)
if [ -n "$eid" ] && [ -n "$detail" ]; then
    echo "entity $eid, rows $lo..$R:"
    "$NATIVE" "${start[@]}" "${extra[@]}" --to-row "$T" --tape "$detail" --trace-rows "$lo" --watch "$eid" "$rec" > "$tmp/watch" 2>&1
    awk '/^row [0-9]*:/ {r=$2} /^  entity [0-9]*:/ {sub(/^  entity [0-9]*: /, ""); print "  native vs oracle at row " r " " $0; next} /^    / {print "  " $0}' "$tmp/watch" \
        | sed 's/row \([0-9]*\): /row \1: /' | grep -v ": equal$" | head -16
    n_equal=$(grep -ac "^  entity $eid: equal" "$tmp/watch")
    echo "  (equal on $n_equal of the rows before)"
    echo "  the oracle's copy, the rows that changed it:"
    tail -n +2 "$detail" | jq -c --argjson lo "$lo" --argjson hi "$R" --arg pre "$eid:" '
        select(.t >= $lo and .t <= $hi) | [.t, ((.x.enbt // []) | map(select(startswith($pre))) | .[0] // null)]' 2>/dev/null \
      | jq -r -s '
        def leaves: [paths(scalars) as $p | {key: ($p | map(tostring) | join(".")), value: getpath($p)}] | from_entries;
        reduce .[] as [$t, $e] ({prev: null, out: []};
          (if $e == null then null else ($e | sub("^[0-9]+:"; "") | fromjson | leaves) end) as $now |
          .prev as $was |
          .out += [if $was == null and $now == null then empty
                   elif $was == null then "    row \($t): present (\($now | keys | length) fields)"
                   elif $now == null then "    row \($t): gone"
                   else ([$now | to_entries[] | select(.value != $was[.key]) | .key] as $ch |
                         if ($ch | length) == 0 then empty
                         else "    row \($t): \($ch | .[0:8] | join(", "))\(if ($ch | length) > 8 then " (+\(($ch | length) - 8))" else "" end)" end)
                   end] | .prev = $now) | .out[]' 2>/dev/null | head -12
elif [ -n "$eid" ]; then
    echo "entity $eid: no oracle detail rows for its history (JAVA=0)"
fi
