#!/usr/bin/env bash
# Worldgen regression suite: run world_verify + genprobe_diff for every seed with a
# structures-off ground-truth save, print one line per seed. Run from magma.
#   bash trace/regression_suite.sh [seed ...]
# Requires trace/out/java_genprobe_seed<N>.log (or _seed<N>_load.log) per seed and
# java/Minecraft/run/saves/qrl_<N>/region. Rebuild world_dump first if headers changed.
set -u
cd "$(dirname "$0")/.." || exit 1
SAVES=../../java/Minecraft/run/saves
export MAGMA_GENPROBE=$PWD/trace/out/magma_genprobe.log
SEEDS=("$@")
[ ${#SEEDS[@]} -eq 0 ] && SEEDS=(0 7 42 18 38 1 30 74 9 19 489 123 777 3141 8675309 999 31337 424242 1000000007)
printf '%-6s %-10s %-9s %s\n' SEED MATCH CLEAN FIRST_DIVERGENT_STAGES
for S in "${SEEDS[@]}"; do
    JLOG=trace/out/java_genprobe_seed${S}.log
    [ -f "$JLOG" ] || JLOG=trace/out/java_genprobe_seed${S}_load.log
    if [ ! -f "$JLOG" ] || [ ! -d "$SAVES/qrl_$S/region" ]; then
        printf '%-6s missing save or java log\n' "$S"; continue
    fi
    V=$(uv run --no-project --with numpy --with nbt python3 trace/world_verify.py \
        --region "$SAVES/qrl_$S/region" --seed "$S" --java-log "$JLOG" 2>&1 | grep -a VERDICT)
    PCT=$(printf '%s' "$V" | grep -oE '= [0-9.]+%' | tr -d '= ')
    D=$(uv run --no-project python3 trace/genprobe_diff.py --java "$JLOG" \
        --magma trace/out/magma_genprobe.log 2>&1)
    CLEAN=$(printf '%s' "$D" | grep -a "clean chunks" | grep -oE '[0-9]+/[0-9]+')
    STAGES=$(printf '%s' "$D" | sed -n '/first-divergent stage histogram:/,/first 25/p' \
        | grep -aE '^  [A-Z]' | sed 's/^ *//; s/ *: /:/' | tr '\n' ' ')
    printf '%-6s %-10s %-9s %s\n' "$S" "$PCT" "$CLEAN" "$STAGES"
done
