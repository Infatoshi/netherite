#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
java_out="$(mktemp)"
native_out="$(mktemp)"
trap 'rm -f "$java_out" "$native_out"' EXIT
make -C magma game/test_ocean_monument >/dev/null
(cd java/Minecraft && ./gradlew -g run/gradle oceanMonumentGolden \
    --console=plain -q) | grep -E '^[MEC] ' >"$java_out"
./magma/game/test_ocean_monument >"$native_out"
if ! diff -u "$java_out" "$native_out"; then
    echo "ocean_monument_oracle: FAIL" >&2
    exit 1
fi
echo "ocean_monument_oracle: PASS (room graph, clipped blocks, elders)"
