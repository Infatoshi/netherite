#!/usr/bin/env bash
# run_oracle.sh - the full tick-trace STATE-VECTOR flywheel, one command.
#
#   1. build the C tracer               (bash trace/build_c_tracer.sh)
#   2. gen a scripted action tape       (trace/gen_tape.py)
#   3. narrow C physics side -> c_phys.csv + c_state_small.jsonl + c_spawn.txt
#   4. Java side (if the qrl bridge is up on :25575): teleport to the C spawn pose, replay
#      the SAME tape -> java_phys.csv + java_state.jsonl, plus pre-tick state/blocks
#   5. package the Java pre-tick state as a validated neutral state capsule
#   6. initialize the full shared GmRuntime from that capsule -> c_state.jsonl
#   7. per-feature diff (trace/diff_trace.py) on Java vs full-runtime state
#   8. strict raw block-state diff after the final input tick
#
# If the Java bridge is NOT up, step 4/5-Java are skipped and a SELF-DIFF (c vs copy == 0)
# proves the harness. To bring the game up first (anvil, headless :1):
#   cd java && setsid nohup bash start_vnc_client.sh >/tmp/mc_launch.out 2>&1 &
#   # wait for a TCP connect to 127.0.0.1:25575, then re-run this script.
set -euo pipefail
MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
cd "$MAGMA"
TICKS="${TICKS:-300}"
SEED="${SEED:-0}"
DIMENSION="${DIMENSION:-0}"
PLATFORM="${PLATFORM:-21}"
PLATFORM_CLEAR_HEIGHT="${PLATFORM_CLEAR_HEIGHT:-6}"
FRESH="${FRESH:-1}"
TAPE_PROFILE="${TAPE_PROFILE:-random}"
FIXTURE_BLOCK="${FIXTURE_BLOCK:-}"
FIXTURE_BLOCKS_FILE="${FIXTURE_BLOCKS_FILE:-}"
FIXTURE_STAGE="${FIXTURE_STAGE:-early}"
FIXTURE_DRAIN_TICKS="${FIXTURE_DRAIN_TICKS:-0}"
ALLOW_FALLING_ENTITIES="${ALLOW_FALLING_ENTITIES:-0}"
DO_FIRE_TICK="${DO_FIRE_TICK:-1}"
DO_ENTITY_DROPS="${DO_ENTITY_DROPS:-1}"
WEATHER_MODE="${WEATHER_MODE:-clear}"
INITIAL_FIRE="${INITIAL_FIRE:-}"
INITIAL_FOOD="${INITIAL_FOOD:-}"
CLEAR_HURT="${CLEAR_HURT:-0}"
NORMALIZE_MOVE_PACKETS="${NORMALIZE_MOVE_PACKETS:-1}"
XP_ORB_FIXTURE="${XP_ORB_FIXTURE:-}"
ITEM_FIXTURE="${ITEM_FIXTURE:-}"
ARROW_FIXTURE="${ARROW_FIXTURE:-}"
PRIMED_TNT_FIXTURE="${PRIMED_TNT_FIXTURE:-}"
SECOND_PRIMED_TNT_FIXTURE="${SECOND_PRIMED_TNT_FIXTURE:-}"
END_CRYSTAL_FIXTURE="${END_CRYSTAL_FIXTURE:-}"
BOAT_FIXTURE="${BOAT_FIXTURE:-}"
SMALL_FIREBALL_FIXTURE="${SMALL_FIREBALL_FIXTURE:-}"
MOB_FIXTURE="${MOB_FIXTURE:-}"
VILLAGER_FIXTURE="${VILLAGER_FIXTURE:-}"
MOB_TNT_ORDER="${MOB_TNT_ORDER:-mob-first}"
ITEM_TNT_ORDER="${ITEM_TNT_ORDER:-item-first}"
POTION_FIXTURE="${POTION_FIXTURE:-}"
SECOND_POTION_FIXTURE="${SECOND_POTION_FIXTURE:-}"
PLAYER_XP_FIXTURE="${PLAYER_XP_FIXTURE:-}"
PLAYER_COMBAT_FIXTURE="${PLAYER_COMBAT_FIXTURE:-}"
ARMOR_FIXTURE="${ARMOR_FIXTURE:-}"
SCHEDULE_TICK_FIXTURE="${SCHEDULE_TICK_FIXTURE:-}"
COMPARATOR_OUTPUT_FIXTURE="${COMPARATOR_OUTPUT_FIXTURE:-}"
POST_COMPARATOR_BLOCK_FIXTURE="${POST_COMPARATOR_BLOCK_FIXTURE:-}"
CONTAINER_SLOT_FIXTURE="${CONTAINER_SLOT_FIXTURE:-}"
CONTAINER_FILL_FIXTURE="${CONTAINER_FILL_FIXTURE:-}"
SHULKER_NBT_FIXTURE="${SHULKER_NBT_FIXTURE:-}"
FLOWER_POT_FIXTURE="${FLOWER_POT_FIXTURE:-}"
SKULL_FIXTURE="${SKULL_FIXTURE:-}"
SKULL_OWNER_FIXTURE="${SKULL_OWNER_FIXTURE:-}"
COMMAND_SUCCESS_FIXTURE="${COMMAND_SUCCESS_FIXTURE:-}"
ITEM_FRAME_FIXTURE="${ITEM_FRAME_FIXTURE:-}"
SCHEDULE_CAPTURE_BLOCKS="${SCHEDULE_CAPTURE_BLOCKS:-}"
RANDOM_TICK_FIXTURE="${RANDOM_TICK_FIXTURE:-}"
RANDOM_SELECTION_FIXTURE="${RANDOM_SELECTION_FIXTURE:-}"
WORLD_RANDOM_SEED48="${WORLD_RANDOM_SEED48:-}"
BLOCK_RANDOM_SEED48="${BLOCK_RANDOM_SEED48:-}"
TICK0_BLOCK_FIXTURE="${TICK0_BLOCK_FIXTURE:-}"
TICK0_HARVEST_FIXTURE="${TICK0_HARVEST_FIXTURE:-}"
BLOCK_EDIT_SEQUENCE="${BLOCK_EDIT_SEQUENCE:-}"
HELD_ITEM_FIXTURE="${HELD_ITEM_FIXTURE:-}"
OFFHAND_ITEM_FIXTURE="${OFFHAND_ITEM_FIXTURE:-}"
CHECKPOINT_TICK="${CHECKPOINT_TICK:-}"
SKY_LIGHT_COMPARE="${SKY_LIGHT_COMPARE:-0}"
BLOCK_LIGHT_COMPARE="${BLOCK_LIGHT_COMPARE:-0}"
if [ "$WEATHER_MODE" != clear ] && [ "$WEATHER_MODE" != rain ] \
        && [ "$WEATHER_MODE" != thunder ]; then
    echo "WEATHER_MODE must be clear, rain, or thunder" >&2
    exit 2
fi
if [ -z "${BLOCK_BOX+x}" ]; then
    # Default box follows the settled player vertically, so state-capsule
    # replay cannot omit the platform on seeds with a different surface Y.
    BLOCK_BOX="-4 -6 -4 20 10 20"
    BLOCK_BOX_RELATIVE_Y=1
else
    BLOCK_BOX_RELATIVE_Y="${BLOCK_BOX_RELATIVE_Y:-0}"
fi
BLOCK_STRICT="${BLOCK_STRICT:-0}"  # 0=diagnostic, 1=final-state strict, transition=delta strict
if [ -z "${REQUIRE_BLOCK_MUTATION+x}" ]; then
    if [ "$TAPE_PROFILE" = block-break ]; then
        REQUIRE_BLOCK_MUTATION=1
    else
        REQUIRE_BLOCK_MUTATION=0
    fi
fi
OUT="${OUT:-trace/out}"
QRL_HOST="${QRL_HOST:-127.0.0.1}"
QRL_PORT="${QRL_PORT:-25575}"
SKIP_BUILD="${SKIP_BUILD:-0}"
if ! [[ "$QRL_PORT" =~ ^[0-9]+$ ]] || [ "$QRL_PORT" -lt 1 ] || [ "$QRL_PORT" -gt 65535 ]; then
    echo "QRL_PORT must be an integer from 1 to 65535" >&2
    exit 2
fi
if [ "$DIMENSION" != -1 ] && [ "$DIMENSION" != 0 ] && [ "$DIMENSION" != 1 ]; then
    echo "DIMENSION must be -1, 0, or 1" >&2
    exit 2
fi
if [ "$DO_FIRE_TICK" != 0 ] && [ "$DO_FIRE_TICK" != 1 ]; then
    echo "DO_FIRE_TICK must be 0 or 1" >&2
    exit 2
fi
if [ "$DO_FIRE_TICK" = 1 ]; then
    DO_FIRE_TICK_ARG=on
else
    DO_FIRE_TICK_ARG=off
fi
if [ "$DO_ENTITY_DROPS" != 0 ] && [ "$DO_ENTITY_DROPS" != 1 ]; then
    echo "DO_ENTITY_DROPS must be 0 or 1" >&2
    exit 2
fi
if [ "$DO_ENTITY_DROPS" = 1 ]; then
    DO_ENTITY_DROPS_ARG=on
else
    DO_ENTITY_DROPS_ARG=off
