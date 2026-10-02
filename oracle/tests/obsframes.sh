#!/usr/bin/env bash
# The frame judges' goldens at an observation's own size: each
# ../out/java/clientframes/NAME replayed on the oracle in a WxH window, its
# frames drawn at every shot csrc/tests/frame_budget.txt lists for NAME
# (at partial ticks 0.5 and 1.0, one replay, as the 854x480 goldens were),
# with the live client's toasts and chat (not the raw screen events: those
# are window pixels; the tape's screen ops drive it), and with --hide-gui
# F1 held from the start:
#   tests/obsframes.sh WxH [--hide-gui] NAME...     (from oracle/)
# writes ../out/java/clientframes-WxH/NAME: the goldens f_TTTTTT[.p500].png,
# base (the recording the inputs come from: clientframes/NAME) and obs (the
# flags play draws that observation with); csrc/tests/frame_judge.sh
# judges such a directory against csrc/tests/frame_budget_WxH.txt. The
# tape's own frame digests (d.px, 854x480 frames) are rewritten, not
# compared (--rerecord-client); every other field of every row still is.
# Runs in a fresh JVM (the pool's window is 854x480); one line per NAME.
set -uo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
root=$(cd "$here/.." && pwd)
size=${1:?usage: obsframes.sh WxH [--hide-gui] NAME...}
shift
[[ $size =~ ^([0-9]+)x([0-9]+)$ ]] || { echo "obsframes: $size is not WxH"; exit 2; }
w=${BASH_REMATCH[1]} h=${BASH_REMATCH[2]}
hide=
[[ ${1:-} == --hide-gui ]] && { hide=--hide-gui; shift; }
rc=0
for name; do
    src=$root/out/java/clientframes/$name
    tape=$src/tape.jsonl
    [[ -f $tape ]] || tape=$root/out/java/$(cat "$src/start")/tape.jsonl
    ticks=$(grep -E "^$name " "$root/csrc/tests/frame_budget.txt" | awk '{print $2}' | sort -un | paste -sd,)
    [[ -n $ticks ]] || { echo "FAIL obsframes $name: no budget line"; rc=1; continue; }
    dst=$root/out/java/clientframes-$size/$name
    mkdir -p "$dst"
    rm -f "$dst"/f_*.png
    # a run directory of its own (make names it after the tape's folder)
    link=$root/out/java/obsgen/$name-$size/tape.jsonl
    mkdir -p "$(dirname "$link")"
    ln -sfn "$tape" "$link"
    args="--frame-ticks $ticks --frame-pts 0.5,1.0 --toasts --chat --rerecord-client --width $w --height $h $hide"
    # a recording's raw screen events are window pixels: at another size the
    # tape's own resolved screen ops drive the replay (and play, the judge)
    log=$root/out/java/obsgen/$name-$size/log
    s=$(date +%s)
    make -s --no-print-directory -C "$here" replay POOL=0 REF="$link" FRAMES="$dst" ARGS="$args" > "$log" 2>&1
    r=$?
    # keep the shots' frames only (the tape's own frame rows drew others)
    keep=" $(echo "$ticks" | tr , ' ') "
    for f in "$dst"/f_??????*.png; do
        [[ -e $f ]] || continue
        t=$(basename "$f"); t=$((10#${t:2:6}))
        [[ $keep == *" $t "* ]] || rm -f "$f"
    done
    echo "clientframes/$name" > "$dst/base"
    echo "--size $size $hide" > "$dst/obs"
    n=$(ls "$dst"/f_*.png 2>/dev/null | wc -l)
    if [[ $r == 0 ]] && grep -aq '^ORACLE REPLAY OK' "$log"; then
        echo "OK obsframes $name $size${hide:+ hide}: $n frames, $(( $(date +%s) - s )) s"
    else
        echo "FAIL obsframes $name $size: rc $r, $(grep -a '^ORACLE' "$log" | tail -1) (log $log)"
        rc=1
    fi
done
exit $rc
