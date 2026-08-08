#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FRAME_DIR="$SCRIPT_DIR/c_frames"

export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}"
export TMPDIR="${TMPDIR:-$ROOT/.tmp}"
mkdir -p "$FRAME_DIR" "$TMPDIR"

for mode in save load corner data; do
    for suffix in a b; do
        golden="$SCRIPT_DIR/goldens/gui_structure_${mode}_${suffix}.png"
        if [[ ! -s "$golden" ]]; then
            echo "structure GUI gate: FATAL missing Java golden $golden" >&2
            exit 1
        fi
    done
done

make -C "$ROOT/magma" game/structure_gui_candidate
"$ROOT/magma/game/structure_gui_candidate" "$FRAME_DIR"

runner=(uv run --no-project --with pillow --with numpy python
    "$SCRIPT_DIR/compare_structure_gui.py" --root "$ROOT")
"${runner[@]}"
"${runner[@]}" --selftest

echo "structure GUI gate: PASS"
