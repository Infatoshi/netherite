#!/usr/bin/env bash
# The GUI input judge over one recording: DIR is out/java/guijudge/NAME, a
# tape the oracle made while it fed DIR/raw.jsonl's raw mouse and key
# events to Java's own GuiContainer (make -C oracle script RAWGUI=..., the
# oracle's RawGui.java), so each row's gui ops are the window clicks Java's
# screen sent for them.
#   tests/gui_judge.sh DIR [PLAY_DEV] [--live-only]
# 1. PLAY_DEV DIR --gui-judge: every row with the tape's act, its screen ops
#    taken out and made again from the raw events by this client's screen
#    handlers inside the tick (and every row's state checked).
# 2. The window's own loop: PLAY_DEV plays the tape up to the first event,
#    then the raw events as an input script (SDL events through the live
#    loop, a frame drawn after each tick) and writes its tape; its screen
#    ops per tick must be the recording's. A drag's slots (mode 5, stage 1)
#    compare as a set: Java sends them in HashSet order.
# 0. test_snapshots replays the recording itself (every row field, the
#    cursor and the inventories included), so the ops are known to be exact.
# --live-only runs 2 alone (a client from before the judge has no 0 or 1).
set -uo pipefail
dir=${1:?usage: gui_judge.sh DIR [PLAY_DEV] [--live-only]}
dir=${dir%/}
root=$(cd "$(dirname "$0")/../.." && pwd)
bin=${2:-$root/out/native/play-dev}
live_only=0
[[ ${3:-} == --live-only ]] && live_only=1
cd "$root"
rc=0
if [[ $live_only == 0 ]]; then
  o=$("$(dirname "$bin")/test_snapshots" "$dir" 2>&1) || { rc=1; grep -v '^  ' <<< "$o" | tail -3; }
  o=$("$bin" "$dir" --gui-judge "$dir/tape.jsonl" --gui-raw "$dir/raw.jsonl" 2>&1) || rc=1
  grep -v '^play: ' <<< "$o"
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/guijudge.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
t0=$(head -1 "$dir/raw.jsonl" | jq '.t')
tn=$(tail -1 "$dir/raw.jsonl" | jq '.t')
{ cat "$dir/raw.jsonl"; echo "{\"t\":$((tn + 2)),\"quit\":1}"; } > "$tmp/script.jsonl"
SDL_VIDEODRIVER=dummy "$bin" "$dir" --threads 2 --prefix-tape "$dir/tape.jsonl" --prefix-until "$t0" \
  --input-script "$tmp/script.jsonl" --tape "$tmp/live.jsonl" > "$tmp/live.log" 2>&1 \
  || { echo "FAIL gui judge live $dir: the client exited $? ($(tail -1 "$tmp/live.log"))"; exit 1; }
norm='def norm: reduce .[] as $op ({out: [], run: []};
        if ($op[0] == "click" and $op[4] == 5 and ($op[3] % 4) == 1) then .run += [$op]
        else .out += (.run | sort_by(.[2])) + [$op] | .run = [] end) | .out + (.run | sort_by(.[2]));
      select(.t != null and .t >= $t0 and .t <= $tn + 1) | "\(.t) \((.act.gui // []) | norm | tojson)"'
jq -r --argjson t0 "$t0" --argjson tn "$tn" "$norm" "$dir/tape.jsonl" > "$tmp/want"
jq -r --argjson t0 "$t0" --argjson tn "$tn" "$norm" "$tmp/live.jsonl" > "$tmp/got"
n=$(grep -vc ' \[\]$' "$tmp/want")
if cmp -s "$tmp/want" "$tmp/got"; then
  echo "PASS gui judge live $dir: $(wc -l < "$tmp/want") ticks through the window's loop, $n with screen ops, all Java's"
else
  first=$(diff "$tmp/want" "$tmp/got" | grep -m1 '^[<>]' | cut -c3- | cut -d' ' -f1)
  echo "FAIL gui judge live $dir: tick $first: Java's screen sent $(grep "^$first " "$tmp/want" | cut -d' ' -f2-), the client $(grep "^$first " "$tmp/got" | cut -d' ' -f2-)"
  echo "  $(diff "$tmp/want" "$tmp/got" | grep -c '^<') of $(wc -l < "$tmp/want") ticks differ"
  rc=1
fi
exit $rc
