#!/usr/bin/env bash
# Driver for `make test-game` body: run every standalone subtest, classify
# outcomes, and print a summary even when some fail.
#
# The Makefile guarantees only the game binary and generated mob atlas. Cheap
# config/registry/launch gates run here so they appear in the final summary too.
set -uo pipefail

MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
cd "$MAGMA" || exit

pass=0
fail=0
skip=0
declare -a FAIL_NAMES=()
declare -a PASS_NAMES=()
declare -a SKIP_NAMES=()

# run_one NAME COMMAND...
# Captures exit status; does not abort the suite.
run_one() {
  local name="$1"; shift
  echo ""
  echo "======== $name ========"
  local rc=0
  if "$@"; then
    rc=0
  else
    rc=$?
  fi
  if [ "$rc" -eq 0 ]; then
    echo "PASS: $name"
    PASS_NAMES+=("$name")
    pass=$((pass + 1))
  elif [ "$rc" -eq 77 ]; then
    # reserved skip code (GNU autotools convention); unused by current scripts
    echo "SKIP: $name"
    SKIP_NAMES+=("$name")
    skip=$((skip + 1))
  else
    echo "FAIL: $name (rc=$rc)"
    FAIL_NAMES+=("$name")
    fail=$((fail + 1))
  fi
}
# Cheap foundational gates that used to be Makefile prerequisites.
run_one test_config         bash game/test_config.sh
run_one test_block_registry bash game/test_block_registry.sh
run_one test_launch         make --no-print-directory test-launch


# Atlas sanity is C now (deleted tests/check_mob_atlas.py).
run_one test_asset_ui make --no-print-directory test-asset-ui

# Ordered list mirrors the historical Makefile test-game body.
run_one test_input_map       bash game/test_input_map.sh
run_one test_view            bash game/test_view.sh
run_one test_overlay         bash game/test_overlay.sh
run_one test_entity_render   bash game/test_entity_render.sh
run_one test_item_render     bash game/test_item_render.sh
run_one test_item_uv_census  bash game/test_item_uv_census.sh
run_one test_hand            bash game/test_hand.sh
run_one test_hud             bash game/test_hud.sh
run_one test_player_ctl      bash game/test_player_ctl.sh
run_one test_world_live      bash game/test_world_live.sh
run_one test_play_compose    bash game/test_play_compose.sh
run_one test_runtime         bash game/test_runtime.sh
run_one test_script          bash game/test_script.sh
run_one test_block_post      bash game/test_block_post.sh
run_one test_fall_reanchor   bash game/test_fall_reanchor.sh
run_one test_fluid_live      bash game/test_fluid_live.sh
run_one test_plants_live     bash game/test_plants_live.sh
run_one test_randtick        bash game/test_randtick.sh
run_one test_furnace_live    bash game/test_furnace_live.sh
run_one test_container_live  bash game/test_container_live.sh
run_one test_armor_live      bash game/test_armor_live.sh
run_one test_screen          bash game/test_screen.sh
run_one test_player_preview  bash game/test_player_preview.sh
run_one test_chest_loot      bash game/test_chest_loot.sh
run_one test_mob_live        bash game/test_mob_live.sh
run_one test_stronghold_live bash game/test_stronghold_live.sh
run_one test_portal_live     bash game/test_portal_live.sh
run_one test_dimensions_live bash game/test_dimensions_live.sh
run_one test_dragon_live     bash game/test_dragon_live.sh
run_one test_route_e2e       bash game/test_route_e2e.sh

echo ""
echo "======== test-game summary ========"
echo "pass=$pass fail=$fail skip=$skip total=$((pass + fail + skip))"
if [ "${#PASS_NAMES[@]}" -gt 0 ]; then
  echo "PASS: ${PASS_NAMES[*]}"
fi
if [ "${#SKIP_NAMES[@]}" -gt 0 ]; then
  echo "SKIP: ${SKIP_NAMES[*]}"
fi
if [ "${#FAIL_NAMES[@]}" -gt 0 ]; then
  echo "FAIL: ${FAIL_NAMES[*]}"
  exit 1
fi
echo "ALL STANDALONE SUBTESTS PASS"
exit 0
