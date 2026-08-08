#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/boat-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p "$OUT"

make -C "$ROOT/magma" game/test_boat_oracle >/dev/null
"$ROOT/magma/game/test_boat_oracle" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q boatGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "boat oracle: PASS (status/motion, shaped collision/slime, steering/paddles, passengers/push, lerp, fall/ejection, save fork)"
