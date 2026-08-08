#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/furnace-output-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR="${TMPDIR:-$ROOT/.tmp}"
mkdir -p "$OUT"

make -C "$ROOT/magma" game/test_furnace_output_oracle >/dev/null
"$ROOT/magma/game/test_furnace_output_oracle" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q furnaceOutputGolden) \
    > "$OUT/java.raw" 2> "$OUT/gradle.log"
sed -n -E 's/^.*\[STDOUT\]: \[[^]]+\]: //p; /^R /p' \
    "$OUT/java.raw" > "$OUT/java.txt"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "furnace output oracle: PASS (stat, XP, event, achievement order)"