fi
mkdir -p "$OUT"
read -r -a BLOCK_BOX_ARGS <<<"$BLOCK_BOX"
if [ "${#BLOCK_BOX_ARGS[@]}" -ne 6 ]; then
    echo "BLOCK_BOX must contain exactly 6 integers: X0 Y0 Z0 X1 Y1 Z1" >&2
    exit 2
fi
FIXTURE_ARGS=()
JAVA_FIXTURE_ARGS=()
FALLING_JAVA_ARGS=()
FRESH_ARGS=()
INITIAL_FIRE_ARGS=()
INITIAL_FOOD_ARGS=()
CLEAR_HURT_ARGS=()
MOVE_PACKET_ARGS=()
HELD_ITEM_JAVA_ARGS=()
OFFHAND_ITEM_JAVA_ARGS=()
XP_JAVA_ARGS=()
XP_RUNTIME_ARGS=()
ITEM_JAVA_ARGS=()
ITEM_RUNTIME_ARGS=()
ARROW_JAVA_ARGS=()
ARROW_RUNTIME_ARGS=()
PRIMED_TNT_JAVA_ARGS=()
PRIMED_TNT_RUNTIME_ARGS=()
END_CRYSTAL_JAVA_ARGS=()
END_CRYSTAL_RUNTIME_ARGS=()
BOAT_JAVA_ARGS=()
BOAT_RUNTIME_ARGS=()
SMALL_FIREBALL_JAVA_ARGS=()
SMALL_FIREBALL_RUNTIME_ARGS=()
MOB_JAVA_ARGS=()
MOB_RUNTIME_ARGS=()
VILLAGER_JAVA_ARGS=()
POTION_JAVA_ARGS=()
POTION_RUNTIME_ARGS=()
PLAYER_XP_JAVA_ARGS=()
PLAYER_COMBAT_JAVA_ARGS=()
ARMOR_JAVA_ARGS=()
ARMOR_RUNTIME_ARGS=()
SCHEDULE_JAVA_ARGS=()
SCHEDULE_RUNTIME_ARGS=()
COMPARATOR_JAVA_ARGS=()
POST_COMPARATOR_JAVA_ARGS=()
CONTAINER_JAVA_ARGS=()
CONTAINER_FILL_JAVA_ARGS=()
SHULKER_NBT_JAVA_ARGS=()
FLOWER_POT_JAVA_ARGS=()
SKULL_JAVA_ARGS=()
COMMAND_SUCCESS_JAVA_ARGS=()
ITEM_FRAME_JAVA_ARGS=()
RANDOM_TICK_JAVA_ARGS=()
RANDOM_TICK_RUNTIME_ARGS=()
RANDOM_SELECTION_JAVA_ARGS=()
RANDOM_SELECTION_RUNTIME_ARGS=()
WORLD_RANDOM_JAVA_ARGS=()
BLOCK_RANDOM_JAVA_ARGS=()
TICK0_BLOCK_JAVA_ARGS=()
TICK0_BLOCK_RUNTIME_ARGS=()
TICK0_HARVEST_JAVA_ARGS=()
TICK0_HARVEST_RUNTIME_ARGS=()
BLOCK_EDIT_JAVA_ARGS=()
BLOCK_EDIT_RUNTIME_ARGS=()
CHECKPOINT_JAVA_ARGS=()
BLOCK_LIGHT_JAVA_ARGS=()
BLOCK_LIGHT_RUNTIME_ARGS=()
SKY_LIGHT_JAVA_ARGS=()
SKY_LIGHT_RUNTIME_ARGS=()
SKY_LIGHT_CAPSULE_ARGS=()
BLOCK_BOX_MODE_ARGS=(--blocks-box-out "$OUT/block_box.txt")
if [ "$BLOCK_BOX_RELATIVE_Y" = 1 ]; then
    BLOCK_BOX_MODE_ARGS+=(--blocks-y-relative)
elif [ "$BLOCK_BOX_RELATIVE_Y" != 0 ]; then
    echo "BLOCK_BOX_RELATIVE_Y must be 0 or 1" >&2
    exit 2
fi
if [ "$FRESH" = 1 ]; then FRESH_ARGS=(--fresh); fi
if [ "$ALLOW_FALLING_ENTITIES" = 1 ]; then
    FALLING_JAVA_ARGS=(--allow-falling-entities)
elif [ "$ALLOW_FALLING_ENTITIES" != 0 ]; then
    echo "ALLOW_FALLING_ENTITIES must be 0 or 1" >&2
    exit 2
fi
if [ -n "$INITIAL_FIRE" ]; then
    if ! [[ "$INITIAL_FIRE" =~ ^-?[0-9]+$ ]] ||
       [ "$INITIAL_FIRE" -lt -20 ] || [ "$INITIAL_FIRE" -gt 32767 ]; then
        echo "INITIAL_FIRE must be an integer in -20..32767" >&2
        exit 2
    fi
    INITIAL_FIRE_ARGS=(--initial-fire "$INITIAL_FIRE")
fi
if [ -n "$INITIAL_FOOD" ]; then
    if ! [[ "$INITIAL_FOOD" =~ ^[0-9]+$ ]] ||
       [ "$INITIAL_FOOD" -gt 20 ]; then
        echo "INITIAL_FOOD must be an integer in 0..20" >&2
        exit 2
    fi
    INITIAL_FOOD_ARGS=(--initial-food "$INITIAL_FOOD")
fi
if [ "$CLEAR_HURT" = 1 ]; then
    CLEAR_HURT_ARGS=(--clear-hurt)
elif [ "$CLEAR_HURT" != 0 ]; then
    echo "CLEAR_HURT must be 0 or 1" >&2
    exit 2
fi
if [ "$NORMALIZE_MOVE_PACKETS" = 1 ]; then
    MOVE_PACKET_ARGS=(--normalize-move-packets)
elif [ "$NORMALIZE_MOVE_PACKETS" != 0 ]; then
    echo "NORMALIZE_MOVE_PACKETS must be 0 or 1" >&2
    exit 2
fi
if [ -n "$XP_ORB_FIXTURE" ]; then
    read -r -a XP_ORB_VALUES <<<"$XP_ORB_FIXTURE"
    if [ "${#XP_ORB_VALUES[@]}" -ne 4 ]; then
        echo "XP_ORB_FIXTURE must contain DX DY DZ VALUE" >&2
        exit 2
    fi
    XP_JAVA_ARGS=(
        --xp-orb-offset "${XP_ORB_VALUES[@]}"
        --entity-fixture-out "$OUT/xp_fixture.json"
    )
    XP_RUNTIME_ARGS=(--xp-fixture "$OUT/xp_fixture.json")
fi
if [ -n "$ITEM_FIXTURE" ]; then
    if [ -n "$XP_ORB_FIXTURE" ] || [ -n "$ARROW_FIXTURE" ] ||
       [ -n "$BOAT_FIXTURE" ] || [ -n "$SMALL_FIREBALL_FIXTURE" ] ||
       [ -n "$MOB_FIXTURE" ]; then
        echo "entity fixtures are mutually exclusive" >&2
        exit 2
    fi
    read -r -a ITEM_VALUES <<<"$ITEM_FIXTURE"
    if [ "${#ITEM_VALUES[@]}" -ne 6 ]; then
        echo "ITEM_FIXTURE must contain DX DY DZ ITEM COUNT META" >&2
        exit 2
    fi
    ITEM_JAVA_ARGS=(
        --item-offset "${ITEM_VALUES[@]}"
        --item-fixture-out "$OUT/item_fixture.json"
    )
    if [ "$ITEM_TNT_ORDER" = tnt-first ]; then
        if [ -z "$PRIMED_TNT_FIXTURE" ]; then
            echo "ITEM_TNT_ORDER=tnt-first requires PRIMED_TNT_FIXTURE" >&2
            exit 2
        fi
        ITEM_JAVA_ARGS+=(--item-after-primed-tnt)
    elif [ "$ITEM_TNT_ORDER" != item-first ]; then
        echo "ITEM_TNT_ORDER must be item-first or tnt-first" >&2
        exit 2
    fi
    ITEM_RUNTIME_ARGS=(--item-fixture "$OUT/item_fixture.json")
fi
if [ -n "$ARROW_FIXTURE" ]; then
    if [ -n "$XP_ORB_FIXTURE" ] || [ -n "$ITEM_FIXTURE" ] ||
       [ -n "$BOAT_FIXTURE" ] ||
       [ -n "$SMALL_FIREBALL_FIXTURE" ]; then
        echo "entity fixtures are mutually exclusive" >&2
        exit 2
    fi
    read -r -a ARROW_VALUES <<<"$ARROW_FIXTURE"
    if [ "${#ARROW_VALUES[@]}" -ne 3 ] &&
       [ "${#ARROW_VALUES[@]}" -ne 4 ]; then
        echo "ARROW_FIXTURE must contain DX DY DZ [FIRE_SECONDS]" >&2
        exit 2
    fi
    ARROW_JAVA_ARGS=(
        --arrow-offset "${ARROW_VALUES[@]:0:3}"
        --entity-fixture-out "$OUT/arrow_fixture.json"
    )
    if [ "${#ARROW_VALUES[@]}" -eq 4 ]; then
        ARROW_JAVA_ARGS+=(--arrow-fire-seconds "${ARROW_VALUES[3]}")
    fi
    ARROW_RUNTIME_ARGS=(--arrow-fixture "$OUT/arrow_fixture.json")
