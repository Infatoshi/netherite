#!/usr/bin/env bash
# The native client judged by a session a person played on the Java client.
# make -C oracle play records every keyboard and mouse event the Java client
# took beside its tape (NAME.raw.jsonl, java RawRec); this plays those raw
# events through out/native/play's own SDL input path from the session's
# start and compares everything the two clients produced:
#   rows    the engine on Java's acts (test_snapshots over Java's tape from
#           the start snapshot) and the client on the raw events (play
#           --judge: every row's act, the look, the key bindings held and
#           pressed, the hotbar, the focus, the left-click counter and
#           control, then the state, against Java's row of the same tick)
#   gui     the window clicks the client's container screens made from the
#           raw events, against the ones Java's GuiContainer sent
#   frames  the client's frame at sampled ticks (partial tick 1) against the
#           Java client's frame of the same tick (the goldens, made once by
#           a Java replay of the tape), within the recording's budget lines
#           in csrc/tests/rawjudge_budget.txt (a ratchet, as
#           tests/frame_budget.txt's)
# One PASS or FAIL line each, with the first difference.
#
#   csrc/play/rawjudge.sh RECORDING [--every N] [--accept] [--play BIN] [--no-frames]
# RECORDING is a Java play tape (NAME.jsonl with NAME.raw.jsonl beside it;
# it is copied to out/java/rawjudge/NAME) or such a directory, holding
# tape.jsonl, tape.raw.jsonl, start (the start snapshot under out/java: the
# world's join, fresh-play-s1 for a seed-1 world, else recorded into
# DIR/snap by the Java oracle) and f_TTTTTT.png (the goldens; a Java replay
# of the tape makes them when there are none, one every N ticks, default
# 500, with the toasts, the chat and the held item's name the live HUD
# draws; standin-s1's and rawjudge-f3f-s1's predate the name). --accept
# appends a missing budget line for each frame measured (after a review
# with out/native/pxdiff). --play BIN judges another build
# of the client (--tag T keeps its work apart); --budgets FILE reads and
# --accept writes FILE's budget lines in place of the tracked ones (the fuzz's
# sessions that are not kept, csrc/tests/rawfuzz); --in-budget: the caller
# holds the memory reservation (make -C csrc test runs each
# out/java/rawjudge directory so). Off the canonical host (the Mac) the recording goes to
# its checkout and the judge runs there, as check.sh does.
set -uo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
rec= every=500 accept=0 bin= frames=1 tag= inbudget=0 budgets=csrc/tests/rawjudge_budget.txt
while [[ $# -gt 0 ]]; do
    case $1 in
        --every) every=$2; shift 2;;
        --accept) accept=1; shift;;
        --play) bin=$(readlink -f "$2"); shift 2;;
        --no-frames) frames=0; shift;;
        --tag) tag=$2; shift 2;;
        --budgets) budgets=$2; shift 2;;
        --in-budget) inbudget=1; shift;;
        -h|--help) sed -n '2,37p' "$0"; exit 0;;
        *) rec=$1; shift;;
    esac
done
[[ -n $rec ]] || { echo "usage: csrc/play/rawjudge.sh RECORDING [--every N] [--accept] [--play BIN] [--no-frames]"; exit 2; }

source "$(dirname "${BASH_SOURCE[0]}")/host.sh"
# any Linux host with the full tree (the dev host, the GPU host) judges here;
# the Mac sends the recording to $home (csrc/play/host.sh)
if [[ $(uname -s) != Linux && -n $home ]]; then
    [[ -f $rec ]] || { echo "rawjudge: off $home RECORDING must be a tape file"; exit 2; }
    name=$(basename "$rec" .jsonl)
    ssh $home "mkdir -p $R/out/java/tapes" && rsync -a "$rec" "${rec%.jsonl}.raw.jsonl" "$home:$R/out/java/tapes/" || exit 1
    exec ssh $home "cd $R && bash csrc/play/rawjudge.sh out/java/tapes/$name.jsonl --every $every $([[ $accept == 1 ]] && echo --accept) $([[ $frames == 0 ]] && echo --no-frames)"
