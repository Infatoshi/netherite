#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/block-callback-drop"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
mkdir -p "$OUT"
make -C "$ROOT/magma" game/test_block_callback_drop >/dev/null
(cd "$ROOT/java/Minecraft" && ./gradlew -q -g run/gradle blockCallbackDropGolden) \
    >"$OUT/java.txt" 2>"$OUT/gradle.log"
"$ROOT/magma/game/test_block_callback_drop" "$OUT/java.txt"
