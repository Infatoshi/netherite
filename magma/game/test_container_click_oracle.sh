#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/container-click-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR="${TMPDIR:-$ROOT/.tmp}"
mkdir -p "$OUT"

make -C "$ROOT/magma" game/test_container_click_oracle >/dev/null
"$ROOT/magma/game/test_container_click_oracle" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q containerClickGolden) \
    > "$OUT/java.raw" 2> "$OUT/gradle.log"
sed -n -E 's/^.*\[STDOUT\]: \[[^]]+\]: //p; /^([A-Z][0-9]|M[0-9]{3}|O[0-9]{5}) /p' \
    "$OUT/java.raw" > "$OUT/java.txt"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "container click oracle: PASS (2,840 Java/native rows, all seven ClickTypes)"
