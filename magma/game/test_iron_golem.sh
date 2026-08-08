#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/.tmp/iron-golem-oracle"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p "$OUT"
make -C "$ROOT/magma" game/test_iron_golem >/dev/null
"$ROOT/magma/game/test_iron_golem" >"$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && \
    ./gradlew -g run/gradle -q ironGolemGolden) \
    >"$OUT/java.txt" 2>"$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "iron golem oracle: PASS (attributes, NBT, timers, attack RNG)"
