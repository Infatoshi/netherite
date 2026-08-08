#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
make -C "$ROOT/magma" game >/dev/null
UV_CACHE_DIR="$HOME/.cache/uv" TMPDIR="$HOME/dev/nw/.tmp" \
    uv run --no-project --with numpy python \
    "$ROOT/verify/video_replay/pipeline.py" "$@"
