#!/usr/bin/env bash
# window_render_check.sh - build magma_game and run the interactive-loop
# regression check (scripts/window_render_check.py).
#
# Covers the windowed path that pixel gates never exercise:
#   LIGHTMAP-SANITY  lightmap vs legacy noon-fold + terrain not washed white
#   MOB-LIT          --set mob_demo=1 entities draw, zombie palette, not unlit-white
#   TICK-RATE        headless --frames dumps are byte-identical across runs
#
# Usage:
#   bash scripts/window_render_check.sh [args passed to the python checker]
#   bash scripts/window_render_check.sh --game /path/to/magma_game
#   bash scripts/window_render_check.sh --only lightmap-sanity
#
# Env:
#   SKIP_BUILD=1   skip `make magma_game` (use existing binary / --game)
#   UV_CACHE_DIR / TMPDIR pinned below for anvil shared-box safety.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}"
export TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"
mkdir -p "$TMPDIR" "$UV_CACHE_DIR"

SKIP_BUILD="${SKIP_BUILD:-0}"
# If caller passes --game, still allow build unless SKIP_BUILD=1 (binary may
# be the default tree one). Build only when using the in-tree binary.
WANT_BUILD=1
for a in "$@"; do
  case "$a" in
    --game) WANT_BUILD=0 ;;
  esac
done

if [[ "$SKIP_BUILD" != "1" && "$WANT_BUILD" == "1" ]]; then
  echo "[window_render_check] building magma_game (-j8)"
  make -C magma magma_game -j8
fi

GAME_DEFAULT="$ROOT/magma/magma_game"
if [[ ! -x "$GAME_DEFAULT" && "$WANT_BUILD" == "1" ]]; then
  echo "ERROR: $GAME_DEFAULT missing after build" >&2
  exit 2
fi

command -v uv >/dev/null || { echo "ERROR: uv required" >&2; exit 2; }

echo "[window_render_check] running checker"
exec uv run --no-project --with numpy,pillow \
  python "$ROOT/scripts/window_render_check.py" "$@"
