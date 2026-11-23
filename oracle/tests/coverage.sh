#!/usr/bin/env bash
# Java coverage: stored recordings replayed with JaCoCo's agent, then the
# report (CovReport.java) over every recording's execution data.
#
#   bash tests/coverage.sh [-j N] [-p POOLMAX] -- JAVA TOOLCP [NAME|DIR ...]
#   make coverage [RECS="a b"] [J=N]
#
# With no names: every directory under ../out/java/snapshots holding a
# tape.jsonl, and ../out/java/coverage/exec starts empty. A name is a
# directory there; a path is taken as it is (a recording made elsewhere,
# e.g. out/java/<area>/<name>). Each runs `make -s replay REF=DIR/tape.jsonl
# COVERAGE=../out/java/coverage/exec/NAME.exec`. First every recording asks
# the result cache only (CACHEONLY=1, as many at once as the machine has
# threads): a hit (this harness's run of these tape bytes under this JaCoCo)
# restores its data without a JVM. Then the misses run, J at a time, one
# line each as selfcheck prints them, with how each ran: "pool" (a coverage
# member of the shared pool: no JVM start) or "fresh" (a new JVM with the
# agent; the memory budget queues them). HARNESS is computed once and
# passed down. Without -j, J follows the machine: its hardware threads less the
# 1-minute load, at least 4 and at most three quarters of the threads (the
# replays are CPU-bound). Coverage members may take POOLMAX (-p, the
# Makefile's) plus J pool slots, the memory half of the budget still
# bounding them, and retire after 120 s idle. A run that diverges
# keeps its data up to the divergence and is marked in recs.tsv and the
# summary. Named runs replace their recordings' data; the report then reads
# every recording in recs.tsv that has an exec file. Logs are under
# ../out/java/coverage/logs.
# Beside the report it writes what the data was made on, for make selfcheck
# SELECT=1 (tests/SelfSelect.java): fingerprints.tsv (the classes' code and
# structure), inputs.tsv (tests/selinputs.sh: the replay recipe and the pool's
# scripts) and state ("running" while the run rewrites recs/, "done HARNESS"
# after).
#
# The frame judges' recordings run too: every directory under
# ../out/java/clientframes and ../out/java/rawjudge holding golden frames
# (f_TTTTTT.png) is replayed with frames drawn at exactly those ticks
# (--frame-ticks; the drawing only, no PNG is written or cached), so the
# render and GUI code their goldens drew counts as run. Its tape is its own
# tape.jsonl, else the one of the snapshot its file start names. A
# clientframes replay draws the toasts and the chat as the live client does
# (frame_judge.sh's goldens) and feeds its raw.jsonl screen events
# (--raw-gui) when it has one, and draws at partial tick 0.5 when the
# frames its rows carry digests of were drawn there (f_T.p500.png), or at
# 1.0 and then 0.5 when its rows name plain frames and the goldens hold
# both (the capture drew both, and a draw moves the next frame); a
# rawjudge replay draws the toasts, the chat and the tooltips
# (rawjudge.sh's goldens). They are named cf-NAME and rj-NAME; an item
# clientframes/NAME or rawjudge/NAME runs one. The frame flags are in the
# result cache's key like every argument.
#
# So do the render judges' recordings: every directory under
# ../out/java/rendertick, ../out/java/hudscreens and ../out/java/render
# holding a tape.jsonl (test_rendertick's scenes, drawn with particles on,
# the HUD and screen scenes, test_render's scenes that kept their tape).
# Their rows carry the digests of the frames they drew (d.px), so a plain
# replay draws those frames again; the options, chat and toasts come from
# the tape's header. They are named rt-NAME, hs-NAME and rs-NAME; an item
# rendertick/NAME, hudscreens/NAME or render/NAME runs one.
set -u
HERE=$(cd "$(dirname "$0")/.." && pwd)
SNAP=$HERE/../out/java/snapshots
COV=$(cd "$HERE/.." && pwd)/out/java/coverage
J= PMAX=8
while [ $# -gt 0 ]; do
    case $1 in
        -j) J=$2; shift 2;;
        -p) PMAX=$2; shift 2;;
        --) shift; break;;
        *) break;;
    esac