fi
if [ -n "$PRIMED_TNT_FIXTURE" ]; then
    if { [ -n "$ITEM_FIXTURE" ] && [ -n "$MOB_FIXTURE" ]; }; then
        echo "entity fixtures are mutually exclusive" >&2
        exit 2
    fi
    read -r -a PRIMED_TNT_VALUES <<<"$PRIMED_TNT_FIXTURE"
    if [ "${#PRIMED_TNT_VALUES[@]}" -ne 7 ]; then
        echo "PRIMED_TNT_FIXTURE must contain DX DY DZ VX VY VZ FUSE" >&2
        exit 2
    fi
    PRIMED_TNT_JAVA_ARGS=(
        --primed-tnt-offset "${PRIMED_TNT_VALUES[@]}"
        --primed-tnt-fixture-out "$OUT/primed_tnt_fixture.json"
    )
    PRIMED_TNT_RUNTIME_ARGS=(
        --primed-tnt-fixture "$OUT/primed_tnt_fixture.json")
fi
if [ -n "$SECOND_PRIMED_TNT_FIXTURE" ]; then
    if [ -z "$PRIMED_TNT_FIXTURE" ]; then
        echo "SECOND_PRIMED_TNT_FIXTURE requires PRIMED_TNT_FIXTURE" >&2
        exit 2
    fi
    read -r -a SECOND_PRIMED_TNT_VALUES <<<"$SECOND_PRIMED_TNT_FIXTURE"
    if [ "${#SECOND_PRIMED_TNT_VALUES[@]}" -ne 7 ]; then
        echo "SECOND_PRIMED_TNT_FIXTURE must contain DX DY DZ VX VY VZ FUSE" >&2
        exit 2
    fi
    PRIMED_TNT_JAVA_ARGS+=(
        --second-primed-tnt-offset "${SECOND_PRIMED_TNT_VALUES[@]}"
        --second-primed-tnt-fixture-out "$OUT/second_primed_tnt_fixture.json"
    )
    PRIMED_TNT_RUNTIME_ARGS+=(
        --second-primed-tnt-fixture "$OUT/second_primed_tnt_fixture.json")
fi
if [ -n "$END_CRYSTAL_FIXTURE" ]; then
    if [ -n "$SECOND_PRIMED_TNT_FIXTURE" ] ||
       [ -n "$XP_ORB_FIXTURE" ] || [ -n "$ITEM_FIXTURE" ] ||
       [ -n "$ARROW_FIXTURE" ] || [ -n "$BOAT_FIXTURE" ] ||
       [ -n "$SMALL_FIREBALL_FIXTURE" ] || [ -n "$MOB_FIXTURE" ]; then
        echo "the standalone or primed-TNT-paired End-crystal fixture is bounded" >&2
        exit 2
    fi
    read -r -a END_CRYSTAL_VALUES <<<"$END_CRYSTAL_FIXTURE"
    if { [ "${#END_CRYSTAL_VALUES[@]}" -ne 3 ] &&
         [ "${#END_CRYSTAL_VALUES[@]}" -ne 6 ]; }; then
        echo "END_CRYSTAL_FIXTURE must contain DX DY DZ [BEAM_DX BEAM_DY BEAM_DZ]" >&2
        exit 2
    fi
    END_CRYSTAL_JAVA_ARGS=(
        --end-crystal-offset "${END_CRYSTAL_VALUES[@]:0:3}"
        --end-crystal-fixture-out "$OUT/end_crystal_fixture.json"
    )
    if [ "${#END_CRYSTAL_VALUES[@]}" -eq 6 ]; then
        END_CRYSTAL_JAVA_ARGS+=(
            --end-crystal-beam-target-offset "${END_CRYSTAL_VALUES[@]:3:3}")
    fi
    END_CRYSTAL_RUNTIME_ARGS=(
        --end-crystal-fixture "$OUT/end_crystal_fixture.json")
fi
if [ -n "$BOAT_FIXTURE" ]; then
    if [ -n "$XP_ORB_FIXTURE" ] || [ -n "$ITEM_FIXTURE" ] ||
       [ -n "$ARROW_FIXTURE" ] ||
       [ -n "$SMALL_FIREBALL_FIXTURE" ]; then
        echo "entity fixtures are mutually exclusive" >&2
        exit 2
    fi
    read -r -a BOAT_VALUES <<<"$BOAT_FIXTURE"
    if [ "${#BOAT_VALUES[@]}" -ne 3 ]; then
        echo "BOAT_FIXTURE must contain DX DY DZ" >&2
        exit 2
    fi
    BOAT_JAVA_ARGS=(
        --boat-offset "${BOAT_VALUES[@]}"
        --boat-fixture-out "$OUT/boat_fixture.json"
    )
    BOAT_RUNTIME_ARGS=(--boat-fixture "$OUT/boat_fixture.json")
fi
if [ -n "$SMALL_FIREBALL_FIXTURE" ]; then
    if [ -n "$XP_ORB_FIXTURE" ] || [ -n "$ITEM_FIXTURE" ] ||
       [ -n "$ARROW_FIXTURE" ] || [ -n "$BOAT_FIXTURE" ]; then
        echo "entity fixtures are mutually exclusive" >&2
        exit 2
    fi
    read -r -a SMALL_FIREBALL_VALUES <<<"$SMALL_FIREBALL_FIXTURE"
    if [ "${#SMALL_FIREBALL_VALUES[@]}" -ne 9 ]; then
        echo "SMALL_FIREBALL_FIXTURE must contain DX DY DZ VX VY VZ AX AY AZ" >&2
        exit 2
    fi
    SMALL_FIREBALL_JAVA_ARGS=(
        --small-fireball-offset "${SMALL_FIREBALL_VALUES[@]}"
        --entity-fixture-out "$OUT/small_fireball_fixture.json"
    )
    SMALL_FIREBALL_RUNTIME_ARGS=(
        --small-fireball-fixture "$OUT/small_fireball_fixture.json")
fi
if [ -n "$MOB_FIXTURE" ]; then
    if [ -n "$XP_ORB_FIXTURE" ] || [ -n "$ITEM_FIXTURE" ] ||
       [ -n "$ARROW_FIXTURE" ] || [ -n "$BOAT_FIXTURE" ] ||
       [ -n "$SMALL_FIREBALL_FIXTURE" ]; then
        echo "entity fixtures are mutually exclusive" >&2
        exit 2
    fi
    read -r -a MOB_VALUES <<<"$MOB_FIXTURE"
    if { [ "${#MOB_VALUES[@]}" -ne 4 ] &&
         [ "${#MOB_VALUES[@]}" -ne 5 ]; } ||
       { [ "${#MOB_VALUES[@]}" -eq 5 ] &&
         [ "${MOB_VALUES[4]}" != "collision" ]; }; then
        echo "MOB_FIXTURE must contain DX DY DZ HEALTH [collision]" >&2
        exit 2
    fi
    MOB_JAVA_ARGS=(
        --mob-offset "${MOB_VALUES[@]:0:4}"
        --mob-fixture-out "$OUT/mob_fixture.json"
    )
    if [ "${#MOB_VALUES[@]}" -eq 5 ]; then
        MOB_JAVA_ARGS+=(--mob-collision)
    fi
    if [ "$MOB_TNT_ORDER" = tnt-first ]; then
        if [ -z "$PRIMED_TNT_FIXTURE" ]; then
            echo "MOB_TNT_ORDER=tnt-first requires PRIMED_TNT_FIXTURE" >&2
            exit 2
        fi
        MOB_JAVA_ARGS+=(--mob-after-primed-tnt)
    elif [ "$MOB_TNT_ORDER" != mob-first ]; then
        echo "MOB_TNT_ORDER must be mob-first or tnt-first" >&2
        exit 2
    fi
    MOB_RUNTIME_ARGS=(--mob-fixture "$OUT/mob_fixture.json")
fi
if [ -n "$VILLAGER_FIXTURE" ]; then
    if [ -n "$XP_ORB_FIXTURE" ] || [ -n "$ITEM_FIXTURE" ] ||
       [ -n "$ARROW_FIXTURE" ] || [ -n "$PRIMED_TNT_FIXTURE" ] ||
       [ -n "$SECOND_PRIMED_TNT_FIXTURE" ] ||
       [ -n "$END_CRYSTAL_FIXTURE" ] || [ -n "$BOAT_FIXTURE" ] ||
       [ -n "$SMALL_FIREBALL_FIXTURE" ] || [ -n "$MOB_FIXTURE" ]; then
        echo "VILLAGER_FIXTURE is a bounded standalone entity fixture" >&2
        exit 2
    fi
    read -r -a VILLAGER_VALUES <<<"$VILLAGER_FIXTURE"
    if [ "${#VILLAGER_VALUES[@]}" -ne 5 ]; then
        echo "VILLAGER_FIXTURE must contain DX DY DZ PROFESSION ENTITY_SEED48" >&2
        exit 2
    fi
    VILLAGER_JAVA_ARGS=(
        --villager-offset "${VILLAGER_VALUES[@]}"
        --villager-fixture-out "$OUT/villager_fixture.json"
    )
