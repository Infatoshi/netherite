#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/villager-trade-oracle"
mkdir -p "$OUT"

make -C "$ROOT/magma" game/test_villager_trade >/dev/null
"$ROOT/magma/game/test_villager_trade" > "$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -q villagerTradeGolden) \
    > "$OUT/java.txt" 2> "$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "villager trade oracle: PASS (11 careers, 22 ordinary offers, execution)"
