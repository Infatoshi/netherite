#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/tool-callback-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}"
export TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"
mkdir -p "$OUT" "$TMPDIR"

make -C "$ROOT/magma" game/test_tool_callback_oracle >/dev/null
(cd "$ROOT/java/Minecraft" && ./gradlew -q -g run/gradle toolCallbackGolden) \
    >"$OUT/java.txt" 2>"$OUT/gradle.log"
"$ROOT/magma/game/test_tool_callback_oracle" "$OUT/java.txt"

cp "$OUT/java.txt" "$OUT/negative.txt"
sed -i '0,/^C .* 0$/s//&__MUTATE__/' "$OUT/negative.txt"
sed -i '0,/ 0__MUTATE__$/s// 1/' "$OUT/negative.txt"
if "$ROOT/magma/game/test_tool_callback_oracle" \
        "$OUT/negative.txt" >/dev/null 2>&1; then
    echo "tool callback negative control did not fail" >&2
    exit 1
fi
