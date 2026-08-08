#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}

seeds=(
  -9055566058453653051
  0
  1
  -1
  1000
  1234567890123456789
)

for seed in "${seeds[@]}"; do
  java_row=$(
    cd "$ROOT/java/Minecraft"
    ./gradlew -g run/gradle -q spawnPositionGolden -PspawnSeed="$seed" \
      2>/dev/null
  )
  native_rows=$(printf '{}\n' | "$ROOT/magma/magma_game" \
    --seed "$seed" --world default --view-distance 2 --mobs off --rl \
    --set vanilla_spawn=1 --width 32 --height 18 2>/dev/null)
  native_row=$(printf '%s\n' "$native_rows" | sed -n '1p')
  if ! jq -en --argjson j "$java_row" --argjson n "$native_row" '
      (($n.x * 2 | round) == ($j.final_x * 2 + 1)) and
      (($n.z * 2 | round) == ($j.final_z * 2 + 1)) and
      (($n.y | round) == $j.player_y)
    ' >/dev/null; then
    printf 'spawn mismatch seed=%s\njava=%s\nnative=%s\n' \
      "$seed" "$java_row" "$native_row" >&2
    exit 1
  fi
done

echo "spawn position Java/native: PASS (${#seeds[@]} seeds)"