fi
if [ -n "$POTION_FIXTURE" ]; then
    read -r -a POTION_VALUES <<<"$POTION_FIXTURE"
    if [ "${#POTION_VALUES[@]}" -ne 3 ] ||
       ! [[ "${POTION_VALUES[0]}" =~ ^[0-9]+$ &&
             "${POTION_VALUES[1]}" =~ ^[0-9]+$ &&
             "${POTION_VALUES[2]}" =~ ^[0-9]+$ ]] ||
       [ "${POTION_VALUES[0]}" -lt 1 ] || [ "${POTION_VALUES[0]}" -gt 255 ] ||
       [ "${POTION_VALUES[1]}" -gt 255 ] ||
       [ "${POTION_VALUES[2]}" -lt 1 ]; then
        echo "POTION_FIXTURE must contain ID(1..255) AMPLIFIER(0..255) DURATION(>0)" >&2
        exit 2
    fi
    POTION_JAVA_ARGS=(--potion-fixture "${POTION_VALUES[@]}")
    POTION_RUNTIME_ARGS=(--potion-fixture "${POTION_VALUES[@]}")
fi
if [ -n "$SECOND_POTION_FIXTURE" ]; then
    if [ -z "$POTION_FIXTURE" ]; then
        echo "SECOND_POTION_FIXTURE requires POTION_FIXTURE" >&2
        exit 2
    fi
    read -r -a SECOND_POTION_VALUES <<<"$SECOND_POTION_FIXTURE"
    if [ "${#SECOND_POTION_VALUES[@]}" -ne 3 ] ||
       ! [[ "${SECOND_POTION_VALUES[0]}" =~ ^[0-9]+$ &&
             "${SECOND_POTION_VALUES[1]}" =~ ^[0-9]+$ &&
             "${SECOND_POTION_VALUES[2]}" =~ ^[0-9]+$ ]]; then
        echo "SECOND_POTION_FIXTURE must contain ID AMPLIFIER DURATION" >&2
        exit 2
    fi
    POTION_JAVA_ARGS+=(--second-potion-fixture "${SECOND_POTION_VALUES[@]}")
fi
if [ -n "$PLAYER_XP_FIXTURE" ]; then
    read -r -a PLAYER_XP_VALUES <<<"$PLAYER_XP_FIXTURE"
    if [ "${#PLAYER_XP_VALUES[@]}" -ne 3 ] ||
       ! [[ "${PLAYER_XP_VALUES[0]}" =~ ^[0-9]+$ &&
             "${PLAYER_XP_VALUES[1]}" =~ ^([0-9]+([.][0-9]*)?|[.][0-9]+)$ &&
             "${PLAYER_XP_VALUES[2]}" =~ ^[0-9]+$ ]]; then
        echo "PLAYER_XP_FIXTURE must contain LEVEL FRACTION TOTAL" >&2
        exit 2
    fi
    PLAYER_XP_JAVA_ARGS=(--player-xp-fixture "${PLAYER_XP_VALUES[@]}")
fi
if [ -n "$PLAYER_COMBAT_FIXTURE" ]; then
    read -r -a PLAYER_COMBAT_VALUES <<<"$PLAYER_COMBAT_FIXTURE"
    if [ "${#PLAYER_COMBAT_VALUES[@]}" -ne 4 ] ||
       ! [[ "${PLAYER_COMBAT_VALUES[0]}" =~ ^[0-9]+$ &&
             "${PLAYER_COMBAT_VALUES[1]}" =~ ^[0-9]+$ &&
             "${PLAYER_COMBAT_VALUES[2]}" =~ ^[0-9]+$ &&
             "${PLAYER_COMBAT_VALUES[3]}" =~ ^[0-9]+$ ]]; then
        echo "PLAYER_COMBAT_FIXTURE must contain ATTACK_TICKS HURT_TIME HURT_RESISTANT DEATH_TIME" >&2
        exit 2
    fi
    PLAYER_COMBAT_JAVA_ARGS=(
        --player-combat-fixture "${PLAYER_COMBAT_VALUES[@]}")
fi
if [ -n "$ARMOR_FIXTURE" ]; then
    read -r -a ARMOR_VALUES <<<"$ARMOR_FIXTURE"
    if [ "${#ARMOR_VALUES[@]}" -ne 5 ] ||
       ! [[ "${ARMOR_VALUES[0]}" =~ ^[0-9]+$ &&
             "${ARMOR_VALUES[1]}" =~ ^[0-9]+$ &&
             "${ARMOR_VALUES[2]}" =~ ^[0-9]+$ &&
             "${ARMOR_VALUES[3]}" =~ ^-?[0-9]+$ &&
             "${ARMOR_VALUES[4]}" =~ ^[0-9]+$ ]] ||
       [ "${ARMOR_VALUES[0]}" -lt 36 ] || [ "${ARMOR_VALUES[0]}" -gt 39 ] ||
       [ "${ARMOR_VALUES[1]}" -lt 1 ] || [ "${ARMOR_VALUES[1]}" -gt 4095 ] ||
       [ "${ARMOR_VALUES[2]}" -gt 32767 ] ||
       { [ "${ARMOR_VALUES[3]}" -eq -1 ] &&
         [ "${ARMOR_VALUES[4]}" -ne 0 ]; } ||
       { [ "${ARMOR_VALUES[3]}" -ge 0 ] &&
         [ "${ARMOR_VALUES[4]}" -lt 1 ]; } ||
       [ "${ARMOR_VALUES[3]}" -lt -1 ]; then
        echo "ARMOR_FIXTURE must contain SLOT(36..39) ITEM(1..4095) META(0..32767) ENCHANTMENT(-1 or >=0) LEVEL" >&2
        exit 2
    fi
    ARMOR_JAVA_ARGS=(--armor-fixture "${ARMOR_VALUES[@]}")
    ARMOR_RUNTIME_ARGS=(--armor-fixture "${ARMOR_VALUES[@]}")
fi
if [ -n "$SCHEDULE_TICK_FIXTURE" ]; then
    read -r -a SCHEDULE_VALUES <<<"$SCHEDULE_TICK_FIXTURE"
    if { [ "${#SCHEDULE_VALUES[@]}" -ne 6 ] &&
         [ "${#SCHEDULE_VALUES[@]}" -ne 7 ]; } ||
       ! [[ "${SCHEDULE_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${SCHEDULE_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${SCHEDULE_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${SCHEDULE_VALUES[3]}" =~ ^[0-9]+$ &&
             "${SCHEDULE_VALUES[4]}" =~ ^[0-9]+$ &&
             "${SCHEDULE_VALUES[5]}" =~ ^-?[0-9]+$ ]] ||
       { [ "${#SCHEDULE_VALUES[@]}" -eq 7 ] &&
         ! [[ "${SCHEDULE_VALUES[6]}" =~ ^-?[0-9]+$ ]]; } ||
       [ "${SCHEDULE_VALUES[3]}" -lt 1 ] ||
       [ "${SCHEDULE_VALUES[3]}" -gt 4095 ] ||
       [ "${SCHEDULE_VALUES[4]}" -gt 1000000 ] ||
       [ "${SCHEDULE_VALUES[5]}" -lt -128 ] ||
       [ "${SCHEDULE_VALUES[5]}" -gt 127 ]; then
        echo "SCHEDULE_TICK_FIXTURE must contain DX DY DZ BLOCK DELAY PRIORITY [CALLBACK_SEED]" >&2
        exit 2
    fi
    SCHEDULE_JAVA_ARGS=(
        --scheduled-tick-offset "${SCHEDULE_VALUES[@]:0:6}")
    if [ "${#SCHEDULE_VALUES[@]}" -eq 7 ]; then
        SCHEDULE_JAVA_ARGS+=(
            --scheduled-tick-seed "${SCHEDULE_VALUES[6]}")
        SCHEDULE_RESET_TICK=$((SCHEDULE_VALUES[4] > 0
            ? SCHEDULE_VALUES[4] - 1 : 0))
        SCHEDULE_RUNTIME_ARGS=(
            --scheduled-random-reset "$SCHEDULE_RESET_TICK"
            "${SCHEDULE_VALUES[6]}")
    fi
fi
if [ -n "$COMPARATOR_OUTPUT_FIXTURE" ]; then
    read -r -a COMPARATOR_VALUES <<<"$COMPARATOR_OUTPUT_FIXTURE"
    if [ "${#COMPARATOR_VALUES[@]}" -ne 4 ] ||
       ! [[ "${COMPARATOR_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${COMPARATOR_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${COMPARATOR_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${COMPARATOR_VALUES[3]}" =~ ^[0-9]+$ ]] ||
       [ "${COMPARATOR_VALUES[3]}" -gt 15 ]; then
        echo "COMPARATOR_OUTPUT_FIXTURE must contain DX DY DZ OUTPUT(0..15)" >&2
        exit 2
    fi
    COMPARATOR_JAVA_ARGS=(
        --comparator-output-offset "${COMPARATOR_VALUES[@]}")
