#!/usr/bin/env bash
# Per-mode launch + capture for the biome 3x3 blend drop-in. Isolates biome (sin+lightmap
# forced off). Usage: capture_biome.sh <off|native|sabotage>
set -u
MODE="$1"; ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
DROP="$ROOT/java/render-opt/dropin/biome"
echo "$MODE" > "$DROP/qbiome_mode.txt"
echo "off"   > "$ROOT/java/render-opt/dropin/qsin_mode.txt"
echo "off"   > "$ROOT/java/render-opt/dropin/lightmap/qlm_mode.txt"
pkill -9 -f "[G]radleStart" 2>/dev/null; sleep 6
cd "$ROOT/java" || exit 1; rm -f runclient.log
setsid bash start_vnc_client.sh >/tmp/launch_biome_$MODE.out 2>&1 </dev/null &
for i in $(seq 1 300); do grep -q "\[qrl\] listening" runclient.log 2>/dev/null && { echo "NetheriteMod up ${i}s"; break; }; sleep 1; done
grep -q "\[qrl\] listening" runclient.log 2>/dev/null || { echo "ERROR no NetheriteMod"; exit 2; }
cd "$ROOT/java" && uv run --no-project python "$DROP/scene_grass.py" 2>&1 | tail -10
sleep 7
DISPLAY=:1 ffmpeg -loglevel error -f x11grab -video_size 1280x720 -i :1 -frames:v 1 -y "$DROP/frame_${MODE}.png"
echo "frame bytes: $(stat -c %s "$DROP/frame_${MODE}.png")"
grep -a "\[qbiome\]" runclient.log | head
pkill -9 -f "[G]radleStart" 2>/dev/null
