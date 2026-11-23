#!/usr/bin/env bash
# Keyframes: snapshots every EVERY ticks of a recording, so a native replay
# can start near any row (test_snapshots --from-row N) and the suite can run
# a long recording as independent segments.
#
#   bash tests/keyframes.sh NAME...      (from oracle/; make keyframes RECS="NAME..." [KEVERY=1000])
#   bash tests/keyframes.sh              (every recording under out/java/snapshots)
#
# For each recording out/java/snapshots/NAME the Java oracle replays the
# tape (rows unchanged: the replay stops on any differing row) and writes a
# Snapshot at the head of ticks start+K, start+2K, ... (K = KEVERY) into
# out/java/keyframes/NAME/tT, the chunk bytes as blobs shared by the set
# (NAME/blobs). Then every segment is checked natively, from its keyframe
# (or the recording's own start) to the next one: its rows exact, and the
# state after its last row against the next keyframe (--state-diff). A
# keyframe whose segment does not replay exactly is dropped (kept as xT for
# forensics) and the segment before it runs on to the next one; set.json lists the kept keyframes,
# each one's round-trip result, the dropped ones with the first failure,
# and the tape's fingerprint (the SHA-1 of its last row), so a re-recorded
# tape never runs from stale keyframes. A recording no longer than K
# rows gets a set with no keyframes (its own start is the only one).
#
#   KEVERY=1000 J=4 FORCE=0 (FORCE=1 remakes a set whose tape still matches;
#   RECHECK=1 checks the snapshots already on disk again, no oracle run)
set -u
cd "$(dirname "$0")/.."
EVERY=${KEVERY:-1000}
J=${J:-4}
FORCE=${FORCE:-0}
RECHECK=${RECHECK:-0}
OUTJ=$(cd ../out/java && pwd)
NATIVE=$(cd ../out/native && pwd)/test_snapshots
[ -x "$NATIVE" ] || { echo "keyframes: build $NATIVE first (make -C csrc)"; exit 2; }
mkdir -p "$OUTJ/keyframes"
rc=0
if [ $# -eq 0 ]; then
    set -- $(for d in "$OUTJ"/snapshots/*/; do [ -f "$d/tape.jsonl" ] && [ -f "$d/manifest.json" ] && basename "$d"; done)
fi

lastsha() { tail -n 1 "$1" | tr -d '\r\n' | sha1sum | cut -c1-40; }

# one segment: FROM (a keyframe dir, or - for the recording's start) to TO
# (a tick, or - for the tape's end), with the state diff against DIFF (-: none)
segment() {
    local rec=$1 from=$2 to=$3 diff=$4 args=()
    [ "$from" = - ] || args+=(--keyframe "$from")
    [ "$to" = - ] || args+=(--to-row "$to")
    [ "$diff" = - ] || args+=(--state-diff "$diff")
    "$NATIVE" "${args[@]}" "$rec" 2>&1
}

for name in "$@"; do
    src="$OUTJ/snapshots/$name"
    tape="$src/tape.jsonl"
    [ -f "$tape" ] || { echo "keyframes: no $tape"; rc=2; continue; }
    set_dir="$OUTJ/keyframes/$name"
    sha=$(lastsha "$tape")
    if [ "$FORCE" != 1 ] && [ "$RECHECK" != 1 ] && [ -f "$set_dir/set.json" ] && [ "$(jq -r .tape.lastSha1 "$set_dir/set.json")" = "$sha" ]; then
        echo "keyframes $name: up to date ($(jq -c .keyframes "$set_dir/set.json"))"
        continue
    fi
    start=$(jq -r .tick "$src/manifest.json")
    nrows=$(($(wc -l < "$tape") - 1))
    # the header's non-Dev setup ticks run before a snapshot at the same
    # tick, which a resume cannot tell apart: a keyframe moves past them
    skip=" $(head -1 "$tape" | jq -r '[.setup[]? | select(.class != "Dev") | .tick] | unique | join(" ")') "
    ticks=""
    for ((t = start + EVERY; t < nrows; t += EVERY)); do
        k=$t
        while case "$skip" in *" $k "*) true;; *) false;; esac; do k=$((k + 1)); done
        [ "$k" -lt "$nrows" ] && ticks="$ticks $k"
    done
    t0=$(date +%s.%N)
    if [ "$RECHECK" = 1 ] && [ -d "$set_dir" ]; then
        # the snapshots on disk (a dropped one back in), checked again
        for x in "$set_dir"/x[0-9]*; do [ -d "$x" ] && mv "$x" "$set_dir/t${x##*/x}"; done
        ticks=$(for x in "$set_dir"/t[0-9]*; do [ -d "$x" ] && echo "${x##*/t}"; done | sort -n | xargs)
    elif [ -n "$ticks" ]; then
        rm -rf "$set_dir"
        mkdir -p "$set_dir"
        list=$(echo $ticks | tr ' ' ',')
        log="$set_dir/java.log"
        make -s replay REF="$tape" SNAPAT="$list" SNAPDIR="$set_dir" EVERY= FRAMES= > "$log" 2>&1
        rm -rf "$OUTJ/run/$name-tape.replay"
        if ! grep -aq "ORACLE REPLAY OK" "$log"; then
            echo "keyframes $name: the Java replay did not finish (see $log)"
            grep -a "ORACLE DIVERGE" "$log" | head -3
            rc=1
            continue
        fi
        # the snapshots' own timing lines, kept for the report
        grep -a "ORACLE SNAPSHOT TIME" "$log" > "$set_dir/snaptime.txt"
        rm -f "$log"
    else
        rm -rf "$set_dir"
        mkdir -p "$set_dir"
    fi
    t1=$(date +%s.%N)

    # verify: segments between the kept keyframes, dropping any keyframe
    # whose own segment fails, until every segment passes
    keep="$ticks"
    dropped=""
    declare -A why=() round=()
    while :; do
        pts=(- $keep)
        jobs=""
        for ((i = 0; i < ${#pts[@]}; ++i)); do
            from=${pts[$i]}; next=${pts[$((i + 1))]:-}
            fdir=-; [ "$from" = - ] || fdir="$set_dir/t$from"
            to=-; ddir=-
            [ -z "$next" ] || { to=$next; ddir="$set_dir/t$next"; }
            jobs="$jobs$from $fdir $to $ddir"$'\n'
        done
        results=$(printf '%s' "$jobs" | xargs -P "$J" -L1 bash -c '
            out=$('"$(declare -f segment)"'; NATIVE='"$NATIVE"' segment "$0" "$2" "$3" "$4")
            first=$(printf "%s\n" "$out" | grep -a "first differing row" | head -1 | sed "s/.*first differing row //" | cut -c1-160)
            other=$(printf "%s\n" "$out" | grep -a "^FAIL" | grep -v "first differing row\|state diff" | head -1 | cut -c6-160)
            sd=$(printf "%s\n" "$out" | grep -a "^FAIL state diff\|^state diff" | head -1 | cut -c1-160)
            rows=$(printf "%s\n" "$out" | grep -a "rows simulated" | sed "s/replay: //; s/ rows simulated.*//")
            # rows exact only when the replay says so itself (a crash prints no FAIL line)
            ok=0; printf "%s\n" "$out" | grep -aq "rows simulated from the snapshot, 0 field mismatches" && [ -z "$first$other" ] && ok=1
            [ "$ok" = 1 ] || [ -n "$first$other" ] || other="no summary line (the replay did not finish)"
            rt=-; case "$sd" in "state diff: the state"*) rt=1;; "FAIL state diff"*) rt=0;; esac
            printf "%s %s %s %s %s|%s|%s\n" "$1" "$3" "$ok" "$rt" "${rows:-0}" "$first$other" "$sd"' "$src")
        bad=""
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            from=${line%% *}; rest=${line#* }; to=${rest%% *}; rest=${rest#* }
            ok=${rest%% *}; rest=${rest#* }; rt=${rest%% *}
            detail=${line#*|}
            [ "$to" = - ] || round[$to]=$rt
            [ "$rt" = 0 ] && why[$to]="round-trip: ${detail#*|}"
            if [ "$ok" = 0 ]; then
                if [ "$from" = - ]; then
                    echo "keyframes $name: the recording's own start fails from row $start (${detail%%|*})"
                else
                    bad="$bad $from"
                    why[$from]="its segment: ${detail%%|*}"
                fi
            fi
        done <<< "$results"
        [ -n "$bad" ] || break
        for b in $bad; do
            keep=$(echo " $keep " | sed "s/ $b / /" | xargs)
            dropped="$dropped $b"
        done
    done
    t2=$(date +%s.%N)

    kj=$(for k in $keep; do echo "$k"; done | jq -s -c .)
    rj=$(for k in $keep; do r=${round[$k]:-null}; [ "$r" = - ] && r=null; echo "{\"t\":$k,\"ok\":$r}"; done | jq -s -c 'map({(.t|tostring): (.ok == 1)}) | add // {}')
    dj=$(for d in $dropped; do jq -n -c --argjson t "$d" --arg w "${why[$d]:-}" '{tick: $t, why: $w}'; done | jq -s -c .)
    fj=$(for k in $keep; do [ "${round[$k]:-}" = 0 ] && jq -n -c --argjson t "$k" --arg w "${why[$k]:-}" '{tick: $t, why: $w}'; done | jq -s -c .)
    jq -n -c --arg sha "$sha" --argjson rows "$nrows" --argjson every "$EVERY" --argjson start "$start" \
        --argjson keyframes "$kj" --argjson roundtrip "$rj" --argjson dropped "$dj" --argjson rtfail "$fj" \
        --arg java "$(awk -v a="$t0" -v b="$t1" 'BEGIN {printf "%.1f", b - a}')" \
        --arg check "$(awk -v a="$t1" -v b="$t2" 'BEGIN {printf "%.1f", b - a}')" \
        '{tape: {rows: $rows, lastSha1: $sha}, every: $every, start: $start, keyframes: $keyframes,
          roundtrip: $roundtrip, roundtripFails: $rtfail, dropped: $dropped, seconds: {java: ($java | tonumber), check: ($check | tonumber)}}' \
        > "$set_dir/set.json"
    # a dropped keyframe's snapshot stays for forensics as xT (out/native/diverge
    # NAME --keyframe .../xT), out of the set
    for d in $dropped; do mv "$set_dir/t$d" "$set_dir/x$d"; done
    unset why round
    echo "keyframes $name: $(echo $keep | wc -w) kept of $(echo $ticks | wc -w) ($nrows rows from $start, every $EVERY)$([ -n "$dropped" ] && echo "; dropped:$dropped")$(echo "$fj" | jq -r 'if length > 0 then "; round-trip differs at " + (map(.tick|tostring)|join(",")) else "" end'); java $(awk -v a="$t0" -v b="$t1" 'BEGIN {printf "%.1f", b - a}') s, check $(awk -v a="$t1" -v b="$t2" 'BEGIN {printf "%.1f", b - a}') s"
done
exit $rc
