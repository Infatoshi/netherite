#!/usr/bin/env bash
# Exercise independent saved-state classes together, then advance both Java
# and magma through the same 20 ticks.
set -euo pipefail

MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$MAGMA/.." && pwd)"
OUT="${OUT:-$MAGMA/trace/out/mixed_capsule_regression}"
QRL_HOST="${QRL_HOST:-127.0.0.1}"
QRL_PORT="${QRL_PORT:-25575}"
export UV_CACHE_DIR="${UV_CACHE_DIR:-/home/jawaugh/.cache/uv}"
export TMPDIR="${TMPDIR:-$REPO/.tmp}"
mkdir -p "$TMPDIR"

if ! timeout 2 bash -c 'exec 3<>"/dev/tcp/$1/$2"' \
        _ "$QRL_HOST" "$QRL_PORT" 2>/dev/null; then
    echo "mixed capsule regression requires the real Java qrl oracle at $QRL_HOST:$QRL_PORT" >&2
    exit 2
fi

export OUT QRL_HOST QRL_PORT
export TICKS=20 TAPE_PROFILE=idle FRESH=1
export FIXTURE_BLOCKS_FILE="$MAGMA/trace/fixtures/redstone_comparator_saved_single_chest_one_full_stack.blocks"
export FIXTURE_STAGE=final
export VILLAGER_FIXTURE='6 0 3 1 57545995238622'
export HELD_ITEM_FIXTURE='minecraft:bow 1 0'
export OFFHAND_ITEM_FIXTURE='minecraft:shield 1 5'
export ARMOR_FIXTURE='38 311 7 0 4'
export PLAYER_XP_FIXTURE='7 0.33333334 98'
export PLAYER_COMBAT_FIXTURE='2 6 16 0'
export POTION_FIXTURE='1 0 40'
export SECOND_POTION_FIXTURE='16 1 25'
export CONTAINER_SLOT_FIXTURE='3 0 0 0 1 64 0'
export SCHEDULE_CAPTURE_BLOCKS='124 149 150'
export WORLD_RANDOM_SEED48=123456789
export BLOCK_RANDOM_SEED48=987654321
export INITIAL_FIRE=-20 INITIAL_FOOD=17 CLEAR_HURT=1
export BLOCK_STRICT=1 REQUIRE_BLOCK_MUTATION=1 BLOCK_LIGHT_COMPARE=1
export SKY_LIGHT_COMPARE=1

bash "$MAGMA/trace/run_oracle.sh"

manifest="$OUT/state_capsule/manifest.json"
jq -e '
    .schema == "netherite.state_capsule" and .version == 2 and
    .state.player.held_slot == 0 and
    .state.player.held_id == 261 and
    .state.player.held_count == 1 and
    .state.player.held_meta == 0 and
    .state.player.xp_level == 7 and
    .state.player.xp_total == 98 and
    .state.player.attack_ticks == 2 and
    .state.player.hurt_time == 6 and
    .state.player.hurt_resistant_time == 16 and
    .state.player.death_time == 0 and
    .state.player.potions == [
        {"amp":0,"dur":40,"id":1},
        {"amp":1,"dur":25,"id":16}
    ] and
    .state.inventory == [
        {"count":1,"enchants":[],"id":261,"meta":0,"slot":0},
        {"count":1,"enchants":[[0,4]],"id":311,"meta":7,"slot":38},
        {"count":1,"enchants":[],"id":442,"meta":5,"slot":40}
    ] and
    ([.state.entities[] | select(
        .type == "EntityVillager" and .no_ai == true and
        .offers_initialized == false and .profession == 1 and
        .entity_seed48 == 57545995238622)] | length) == 1 and
    ([.state.containers[] | select(
        .type == "single_chest" and .size == 27 and
        .items == [{"count":64,"id":1,"meta":0,"slot":0}])] | length) == 1 and
    ([.state.comparators[] | select(.output_signal == 0)] | length) == 1 and
    ([.state.scheduled_ticks[] | select(.block == 149)] | length) == 1 and
    .state.world_rng.java_seed48 == 123456789 and
    .state.world_rng.block_seed48 == 987654321
' "$manifest" >/dev/null

test "$(wc -l < "$OUT/java_state.jsonl")" -eq 20
test "$(wc -l < "$OUT/c_state.jsonl")" -eq 20
cmp "$OUT/java_blocks.bin" "$OUT/c_runtime_blocks.bin"
cmp "$OUT/java_block_light.bin" "$OUT/c_runtime_block_light.bin"
cmp "$OUT/java_sky_light.bin" "$OUT/c_runtime_sky_light.bin"

echo "mixed capsule regression PASS: 20 ticks, player XP/combat/potion, main/armor/offhand inventory, villager, chest, comparator callback, RNG, 10,625 blocks, block light, and sky light exact"
