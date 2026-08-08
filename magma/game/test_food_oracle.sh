#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/food-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}"
export TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"
mkdir -p "$OUT" "$TMPDIR"

make -C "$ROOT/magma" game/test_food_oracle >/dev/null
"$ROOT/magma/game/test_food_oracle" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q foodGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
uv run --no-project python "$ROOT/verify/completeness/food_registry_gate.py" \
    "$OUT/java.txt" "$OUT/c.txt"
