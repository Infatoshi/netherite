#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/map-update-oracle"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p "$OUT"

make -C "$ROOT/magma" game/test_map_update_oracle >/dev/null
"$ROOT/magma/game/test_map_update_oracle" > "$OUT/native.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q mapUpdateGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/native.txt"

cp "$OUT/native.txt" "$OUT/mutated.txt"
printf 'ff' | dd of="$OUT/mutated.txt" bs=1 seek=8 conv=notrunc status=none
if cmp -s "$OUT/java.txt" "$OUT/mutated.txt"; then
    echo "map update oracle: mutation control failed" >&2
    exit 1
fi
echo "map update oracle: PASS (3 terrain planes, 16 update phases each)"
