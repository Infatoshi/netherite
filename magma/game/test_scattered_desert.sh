#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
make game/test_scattered_desert >/dev/null
mkdir -p .tmp
./game/test_scattered_desert >.tmp/scattered-desert-c.txt
cd ../java/Minecraft
./gradlew -g run/gradle scatteredGolden -q \
    >../../magma/.tmp/scattered-desert-java.txt
cmp ../../magma/.tmp/scattered-desert-java.txt \
    ../../magma/.tmp/scattered-desert-c.txt
echo "scattered_desert_oracle: PASS (real 1.11.2 piece, four orientations, volume, metadata, loot seeds)"
