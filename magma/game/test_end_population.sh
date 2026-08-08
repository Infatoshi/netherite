#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
make game/test_end_population game/test_end_population_runtime >/dev/null
mkdir -p .tmp/end-population
./game/test_end_population >.tmp/end-population/c.txt
(cd ../java/Minecraft && ./gradlew -g run/gradle endPopulationGolden --quiet) \
    >.tmp/end-population/java.txt
cmp .tmp/end-population/java.txt .tmp/end-population/c.txt
./game/test_end_population_runtime
echo "end_population_oracle: PASS (real 1.11.2 chorus, island, gateway generators)"
