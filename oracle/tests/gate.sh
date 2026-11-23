#!/usr/bin/env bash
# Every oracle gate, in about three minutes. Run after any change to src/
# (`make gate` from oracle/). Linux: needs Xvfb, xdotool and jq.
set -uo pipefail
cd "$(dirname "$0")/.."
G=$(realpath -m ../out/java/gate); rm -rf "$G"; mkdir -p "$G"
pass=0; fail=0
ok()  { echo "PASS  $1"; pass=$((pass + 1)); }
bad() { echo "FAIL  $1"; fail=$((fail + 1)); }
verdict() { grep -ao "ORACLE REPLAY OK ticks=[0-9]*\|ORACLE DIVERGE.*" "$1" | head -1; }

# Every run is fresh (CACHE=0): the gates prove the oracle, not its result cache.

# 1. Three concurrent runs of the same script are byte-identical, frames included.
for i in 1 2 3; do
  make -s script CACHE=0 SEED=1 SCRIPT=tests/smoke.jsonl TAPE="$G/smoke$i.jsonl" FRAMES="$G/frames$i" > "$G/smoke$i.log" 2>&1 &
done; wait
tapes=$(for i in 1 2 3; do jq -c 'del(.frame)' "$G/smoke$i.jsonl" | sha1sum; done | sort -u | wc -l)
fdiff=$(for i in 2 3; do diff -rq "$G/frames1" "$G/frames$i"; done | wc -l)
rows=$(( $(wc -l < "$G/smoke1.jsonl") - 1 ))
if [ "$tapes" = 1 ] && [ "$fdiff" = 0 ] && [ "$rows" -gt 100 ]; then
  ok "concurrent identity: 3 runs, $rows rows, $(ls "$G/frames1" | wc -l) frames"
else bad "concurrent identity: $tapes distinct tapes, $fdiff frame diffs, $rows rows"; fi

# 2. Replay reproduces every row, and rendering every tick reproduces the sparse frames.
make -s replay CACHE=0 REF="$G/smoke1.jsonl" EVERY=1 FRAMES="$G/every" > "$G/replay.log" 2>&1
v=$(verdict "$G/replay.log")
fd=$(for f in "$G"/frames1/*.png; do cmp -s "$f" "$G/every/$(basename "$f")" || echo x; done | wc -l)
if [[ "$v" == "ORACLE REPLAY OK"* ]] && [ "$fd" = 0 ]; then ok "replay with every-tick frames: ${v#ORACLE REPLAY OK }"
else bad "replay: ${v:-no verdict}, $fd frame diffs"; fi

# 3. The gate can fail: a 0.00001 degree look change at t=60 is caught at t=60.
perl -pe 'if (/^\{"t":60,/) { s/"look":\[([-0-9.E]+)/sprintf("\"look\":[%.5f", $1 + 0.00001)/e }' "$G/smoke1.jsonl" > "$G/neg.jsonl"
make -s replay CACHE=0 REF="$G/neg.jsonl" > "$G/neg.log" 2>&1
v=$(verdict "$G/neg.log")
if [[ "$v" == "ORACLE DIVERGE t=60 "* ]]; then ok "negative: $v"; else bad "negative: ${v:-no verdict}"; fi

# 4. TCP control port: step, state, frame, quit; a re-render equals the step frame.
P=$((25600 + RANDOM % 300))
# the server runs in its own process group: it may queue for memory and a display before its
# JVM starts (almost 2 minutes at load 140 on 2026-09-28, past the old 90 s wait), and if the
# smoke test never connects nothing sends it quit, so the gate ended the group instead of a
# bare `wait` that hung a merge trial for an hour
setsid make -s serve PORT=$P SEED=7 TAPE="$G/tcp.jsonl" > "$G/tcp.log" 2>&1 &
sp=$!
for _ in $(seq 1 600); do grep -aq "ORACLE LISTEN" "$G/tcp.log" 2>/dev/null && break; kill -0 $sp 2>/dev/null || break; sleep 1; done
t=$(bash tests/tcp_smoke.sh $P "$G/tcpframes" 2>&1 | tail -1)
for _ in $(seq 1 30); do kill -0 $sp 2>/dev/null || break; sleep 1; done
kill -0 $sp 2>/dev/null && { kill -- -$sp 2>/dev/null; t="${t:-no reply}; the server did not quit"; }
wait
if [[ "$t" == "frame command reproduces"* ]]; then ok "tcp: $t"; else bad "tcp: $t"; fi

# 5. A synthetic human session (xdotool) replays exactly in agent mode.
bash tests/play_synth.sh 3 "$G/play.jsonl" > "$G/play-synth.log" 2>&1
make -s replay CACHE=0 REF="$G/play.jsonl" > "$G/playreplay.log" 2>&1
v=$(verdict "$G/playreplay.log")
if [[ "$v" == "ORACLE REPLAY OK"* ]]; then ok "human record and replay: ${v#ORACLE REPLAY OK }"
else bad "human record and replay: ${v:-no verdict} ($(tail -1 "$G/play-synth.log"))"; fi

echo "$pass passed, $fail failed  (logs: $G)"
exit $fail
