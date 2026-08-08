#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/.tmp/audio-source-oracle"
SPEC="$OUT/selector.tsv"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}"
export PATH="$JAVA_HOME/bin:$PATH"
export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}"
export TMPDIR="${TMPDIR:-$HOME/dev/nw/.tmp}"
mkdir -p "$OUT" "$TMPDIR"

uv run --no-project python "$ROOT/magma/assets/build_sound_manifest.py" \
    "$ROOT/magma/assets/sound_manifest.h" "$SPEC" >/dev/null
make -C "$ROOT/magma" game/test_audio_source_oracle >/dev/null
"$ROOT/magma/game/test_audio_source_oracle" >"$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && ./gradlew -g run/gradle -q \
    -PsoundSelectorSpec="$SPEC" \
    -PsoundSelectorSeed=0x4e65746865726974 soundSourceGolden) \
    >"$OUT/java.txt" 2>"$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
cp "$OUT/c.txt" "$OUT/negative.txt"
sed -i '1s/00000000/00000001/' "$OUT/negative.txt"
if cmp -s "$OUT/java.txt" "$OUT/negative.txt"; then
    echo "audio source negative control did not fail" >&2
    exit 1
fi
echo "audio source oracle: PASS (all events, selected asset/routing scalars)"
