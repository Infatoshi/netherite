#!/usr/bin/env bash
# Synthetic human for play mode: xdotool drives a real client on a private Xvfb
# display, then SIGTERM ends the session. Tests the play recorder end to end;
# replay the tape with `make replay REF=...`.
# Usage (from oracle/): bash tests/play_synth.sh SEED TAPE
set -euo pipefail
cd "$(dirname "$0")/.."
SEED=$1; TAPE=$(realpath -m "$2"); D=:$((70 + RANDOM % 20)); LOG=${TAPE%.jsonl}.log   # the game log, written by make play
mkdir -p "$(dirname "$TAPE")"
Xvfb $D -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 & XV=$!
trap 'kill $XV 2>/dev/null || true' EXIT
sleep 1
DISPLAY=$D make -s play CHECK=0 SEED=$SEED TAPE="$TAPE" ${PLAY_ARGS:+ARGS="$PLAY_ARGS"} > "${TAPE%.jsonl}.make.log" 2>&1 & MK=$!
for _ in $(seq 1 120); do grep -aq "ORACLE READY" "$LOG" 2>/dev/null && break; sleep 0.5; done
grep -aq "ORACLE READY" "$LOG" || { echo "no READY"; tail -20 "$LOG"; exit 1; }
export DISPLAY=$D
W=$(xdotool search --sync --name "Minecraft" | head -1)
xdotool windowfocus --sync "$W"; sleep 0.5
xdotool mousemove --window "$W" 427 240 click 1; sleep 0.5          # grab focus
xdotool keydown w; sleep 1.2; xdotool key space; sleep 0.8; xdotool keyup w
xdotool mousemove_relative --sync -- 120 40; sleep 0.3               # look
xdotool mousedown 1; sleep 2.5; xdotool mouseup 1                     # dig
xdotool keydown d; sleep 0.6; xdotool keyup d
xdotool key e; sleep 0.6                                              # inventory
xdotool mousemove --window "$W" 350 330 click 1; sleep 0.3           # pick up / put down in slots
xdotool mousemove --window "$W" 390 330 click 1; sleep 0.3
xdotool mousemove --window "$W" 350 330 click 3; sleep 0.3
xdotool key e; sleep 0.5                                              # close
xdotool mousemove --window "$W" 427 240 click 1; sleep 0.3           # regrab
xdotool key 3; sleep 0.3; xdotool key q; sleep 0.3                    # hotbar, drop
xdotool keydown s keydown shift; sleep 0.8; xdotool keyup s keyup shift
xdotool click --repeat 3 --delay 150 3; sleep 0.5                     # use
sleep 1
# the client's JVM: the java process under make's own tree (make play runs it directly: the
# pool never takes --play). Found by the process tree, never by a regex on the tape's path: a
# merge train's worktree is .try-train-A+B, and the '+' made the pattern match nothing (a
# trial's gate died on `kill ''`, the next one waited 36 minutes on a client nobody stopped,
# 2026-09-28). Under load it can take a moment to appear; a client that already ended has none:
# say which, with the game log's last lines, and end make's tree so `wait` cannot hang.
jvm_under() { local c; for c in $(pgrep -P "$1"); do [ "$(ps -o comm= -p "$c")" = java ] && { echo "$c"; return 0; }; jvm_under "$c" && return 0; done; return 1; }
PID=
for _ in $(seq 1 20); do PID=$(jvm_under "$MK" || true); [ -n "$PID" ] && break; sleep 0.5; done
if [ -n "$PID" ]; then kill -TERM "$PID"
else echo "play_synth: the client's JVM was not running at the end of the drive"; tail -5 "$LOG"; kill -TERM "$MK" 2>/dev/null || true; fi
wait $MK || true
grep -a "ORACLE" "$LOG" | tail -3
echo "rows: $(( $(wc -l < "$TAPE") - 1 ))"
