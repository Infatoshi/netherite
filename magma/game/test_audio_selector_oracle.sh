#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/audio-selector-oracle"
SPEC="$OUT/selector.tsv"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}"
export TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"
mkdir -p "$OUT" "$TMPDIR"

uv run --no-project python "$ROOT/magma/assets/build_sound_manifest.py" \
    "$ROOT/magma/assets/sound_manifest.h" "$SPEC" >/dev/null
make -C "$ROOT/magma" game/test_audio_selector_oracle >/dev/null
"$ROOT/magma/game/test_audio_selector_oracle" >"$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q \
    -PsoundSelectorSpec="$SPEC" \
    -PsoundSelectorSeed=0x4e65746865726974 soundSelectorGolden) \
    >"$OUT/java.txt" 2>"$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
echo "audio selector oracle: PASS (all represented accessors, 16 draws each)"
