#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
export TMPDIR=${TMPDIR:-"$PWD/.tmp"}
mkdir -p .tmp/horse-particle-oracle
make game/test_horse_particle_oracle >/dev/null
./game/test_horse_particle_oracle >.tmp/horse-particle-oracle/c.txt
(cd ../java/Minecraft && ./gradlew -g run/gradle horseParticleGolden --quiet) \
    2>/dev/null | sed -n -e '/^[HS] /p' \
    >.tmp/horse-particle-oracle/java.txt
cmp .tmp/horse-particle-oracle/java.txt \
    .tmp/horse-particle-oracle/c.txt
echo "horse particle oracle: PASS (status 6/7 positions and Java RNG cursor)"
