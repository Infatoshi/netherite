#!/usr/bin/env bash
# Compose ALL THREE proven drop-ins (sin + lightmap + biome) at once and capture the
# grass scene. native == vanilla (all three bit-exact) is the multi-kernel composition
# proof. Usage: capture_composed.sh <off|native>
set -u
MODE="$1"; ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
OUT="$ROOT/java/render-opt/dropin/composed"
echo "$MODE" > "$ROOT/java/render-opt/dropin/qsin_mode.txt"
echo "$MODE" > "$ROOT/java/render-opt/dropin/lightmap/qlm_mode.txt"
echo "$MODE" > "$ROOT/java/render-opt/dropin/biome/qbiome_mode.txt"
pkill -9 -f "[G]radleStart" 2>/dev/null; sleep 6
cd "$ROOT/java" || exit 1; rm -f runclient.log
setsid bash start_vnc_client.sh >/tmp/launch_composed_$MODE.out 2>&1 </dev/null &
for i in $(seq 1 300); do grep -q "\[qrl\] listening" runclient.log 2>/dev/null && { echo "NetheriteMod up ${i}s"; break; }; sleep 1; done
grep -q "\[qrl\] listening" runclient.log 2>/dev/null || { echo "ERROR no NetheriteMod"; exit 2; }
cd "$ROOT/java" && uv run --no-project python "$ROOT/java/render-opt/dropin/biome/scene_grass.py" 2>&1 | tail -6
sleep 7
DISPLAY=:1 ffmpeg -loglevel error -f x11grab -video_size 1280x720 -i :1 -frames:v 1 -y "$OUT/frame_${MODE}.png"
echo "frame bytes: $(stat -c %s "$OUT/frame_${MODE}.png")"
echo "--- invocation proofs ---"
grep -aE "\[qsin\] native|\[qlm\] native|\[qbiome\] native|MODE=" runclient.log | head -12
pkill -9 -f "[G]radleStart" 2>/dev/null
