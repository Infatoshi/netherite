#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR=${TMPDIR:-"$PWD/.tmp"}
mkdir -p .tmp/minecart-live
make game/test_minecart_live >/dev/null
./game/test_minecart_live >.tmp/minecart-live/c.txt
(cd ../java/Minecraft && ./gradlew -g run/gradle minecartGolden --quiet) \
    2>/dev/null | sed -n \
    -e '/^S /p' -e '/^B /p' -e '/^P /p' -e '/^U /p' \
    -e '/^D /p' -e '/^A /p' -e '/^H /p' \
    -e '/minecart_live:/s/.*minecart_live:/minecart_live:/p' \
    >.tmp/minecart-live/java.txt
cmp .tmp/minecart-live/java.txt .tmp/minecart-live/c.txt
echo "minecart_oracle: PASS (real 1.11.2 rails, power, slope, detector, activator)"
