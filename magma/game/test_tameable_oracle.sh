#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR=${TMPDIR:-"$PWD/.tmp"}
mkdir -p .tmp/tameable-oracle
make game/test_tameable_oracle >/dev/null
./game/test_tameable_oracle >.tmp/tameable-oracle/c.txt
(cd ../java/Minecraft && ./gradlew -g run/gradle tameableGolden --quiet) \
    2>/dev/null | sed -n -e '/^W[01CHDS] /p' -e '/^O[01C] /p' \
    -e '/^[WO]B /p' \
    >.tmp/tameable-oracle/java.txt
cmp .tmp/tameable-oracle/java.txt .tmp/tameable-oracle/c.txt
echo "tameable oracle: PASS (wolf/ocelot tame, heal, dye, sit, breeding, RNG)"
