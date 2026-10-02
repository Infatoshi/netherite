#!/usr/bin/env bash
# The any-tick sweep: a snapshot taken at any tick must resume exactly.
#
#   bash tests/anytick.sh NAME...        (from oracle/; make anytick RECS="NAME...")
#   bash tests/anytick.sh                (every recording in tests/anytick.list)
#
# For each recording out/java/snapshots/NAME (a tape.jsonl beside its start
# snapshot), the Java oracle replays the tape and writes a Snapshot at the
# head of each chosen tick (Oracle --snap-at, the place a script's Snapshot
# command runs), into out/java/anytick/NAME/tT. The native replay then
# resumes from each one (test_snapshots --rows ROWS) and compares the tape's
# next ROWS rows. Ticks: every tick for the first EARLY ticks after the
# start snapshot, then SPREAD more spaced evenly up to the last tick that
# still has ROWS rows after it. A tick that passes has its directory
# deleted unless KEEP=1; a failing one keeps it for forensics.
#
#   ROWS=200 EARLY=30 SPREAD=25 J=4 KEEP=0 DETAIL=0 TICKS=list
#
# DETAIL=1 replays with --detail --enbt and resumes against the replay's own
# tape (identical rows plus per-entity NBT), so test_snapshots names the
# first differing entity. TICKS overrides the choice (comma separated).
# Output: one line per tick (NAME tT OK|FAIL ...), then per recording
# "anytick NAME: P/N ticks resume exactly", in out/java/anytick/NAME/summary.txt.
set -u
cd "$(dirname "$0")/.."
ROWS=${ROWS:-200}
EARLY=${EARLY:-30}
SPREAD=${SPREAD:-25}
J=${J:-4}
KEEP=${KEEP:-0}
DETAIL=${DETAIL:-0}
OUTJ=$(cd ../out/java && pwd)
NATIVE=$(cd ../out/native && pwd)/test_snapshots
[ -x "$NATIVE" ] || { echo "anytick: build $NATIVE first (make -C csrc)"; exit 2; }
mkdir -p "$OUTJ/anytick"
# test_snapshots finds a checkpoint start under DIR/../../checkpoints
ln -sfn ../checkpoints "$OUTJ/anytick/checkpoints"
rc=0
[ $# -gt 0 ] || set -- $(grep -v '^#' tests/anytick.list)

for name in "$@"; do
    src="$OUTJ/snapshots/$name"
    tape="$src/tape.jsonl"
    [ -f "$tape" ] || { echo "anytick: no $tape"; rc=2; continue; }
    start=$(jq -r .tick "$src/manifest.json")
    nrows=$(($(wc -l < "$tape") - 1))
    last=$((nrows - ROWS))
    # the header's non-Dev setup ticks: those run before a snapshot at the
    # same tick, which the resume cannot tell apart; skip them
    skip=$(head -1 "$tape" | jq -r '[.setup[]? | select(.class != "Dev") | .tick] | unique | join(" ")')
    if [ -n "${TICKS:-}" ]; then
        ticks=${TICKS//,/ }
    else
        ticks=""
        for ((t = start + 1; t <= start + EARLY && t <= last; ++t)); do ticks="$ticks $t"; done
        lo=$((start + EARLY + 1))
        if [ "$last" -gt "$lo" ]; then
            for ((i = 0; i < SPREAD; ++i)); do ticks="$ticks $((lo + (last - lo) * i / (SPREAD - 1)))"; done
        fi
    fi
    list=""
    for t in $ticks; do
        case " $skip " in *" $t "*) continue ;; esac
        case ",$list," in *",$t,"*) continue ;; esac
        list="${list:+$list,}$t"
    done
    [ -n "$list" ] || { echo "anytick: $name has no tick with $ROWS rows after it"; rc=2; continue; }

    dir="$OUTJ/anytick/$name"
    rm -rf "$dir"
    mkdir -p "$dir"
    args=""
    [ "$DETAIL" = 1 ] && args="--detail --enbt"
    log="$dir/java.log"
    make -s replay REF="$tape" SNAPAT="$list" SNAPDIR="$dir" ARGS="$args" > "$log" 2>&1
    if ! grep -aq "ORACLE REPLAY OK" "$log"; then
        echo "anytick: $name: the Java replay did not finish (see $log)"
        grep -a "ORACLE DIVERGE" "$log" | head -3
        rc=1
        continue
    fi
    ref="$tape"
    if [ "$DETAIL" = 1 ]; then
        cp "$OUTJ/run/$name-tape.replay/replay.jsonl" "$dir/tape.jsonl"
        ref="$dir/tape.jsonl"
    fi

    for t in ${list//,/ }; do
        [ -d "$dir/t$t" ] || { echo "$name t$t FAIL no snapshot written"; continue; }
        ln -sfn "$ref" "$dir/t$t/tape.jsonl"
        echo "$t"
    done | xargs -P "$J" -I{} bash -c '
        d="$1/t$2"
        out=$("$3" --rows "$4" "$d" 2>&1)
        sim=$(printf "%s\n" "$out" | grep -a "rows simulated" | sed "s/replay: //")
        if printf "%s\n" "$out" | grep -aq ": OK:"; then
            echo "$5 t$2 OK $sim"
            [ "$6" = 1 ] || rm -rf "$d"
        else
            first=$(printf "%s\n" "$out" | grep -a "^FAIL" | head -1 | cut -c6-240)
            printf "%s\n" "$out" > "$d/native.log"
            echo "$5 t$2 FAIL $first"
        fi' _ "$dir" {} "$NATIVE" "$ROWS" "$name" "$KEEP" | sort -V -k2,2 > "$dir/summary.txt"

    n=$(wc -l < "$dir/summary.txt")
    ok=$(grep -c " OK " "$dir/summary.txt")
    cat "$dir/summary.txt"
    echo "anytick $name: $ok/$n ticks resume exactly ($ROWS rows each, start snapshot at $start, $nrows rows)" | tee -a "$dir/summary.txt"
    [ "$ok" = "$n" ] || rc=1
done
exit $rc
