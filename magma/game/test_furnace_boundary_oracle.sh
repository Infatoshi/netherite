#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/furnace-boundary-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR="${TMPDIR:-$ROOT/.tmp}"
mkdir -p "$OUT"

"${CC:-cc}" -O2 -ffp-contract=off -Wall -Wextra -std=c11 \
    -I"$ROOT/magma" -I"$ROOT/blaze/core" \
    "$ROOT/magma/game/test_furnace_boundary_oracle.c" \
    "$ROOT/magma/game/furnace_live.c" -o "$OUT/native"
"$OUT/native" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q furnaceBoundaryGolden) \
    > "$OUT/java.raw" 2> "$OUT/gradle.log"
sed -n -E \
    's/^.*\[STDOUT\]: \[[^]]+\]: //p; /^(L0|L1|C1|B1|D0|I0|Z1|W1) /p' \
    "$OUT/java.raw" > "$OUT/java.txt"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "furnace boundary oracle: PASS (NBT reload and adjacent tick states)"