done
JAVA=$1 TOOLCP=$2
shift 2
items=()
if [ $# -eq 0 ]; then
    for d in "$SNAP"/*/; do [ -f "$d/tape.jsonl" ] && items+=("$(basename "$d")"); done
    for a in clientframes rawjudge; do
        for d in "$HERE/../out/java/$a"/*/; do compgen -G "$d/f_??????.png" > /dev/null && items+=("$a/$(basename "$d")"); done
    done
    for a in rendertick hudscreens render; do
        for d in "$HERE/../out/java/$a"/*/; do [ -f "$d/tape.jsonl" ] && items+=("$a/$(basename "$d")"); done
    done
    rm -rf "$COV/exec" "$COV/logs" "$COV/recs.tsv"
else
    items=("$@")
fi
[ ${#items[@]} -gt 0 ] || { echo "coverage: no recordings"; exit 2; }
# the self-check's selection (make selfcheck SELECT=1) reads this data only while
# state says done: a run in progress rewrites recs/ under it
mkdir -p "$COV"
echo running > "$COV/state"
rm -rf "$COV/line"
mkdir -p "$COV/exec" "$COV/logs" "$COV/line"
touch "$COV/recs.tsv"
# the harness once (make evaluates it several times a run otherwise)
HARN=$(make -s --no-print-directory -C "$HERE" harness)
POOLARGS="HARNESS=$HARN POOLMAX=$((PMAX + ${J:-8})) POOLIDLE=120"
export HERE SNAP COV POOLARGS
# an item's directory d, its name n, its tape and its extra oracle arguments
resolve() {
    it=$1 fr=
    case $it in
        clientframes/*|rawjudge/*|rendertick/*|hudscreens/*|render/*) d=$(cd "$HERE/../out/java/$it" && pwd); fr=${it%%/*};;
        */*) d=$(cd "$it" && pwd)
             case $d in */out/java/clientframes/*) fr=clientframes;; */out/java/rawjudge/*) fr=rawjudge;;
                 */out/java/rendertick/*) fr=rendertick;; */out/java/hudscreens/*) fr=hudscreens;; */out/java/render/*) fr=render;; esac;;
        *) d=$SNAP/$it;;
    esac
    n=$(basename "$d") tape=$d/tape.jsonl args=
    [ -n "$fr" ] || return 0
    case $fr in rendertick) n=rt-$n; return 0;; hudscreens) n=hs-$n; return 0;; render) n=rs-$n; return 0;; esac
    [ -f "$tape" ] || tape=$HERE/../out/java/$(cat "$d/start")/tape.jsonl
    args="--frame-ticks $(for f in "$d"/f_??????.png; do t=${f##*/f_}; echo $((10#${t%.png})); done | sort -n | paste -sd,)"
    if [ "$fr" = clientframes ]; then
        n=cf-$n args="$args --toasts --chat"
        [ -f "$d/raw.jsonl" ] && args="$args --raw-gui $d/raw.jsonl"
        # a tape whose rows carry frame digests drew them first at the partial tick its frame files name
        # one whose rows name plain frames beside p500 goldens drew each at 1.0 and then 0.5,
        # and the second draw moves the next frame (idx-pickcap-s1): draw both
        if grep -m1 -ao '"frame":"[^"]*"' "$tape" | grep -q '\.p500\.png'; then args="$args --frame-pt 0.5"
        elif grep -m1 -aq '"frame":"' "$tape" && compgen -G "$d/f_??????.p500.png" > /dev/null; then args="$args --frame-pts 1.0,0.5"; fi
    else
        n=rj-$n args="$args --toasts --chat --tooltips"
    fi
}
# one recording: lookup (the cache only; a miss leaves no line) or run
one() {
    mode=$1 it=$2
    resolve "$it"
    [ -f "$COV/line/$n" ] && return 0
    s=$(date +%s%N)
    rm -f "$COV/exec/$n.exec"
    if [ "$mode" = lookup ]; then
        make -s --no-print-directory -C "$HERE" replay REF="$tape" COVERAGE="$COV/exec/$n.exec" CACHEONLY=1 ${args:+ARGS="$args"} $POOLARGS > "$COV/logs/$n.log" 2>&1
        grep -aq "^oracle cache: hit" "$COV/logs/$n.log" || return 0
    fi
    [ "$mode" = lookup ] || make -s --no-print-directory -C "$HERE" replay REF="$tape" COVERAGE="$COV/exec/$n.exec" ${args:+ARGS="$args"} $POOLARGS > "$COV/logs/$n.log" 2>&1
    rc=$?
    e=$(date +%s%N); sec=$(( (e - s) / 1000000000 ))
    rows=$(grep -a "^ORACLE REPLAY OK" "$COV/logs/$n.log" | sed -E "s/.*ticks=([0-9]+).*/\1/")
    [ -n "$rows" ] || rows=$(( $(wc -l < "$tape") - 1 ))
    if grep -aq "^ORACLE REPLAY OK" "$COV/logs/$n.log" && [ $rc -eq 0 ]; then st=OK
    elif grep -aq "^ORACLE DIVERGE" "$COV/logs/$n.log"; then st="DIVERGE $(grep -a "^ORACLE DIVERGE" "$COV/logs/$n.log" | head -1 | cut -c16- | cut -c1-80)"
    else st="FAIL rc=$rc"; fi
    [ -f "$COV/exec/$n.exec" ] || st="$st, no coverage data"
    if grep -aq "^oracle cache: hit" "$COV/logs/$n.log"; then how=cache
    elif grep -aq "^pool: member .* takes the run" "$COV/logs/$n.log"; then how=pool
    else how=fresh; fi
    echo "$how" > "$COV/line/$n.how"
    printf "%s\t%s\t%s\t%s\t%s\n" "$n" "$d" "$st" "$rows" "$sec" > "$COV/line/$n"
    [ "$how" = cache ] || printf "%-32s %s\t%s\t%ss\t%s\n" "$n" "$st" "$rows" "$sec" "$how"
}
export -f one resolve
t0=$(date +%s)
# first the cache, many at a time (a hit is about a second of make and bash, no JVM)
printf '%s\n' "${items[@]}" | xargs -P "$(nproc)" -I{} bash -c 'one lookup "$1"' _ {}
hits=$(ls "$COV"/line/*.how 2>/dev/null | wc -l)
t0b=$(date +%s)
[ "$hits" -gt 0 ] && echo "coverage: $hits of ${#items[@]} from the cache in $((t0b - t0)) s"
# then the misses, J at a time
if [ -z "$J" ]; then
    thr=$(nproc)
    J=$(awk -v t="$thr" -v l="$(cut -d' ' -f1 /proc/loadavg)" 'BEGIN { j = int(t - l); m = int(t * 3 / 4); if (j > m) j = m; if (j < 4) j = 4; print j }')
    POOLARGS="HARNESS=$HARN POOLMAX=$((PMAX + J)) POOLIDLE=120"
fi
miss=()
for it in "${items[@]}"; do resolve "$it"; [ -f "$COV/line/$n" ] || miss+=("$it"); done
[ ${#miss[@]} -eq 0 ] || printf '%s\n' "${miss[@]}" | xargs -P "$J" -I{} bash -c 'one run "$1"' _ {}
for it in "${items[@]}"; do resolve "$it"; cat "$COV/line/$n"; done >> "$COV/recs.tsv"
hows=$(cat "$COV"/line/*.how 2>/dev/null | sort | uniq -c | awk '{printf "%s%d %s", (NR > 1 ? ", " : ""), $1, $2}')
rm -rf "$COV/line"
t1=$(date +%s)
echo "coverage: ${#items[@]} recordings in $((t1 - t0)) s, $((${#items[@]} - hits)) replayed in $((t1 - t0b)) s at J=$J (load $(cut -d' ' -f1 /proc/loadavg)): $hows"
"$JAVA" -Xmx6G -XX:+UseSerialGC -cp "$TOOLCP" CovReport "$HERE/../out/java/classes" "$HERE/../index" "$COV" 16 || exit 1
# what the data was made on, for make selfcheck SELECT=1: the classes' fingerprints and
# the replay's other inputs (tests/SelfSelect.java, tests/selinputs.sh)
"$JAVA" -cp "$TOOLCP" SelfSelect fingerprint "$HERE/../out/java/classes" "$COV/fingerprints.tsv" || exit 1
bash "$HERE/tests/selinputs.sh" "$HERE" > "$COV/inputs.tsv" || exit 1
echo "done $HARN" > "$COV/state"
echo "coverage: report in $(( $(date +%s) - t1 )) s: $COV/summary.txt, unexecuted.tsv, methods.tsv, lines.tsv, merged.exec, recs/"
