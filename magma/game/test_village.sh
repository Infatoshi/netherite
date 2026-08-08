#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/village-oracle"
mkdir -p "$OUT"

make -C "$ROOT/magma" game/test_village >/dev/null
"$ROOT/magma/game/test_village" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q villageGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "village graph oracle: PASS (6 cases)"

make -C "$ROOT/magma" game/test_village_residents >/dev/null
"$ROOT/magma/game/test_village_residents" > "$OUT/residents-c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q villageTemplateGolden) \
    2> "$OUT/residents-gradle.log" | sed -n '/^V /p' \
    > "$OUT/residents-java.txt"
cmp "$OUT/residents-java.txt" "$OUT/residents-c.txt"
echo "village residents oracle: PASS (spawn sites and professions)"

make -C "$ROOT/magma" game/test_village_runtime >/dev/null
"$ROOT/magma/game/test_village_runtime"

bash "$ROOT/magma/game/test_villager_trade.sh"
