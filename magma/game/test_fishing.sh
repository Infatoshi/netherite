#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
make game/test_fishing >/dev/null
mkdir -p .tmp/fishing-java
./game/test_fishing >.tmp/fishing-c.txt
javac -d .tmp/fishing-java game/FishingGolden.java
java -cp .tmp/fishing-java FishingGolden >.tmp/fishing-java.txt
cmp .tmp/fishing-java.txt .tmp/fishing-c.txt
echo "fishing_oracle: PASS (Java-locked catching timers and loot weights)"
make game/test_fishing_render >/dev/null
./game/test_fishing_render >.tmp/fishing-render-c.txt
javac -d .tmp/fishing-java game/FishingRenderGolden.java
java -cp .tmp/fishing-java FishingRenderGolden >.tmp/fishing-render-java.txt
cmp .tmp/fishing-render-java.txt .tmp/fishing-render-c.txt
echo "fishing_render_oracle: PASS (17-point RenderFish line curve)"
make game/test_fishing_loot >/dev/null
./game/test_fishing_loot >.tmp/fishing-loot-c.txt
cd ../java/Minecraft
./gradlew -g run/gradle fishingLootGolden -q >../../magma/.tmp/fishing-loot-java.txt
cmp ../../magma/.tmp/fishing-loot-java.txt ../../magma/.tmp/fishing-loot-c.txt
echo "fishing_loot_oracle: PASS (real nested tables, damage, and enchantments)"
