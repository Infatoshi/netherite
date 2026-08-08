#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="$ROOT/.tmp/no-ai-mob-oracle"
mkdir -p "$OUT"

export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export UV_CACHE_DIR=${UV_CACHE_DIR:-$HOME/.cache/uv}
export TMPDIR=${TMPDIR:-$ROOT/.tmp}

make -C "$ROOT/magma" game/test_no_ai_mob_oracle >/dev/null
"$ROOT/magma/game/test_no_ai_mob_oracle" > "$OUT/c.txt"
(
    cd "$ROOT/java/Minecraft"
    ./gradlew -g run/gradle noAiMobGolden --quiet
) 2>/dev/null | grep '^N' > "$OUT/java.txt"

diff -u "$OUT/java.txt" "$OUT/c.txt"
echo "no_ai_mob_oracle: PASS (19 living classes, base state, RNG, motion)"
