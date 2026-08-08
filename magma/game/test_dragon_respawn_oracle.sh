#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p magma/.tmp/dragon-respawn
make -C magma game/test_dragon_spike_oracle game/test_dragon_respawn >/dev/null
magma/game/test_dragon_spike_oracle >magma/.tmp/dragon-respawn/c.txt
(cd java/Minecraft && ./gradlew -g run/gradle dragonRespawnGolden --console=plain) \
    | sed -n '/^D /p; /^S /p' >magma/.tmp/dragon-respawn/java.txt
cmp magma/.tmp/dragon-respawn/java.txt magma/.tmp/dragon-respawn/c.txt
magma/game/test_dragon_respawn
echo "dragon_respawn_oracle: PASS (Java-locked spikes plus 604-tick lifecycle)"
