#!/usr/bin/env bash
# The oracle's self-check: every stored recording's tape replayed on this
# tree's oracle, fresh (CACHE=0), one line each.
#
#   bash tests/selfcheck.sh [-j N] [-o TSV] [NAME|DIR ...]
#   bash tests/selfcheck.sh [-j N] [-o TSV] [--select COVDIR [--java JAVA]] [NAME|DIR ...]
#   make selfcheck [RECS="a b"] [J=8] [SELECT=1 [COV=DIR]]
#
# With no names: every directory under ../out/java/snapshots holding a
# tape.jsonl, and every one under ../out/java/rendertick, hudscreens and
# render (the render judges' scenes: named rt-NAME, hs-NAME and rs-NAME as
# make coverage names them; an item rendertick/NAME and so on runs one), but
# the stale scenes lane/rerender re-recorded as NAME_rr (csrc/tests/
# retired_scenes.txt, until master moves them to out/java/<area>-retired).
# The two scenes that enter the End (te_portal_end*, te_portal_sh*) run in
# fresh JVMs: on a warm member their End varies from run to run (ledger
# poolleak-endpool); every other one goes to the pool. A name is a directory
# there; a path is taken as it is. Each
# runs `make -s replay REF=DIR/tape.jsonl CACHE=0` (through the shared warm
# pool when one of this harness is free; the memory budget queues the rest),
# J at a time (default 8), and prints in the order given
#   NAME  OK     rows  seconds
#   NAME  DIVERGE t=T FIELD want=... got=...
#   NAME  FAIL   rc=N <the last line of the run>
# then "selfcheck: P/N OK"; the exit status is 1 when any is not OK. Each
# run's whole output is under ../out/java/selfcheck/PID/NAME.log, and the
# lines go to TSV as well (-o). A recording made before a change to the
# oracle that moves a row field is found here: the merge trial runs it for
# a lane that changes oracle/ (lane/clientgate).
#
# --select COVDIR (make selfcheck SELECT=1; COV, default ../out/java/coverage):
# only the recordings that can reach code this tree changed since COVDIR's
# coverage data was made (make coverage on any earlier tree: the methods
# each recording ran, the fingerprints of the classes it ran them in, the
# replay's other inputs). tests/SelfSelect.java picks them (its comment
# says why the rest replay as they did); the lines it prints go to
# ../out/java/selfcheck/PID/select.txt and the first ones to stdout. It
# falls back to every recording, saying why on a FALLBACK line, when the
# data is missing or being rewritten (COVDIR/state is not "done"), the
# replay recipe or the pool's scripts differ (tests/selinputs.sh against
# COVDIR/inputs.tsv), or a class it cannot map changed (the pool's own, or
# one in tests/selfselect.racy).
set -u
HERE=$(cd "$(dirname "$0")/.." && pwd)
SNAP=$HERE/../out/java/snapshots
J=8; TSV=; SEL=; JAVA8=java
while [ $# -gt 0 ]; do
    case $1 in
        -j) J=$2; shift 2;;
        -o) TSV=$2; shift 2;;
        --select) SEL=$2; shift 2;;
        --java) JAVA8=$2; shift 2;;
        *) break;;
    esac
