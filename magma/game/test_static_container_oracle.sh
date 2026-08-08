#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/static-container-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
mkdir -p "$OUT"
export NETHERITE_STATIC_CONTAINER_CHECKPOINT="$OUT/checkpoint.bin"

make -C "$ROOT/magma" game/test_static_container_oracle >/dev/null
"$ROOT/magma/game/test_static_container_oracle" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q staticContainerGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "static container oracle: PASS (layout, pickup, merge, quick-move order)"
