#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/.tmp/video-replay-gate"
mkdir -p "$OUT"

UV_CACHE_DIR="$HOME/.cache/uv" TMPDIR="$HOME/dev/nw/.tmp" \
    uv run --no-project --with pytest --with numpy pytest -q \
    "$ROOT/verify/video_replay/test_ingest.py" \
    "$ROOT/verify/video_replay/test_video_features.py" \
    "$ROOT/verify/video_replay/test_auto_search.py" \
    "$ROOT/verify/video_replay/test_beam_search.py"

make -C "$ROOT/magma" game >/dev/null
"$ROOT/magma/magma_game" --seed 0 --world superflat --mobs off --headless \
    --ticks 22 --script "$ROOT/verify/video_replay/fixtures/death_respawn.jsonl" \
    --state-out "$OUT/death_respawn_state.jsonl" >/dev/null
jq -e 'select(.tick == 21 and .dead == 0 and .health == 20)' \
    "$OUT/death_respawn_state.jsonl" >/dev/null
echo "video_replay_gate: PASS"
