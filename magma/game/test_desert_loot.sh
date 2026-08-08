#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR=${TMPDIR:-"$ROOT/.tmp"}
mkdir -p "$ROOT/magma/.tmp" "$TMPDIR"
cd "$ROOT/magma"
make game/test_desert_loot >/dev/null
./game/test_desert_loot >.tmp/desert-loot-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle desertLootGolden -q >../../magma/.tmp/desert-loot-java.txt
cmp ../../magma/.tmp/desert-loot-java.txt ../../magma/.tmp/desert-loot-c.txt
echo "desert_loot_oracle: PASS (real 1.11.2 table, fill, and enchantments)"