fi
if [ -n "$POST_COMPARATOR_BLOCK_FIXTURE" ]; then
    read -r -a POST_COMPARATOR_VALUES <<<"$POST_COMPARATOR_BLOCK_FIXTURE"
    if [ "${#POST_COMPARATOR_VALUES[@]}" -ne 5 ] ||
       ! [[ "${POST_COMPARATOR_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${POST_COMPARATOR_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${POST_COMPARATOR_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${POST_COMPARATOR_VALUES[3]}" =~ ^[0-9]+$ &&
             "${POST_COMPARATOR_VALUES[4]}" =~ ^[0-9]+$ ]] ||
       [ "${POST_COMPARATOR_VALUES[3]}" -gt 4095 ] ||
       [ "${POST_COMPARATOR_VALUES[4]}" -gt 15 ]; then
        echo "POST_COMPARATOR_BLOCK_FIXTURE must contain DX DY DZ BLOCK(0..4095) META(0..15)" >&2
        exit 2
    fi
    if [ -z "$COMPARATOR_OUTPUT_FIXTURE" ]; then
        echo "POST_COMPARATOR_BLOCK_FIXTURE requires COMPARATOR_OUTPUT_FIXTURE" >&2
        exit 2
    fi
    POST_COMPARATOR_JAVA_ARGS=(
        --post-comparator-set-block-offset "${POST_COMPARATOR_VALUES[@]}")
fi
if [ -n "$CONTAINER_SLOT_FIXTURE" ]; then
    read -r -a CONTAINER_VALUES <<<"$CONTAINER_SLOT_FIXTURE"
    if [ "${#CONTAINER_VALUES[@]}" -ne 7 ] ||
       ! [[ "${CONTAINER_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${CONTAINER_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${CONTAINER_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${CONTAINER_VALUES[3]}" =~ ^[0-9]+$ &&
             "${CONTAINER_VALUES[4]}" =~ ^[0-9]+$ &&
             "${CONTAINER_VALUES[5]}" =~ ^[0-9]+$ &&
             "${CONTAINER_VALUES[6]}" =~ ^[0-9]+$ ]] ||
       [ "${CONTAINER_VALUES[3]}" -gt 26 ] ||
       [ "${CONTAINER_VALUES[4]}" -gt 4095 ] ||
       [ "${CONTAINER_VALUES[5]}" -gt 64 ] ||
       [ "${CONTAINER_VALUES[6]}" -gt 32767 ] ||
       { [ "${CONTAINER_VALUES[4]}" -eq 0 ] &&
         [ "${CONTAINER_VALUES[5]}" -ne 0 ]; } ||
       { [ "${CONTAINER_VALUES[4]}" -ne 0 ] &&
         [ "${CONTAINER_VALUES[5]}" -eq 0 ]; }; then
        echo "CONTAINER_SLOT_FIXTURE must contain DX DY DZ SLOT(0..26) ITEM(0..4095) COUNT(0..64) META(0..32767); the target container applies its own slot bound" >&2
        exit 2
    fi
    CONTAINER_JAVA_ARGS=(
        --container-slot-offset "${CONTAINER_VALUES[@]}")
fi
if [ -n "$CONTAINER_FILL_FIXTURE" ]; then
    read -r -a CONTAINER_FILL_VALUES <<<"$CONTAINER_FILL_FIXTURE"
    if [ "${#CONTAINER_FILL_VALUES[@]}" -ne 7 ] ||
       ! [[ "${CONTAINER_FILL_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${CONTAINER_FILL_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${CONTAINER_FILL_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${CONTAINER_FILL_VALUES[3]}" =~ ^[0-9]+$ &&
             "${CONTAINER_FILL_VALUES[4]}" =~ ^[0-9]+$ &&
             "${CONTAINER_FILL_VALUES[5]}" =~ ^[0-9]+$ &&
             "${CONTAINER_FILL_VALUES[6]}" =~ ^[0-9]+$ ]] ||
       [ "${CONTAINER_FILL_VALUES[3]}" -lt 1 ] ||
       [ "${CONTAINER_FILL_VALUES[3]}" -gt 27 ] ||
       [ "${CONTAINER_FILL_VALUES[4]}" -lt 1 ] ||
       [ "${CONTAINER_FILL_VALUES[4]}" -gt 4095 ] ||
       [ "${CONTAINER_FILL_VALUES[5]}" -lt 1 ] ||
       [ "${CONTAINER_FILL_VALUES[5]}" -gt 64 ] ||
       [ "${CONTAINER_FILL_VALUES[6]}" -gt 32767 ]; then
        echo "CONTAINER_FILL_FIXTURE must contain DX DY DZ SLOTS(1..27) ITEM(1..4095) COUNT(1..64) META(0..32767)" >&2
        exit 2
    fi
    CONTAINER_FILL_JAVA_ARGS=(
        --container-fill-offset "${CONTAINER_FILL_VALUES[@]}")
fi
if [ -n "$SHULKER_NBT_FIXTURE" ]; then
    read -r -a SHULKER_NBT_VALUES <<<"$SHULKER_NBT_FIXTURE"
    if [ "${#SHULKER_NBT_VALUES[@]}" -ne 4 ] ||
       ! [[ "${SHULKER_NBT_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${SHULKER_NBT_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${SHULKER_NBT_VALUES[2]}" =~ ^-?[0-9]+$ ]] ||
       [ ! -f "${SHULKER_NBT_VALUES[3]}" ]; then
        echo "SHULKER_NBT_FIXTURE must contain DX DY DZ NBT_JSON" >&2
        exit 2
    fi
    SHULKER_NBT_JAVA_ARGS=(
        --shulker-nbt-offset "${SHULKER_NBT_VALUES[@]}")
fi
if [ -n "$FLOWER_POT_FIXTURE" ]; then
    read -r -a FLOWER_POT_VALUES <<<"$FLOWER_POT_FIXTURE"
    if [ "${#FLOWER_POT_VALUES[@]}" -ne 5 ] ||
       ! [[ "${FLOWER_POT_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${FLOWER_POT_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${FLOWER_POT_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${FLOWER_POT_VALUES[3]}" =~ ^[0-9]+$ &&
             "${FLOWER_POT_VALUES[4]}" =~ ^[0-9]+$ ]] ||
       [ "${FLOWER_POT_VALUES[3]}" -gt 4095 ] ||
       [ "${FLOWER_POT_VALUES[4]}" -gt 32767 ] ||
       { [ "${FLOWER_POT_VALUES[3]}" -eq 0 ] &&
         [ "${FLOWER_POT_VALUES[4]}" -ne 0 ]; }; then
        echo "FLOWER_POT_FIXTURE must contain DX DY DZ ITEM(0..4095) META(0..32767); empty item 0 requires metadata 0" >&2
        exit 2
    fi
    FLOWER_POT_JAVA_ARGS=(
        --flower-pot-offset "${FLOWER_POT_VALUES[@]}")
fi
if [ -n "$SKULL_FIXTURE" ]; then
    read -r -a SKULL_VALUES <<<"$SKULL_FIXTURE"
    if [ "${#SKULL_VALUES[@]}" -ne 5 ] ||
       ! [[ "${SKULL_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${SKULL_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${SKULL_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${SKULL_VALUES[3]}" =~ ^[0-9]+$ &&
             "${SKULL_VALUES[4]}" =~ ^[0-9]+$ ]] ||
       [ "${SKULL_VALUES[3]}" -gt 5 ] ||
       [ "${SKULL_VALUES[4]}" -gt 15 ]; then
        echo "SKULL_FIXTURE must contain DX DY DZ TYPE(0..5) ROTATION(0..15)" >&2
        exit 2
    fi
    SKULL_JAVA_ARGS=(--skull-offset "${SKULL_VALUES[@]}")
fi
if [ -n "$SKULL_OWNER_FIXTURE" ]; then
    read -r -a SKULL_OWNER_VALUES <<<"$SKULL_OWNER_FIXTURE"
    if [ "${#SKULL_OWNER_VALUES[@]}" -ne 5 ] ||
       [ -z "$SKULL_FIXTURE" ] ||
       [ "${SKULL_VALUES[3]}" -ne 3 ]; then
        echo "SKULL_OWNER_FIXTURE requires a type-3 SKULL_FIXTURE and NAME UUID PROPERTY VALUE SIGNATURE" >&2
        exit 2
    fi
    SKULL_JAVA_ARGS+=(--skull-owner "${SKULL_OWNER_VALUES[@]}")
fi
if [ -n "$COMMAND_SUCCESS_FIXTURE" ]; then
    read -r -a COMMAND_SUCCESS_VALUES <<<"$COMMAND_SUCCESS_FIXTURE"
    if [ "${#COMMAND_SUCCESS_VALUES[@]}" -ne 4 ] ||
       ! [[ "${COMMAND_SUCCESS_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${COMMAND_SUCCESS_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${COMMAND_SUCCESS_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${COMMAND_SUCCESS_VALUES[3]}" =~ ^[0-9]+$ ]] ||
       [ "${COMMAND_SUCCESS_VALUES[3]}" -gt 15 ]; then
        echo "COMMAND_SUCCESS_FIXTURE must contain DX DY DZ SUCCESS(0..15)" >&2
        exit 2
    fi
    COMMAND_SUCCESS_JAVA_ARGS=(
        --command-success-offset "${COMMAND_SUCCESS_VALUES[@]}")
