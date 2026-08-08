#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/item-block-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}"
export TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"
mkdir -p "$OUT" "$TMPDIR"

make -C "$ROOT/magma" game/test_item_block_oracle >/dev/null
(cd "$ROOT/java/Minecraft" && ./gradlew -q -g run/gradle itemBlockGolden) \
    >"$OUT/java.txt" 2>"$OUT/gradle.log"
"$ROOT/magma/game/test_item_block_oracle" "$OUT/java.txt"

cp "$OUT/java.txt" "$OUT/negative.txt"
sed -i '0,/ SUCCESS /s// FAIL /' "$OUT/negative.txt"
if "$ROOT/magma/game/test_item_block_oracle" \
        "$OUT/negative.txt" >/dev/null 2>&1; then
    echo "item block negative control did not fail" >&2
    exit 1
fi