fi
cd "$root"
bin=${bin:-$root/out/native/play}
# the machine-wide memory budget (csrc/tests/memslot.sh), unless the caller
# (make -C csrc test) holds a reservation for this job already
ms() { if [[ $inbudget == 1 ]]; then "$@"; else bash csrc/tests/memslot.sh "$start" "$@"; fi; }

if [[ -f $rec ]]; then
    name=$(basename "$rec" .jsonl)
    raw=${rec%.jsonl}.raw.jsonl
    [[ -s $raw ]] || { echo "FAIL rawjudge $name: no raw events beside the tape ($raw)"; exit 1; }
    dir=out/java/rawjudge/$name
    mkdir -p "$dir"
    cmp -s "$rec" "$dir/tape.jsonl" || cp "$rec" "$dir/tape.jsonl"
    cmp -s "$raw" "$dir/tape.raw.jsonl" || cp "$raw" "$dir/tape.raw.jsonl"
else
    dir=${rec%/}
    name=$(basename "$dir")
fi
[[ -s $dir/tape.jsonl && -s $dir/tape.raw.jsonl ]] || { echo "FAIL rawjudge $name: $dir needs tape.jsonl and tape.raw.jsonl"; exit 1; }
hdr=$(head -1 "$dir/tape.jsonl")
[[ $(jq -r .mode <<< "$hdr") == play ]] || { echo "FAIL rawjudge $name: $dir/tape.jsonl is not a play-mode tape"; exit 1; }

# the start: the Java client's join of the same world, as a snapshot
start=
if [[ -f $dir/start ]]; then
    start=out/java/$(cat "$dir/start")
else
    want=$(jq -c '{seed, world, start: (.start | {kind, dir, quietJoin})}' <<< "$hdr")
    for c in out/java/snapshots/fresh-play-s1 "$dir/snap"; do
        [[ -s $c/tape.jsonl && -s $c/manifest.json ]] || continue
        [[ $(head -1 "$c/tape.jsonl" | jq -c '{seed, world, start: (.start | {kind, dir, quietJoin})}') == "$want" ]] && { start=$c; break; }
    done
    if [[ -z $start ]]; then
        echo "rawjudge $name: recording the start snapshot (the join of the tape's world) into $dir/snap"
        set=$(jq -r '.world | to_entries | map("\(.key)=\(.value)") | join(" ")' <<< "$hdr")
        from=$(jq -r 'if .start.kind == "checkpoint" then .start.dir else "" end' <<< "$hdr")
        mkdir -p "$dir/snap"
        echo '{"cmd":"run","class":"Snapshot"}' > "$dir/snap/script.jsonl"
        make -s -C oracle script SET="$set" ${from:+FROM="$from"} SCRIPT="../$dir/snap/script.jsonl" \
            TAPE="../$dir/snap/tape.jsonl" > "$dir/snap/java.log" 2>&1
        [[ -s $dir/snap/manifest.json ]] || { echo "FAIL rawjudge $name: the start snapshot was not written ($dir/snap/java.log)"; exit 1; }
        start=$dir/snap
    fi
    echo "${start#out/java/}" > "$dir/start"
fi
[[ -s $start/manifest.json ]] || { echo "FAIL rawjudge $name: no start snapshot at $start"; exit 1; }
S=$(jq -r .tick "$start/manifest.json")
playargs=()
[[ $(jq -r '.mac // 0' < <(head -1 "$dir/tape.raw.jsonl")) == 1 ]] && playargs+=(--mac-keys)

work=$root/out/native/rawjudge/$name${tag:+-$tag}
rm -rf "$work"; mkdir -p "$work"
jq -n -c --argjson start "$S" -f csrc/play/raw2script.jq "$dir/tape.raw.jsonl" > "$work/script.jsonl" \
    || { echo "FAIL rawjudge $name: the raw events did not convert"; exit 1; }
