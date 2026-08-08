#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
make game/test_scattered_jungle >/dev/null
mkdir -p .tmp
./game/test_scattered_jungle >.tmp/scattered-jungle-c.txt
cd ../java/Minecraft
./gradlew -g run/gradle scatteredJungleGolden -q \
    >../../magma/.tmp/scattered-jungle-java.txt
cmp ../../magma/.tmp/scattered-jungle-java.txt \
    ../../magma/.tmp/scattered-jungle-c.txt
echo "scattered_jungle_oracle: PASS (real 1.11.2 piece, four orientations, volume, metadata, loot seeds)"
