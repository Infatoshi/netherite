#!/usr/bin/env bash
# Pure-Java self-test for ContainerTapeFormat (no full MC classpath).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src/main/java/netheritemod/ContainerTapeFormat.java"
OUT="${TMPDIR:-/tmp}/container_tape_format_test"
rm -rf "$OUT"
mkdir -p "$OUT"
javac -d "$OUT" "$SRC"
java -cp "$OUT" netheritemod.ContainerTapeFormat
echo "test_container_tape_format: PASS"