done
# the scenes the oracle no longer replays, each re-recorded as NAME_rr (lane/rerender)
RETIRED=$(grep -v '^#' "$HERE/../csrc/tests/retired_scenes.txt")
RETIRED=" $(echo $RETIRED) "
items=()
if [ $# -eq 0 ]; then
    for d in "$SNAP"/*/; do [ -f "$d/tape.jsonl" ] && items+=("$(basename "$d")"); done
    for a in rendertick hudscreens render; do
        for d in "$HERE/../out/java/$a"/*/; do
            it=$a/$(basename "$d")
            [ -f "$d/tape.jsonl" ] && [[ $RETIRED != *" $it "* ]] && items+=("$it")
        done
    done
else
    items=("$@")
fi
[ ${#items[@]} -gt 0 ] || { echo "selfcheck: no recordings"; exit 2; }
LOG=$HERE/../out/java/selfcheck/$$
mkdir -p "$LOG"
# an item's directory and its name (make coverage's: rt-, hs-, rs- for the render judges' scenes)
recdir() {
    case $1 in
        rendertick/*|hudscreens/*|render/*) echo "$HERE/../out/java/$1";;
        */*) echo "$1";;
        *) echo "$SNAP/$1";;
    esac
}
recname() {
    local d; d=$(recdir "$1")
    case $d in
        */out/java/rendertick/*) echo "rt-$(basename "$d")";;
        */out/java/hudscreens/*) echo "hs-$(basename "$d")";;
        */out/java/render/*) echo "rs-$(basename "$d")";;
        *) basename "$d";;
    esac
}
export -f recdir recname
if [ -n "$SEL" ]; then
    all=${#items[@]}
    for it in "${items[@]}"; do recname "$it"; done > "$LOG/all.txt"
    fb=
    st0=$(cat "$SEL/state" 2>/dev/null)
    case $st0 in done*) ;; *) fb="the coverage data $SEL is not complete (state: ${st0:-none}): make coverage";; esac
    if [ -z "$fb" ]; then
        d=$(diff <(bash "$HERE/tests/selinputs.sh" "$HERE") "$SEL/inputs.tsv" 2>&1 | grep '^[<>]' | cut -d' ' -f2 | sort -u | tr '\n' ' ')
        [ -z "$d" ] || fb="the replay's inputs differ from the coverage data's: $d"
    fi
    OUT=$(cd "$HERE/.." && pwd)/out/java
    if [ -z "$fb" ]; then
        "$JAVA8" -cp "$OUT/covtool:$OUT/jacoco-0.8.12/lib/jacococli.jar:$OUT/lib/*" SelfSelect select "$SEL" "$OUT/classes" "$LOG/all.txt" "$LOG/selected.txt" "$HERE/tests/selfselect.racy" > "$LOG/select.txt" 2>&1 || fb="the selector failed: $(tail -1 "$LOG/select.txt")"
        [ "$(cat "$SEL/state" 2>/dev/null)" = "$st0" ] || fb="the coverage data $SEL changed while it was read"
        [ -z "$fb" ] && grep -q '^FALLBACK' "$LOG/select.txt" && fb=$(grep '^FALLBACK' "$LOG/select.txt" | head -3 | cut -c10- | tr '\n' ';')
    fi
    if [ -n "$fb" ]; then
        echo "FALLBACK selfcheck: every recording: $fb"
    else
        grep -v '^SELECT' "$LOG/select.txt" | grep -v '^selfselect: [0-9]' | head -30
        grep '^SELECT' "$LOG/select.txt" | head -10
        grep '^selfselect: [0-9]' "$LOG/select.txt"
        declare -A byname=()
        for it in "${items[@]}"; do byname[$(recname "$it")]=$it; done
        items=()
        while read -r n; do items+=("${byname[$n]}"); done < "$LOG/selected.txt"
        [ ${#items[@]} -gt 0 ] || { echo "selfcheck: 0/0 OK: 0 of $all recordings selected (logs $LOG)"; exit 0; }
    fi
fi
export HERE SNAP LOG
printf '%s\n' "${items[@]}" | xargs -P "$J" -I{} bash -c '
    it=$1
    d=$(recdir "$it")
    n=$(recname "$it")
    s=$(date +%s%N)
    # the End scenes run in fresh JVMs: pooled, the End after the dimension change varies (ledger
    # poolleak-endpool). The other render scenes are pooled again: the fog leak lane/rerender saw
    # (worldfx_underwater after an air scene, t=25 d.px) was the fog distance mode PoolGl now restores, lane/poolleak
    case $n in rt-te_portal_end*|rt-te_portal_sh*) pool=0;; *) pool=;; esac
    make -s --no-print-directory -C "$HERE" replay REF="$d/tape.jsonl" CACHE=0 ${pool:+POOL=$pool} > "$LOG/$n.log" 2>&1; rc=$?
    e=$(date +%s%N); sec=$(( (e - s) / 1000000000 ))
    if grep -aq "^ORACLE REPLAY OK" "$LOG/$n.log" && [ $rc -eq 0 ]; then
        rows=$(grep -a "^ORACLE REPLAY OK" "$LOG/$n.log" | sed -E "s/.*ticks=([0-9]+).*/\1/")
        line="OK	$rows	${sec}s"
    elif grep -aq "^ORACLE DIVERGE" "$LOG/$n.log"; then
        line="DIVERGE	$(grep -a "^ORACLE DIVERGE" "$LOG/$n.log" | head -1 | cut -c16-)"
    else
        line="FAIL	rc=$rc $(grep -av "^\[" "$LOG/$n.log" | tail -1)"
    fi
    printf "%s\t%s\n" "$n" "$line" > "$LOG/$n.line"
    printf "%-32s %s\n" "$n" "$line"
' _ {}
ok=0
for it in "${items[@]}"; do
    n=$(recname "$it")
    [ -n "$TSV" ] && cat "$LOG/$n.line" >> "$TSV"
    grep -q "	OK	" "$LOG/$n.line" 2>/dev/null && ok=$((ok + 1))
done
echo "selfcheck: $ok/${#items[@]} OK${SEL:+ (${#items[@]} of $all selected)} (logs $LOG)"
[ "$ok" -eq "${#items[@]}" ]
