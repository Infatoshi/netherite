#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
export UV_CACHE_DIR=${UV_CACHE_DIR:-/home/jawaugh/.cache/uv}
export TMPDIR=${TMPDIR:-"$ROOT/.tmp"}
mkdir -p "$TMPDIR"

bash "$ROOT/magma/game/test_hopper_live.sh"
bash "$ROOT/magma/game/test_weather_lightning.sh"
bash "$ROOT/magma/game/test_firework.sh"
bash "$ROOT/magma/game/test_fishing.sh"
bash "$ROOT/magma/game/test_village.sh"
make -C "$ROOT/magma" test-block-audio-oracle
bash "$ROOT/magma/game/test_audio_live.sh"