fi
if [ -n "$ITEM_FRAME_FIXTURE" ]; then
    read -r -a ITEM_FRAME_VALUES <<<"$ITEM_FRAME_FIXTURE"
    if [ "${#ITEM_FRAME_VALUES[@]}" -ne 7 ] ||
       ! [[ "${ITEM_FRAME_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${ITEM_FRAME_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${ITEM_FRAME_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${ITEM_FRAME_VALUES[3]}" =~ ^[0-9]+$ &&
             "${ITEM_FRAME_VALUES[4]}" =~ ^[0-9]+$ &&
             "${ITEM_FRAME_VALUES[5]}" =~ ^[0-9]+$ &&
             "${ITEM_FRAME_VALUES[6]}" =~ ^[0-9]+$ ]] ||
       [ "${ITEM_FRAME_VALUES[3]}" -lt 2 ] ||
       [ "${ITEM_FRAME_VALUES[3]}" -gt 5 ] ||
       { [ "${ITEM_FRAME_VALUES[4]}" -ne 0 ] &&
         [ "${ITEM_FRAME_VALUES[4]}" -ne 1 ]; } ||
       [ "${ITEM_FRAME_VALUES[5]}" -ne 0 ] ||
       [ "${ITEM_FRAME_VALUES[6]}" -gt 7 ] ||
       { [ "${ITEM_FRAME_VALUES[4]}" -eq 0 ] &&
         [ "${ITEM_FRAME_VALUES[6]}" -ne 0 ]; }; then
        echo "ITEM_FRAME_FIXTURE must contain DX DY DZ FACING(2..5) ITEM(0|1) META(0) ROTATION(0..7; empty requires 0)" >&2
        exit 2
    fi
    ITEM_FRAME_JAVA_ARGS=(
        --item-frame-offset "${ITEM_FRAME_VALUES[@]}")
fi
if [ -n "$SCHEDULE_CAPTURE_BLOCKS" ]; then
    read -r -a SCHEDULE_CAPTURE_VALUES <<<"$SCHEDULE_CAPTURE_BLOCKS"
    for block in "${SCHEDULE_CAPTURE_VALUES[@]}"; do
        if ! [[ "$block" =~ ^[0-9]+$ ]] ||
           [ "$block" -lt 1 ] || [ "$block" -gt 4095 ]; then
            echo "SCHEDULE_CAPTURE_BLOCKS must contain block IDs 1..4095" >&2
            exit 2
        fi
        SCHEDULE_JAVA_ARGS+=(--scheduled-capture-block "$block")
    done
fi
if [ -n "$RANDOM_TICK_FIXTURE" ]; then
    read -r -a RANDOM_TICK_VALUES <<<"$RANDOM_TICK_FIXTURE"
    if [ "${#RANDOM_TICK_VALUES[@]}" -ne 5 ] ||
       ! [[ "${RANDOM_TICK_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${RANDOM_TICK_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${RANDOM_TICK_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${RANDOM_TICK_VALUES[3]}" =~ ^[0-9]+$ &&
             "${RANDOM_TICK_VALUES[4]}" =~ ^-?[0-9]+$ ]] ||
       [ "${RANDOM_TICK_VALUES[3]}" -lt 1 ] ||
       [ "${RANDOM_TICK_VALUES[3]}" -gt 4095 ]; then
        echo "RANDOM_TICK_FIXTURE must contain DX DY DZ BLOCK SEED" >&2
        exit 2
    fi
    RANDOM_TICK_JAVA_ARGS=(
        --random-tick-offset "${RANDOM_TICK_VALUES[@]}")
    RANDOM_TICK_RUNTIME_ARGS=(
        --random-tick-offset "${RANDOM_TICK_VALUES[@]}")
fi
if [ -n "$RANDOM_SELECTION_FIXTURE" ]; then
    if [ -n "$RANDOM_TICK_FIXTURE" ]; then
        echo "RANDOM_TICK_FIXTURE and RANDOM_SELECTION_FIXTURE are mutually exclusive" >&2
        exit 2
    fi
    read -r -a RANDOM_SELECTION_VALUES <<<"$RANDOM_SELECTION_FIXTURE"
    if [ "${#RANDOM_SELECTION_VALUES[@]}" -ne 5 ] ||
       ! [[ "${RANDOM_SELECTION_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${RANDOM_SELECTION_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${RANDOM_SELECTION_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${RANDOM_SELECTION_VALUES[3]}" =~ ^[0-9]+$ &&
             "${RANDOM_SELECTION_VALUES[4]}" =~ ^-?[0-9]+$ ]] ||
       [ "${RANDOM_SELECTION_VALUES[3]}" -lt 1 ] ||
       [ "${RANDOM_SELECTION_VALUES[3]}" -gt 4095 ]; then
        echo "RANDOM_SELECTION_FIXTURE must contain DX DY DZ BLOCK SEED" >&2
        exit 2
    fi
    RANDOM_SELECTION_JAVA_ARGS=(
        --random-selection-offset "${RANDOM_SELECTION_VALUES[@]}"
        --random-selection-fixture-out "$OUT/random_selection_fixture.json")
    RANDOM_SELECTION_RUNTIME_ARGS=(
        --random-selection-fixture "$OUT/random_selection_fixture.json")
fi
if [ -n "$WORLD_RANDOM_SEED48" ]; then
    if ! [[ "$WORLD_RANDOM_SEED48" =~ ^[0-9]+$ ]] ||
       [ "$WORLD_RANDOM_SEED48" -gt 281474976710655 ]; then
        echo "WORLD_RANDOM_SEED48 must be an integer in 0..2^48-1" >&2
        exit 2
    fi
    WORLD_RANDOM_JAVA_ARGS=(
        --world-random-seed48 "$WORLD_RANDOM_SEED48")
fi
if [ -n "$BLOCK_RANDOM_SEED48" ]; then
    if ! [[ "$BLOCK_RANDOM_SEED48" =~ ^[0-9]+$ ]] ||
       [ "$BLOCK_RANDOM_SEED48" -gt 281474976710655 ]; then
        echo "BLOCK_RANDOM_SEED48 must be an integer in 0..2^48-1" >&2
        exit 2
    fi
    BLOCK_RANDOM_JAVA_ARGS=(
        --block-random-seed48 "$BLOCK_RANDOM_SEED48")
fi
if [ -n "$TICK0_BLOCK_FIXTURE" ]; then
    read -r -a TICK0_BLOCK_VALUES <<<"$TICK0_BLOCK_FIXTURE"
    if [ "${#TICK0_BLOCK_VALUES[@]}" -ne 5 ] ||
       ! [[ "${TICK0_BLOCK_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${TICK0_BLOCK_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${TICK0_BLOCK_VALUES[2]}" =~ ^-?[0-9]+$ &&
             "${TICK0_BLOCK_VALUES[3]}" =~ ^[0-9]+$ &&
             "${TICK0_BLOCK_VALUES[4]}" =~ ^[0-9]+$ ]] ||
       [ "${TICK0_BLOCK_VALUES[3]}" -gt 4095 ] ||
       [ "${TICK0_BLOCK_VALUES[4]}" -gt 15 ]; then
        echo "TICK0_BLOCK_FIXTURE must contain DX DY DZ BLOCK(0..4095) META(0..15)" >&2
        exit 2
    fi
    TICK0_BLOCK_JAVA_ARGS=(
        --tick0-set-block-offset "${TICK0_BLOCK_VALUES[@]}")
    TICK0_BLOCK_RUNTIME_ARGS=(
        --tick0-set-block-offset "${TICK0_BLOCK_VALUES[@]}")
fi
if [ -n "$HELD_ITEM_FIXTURE" ]; then
    read -r -a HELD_ITEM_VALUES <<<"$HELD_ITEM_FIXTURE"
    if [ "${#HELD_ITEM_VALUES[@]}" -ne 3 ]; then
        echo "HELD_ITEM_FIXTURE must contain ITEM COUNT META" >&2
        exit 2
    fi
    HELD_ITEM_JAVA_ARGS=(--held-item-fixture "${HELD_ITEM_VALUES[@]}")
fi
if [ -n "$OFFHAND_ITEM_FIXTURE" ]; then
    read -r -a OFFHAND_ITEM_VALUES <<<"$OFFHAND_ITEM_FIXTURE"
    if [ "${#OFFHAND_ITEM_VALUES[@]}" -ne 3 ]; then
        echo "OFFHAND_ITEM_FIXTURE must contain ITEM COUNT META" >&2
        exit 2
    fi
    OFFHAND_ITEM_JAVA_ARGS=(
        --offhand-item-fixture "${OFFHAND_ITEM_VALUES[@]}")
fi
if [ -n "$TICK0_HARVEST_FIXTURE" ]; then
    read -r -a TICK0_HARVEST_VALUES <<<"$TICK0_HARVEST_FIXTURE"
    if [ "${#TICK0_HARVEST_VALUES[@]}" -ne 3 ] ||
       ! [[ "${TICK0_HARVEST_VALUES[0]}" =~ ^-?[0-9]+$ &&
             "${TICK0_HARVEST_VALUES[1]}" =~ ^-?[0-9]+$ &&
             "${TICK0_HARVEST_VALUES[2]}" =~ ^-?[0-9]+$ ]]; then
        echo "TICK0_HARVEST_FIXTURE must contain DX DY DZ" >&2
        exit 2
    fi
    if [ -n "$TICK0_BLOCK_FIXTURE" ] || [ -n "$BLOCK_EDIT_SEQUENCE" ]; then
        echo "TICK0_HARVEST_FIXTURE is mutually exclusive with block edits" >&2
        exit 2
    fi
    TICK0_HARVEST_JAVA_ARGS=(
        --tick0-harvest-offset "${TICK0_HARVEST_VALUES[@]}")
    TICK0_HARVEST_RUNTIME_ARGS=(
        --tick0-harvest-offset "${TICK0_HARVEST_VALUES[@]}")
