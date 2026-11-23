#!/bin/bash
# One renderstate recording: replay a tape with --frame-every and --renderstate.
#   bash tests/rs.sh <tape.jsonl> <frame-every> <outdir>
# The recorder only turns on here; a plain replay records nothing.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)      # oracle/tests
cd "$HERE/.."                            # oracle/
OUT=$(cd .. && pwd)/out/java
REF=$(realpath "$1"); EV="$2"; DIR=$(realpath -m "$3")
NAME="rs-$(basename "$DIR")"
mkdir -p "$DIR" "$OUT/run/$NAME"
cd "$OUT/run/$NAME"
bash "$HERE/headless.sh" "${SLOTS:-24}" \
  /usr/lib/jvm/java-8-openjdk-amd64/bin/java -Xmx3G -XX:+UseSerialGC -XX:MinHeapFreeRatio=10 -XX:MaxHeapFreeRatio=30 \
  -Djavax.accessibility.assistive_technologies= \
  -Djava.library.path="$OUT/natives" \
  -cp "$OUT/classes:$OUT/lib/*:$OUT/client/1.7.10.jar" netherite.oracle.Main \
  --game-dir . --assets "$OUT/assets" \
  --agent --replay "$REF" --tape "${REF%.jsonl}.replay.jsonl" \
  --frame-every "$EV" --renderstate "$DIR" ${RS_ARGS:-} 2>&1 | grep -a "ORACLE\|RenderStateProbe\|Exception\|java.lang"
# The probe's close hook writes manifest.json into the run's game dir.
M="$OUT/run/$NAME/manifest.json"
[ -f "$M" ] && cp "$M" "$DIR/manifest.json"
# The recording keeps the tape it replays: the gate checks each frame's tick
# against it.
cp "$REF" "$DIR/tape.jsonl"
# The probe writes its manifest from a shutdown hook, but the oracle's finish
# path halts the JVM and hooks never run; write it here instead.
SEED=$(grep -m1 -o '"seed":[0-9]*' "$DIR/tape.jsonl" | cut -d: -f2)
FRAME_N=$(grep -c . "$DIR/frames.jsonl")
printf '{"kind":"renderstate","tape":"%s","seed":%s,"frames":%s}\n' "$REF" "${SEED:-0}" "$FRAME_N" > "$DIR/manifest.json"
