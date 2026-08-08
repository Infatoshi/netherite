#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/.tmp/villager-ai-oracle"
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p "$OUT"
make -C "$ROOT/magma" \
    game/test_villager_mate_oracle \
    game/test_villager_follow_golem_oracle \
    game/test_villager_door_oracle \
    game/test_villager_social_oracle \
    game/test_villager_ai_runtime >/dev/null
"$ROOT/magma/game/test_villager_mate_oracle" >"$OUT/c.txt"
(cd "$ROOT/java/Minecraft" && \
    ./gradlew -g run/gradle -q villagerMateGolden) \
    >"$OUT/java.txt" 2>"$OUT/gradle.log"
cmp "$OUT/java.txt" "$OUT/c.txt"
"$ROOT/magma/game/test_villager_follow_golem_oracle" \
    >"$OUT/c-follow-golem.txt"
(cd "$ROOT/java/Minecraft" && \
    ./gradlew -g run/gradle -q villagerFollowGolemGolden) \
    >"$OUT/java-follow-golem.txt" 2>"$OUT/gradle-follow-golem.log"
cmp "$OUT/java-follow-golem.txt" "$OUT/c-follow-golem.txt"
"$ROOT/magma/game/test_villager_door_oracle" >"$OUT/c-door.txt"
(cd "$ROOT/java/Minecraft" && \
    ./gradlew -g run/gradle -q villagerDoorTaskGolden) \
    >"$OUT/java-door.txt" 2>"$OUT/gradle-door.log"
cmp "$OUT/java-door.txt" "$OUT/c-door.txt"
"$ROOT/magma/game/test_villager_social_oracle" >"$OUT/c-social.txt"
(cd "$ROOT/java/Minecraft" && \
    ./gradlew -g run/gradle -q villagerSocialGolden) \
    >"$OUT/java-social.txt" 2>"$OUT/gradle-social.log"
cmp "$OUT/java-social.txt" "$OUT/c-social.txt"
"$ROOT/magma/game/test_villager_ai_runtime"
echo "villager AI oracle: PASS (willingness, mating, birth, doors, golem rose)"
