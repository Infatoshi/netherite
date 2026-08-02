#!/usr/bin/env bash
# One qsin_mode run: set mode -> launch client -> wait for NetheriteMod -> deterministic scene
# -> settle fixed ticks -> grab one frame -> record proof line -> kill client.
# Usage: capture_run.sh <off|native|sabotage>
set -u
MODE="$1"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DROP="$ROOT/java/render-opt/dropin"
echo "$MODE" > "$DROP/qsin_mode.txt"   # sidecar is the only mode source

pkill -9 -f "[G]radleStart" 2>/dev/null; sleep 3
cd "$ROOT/java" || exit 1
rm -f runclient.log
setsid bash start_vnc_client.sh >/tmp/launch_$MODE.out 2>&1 </dev/null &
echo "[driver] launched mode=$MODE, waiting for NetheriteMod listening..."

# wait up to 240s for the bridge
for i in $(seq 1 240); do
  if grep -q "\[qrl\] listening" runclient.log 2>/dev/null; then echo "[driver] NetheriteMod up after ${i}s"; break; fi
  sleep 1
done
grep -q "\[qrl\] listening" runclient.log 2>/dev/null || { echo "[driver] ERROR NetheriteMod never came up"; tail -20 runclient.log; exit 2; }

# deterministic scene + fixed-tick settle
cd "$ROOT/java" && uv run --no-project python "$DROP/scene_setup.py" 2>&1 | tail -8
sleep 1

# capture one frame from Xvfb :1
DISPLAY=:1 ffmpeg -loglevel error -f x11grab -video_size 1280x720 -i :1 -frames:v 1 -y "$DROP/frame_${MODE}.png"
echo "[driver] frame: $(ls -la $DROP/frame_${MODE}.png)"

# proof: did native path / mode resolve fire?
echo "=== QSIN proof lines (mode=$MODE) ==="
grep -a "\[qsin\]" runclient.log | head
echo "=== end proof ==="

pkill -9 -f "[G]radleStart" 2>/dev/null
