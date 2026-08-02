#!/usr/bin/env bash
# Per-mode launch + capture for the lightmap heavy-buffer drop-in.
# Usage: capture_lm.sh <off|native|sabotage>
set -u
MODE="$1"; ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"; DROP="$ROOT/java/render-opt/dropin/lightmap"
echo "$MODE" > "$DROP/qlm_mode.txt"   # sidecar is the only mode source
pkill -9 -f "[G]radleStart" 2>/dev/null; sleep 6
cd "$ROOT/java" || exit 1; rm -f runclient.log
setsid bash start_vnc_client.sh >/tmp/launch_lm_$MODE.out 2>&1 </dev/null &
for i in $(seq 1 300); do grep -q "\[qrl\] listening" runclient.log 2>/dev/null && { echo "NetheriteMod up ${i}s"; break; }; sleep 1; done
grep -q "\[qrl\] listening" runclient.log 2>/dev/null || { echo "ERROR no NetheriteMod"; exit 2; }
cd "$ROOT/java" && uv run --no-project python "$DROP/scene_lm.py" 2>&1 | tail -8
sleep 8
DISPLAY=:1 ffmpeg -loglevel error -f x11grab -video_size 1280x720 -i :1 -frames:v 1 -y "$DROP/frame_${MODE}.png"
echo "frame bytes: $(stat -c %s $DROP/frame_${MODE}.png)"
grep -a "\[qlm\]" runclient.log | head
pkill -9 -f "[G]radleStart" 2>/dev/null
