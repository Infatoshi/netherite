#!/usr/bin/env bash
# Per-mode launch + capture of the USER'S REAL superflat course scene for the
# lightmap heavy-buffer drop-in. Isolates lightmap (sin forced off).
# Usage: capture_course.sh <off|native|sabotage> [time]
set -u
MODE="$1"; TIMEVAL="${2:-6000}"
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
DROP="$ROOT/java/render-opt/dropin/lightmap"; OUT="$DROP/course"
# authoritative sidecars: this drop-in native/sabotage, sin always off (isolate lightmap)
echo "$MODE" > "$DROP/qlm_mode.txt"
echo "off"   > "$ROOT/java/render-opt/dropin/qsin_mode.txt"
export SCENE_TIME="$TIMEVAL"
pkill -9 -f "[G]radleStart" 2>/dev/null; sleep 6
cd "$ROOT/java" || exit 1; rm -f runclient.log
setsid bash start_vnc_client.sh >/tmp/launch_course_${MODE}_${TIMEVAL}.out 2>&1 </dev/null &
for i in $(seq 1 300); do grep -q "\[qrl\] listening" runclient.log 2>/dev/null && { echo "NetheriteMod up ${i}s"; break; }; sleep 1; done
grep -q "\[qrl\] listening" runclient.log 2>/dev/null || { echo "ERROR no NetheriteMod"; exit 2; }
cd "$ROOT/java" && SCENE_TIME="$TIMEVAL" uv run --no-project python "$DROP/scene_course.py" 2>&1 | tail -8
FRAME="$OUT/frame_${MODE}_t${TIMEVAL}.png"
sleep 6
DISPLAY=:1 ffmpeg -loglevel error -f x11grab -video_size 1280x720 -i :1 -frames:v 1 -y "$FRAME"
echo "frame bytes: $(stat -c %s "$FRAME")"
grep -a "\[qlm\]" runclient.log | head
pkill -9 -f "[G]radleStart" 2>/dev/null
