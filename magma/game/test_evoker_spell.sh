#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
cd "$ROOT/magma"
mkdir -p .tmp
make game/test_evoker_spell >/dev/null
./game/test_evoker_spell >.tmp/evoker-spell-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle evokerSpellGolden -q \
    >../../magma/.tmp/evoker-spell-java.txt
cmp ../../magma/.tmp/evoker-spell-java.txt \
    ../../magma/.tmp/evoker-spell-c.txt
echo "evoker_spell_oracle: PASS (fang geometry/lifecycle, Vex summon, Wololo)"
