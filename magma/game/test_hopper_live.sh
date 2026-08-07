#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR=${TMPDIR:-"$PWD/.tmp"}
mkdir -p .tmp/hopper-live
make game/test_hopper_live >/dev/null
./game/test_hopper_live >.tmp/hopper-live/c.txt
(cd ../java/Minecraft && ./gradlew -g run/gradle hopperGolden --quiet) \
    2>/dev/null | sed -n \
    -e '/^A /p' -e '/^P /p' -e '/^C /p' -e '/^I /p' -e '/^D /p' \
    -e '/^E /p' \
    -e '/^X /p' \
    -e '/hopper_live:/s/.*hopper_live:/hopper_live:/p' \
    >.tmp/hopper-live/java.txt
cmp .tmp/hopper-live/java.txt .tmp/hopper-live/c.txt
echo "automation_oracle: PASS (hopper, dropper, and twelve dispenser behaviors)"
