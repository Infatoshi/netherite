#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/inventory-helper-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p "$OUT"

make -C "$ROOT/magma" game/test_inventory_helper_oracle >/dev/null
"$ROOT/magma/game/test_inventory_helper_oracle" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q inventoryHelperGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "inventory helper oracle: PASS (split, offsets, Gaussian motion, cursors)"
