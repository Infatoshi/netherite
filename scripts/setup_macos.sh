#!/usr/bin/env bash
# Native Apple Silicon setup:
#   legal local assets -> CPU/Metal builds -> macOS verification sweep.
#
# Usage:
#   bash scripts/setup_macos.sh                  # bootstrap + quick sweep
#   bash scripts/setup_macos.sh --full           # broader CPU/Metal pyramid
#   bash scripts/setup_macos.sh --bootstrap-only # assets only
#   bash scripts/setup_macos.sh --skip-assets    # assets already generated
#   bash scripts/setup_macos.sh --no-fetch-client
#
# By default, a missing Minecraft 1.11.2 client jar is downloaded from
# Mojang's published version manifest and kept only in the ignored local
# ForgeGradle cache.  You must own Minecraft.  No Mojang content is committed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODE=quick
DO_ASSETS=1
DO_FETCH=1
BOOTSTRAP_ONLY=0

for arg in "$@"; do
  case "$arg" in
    --quick) MODE=quick ;;
    --full) MODE=full ;;
    --bootstrap-only) BOOTSTRAP_ONLY=1 ;;
    --skip-assets) DO_ASSETS=0 ;;
    --fetch-client) DO_FETCH=1 ;;
    --no-fetch-client) DO_FETCH=0 ;;
    -h|--help)
      sed -n '2,14p' "$0" | sed 's/^#[[:space:]]*//'
      exit 0
      ;;
    *)
      echo "ERROR: unknown argument: $arg (try --help)" >&2
      exit 2
      ;;
  esac
done

if [ "$(uname -s)" != Darwin ]; then
  echo "ERROR: setup_macos.sh must run on macOS" >&2
  exit 2
fi
if [ "$(uname -m)" != arm64 ]; then
  echo "ERROR: an Apple Silicon arm64 process is required (Rosetta is unsupported)" >&2
  exit 2
fi

for tool in make uv xcrun file; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "ERROR: required tool '$tool' is missing" >&2
    exit 2
  }
done

if ! command -v sdl2-config >/dev/null 2>&1 && \
   [ ! -x /opt/homebrew/bin/sdl2-config ] && \
   ! command -v pkg-config >/dev/null 2>&1; then
  echo "ERROR: SDL2 was not found; install it with 'brew install sdl2 pkg-config'" >&2
  exit 2
fi
if command -v pkg-config >/dev/null 2>&1 && ! pkg-config --exists sdl2 2>/dev/null && \
   ! command -v sdl2-config >/dev/null 2>&1 && \
   [ ! -x /opt/homebrew/bin/sdl2-config ]; then
  echo "ERROR: SDL2 was not found; install it with 'brew install sdl2 pkg-config'" >&2
  exit 2
fi

echo "== native macOS host =="
sw_vers
uname -m
xcrun clang --version | head -1
bash scripts/check_metal.sh

if command -v java >/dev/null 2>&1 && java -version 2>&1 | head -1 | grep -q '1\.8\.'; then
  echo "JDK 8: available (Java-oracle gates may be run separately)"
else
  echo "JDK 8: unavailable; native CPU/Metal gates do not require it"
fi

if [ "$DO_ASSETS" -eq 1 ]; then
  echo "== legal local asset bootstrap =="
  if [ "$DO_FETCH" -eq 1 ]; then
    bash scripts/bootstrap_assets.sh --fetch-client
  else
    bash scripts/bootstrap_assets.sh
  fi
else
  echo "== asset bootstrap skipped =="
fi

if [ "$BOOTSTRAP_ONLY" -eq 1 ]; then
  echo "macOS bootstrap complete; build and sweep were not requested"
  exit 0
fi

echo "== native CPU/Metal builds =="
make -C c/magma demo world game game-dbg game-metal
make -C c/mc-sim metal-all
make -C c/magma blaze_so blaze_verify blaze_metal_dylib blaze_metal_check

if [ ! -s c/magma/rl/out/snaps/s10_t0.bsnp ]; then
  echo "== bake local tick-zero blaze snapshots =="
  (
    cd c/magma
    T0=1 uv run --no-project --with numpy python rl/blaze/make_snapshots.py
  )
else
  echo "== blaze tick-zero snapshots already present =="
fi

echo "== native macOS verification =="
bash netherite_macos_sweep.sh --"$MODE" --skip-build

echo "== setup_macos done =="