nframes=$(grep -c '"frame"' "$work/script.jsonl")
nev=$(grep -c '"key"\|"mouse"\|"motion"\|"wheel"' "$work/script.jsonl")
unm=$(grep -c '"unmapped"' "$work/script.jsonl")
smooth=$(grep -c '"smooth"' "$work/script.jsonl")
late=$(jq -s '[.[] | select(.late != null and .late > 200)] | length' "$dir/tape.raw.jsonl")
last=$(tail -1 "$dir/tape.jsonl" | jq .t)
echo "rawjudge $name: ticks $S to $last from $start, $nframes frames, $nev events ($unm unmapped keys or buttons, $smooth smooth-camera frames, $late ticks over 200 ms late)"

# 1. the engine on Java's acts, 2. the client on the raw events, 3. the goldens
chk=$work/check
mkdir -p "$chk"
for f in "$start"/*; do [[ $(basename "$f") == tape.jsonl ]] || ln -s "$(readlink -f "$f")" "$chk/"; done
cp "$dir/tape.jsonl" "$chk/tape.jsonl"
( ms out/native/test_snapshots "$chk" > "$work/engine.log" 2>&1; echo $? > "$work/engine.rc" ) &
( SDL_VIDEODRIVER=dummy ms "$bin" "$start" --threads 2 --no-draw "${playargs[@]}" \
    --input-script "$work/script.jsonl" --judge "$dir/tape.jsonl" --tape "$work/native.jsonl" > "$work/judge.log" 2>&1
  echo $? > "$work/judge.rc" ) &
if [[ $frames == 1 ]] && ! compgen -G "$dir/f_*.png" > /dev/null; then
    echo "rawjudge $name: the goldens: a Java replay of the tape, a frame every $every ticks"
    echo clientrand > "$dir/pins"
    ( make -s -C oracle replay REF="../$dir/tape.jsonl" FRAMES="../$dir" EVERY="$every" ARGS="--toasts --chat --tooltips" > "$work/java.log" 2>&1
      grep -a 'ORACLE REPLAY OK\|ORACLE DIVERGE' "$work/java.log" | tail -1 > "$work/java.result" ) &
fi
wait
rc=0
eng=$(grep -a '^replay: .* rows simulated' "$work/engine.log" | tail -1)
if [[ $(cat "$work/engine.rc") != 0 ]] || ! grep -aq ': OK: ' "$work/engine.log"; then
    echo "FAIL rawjudge rows $name: the engine on Java's acts: $(grep -a 'FAIL\|MISMATCH\|mismatch' "$work/engine.log" | head -1) (out/native/diverge $chk)"
    rc=1
elif ! grep -q '^PASS rawjudge rows' "$work/judge.log"; then
    echo "$(grep -m1 '^FAIL' "$work/judge.log" | sed "s/^FAIL rawjudge \([a-z]*\) /FAIL rawjudge \1 $name /; s/^FAIL play check rawjudge: /FAIL rawjudge rows $name: /" || true)"
    grep -q '^FAIL' "$work/judge.log" || echo "FAIL rawjudge rows $name: the client exited $(cat "$work/judge.rc"): $(tail -1 "$work/judge.log")"
    rc=1
else
    echo "PASS rawjudge rows $name: the engine on Java's acts (${eng#replay: }), the client on the raw events: $(sed -n 's/^PASS rawjudge rows: //p' "$work/judge.log")"
fi
if grep -q '^PASS rawjudge gui' "$work/judge.log"; then
    echo "PASS rawjudge gui $name: $(sed -n 's/^PASS rawjudge gui: //p' "$work/judge.log")"
elif grep -q '^FAIL rawjudge gui' "$work/judge.log"; then
    rc=1   # printed above
else
    t=$(grep -m1 -o '^FAIL[^0-9]*t=[0-9]*' "$work/judge.log" | grep -o '[0-9]*$')
    echo "FAIL rawjudge gui $name: not judged past tick ${t:-?} (the rows failed there)"
    rc=1
fi

# 4. the frames: play replays its own rows (the ones just judged) and draws
# each golden's tick
[[ $frames == 1 ]] || exit $rc
if [[ -f $work/java.result ]] && ! grep -q 'ORACLE REPLAY OK' "$work/java.result"; then
    echo "FAIL rawjudge frames $name: the Java replay of the tape for the goldens: $(cat "$work/java.result") ($work/java.log)"
    exit 1
fi
nrow=$(($(wc -l < "$work/native.jsonl") - 1))
lastn=$(tail -1 "$work/native.jsonl" | jq '.t // -1')
shots=()
for g in "$dir"/f_*.png; do
    [[ -f $g ]] || continue
    t=$((10#$(basename "$g" .png | cut -c3-)))
    [[ $t -ge $S ]] || continue
    shots+=("$t")
done
[[ ${#shots[@]} -gt 0 ]] || { echo "FAIL rawjudge frames $name: no golden frame at or after tick $S"; exit 1; }
# every golden in one replay (play --shots), the frames Java's replay drew
# and no others: what the pointer and the inventory's stored pointer are in
# a frame depends on the frames drawn before it
skipped=0
for t in "${shots[@]}"; do
    if [[ $t -gt $lastn ]]; then skipped=$((skipped + 1)); continue; fi
    b=$(awk -v n="$name" -v t="$t" '$1 == n && $2 == t {print $3, $4, $5, $6}' "$budgets" 2>/dev/null)
    echo "$t 1.0 ${b:-}"
done > "$work/shots"
if [[ -s $work/shots ]]; then
    SDL_VIDEODRIVER=dummy ms "$bin" "$start" --threads 2 "${playargs[@]}" \
        --replay "$work/native.jsonl" --tape "$work/shots.jsonl" --shots "$work/shots" \
        --shot-golden-dir "$dir" --shot-dir "$work" > "$work/shots.log" 2>&1
fi
fr=0 ok=0 first= new=()
for t in "${shots[@]}"; do
    [[ $t -gt $lastn ]] && continue
    fr=$((fr + 1))
    m=$(grep -m1 "^frame t=$t " "$work/shots.log" | sed -E 's/.*mean ([^ ]+) ([^ ]+) ([^ ]+) \/ch, ([0-9]+) px.*/\1 \2 \3 \4/')
    if [[ -z $m ]]; then
        first=${first:-"t=$t: the shot did not finish ($(tail -1 "$work/shots.log"))"}
    elif grep -q "^FAIL device frame t=$t " "$work/shots.log"; then
        # the device's world frame against the C renderer's (play/framedev.h)
        first=${first:-"t=$t: $(grep -m1 "^FAIL device frame t=$t " "$work/shots.log" | sed 's/^FAIL device frame t=[0-9]* //')"}
    elif grep -q "^PASS frame judge t=$t " "$work/shots.log"; then
        ok=$((ok + 1))
    elif grep -q "^FAIL frame judge t=$t " "$work/shots.log"; then
        first=${first:-"t=$t: $(grep -m1 "^FAIL frame judge t=$t " "$work/shots.log" | sed 's/^FAIL frame judge t=[0-9]* //') (measured $m)"}
    else
        new+=("$name $t $m")
        first=${first:-"t=$t: no budget line (measured $m)"}
    fi
done
if [[ $accept == 1 && ${#new[@]} -gt 0 ]]; then
    printf '%s\n' "${new[@]}" >> "$budgets"
    echo "rawjudge $name: ${#new[@]} budget lines added to $budgets"
    ok=$((ok + ${#new[@]}))
    [[ -z ${first:-} || $first == *"no budget line"* ]] && first=
fi
if [[ -z $first && $ok == "$fr" ]]; then
    echo "PASS rawjudge frames $name: $fr frames within their budgets$([[ $skipped -gt 0 ]] && echo ", $skipped past the client's last row")"
else
    echo "FAIL rawjudge frames $name: $ok of $fr frames within budget; first: $first"
    [[ ${#new[@]} -gt 0 ]] && printf '  measured, not budgeted: %s\n' "${new[@]}"
    rc=1
fi
exit $rc
