#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR=${TMPDIR:-"$ROOT/.tmp"}
mkdir -p "$ROOT/magma/.tmp" "$TMPDIR"
cd "$ROOT/magma"
make game/test_jungle_loot >/dev/null
./game/test_jungle_loot >.tmp/jungle-loot-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle jungleLootGolden -q >../../magma/.tmp/jungle-loot-java.txt
cmp ../../magma/.tmp/jungle-loot-java.txt ../../magma/.tmp/jungle-loot-c.txt
echo "jungle_loot_oracle: PASS (real 1.11.2 chest and nine-slot dispenser tables)"
