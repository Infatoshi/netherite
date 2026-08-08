#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
cd "$ROOT/magma"
mkdir -p .tmp
make game/test_mansion >/dev/null
./game/test_mansion >.tmp/mansion-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle woodlandMansionGolden -q \
    >../../magma/.tmp/mansion-java.txt
cmp ../../magma/.tmp/mansion-java.txt ../../magma/.tmp/mansion-c.txt
echo "mansion_oracle: PASS (real Java 1.11.2 grid, rooms, and piece graph)"
cd "$ROOT/magma"
make game/test_mansion_runtime >/dev/null
./game/test_mansion_runtime
make game/test_mansion_loot >/dev/null
./game/test_mansion_loot >.tmp/mansion-loot-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle mansionLootGolden -q \
    >../../magma/.tmp/mansion-loot-java.txt
cmp ../../magma/.tmp/mansion-loot-java.txt \
    ../../magma/.tmp/mansion-loot-c.txt
echo "mansion_loot_oracle: PASS (real 1.11.2 table, fill, and enchantments)"
cd "$ROOT/magma"
make game/test_illager_loot >/dev/null
./game/test_illager_loot >.tmp/illager-loot-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle illagerLootGolden -q \
    >../../magma/.tmp/illager-loot-java.txt
cmp ../../magma/.tmp/illager-loot-java.txt \
    ../../magma/.tmp/illager-loot-c.txt
echo "illager_loot_oracle: PASS (real 1.11.2 tables and entity RNG cursor)"
