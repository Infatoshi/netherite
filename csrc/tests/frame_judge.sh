#!/usr/bin/env bash
# The playable client's live frames against the oracle's, over one
# recording under out/java/clientframes/NAME:
#   tests/frame_judge.sh DIR [OUT_DIR]
# DIR holds tape.jsonl (the inputs play replays; a file start names the
# snapshot under out/java it starts from when DIR is not one itself, and
# without tape.jsonl that snapshot's own tape is the inputs), the
# oracle's frames f_TTTTTT.png of the same ticks (make replay or script
# FRAMES=, with --toasts --chat as the live client draws them) and
# drops.jsonl (the items and orbs each of those frames drew: the client's
# copies, from a renderstate recording's entity list). With raw.jsonl (the
# raw screen events the oracle ran, RAWGUI=) the screens' ops are made again
# from them, as the GUI judge makes them, so a frame shows the drag's state.
# Each line "NAME TICK PT R G B N [drawn]" of tests/frame_budget.txt is one
# shot (drawn: the oracle drew the open screen in an earlier frame, so the
# inventory's player preview looks where that frame's pointer was): play
# replays the inputs once (--shots), draws after each shot's TICK the frame
# at partial tick PT the way its window does (against the oracle's frame at
# that partial tick, f_TTTTTT.pNNN.png, when the recording has one: make
# replay ARGS="--frame-pt 0.5"; the goldens come from an oracle with Det's
# client consumer pin, every one since lane/clientrand, whose streams the
# client draws; DIR/pins, when there, names it: clientrand), and
#  - measures it against the oracle's frame as test_render measures a scene
#    (each channel's mean absolute error, the pixels over 25): worse than
#    the line's budget (what it measured when accepted, a ratchet) fails;
#  - finds a drop of its own drawn near each of the oracle's (a dropped
#    item or orb it does not draw fails).
# A recording with no budget line fails, and prints the lines it measured.
# On the dev host the shots' world frames are drawn on the device (play/framedev.h:
# out/native/cuda/libframedev.so, when make test built it), every 16th
# shot's again by the C renderer: "PASS device frame" when the two are the
# same bytes, a FAIL line that fails the judge when not (a lane that changes
# the C renderer ports the change to csrc/cuda/render, the mirror rule,
# or judges with --frames c meanwhile).
# An observation's directory (oracle/tests/obsframes.sh: out/java/
# clientframes-WxH/NAME) holds the goldens only: its file base names the
# recording the inputs come from, its file obs the flags play draws with
# (--size WxH, --hide-gui), and its budgets are tests/frame_budget_WxH.txt.
set -uo pipefail
# --frames c|device: play's (the C renderer for every shot; the device or fail)
# --shot-dir DIR: play keeps each shot as DIR/s_TTTTTT.pNNN.png
frames=()
args=()
while [[ $# -gt 0 ]]; do
  case $1 in
    --frames) frames=(--frames "$2"); shift 2;;
    --shot-dir) frames+=(--shot-dir "$2"); shift 2;;
    *) args+=("$1"); shift;;
  esac
done
set -- "${args[@]}"
dir=${1:?usage: frame_judge.sh DIR [OUT_DIR] [--frames c|device] [--shot-dir DIR]}
dir=${dir%/}
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${2:-$root/out/native}
cd "$root"
name=$(basename "$dir")
budgets=$root/csrc/tests/frame_budget.txt
base=$dir
obs=()
if [[ -f $dir/obs ]]; then
  read -ra obs < "$dir/obs"
  base=$(dirname "$(dirname "$dir")")/$(cat "$dir/base")
  for ((k = 0; k < ${#obs[@]}; ++k)); do [[ ${obs[k]} == --size ]] && budgets=$root/csrc/tests/frame_budget_${obs[k+1]}.txt; done
fi
start=$base
[[ -f $base/start ]] && start=$(dirname "$(dirname "$base")")/$(cat "$base/start")
tape=$base/tape.jsonl
[[ -f $tape ]] || tape=$start/tape.jsonl
bin=$out/play
head -1 "$start/tape.jsonl" | grep -q '"dev":true' && bin=$out/play-dev
tmp=$(mktemp -d "${TMPDIR:-/tmp}/framejudge.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
mapfile -t lines < <(grep -E "^$name " "$budgets" 2>/dev/null)
# every shot of the recording in one replay (play --shots): a shot is drawn
# after its tick, as the live loop draws a frame between ticks
if [[ ${#lines[@]} == 0 && ${#obs[@]} != 0 ]]; then
  # an observation not yet budgeted: the 854x480 judge's shots
  grep -E "^$name " "$root/csrc/tests/frame_budget.txt" | while read -r _ t pt r g b n f; do echo "$t $pt - - - - ${f:-}"; done > "$tmp/shots"
elif [[ ${#lines[@]} == 0 ]]; then
  for f in "$dir"/f_??????.png; do t=$((10#$(basename "$f" .png | cut -c3-))); printf '%s 0.5\n%s 1.0\n' "$t" "$t"; done > "$tmp/shots"
else
  for l in "${lines[@]}"; do read -r _ t pt r g b n f <<< "$l"; echo "$t $pt $r $g $b $n ${f:-}"; done > "$tmp/shots"
fi
extra=("${obs[@]}")
# raw screen events are window pixels: an observation of another size takes
# the tape's screen ops, as its goldens did (oracle/tests/obsframes.sh)
[[ -f $base/raw.jsonl && ${#obs[@]} == 0 ]] && extra+=(--gui-raw "$base/raw.jsonl")
SDL_VIDEODRIVER=dummy "$bin" "$start" --threads 2 --replay "$tape" --tape "$tmp/tape.jsonl" \
  --shots "$tmp/shots" --shot-golden-dir "$dir" "${extra[@]}" "${frames[@]}" --shot-items "$base/drops.jsonl" > "$tmp/log" 2>&1
prc=$?
rc=0
if [[ ${#lines[@]} == 0 ]]; then
  sed -nE 's/^frame t=([0-9]+) pt=([0-9.]+): mean ([^ ]+) ([^ ]+) ([^ ]+) \/ch, ([0-9]+) px.*/\1 \2 \3 \4 \5 \6/p' "$tmp/log" |
    while read -r t pt r g b n; do
      echo "FAIL frame judge $name t=$t pt=$pt: no budget; after review add to tests/$(basename "$budgets"): $name $t $pt $r $g $b $n"
    done
  rc=1
else
  grep -E '^(PASS|FAIL) (frame judge|device frame)' "$tmp/log" | sed -E "s/^([A-Z]*) (frame judge|device frame) /\1 \2 $name /"
  # who drew the world frames (play/framedev.h)
  grep -E '^play: (the device|the C renderer draws|[0-9]+ world frames on the device)' "$tmp/log" | sed "s/^play: /frame judge $name: /"
  grep -q '^FAIL' "$tmp/log" && rc=1
  want=${#lines[@]}
  got=$(grep -cE '^(PASS|FAIL) frame judge t=[0-9]+ pt=' "$tmp/log")
  [[ $got == $((want * 2)) ]] || { echo "FAIL frame judge $name: $((got / 2)) of $want shots finished, play rc $prc ($(tail -1 "$tmp/log"))"; rc=1; }
fi
[[ $prc == 0 || $prc == 3 ]] || rc=1
exit $rc
