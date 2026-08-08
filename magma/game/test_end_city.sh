#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
cd "$ROOT/magma"
make game/test_end_city >/dev/null
./game/test_end_city >.tmp/end-city-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle endCityGolden -q >../../magma/.tmp/end-city-java.txt
cmp ../../magma/.tmp/end-city-java.txt ../../magma/.tmp/end-city-c.txt
echo "end_city_oracle: PASS (real Java 1.11.2 recursive piece graph)"
cd "$ROOT/magma"
make game/test_end_city_runtime >/dev/null
./game/test_end_city_runtime
make game/test_end_city_loot >/dev/null
./game/test_end_city_loot >.tmp/end-city-loot-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle endCityLootGolden -q >../../magma/.tmp/end-city-loot-java.txt
cmp ../../magma/.tmp/end-city-loot-java.txt ../../magma/.tmp/end-city-loot-c.txt
echo "end_city_loot_oracle: PASS (real 1.11.2 table, fill, and enchantments)"
