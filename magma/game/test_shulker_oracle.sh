#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="$ROOT/.tmp/shulker-oracle"
mkdir -p "$OUT"

export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export UV_CACHE_DIR=${UV_CACHE_DIR:-$HOME/.cache/uv}
export TMPDIR=${TMPDIR:-$ROOT/.tmp}

make -C "$ROOT/magma" game/test_shulker_live >/dev/null
"$ROOT/magma/game/test_shulker_live" > "$OUT/c.txt"
(
    cd "$ROOT/java/Minecraft"
    ./gradlew -g run/gradle shulkerGolden
) | grep -E '^[ABFT] ' > "$OUT/java.txt"

diff -u "$OUT/java.txt" "$OUT/c.txt"
echo "shulker_oracle: PASS (AI, peek, RNG, guided bullet, teleport exact)"
