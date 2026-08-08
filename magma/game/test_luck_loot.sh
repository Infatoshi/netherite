#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$ROOT/../blaze" && pwd)"
cd "$ROOT"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p .tmp
${CC:-gcc} -O2 -ffp-contract=off -Wall -Wextra \
  -I. -Icore -I"$BLAZE/core" game/test_luck_loot.c -lm \
  -o .tmp/test_luck_loot
.tmp/test_luck_loot >.tmp/luck-loot-c.txt
cd ../java/Minecraft
./gradlew -g run/gradle luckLootGolden -q \
  >../../magma/.tmp/luck-loot-java.txt
cmp ../../magma/.tmp/luck-loot-java.txt \
    ../../magma/.tmp/luck-loot-c.txt
echo "luck_loot_oracle: PASS (quality weights and bonus rolls, 64 cases)"
