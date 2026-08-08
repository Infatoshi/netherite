#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR=${TMPDIR:-"$ROOT/.tmp"}
mkdir -p "$ROOT/magma/.tmp" "$TMPDIR"
cd "$ROOT/magma"
make game/test_scattered_swamp >/dev/null
./game/test_scattered_swamp >.tmp/scattered-swamp-c.txt
cd "$ROOT/java/Minecraft"
./gradlew -g run/gradle scatteredSwampGolden -q >../../magma/.tmp/scattered-swamp-java.txt
cmp ../../magma/.tmp/scattered-swamp-java.txt ../../magma/.tmp/scattered-swamp-c.txt
echo "scattered_swamp_oracle: PASS (real 1.11.2 piece, four orientations, witch position)"
