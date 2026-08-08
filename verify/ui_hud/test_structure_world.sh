#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ORACLE="$ROOT/verify/ui_hud/goldens"
FRAMES="$ROOT/.tmp/structure_world_frames"
REPORT="$ROOT/.tmp/structure_world_report"

for state in save_air load_transform load_hidden; do
    for suffix in on on_2; do
        frame="$ORACLE/structure_world_${state}_${suffix}.png"
        if [[ ! -s "$frame" ]]; then
            echo "FATAL: missing real-Java Structure frame: $frame" >&2
            exit 1
        fi
    done
done

mkdir -p "$FRAMES" "$REPORT"
make -C "$ROOT/magma" game/structure_world_candidate
"$ROOT/magma/game/structure_world_candidate" "$FRAMES"
UV_CACHE_DIR="$HOME/.cache/uv" TMPDIR="$HOME/dev/nw/.tmp" \
    uv run --no-project --with numpy,pillow python \
    "$ROOT/verify/ui_hud/compare_structure_world.py" \
    --oracle "$ORACLE" --candidate "$FRAMES" --out "$REPORT" --selftest
