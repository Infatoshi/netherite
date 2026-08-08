#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
make game/test_end_gateway >/dev/null
mkdir -p .tmp/end-gateway-java
./game/test_end_gateway >.tmp/end-gateway-c.txt
javac -d .tmp/end-gateway-java game/EndGatewayGolden.java
java -cp .tmp/end-gateway-java EndGatewayGolden >.tmp/end-gateway-java.txt
cmp .tmp/end-gateway-java.txt .tmp/end-gateway-c.txt
echo "end_gateway_oracle: PASS (Java-locked gateway order and volume)"
