#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/map-color-registry"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"
mkdir -p "$OUT"

(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q mapColorGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
uv run --no-project python "$ROOT/blaze/tools/gen_map_color_registry.py" \
    "$OUT/java.txt" "$OUT/generated.h"
cmp "$ROOT/blaze/core/map_color_registry.h" "$OUT/generated.h"

cp "$OUT/generated.h" "$OUT/mutated.h"
printf '9' | dd of="$OUT/mutated.h" bs=1 seek=170 conv=notrunc status=none
if cmp -s "$ROOT/blaze/core/map_color_registry.h" "$OUT/mutated.h"; then
    echo "map color registry: mutation control failed" >&2
    exit 1
fi
echo "map color registry: PASS (3,776 initialized raw block states)"
