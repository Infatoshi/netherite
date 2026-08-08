#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
cd "$ROOT/magma"
mkdir -p .tmp
make game/test_guardian >/dev/null
./game/test_guardian >.tmp/guardian-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle guardianGolden -q \
    >../../magma/.tmp/guardian-java.txt
./gradlew -g run/gradle guardianLootGolden -q \
    >>../../magma/.tmp/guardian-java.txt
cmp ../../magma/.tmp/guardian-java.txt \
    ../../magma/.tmp/guardian-c.txt
echo "guardian_oracle: PASS (attributes, beam timing/damage, thorns, loot RNG)"