fi
if [ -n "$BLOCK_EDIT_SEQUENCE" ]; then
    if [ -n "$TICK0_BLOCK_FIXTURE" ]; then
        echo "TICK0_BLOCK_FIXTURE and BLOCK_EDIT_SEQUENCE are mutually exclusive" >&2
        exit 2
    fi
    if [ ! -f "$BLOCK_EDIT_SEQUENCE" ]; then
        echo "BLOCK_EDIT_SEQUENCE does not exist: $BLOCK_EDIT_SEQUENCE" >&2
        exit 2
    fi
    BLOCK_EDIT_JAVA_ARGS=(--block-edit-sequence "$BLOCK_EDIT_SEQUENCE")
    BLOCK_EDIT_RUNTIME_ARGS=(--block-edit-sequence "$BLOCK_EDIT_SEQUENCE")
fi
if [ -n "$CHECKPOINT_TICK" ]; then
    if ! [[ "$CHECKPOINT_TICK" =~ ^[0-9]+$ ]] ||
       [ "$CHECKPOINT_TICK" -ge "$TICKS" ]; then
        echo "CHECKPOINT_TICK must be in 0..TICKS-1" >&2
        exit 2
    fi
    CHECKPOINT_JAVA_ARGS=(
        --checkpoint-tick "$CHECKPOINT_TICK"
        --checkpoint-state-out "$OUT/java_checkpoint_state.json"
        --checkpoint-blocks-out "$OUT/java_checkpoint_blocks.bin"
        --checkpoint-block-light-out "$OUT/java_checkpoint_block_light.bin")
fi
if [ -n "$TICK0_BLOCK_FIXTURE" ] || [ -n "$BLOCK_EDIT_SEQUENCE" ] \
        || [ "$BLOCK_LIGHT_COMPARE" = 1 ]; then
    BLOCK_LIGHT_JAVA_ARGS=(
        --block-light-before-out "$OUT/java_block_light_before.bin"
        --block-light-out "$OUT/java_block_light.bin")
    BLOCK_LIGHT_RUNTIME_ARGS=(
        --block-light-out "$OUT/c_runtime_block_light.bin")
fi
if [ "$BLOCK_LIGHT_COMPARE" != 0 ] && [ "$BLOCK_LIGHT_COMPARE" != 1 ]; then
    echo "BLOCK_LIGHT_COMPARE must be 0 or 1" >&2
    exit 2
fi
if [ "$SKY_LIGHT_COMPARE" = 1 ]; then
    SKY_LIGHT_JAVA_ARGS=(
        --sky-light-before-out "$OUT/java_sky_light_before.bin"
        --sky-light-out "$OUT/java_sky_light.bin")
    SKY_LIGHT_RUNTIME_ARGS=(
        --sky-light-out "$OUT/c_runtime_sky_light.bin")
    SKY_LIGHT_CAPSULE_ARGS=(
        --sky-light "$OUT/java_sky_light_before.bin")
elif [ "$SKY_LIGHT_COMPARE" != 0 ]; then
    echo "SKY_LIGHT_COMPARE must be 0 or 1" >&2
    exit 2
fi
if [ -n "$FIXTURE_BLOCK" ]; then
    read -r -a FIXTURE_VALUES <<<"$FIXTURE_BLOCK"
    if [ "${#FIXTURE_VALUES[@]}" -ne 5 ]; then
        echo "FIXTURE_BLOCK must contain exactly 5 integers: X Y Z ID META" >&2
        exit 2
    fi
    FIXTURE_ARGS=(--set-block "${FIXTURE_VALUES[@]}")
