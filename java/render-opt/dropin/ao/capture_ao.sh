#!/usr/bin/env bash
# Per-mode launch + capture for the AO drop-in (render-opt kernel 13, getAoBrightness).
# All three modes disable Forge's light pipeline (done in Recorder.init from qao_mode.txt)
# so the VANILLA AmbientOcclusionFace.getAoBrightness path is live and the coremod hook
# (OverclockingClassTransformer) drives it:
#   vanilla  -> MODE 0, hook falls through to the untouched vanilla body (baseline)
#   native   -> MODE 1, hook returns the C kernel result (libqao.so)
#   sabotage -> MODE 2, hook returns 0 (AO forced dark)
# Other drop-ins (sin/lightmap/biome) forced off so this isolates AO.
# Usage: capture_ao.sh <vanilla|native|sabotage>
set -u
MODE="$1"; ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
DROP="$ROOT/java/render-opt/dropin/ao"
echo "$MODE" > "$DROP/qao_mode.txt"
echo "off"   > "$ROOT/java/render-opt/dropin/qsin_mode.txt"
echo "off"   > "$ROOT/java/render-opt/dropin/lightmap/qlm_mode.txt"
echo "off"   > "$ROOT/java/render-opt/dropin/biome/qbiome_mode.txt"
pkill -9 -f "[G]radleStart" 2>/dev/null; sleep 6
cd "$ROOT/java" || exit 1; rm -f runclient.log
setsid bash start_vnc_client.sh >/tmp/launch_ao_$MODE.out 2>&1 </dev/null &
for i in $(seq 1 300); do grep -q "\[qrl\] listening" runclient.log 2>/dev/null && { echo "NetheriteMod up ${i}s"; break; }; sleep 1; done
grep -q "\[qrl\] listening" runclient.log 2>/dev/null || { echo "ERROR no NetheriteMod"; exit 2; }
cd "$ROOT/java" && uv run --no-project python "$DROP/scene_ao.py" 2>&1 | tail -6
sleep 7
DISPLAY=:1 ffmpeg -loglevel error -f x11grab -video_size 1280x720 -i :1 -frames:v 1 -y "$DROP/frame_${MODE}.png"
echo "frame bytes: $(stat -c %s "$DROP/frame_${MODE}.png")"
grep -a "Forge light pipeline DISABLED\|MALMO/qao:\|\[qao\]" runclient.log | head
pkill -9 -f "[G]radleStart" 2>/dev/null
