#!/usr/bin/env bash
# Focused pure-Java unit test for ChunkFillDiff (no Minecraft / Gradle).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src/main/java/netheritemod/ChunkFillDiff.java"
TEST="$ROOT/src/test/java/netheritemod/ChunkFillDiffTest.java"
OUT="${TMPDIR:-/tmp}/qrl-chunk-fill-diff-test"
rm -rf "$OUT"
mkdir -p "$OUT"
javac -source 1.8 -target 1.8 -d "$OUT" "$SRC" "$TEST"
java -cp "$OUT" netheritemod.ChunkFillDiffTest