fi
if [ -n "$FIXTURE_BLOCKS_FILE" ]; then
    if [ ! -f "$FIXTURE_BLOCKS_FILE" ]; then
        echo "FIXTURE_BLOCKS_FILE does not exist: $FIXTURE_BLOCKS_FILE" >&2
        exit 2
    fi
    while read -r fx fy fz fid fmeta extra; do
        if [ -z "${fx:-}" ] || [[ "$fx" = \#* ]]; then
            continue
        fi
        if [ -n "${extra:-}" ] ||
           ! [[ "$fx" =~ ^-?[0-9]+$ && "$fy" =~ ^-?[0-9]+$ &&
                "$fz" =~ ^-?[0-9]+$ && "$fid" =~ ^[0-9]+$ &&
                "$fmeta" =~ ^[0-9]+$ ]]; then
            echo "invalid fixture row in $FIXTURE_BLOCKS_FILE: " \
                 "$fx $fy $fz $fid $fmeta ${extra:-}" >&2
            exit 2
        fi
        FIXTURE_ARGS+=(--set-block "$fx" "$fy" "$fz" "$fid" "$fmeta")
    done <"$FIXTURE_BLOCKS_FILE"
fi
if [ "$FIXTURE_STAGE" != early ] &&
   [ "$FIXTURE_STAGE" != late ] &&
   [ "$FIXTURE_STAGE" != final ]; then
    echo "FIXTURE_STAGE must be early, late, or final" >&2
    exit 2
fi
if ! [[ "$FIXTURE_DRAIN_TICKS" =~ ^[0-9]+$ ]] ||
   [ "$FIXTURE_DRAIN_TICKS" -gt 1000 ]; then
    echo "FIXTURE_DRAIN_TICKS must be an integer in 0..1000" >&2
    exit 2
fi
for fixture_arg in "${FIXTURE_ARGS[@]}"; do
    if [ "$FIXTURE_STAGE" = late ] && [ "$fixture_arg" = --set-block ]; then
        JAVA_FIXTURE_ARGS+=(--late-set-block)
    elif [ "$FIXTURE_STAGE" = final ] && [ "$fixture_arg" = --set-block ]; then
        JAVA_FIXTURE_ARGS+=(--final-set-block)
    else
        JAVA_FIXTURE_ARGS+=("$fixture_arg")
    fi
done

if [ "$SKIP_BUILD" = 1 ]; then
    echo "[1/8] C tracer already built (SKIP_BUILD=1)"
else
    echo "[1/8] build C tracer + full runtime"
    bash trace/build_c_tracer.sh >"$OUT/build.log" 2>&1 || {
        echo FAIL
        tail "$OUT/build.log"
        exit 1
    }
fi
echo "[2/8] gen tape ($TICKS ticks, seed $SEED, profile $TAPE_PROFILE)"
uv run --no-project python trace/gen_tape.py --ticks "$TICKS" --seed "$SEED" \
    --profile "$TAPE_PROFILE" --out $OUT/tape.txt
echo "[3/8] narrow C physics tracer"
./trace_game --tape $OUT/tape.txt --seed "$SEED" --out $OUT/c_phys.csv \
    --state $OUT/c_state_small.jsonl --spawn-out $OUT/c_spawn.txt --render 0 \
    --platform "$PLATFORM" --empty-inventory \
    --world-time 6000 --daylight 0 \
    --blocks-before-out $OUT/c_blocks_before.bin \
    --blocks-out $OUT/c_blocks.bin \
    --blocks-box "${BLOCK_BOX_ARGS[@]}" "${BLOCK_BOX_MODE_ARGS[@]}" \
    "${FIXTURE_ARGS[@]}"
read -r -a BLOCK_BOX_ARGS <"$OUT/block_box.txt"
if [ "${#BLOCK_BOX_ARGS[@]}" -ne 6 ]; then
    echo "C tracer did not write a valid resolved block box" >&2
    exit 1
fi
# is the qrl bridge up?
if timeout 2 bash -c 'exec 3<>"/dev/tcp/$1/$2"' _ "$QRL_HOST" "$QRL_PORT" 2>/dev/null; then
    echo "[4/8] Java tracer (bridge up at $QRL_HOST:$QRL_PORT) -- spawn-aligned to C pose"
    # PLATFORM=N fills an NxN stone pad under the spawn so the Java player is GROUNDED at C's y
    # (the two worldgens differ at a shared column; without it Java free-falls + dies, cascading
    # into every feature). The C tracer receives the same fixture so the block diff stays fair.
    uv run --no-project python trace/trace_java.py \
        --tape $OUT/tape.txt --seed "$SEED" --empty-inventory --clean-entities --freeze-time \
        --host "$QRL_HOST" --port "$QRL_PORT" \
        --dimension "$DIMENSION" \
        --out $OUT/java_phys.csv --state $OUT/java_state.jsonl \
        --state-before-out $OUT/java_state_before.json \
        --spawn-file $OUT/c_spawn.txt --platform "$PLATFORM" \
        --platform-clear-height "$PLATFORM_CLEAR_HEIGHT" \
        --do-fire-tick "$DO_FIRE_TICK_ARG" \
        --do-entity-drops "$DO_ENTITY_DROPS_ARG" \
        --weather-mode "$WEATHER_MODE" \
        --fixture-drain-ticks "$FIXTURE_DRAIN_TICKS" \
        "${FALLING_JAVA_ARGS[@]}" \
        --blocks-before-out $OUT/java_blocks_before.bin \
        --blocks-out $OUT/java_blocks.bin --blocks-box "${BLOCK_BOX_ARGS[@]}" \
        "${JAVA_FIXTURE_ARGS[@]}" "${INITIAL_FIRE_ARGS[@]}" \
        "${INITIAL_FOOD_ARGS[@]}" \
        "${CLEAR_HURT_ARGS[@]}" "${MOVE_PACKET_ARGS[@]}" \
        "${HELD_ITEM_JAVA_ARGS[@]}" \
        "${OFFHAND_ITEM_JAVA_ARGS[@]}" \
        "${XP_JAVA_ARGS[@]}" "${ITEM_JAVA_ARGS[@]}" "${ARROW_JAVA_ARGS[@]}" \
        "${PRIMED_TNT_JAVA_ARGS[@]}" \
        "${END_CRYSTAL_JAVA_ARGS[@]}" \
        "${BOAT_JAVA_ARGS[@]}" \
        "${SMALL_FIREBALL_JAVA_ARGS[@]}" \
        "${MOB_JAVA_ARGS[@]}" \
        "${VILLAGER_JAVA_ARGS[@]}" \
        "${POTION_JAVA_ARGS[@]}" \
        "${PLAYER_XP_JAVA_ARGS[@]}" "${PLAYER_COMBAT_JAVA_ARGS[@]}" \
        "${ARMOR_JAVA_ARGS[@]}" \
        "${COMPARATOR_JAVA_ARGS[@]}" \
        "${POST_COMPARATOR_JAVA_ARGS[@]}" \
        "${CONTAINER_JAVA_ARGS[@]}" \
        "${CONTAINER_FILL_JAVA_ARGS[@]}" \
        "${SHULKER_NBT_JAVA_ARGS[@]}" \
        "${FLOWER_POT_JAVA_ARGS[@]}" \
        "${SKULL_JAVA_ARGS[@]}" \
        "${COMMAND_SUCCESS_JAVA_ARGS[@]}" \
        "${ITEM_FRAME_JAVA_ARGS[@]}" \
        "${SCHEDULE_JAVA_ARGS[@]}" \
        "${RANDOM_TICK_JAVA_ARGS[@]}" \
        "${RANDOM_SELECTION_JAVA_ARGS[@]}" \
        "${WORLD_RANDOM_JAVA_ARGS[@]}" \
        "${BLOCK_RANDOM_JAVA_ARGS[@]}" \
        "${TICK0_BLOCK_JAVA_ARGS[@]}" "${TICK0_HARVEST_JAVA_ARGS[@]}" \
        "${BLOCK_EDIT_JAVA_ARGS[@]}" \
        "${CHECKPOINT_JAVA_ARGS[@]}" \
        "${BLOCK_LIGHT_JAVA_ARGS[@]}" \
        "${SKY_LIGHT_JAVA_ARGS[@]}" \
        "${FRESH_ARGS[@]}"
    echo "[5/8] package + validate the Java pre-tick state capsule"
    uv run --no-project python trace/state_capsule.py create \
        --state $OUT/java_state_before.json \
        --blocks $OUT/java_blocks_before.bin \
        --box "${BLOCK_BOX_ARGS[@]}" \
        --seed "$SEED" --source-engine minecraft-java --source-version 1.11.2 \
        "${SKY_LIGHT_CAPSULE_ARGS[@]}" \
        --out $OUT/state_capsule
    echo "[6/8] full shared GmRuntime tracer (capsule-initialized)"
    uv run --no-project python trace/trace_runtime.py \
        --tape $OUT/tape.txt --spawn-file $OUT/c_spawn.txt --seed "$SEED" \
        --capsule $OUT/state_capsule \
        --script-out $OUT/c_runtime_script.jsonl \
        --raw-state $OUT/c_state_raw.jsonl --state $OUT/c_state.jsonl \
        --blocks-out $OUT/c_runtime_blocks.bin \
        --blocks-box "${BLOCK_BOX_ARGS[@]}" \
        --skip-build "${XP_RUNTIME_ARGS[@]}" "${ITEM_RUNTIME_ARGS[@]}" \
        "${ARROW_RUNTIME_ARGS[@]}" \
        "${PRIMED_TNT_RUNTIME_ARGS[@]}" \
        "${END_CRYSTAL_RUNTIME_ARGS[@]}" \
        "${BOAT_RUNTIME_ARGS[@]}" \
        "${SMALL_FIREBALL_RUNTIME_ARGS[@]}" \
        "${MOB_RUNTIME_ARGS[@]}" \
        "${POTION_RUNTIME_ARGS[@]}" "${ARMOR_RUNTIME_ARGS[@]}" \
        "${RANDOM_TICK_RUNTIME_ARGS[@]}" \
        "${SCHEDULE_RUNTIME_ARGS[@]}" \
        "${RANDOM_SELECTION_RUNTIME_ARGS[@]}" \
        "${TICK0_BLOCK_RUNTIME_ARGS[@]}" "${TICK0_HARVEST_RUNTIME_ARGS[@]}" \
        "${BLOCK_EDIT_RUNTIME_ARGS[@]}" \
        "${BLOCK_LIGHT_RUNTIME_ARGS[@]}" \
        "${SKY_LIGHT_RUNTIME_ARGS[@]}"
    echo "[7/8] PER-FEATURE STATE DIFF (Java vs full shared GmRuntime)"
    uv run --no-project python trace/diff_trace.py \
        --java $OUT/java_state.jsonl --c $OUT/c_state.jsonl
    echo "[8/8] POST-TICK RAW BLOCK DIFF (Java vs full shared GmRuntime)"
    BLOCK_DIFF_ARGS=(
        --java $OUT/java_blocks.bin --c $OUT/c_runtime_blocks.bin
        --java-before $OUT/java_blocks_before.bin --c-before $OUT/java_blocks_before.bin
        --box "${BLOCK_BOX_ARGS[@]}"
        --out $OUT/block_mismatches.csv
    )
    if [ "$BLOCK_STRICT" = transition ]; then
        BLOCK_DIFF_ARGS+=(--transition-strict)
    elif [ "$BLOCK_STRICT" != 1 ]; then
        BLOCK_DIFF_ARGS+=(--allow-diff)
    fi
    if [ "$REQUIRE_BLOCK_MUTATION" = 1 ]; then
        BLOCK_DIFF_ARGS+=(--require-mutation)
    fi
    uv run --no-project python trace/block_diff.py "${BLOCK_DIFF_ARGS[@]}"
    if [ -n "$TICK0_BLOCK_FIXTURE" ] || [ -n "$TICK0_HARVEST_FIXTURE" ] \
       || [ -n "$BLOCK_EDIT_SEQUENCE" ] \
            || [ "$BLOCK_LIGHT_COMPARE" = 1 ]; then
        echo "      STRICT RAW BLOCK-LIGHT DIFF (Java vs full shared GmRuntime)"
        uv run --no-project python trace/light_diff.py \
            --java "$OUT/java_block_light.bin" \
            --c "$OUT/c_runtime_block_light.bin" \
            --box "${BLOCK_BOX_ARGS[@]}" \
            --out "$OUT/block_light_mismatches.csv"
    fi
    if [ "$SKY_LIGHT_COMPARE" = 1 ]; then
        echo "      STRICT RAW SKY-LIGHT DIFF (Java vs full shared GmRuntime)"
        uv run --no-project python trace/light_diff.py \
            --java "$OUT/java_sky_light.bin" \
            --c "$OUT/c_runtime_sky_light.bin" \
            --kind sky \
            --box "${BLOCK_BOX_ARGS[@]}" \
            --out "$OUT/sky_light_mismatches.csv"
    fi
else
    echo "[4/8] qrl bridge DOWN on $QRL_HOST:$QRL_PORT -- skipping live Java run."
    echo "      launch: (cd java && setsid nohup bash start_vnc_client.sh >/tmp/mc_launch.out 2>&1 &)"
    echo "[5/8] no Java capsule available"
    echo "[6/8] full shared GmRuntime tracer (synthetic fixture fallback)"
    uv run --no-project python trace/trace_runtime.py \
        --tape $OUT/tape.txt --spawn-file $OUT/c_spawn.txt --seed "$SEED" \
        --platform "$PLATFORM" --world-time 6000 \
        --script-out $OUT/c_runtime_script.jsonl \
        --raw-state $OUT/c_state_raw.jsonl --state $OUT/c_state.jsonl \
        --skip-build "${FIXTURE_ARGS[@]}"
    echo "[7/8] STATE SELF-DIFF (harness proof: full runtime vs copy must be ZERO divergence)"
    cp $OUT/c_state.jsonl /tmp/c_selfcopy.jsonl
    uv run --no-project python trace/diff_trace.py \
        --java $OUT/c_state.jsonl --c /tmp/c_selfcopy.jsonl
    echo "[8/8] BLOCK SELF-DIFF (must be bit-exact)"
    uv run --no-project python trace/block_diff.py \
        --java $OUT/c_blocks.bin --c $OUT/c_blocks.bin --box "${BLOCK_BOX_ARGS[@]}"
fi
