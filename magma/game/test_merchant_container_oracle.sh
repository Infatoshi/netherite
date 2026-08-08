#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/merchant-container-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
mkdir -p "$OUT"

make -C "$ROOT/magma" game/test_merchant_container_oracle >/dev/null
"$ROOT/magma/game/test_merchant_container_oracle" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q merchantContainerGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "merchant container oracle: PASS (preview, reversed inputs, pickup, quick-move retry)"
