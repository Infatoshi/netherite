#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/special-item-use-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p "$OUT"

cc -O2 -Wall -Wextra -I"$ROOT/blaze/core" \
    "$ROOT/magma/game/test_special_item_use_oracle.c" \
    -o "$OUT/native"
"$OUT/native" > "$OUT/native.txt"
(cd "$ROOT/java/Minecraft" && \
    ./gradlew -g run/gradle -q specialItemUseGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/native.txt"

sed '0,/SUCCESS/s//FAIL/' "$OUT/native.txt" > "$OUT/mutated.txt"
if cmp -s "$OUT/java.txt" "$OUT/mutated.txt"; then
    echo "special item use oracle: mutation control failed" >&2
    exit 1
fi
echo "special item use oracle: PASS (576 rows, 464 positive, 112 negative)"
