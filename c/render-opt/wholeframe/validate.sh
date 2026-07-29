#!/usr/bin/env bash
# Offline sanity check: run the whole-frame diff on the two committed spike frame
# sets (REAL-MC captures). Proves diff_frame.py builds/runs and reproduces the
# published spike numbers without touching the game.
set -eu
cd "$(dirname "$0")"
echo "===== sin spike (../dropin) ====="
uv run --no-project python diff_frame.py \
  ../dropin/frame_off.png ../dropin/frame_native.png ../dropin/frame_sabotage.png
echo "===== lightmap spike (../dropin/lightmap) ====="
uv run --no-project python diff_frame.py \
  ../dropin/lightmap/frame_off.png ../dropin/lightmap/frame_native.png ../dropin/lightmap/frame_sabotage.png
