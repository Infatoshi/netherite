#!/usr/bin/env bash
# Capture one mode (off = pure-Java oracle | native = all four C kernels routed) of the
# stripped CLI instance running a fixed scene. Both modes run with identical strip
# flags, so the diff isolates C-vs-Java compute only.
# Usage: capture_stripped.sh <off|native> [actions|nether|end]   (default: actions)
set -u
MODE="${1:?usage: capture_stripped.sh <off|native> [scene]}"
SCENE="${2:-actions}"
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
HERE="$ROOT/java/render-opt/wholeframe/stripcheck"
DROP="$ROOT/java/render-opt/dropin"

echo "$MODE" > "$DROP/qsin_mode.txt"
echo "$MODE" > "$DROP/lightmap/qlm_mode.txt"
echo "$MODE" > "$DROP/biome/qbiome_mode.txt"
# AO baseline must be "vanilla" (Forge light pipeline OFF, Java vanilla AO), not "off"
# (which re-enables Forge's DIFFERENT continuous-AO algorithm): apples-to-apples with
# native, which also runs the vanilla path, just with the C kernel.
if [ "$MODE" = off ]; then echo vanilla > "$DROP/ao/qao_mode.txt"; else echo native > "$DROP/ao/qao_mode.txt"; fi

# scene "actions" keeps the historical frame_<mode>.png names; others get a scene tag
if [ "$SCENE" = actions ]; then FRAME="frame_$MODE.png"; else FRAME="frame_${SCENE}_$MODE.png"; fi

# up to 2 attempts: cross-dim transfers can hit a flaky vanilla PlayerChunkMap CME
# server crash; a fresh relaunch on an identical fresh world is still deterministic
SCENE_OK=0
for attempt in 1 2; do
  pkill -9 -f '[G]radleStart' 2>/dev/null; sleep 2
  : > "$ROOT/java/runclient.log"   # start_vnc_client.sh truncates it too, but only after ~7s
                              # of Xvfb setup; clear now so the wait loop can't match a
                              # stale "[qrl] listening" from the previous session
  # fresh world EVERY capture: seed-deterministic worldgen means off and native launches
  # get bit-identical worlds; a reused folder carries the previous run's mutations (death
  # drops, fills) and its original level.dat gamemode (which overrides world.mode)
  rm -rf "$ROOT/java/Minecraft/run/saves/qrl_0_flat"

  # CLI launch: survival for the full HUD (health/hunger/xp), --vnc for the Xvfb stack
  cd "$ROOT/java"
  uv run --no-project --with pyyaml python mc_cli.py --vnc \
      --set world.mode=survival --set world.seed=0 > "$HERE/launch_$MODE.out" 2>&1

  for i in $(seq 1 60); do
    rg -q '\[qrl\] listening' "$ROOT/java/runclient.log" 2>/dev/null && break
    sleep 5
  done
  rg -q '\[qrl\] listening' "$ROOT/java/runclient.log" || { echo "attempt $attempt: client never came up"; continue; }

  uv run --no-project python "$HERE/scene_$SCENE.py" 2>&1 | tee "$HERE/scene_${SCENE}_$MODE.out"
  if grep -q SCENE_READY "$HERE/scene_${SCENE}_$MODE.out"; then SCENE_OK=1; break; fi
  echo "attempt $attempt: scene failed, retrying"
done
[ "$SCENE_OK" = 1 ] || { echo "FATAL: scene failed after retries"; exit 1; }

sleep 3
DISPLAY=:1 ffmpeg -loglevel error -f x11grab -video_size 1280x720 -i :1 \
    -frames:v 1 -y "$HERE/$FRAME"
echo "captured $FRAME"

# behavioral checks (in-session, after the frame so they cannot disturb framing)
if [ "$SCENE" = actions ]; then
  uv run --no-project python "$HERE/death_check.py" 2>&1 | tee "$HERE/death_$MODE.out"
fi
if [ "$SCENE" = end ] && [ "$MODE" = native ]; then
  uv run --no-project python "$HERE/dragon_coverage.py" 2>&1 | tee "$HERE/dragon_$MODE.out"
fi

# log assertions for this mode
{
  echo "--- log assertions ($MODE) ---"
  rg -c 'Starting up SoundSystem' "$ROOT/java/runclient.log" > /dev/null \
      && echo "SOUND: SoundSystem STARTED (strip failed)" || echo "SOUND: no SoundSystem boot (stripped) OK"
  rg -o '\[qlaunch\] strip:.*' "$ROOT/java/runclient.log" | tail -1
  rg -o '\[qrl\] auto-launching world.*' "$ROOT/java/runclient.log" | tail -1
  rg -o '\[qrl\] launch settings applied.*' "$ROOT/java/runclient.log" | tail -1
  if [ "$MODE" = native ]; then
    for tag in '\[qsin\] native nsin\(\) INVOKED' '\[qlm\] native nlightmap\(\) INVOKED' \
               '\[qbiome\] *native nblend\(\) INVOKED' '\[qao\] native naoBrightness\(\) INVOKED'; do
      rg -q "$tag" "$ROOT/java/runclient.log" && echo "PROOF: $tag OK" || echo "PROOF: $tag MISSING"
    done
  fi
} | tee "$HERE/asserts_$MODE.out"

pkill -9 -f '[G]radleStart' 2>/dev/null
true
