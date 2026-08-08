#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
make game/test_firework >/dev/null
mkdir -p .tmp/firework-java
./game/test_firework >.tmp/firework-c.txt
javac -d .tmp/firework-java game/FireworkGolden.java
java -cp .tmp/firework-java FireworkGolden >.tmp/firework-java.txt
cmp .tmp/firework-java.txt .tmp/firework-c.txt
echo "firework_oracle: PASS (Java-locked constructor and first motion tick)"
