#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
export UV_CACHE_DIR="${UV_CACHE_DIR:-$HOME/.cache/uv}"
export TMPDIR="${TMPDIR:-$ROOT/.tmp}"

U=(uv run --no-project python)
"${U[@]}" verify/completeness/registry_gate.py
"${U[@]}" verify/completeness/gap_audit.py --check
"${U[@]}" verify/completeness/surface_registry_gate.py
"${U[@]}" verify/completeness/callback_census.py --check
"${U[@]}" verify/completeness/block_callback_evidence.py
"${U[@]}" verify/completeness/block_callback_noop_gate.py
"${U[@]}" verify/completeness/block_callback_falling_gate.py
"${U[@]}" verify/completeness/block_callback_portal_gate.py
"${U[@]}" verify/completeness/block_callback_neighbor_gate.py
"${U[@]}" verify/completeness/block_callback_leaves_gate.py
"${U[@]}" verify/completeness/block_callback_note_liquid_gate.py
"${U[@]}" verify/completeness/block_callback_growth_gate.py
"${U[@]}" verify/completeness/block_callback_ui_gate.py
"${U[@]}" verify/completeness/block_callback_redstone_gate.py
"${U[@]}" verify/completeness/block_callback_update_tail_gate.py
"${U[@]}" verify/completeness/block_callback_detector_gate.py
"${U[@]}" verify/completeness/block_callback_harvest_gate.py
"${U[@]}" verify/completeness/block_callback_actions_gate.py
"${U[@]}" verify/completeness/block_callback_support_gate.py
"${U[@]}" verify/completeness/block_callback_plant_root_gate.py
"${U[@]}" verify/completeness/block_callback_nested_gate.py
"${U[@]}" verify/completeness/block_callback_drops_gate.py
"${U[@]}" verify/completeness/block_callback_jukebox_gate.py
"${U[@]}" verify/completeness/block_callback_daylight_gate.py
"${U[@]}" verify/completeness/block_callback_material_use_gate.py
"${U[@]}" verify/completeness/block_callback_tnt_neighbor_gate.py
"${U[@]}" verify/completeness/block_callback_container_use_gate.py
"${U[@]}" verify/completeness/block_callback_simple_use_gate.py
"${U[@]}" verify/completeness/block_callback_placement_gate.py
"${U[@]}" verify/completeness/block_callback_break_gate.py
"${U[@]}" verify/completeness/block_callback_neighbor_tail_gate.py
"${U[@]}" verify/completeness/block_callback_drop_tail_gate.py
bash magma/game/test_block_callback_drop.sh
"${U[@]}" verify/completeness/block_callback_beacon_gate.py
make -C magma game/test_beacon_oracle
"${U[@]}" magma/trace/test_beacon.py --port "${QRL_PORT:-25600}" \
    --native magma/game/test_beacon_oracle
"${U[@]}" verify/completeness/block_callback_activation_tail_gate.py
bash magma/game/test_block_callback_activation.sh
"${U[@]}" verify/completeness/block_callback_strict_progress.py
"${U[@]}" verify/completeness/item_callback_evidence.py
"${U[@]}" verify/completeness/capacity_boundary_gate.py
"${U[@]}" verify/completeness/capacity_stress_gate.py
"${U[@]}" verify/completeness/generate_living_cold_slot.py --check
"${U[@]}" verify/completeness/living_capacity_census.py --strict
make -C magma game/test_living_cold_slot
magma/game/test_living_cold_slot
"${U[@]}" verify/completeness/cross_store_order_gate.py
"${U[@]}" verify/completeness/random_tick_order_gate.py
"${U[@]}" verify/completeness/random_tick_campaign_gate.py
"${U[@]}" verify/completeness/random_tick_registry_gate.py
"${U[@]}" verify/completeness/scheduled_tick_registry_gate.py
"${U[@]}" verify/completeness/projectile_boundary_gate.py
"${U[@]}" verify/completeness/hostile_boundary_gate.py
"${U[@]}" verify/completeness/passive_mob_boundary_gate.py
"${U[@]}" verify/completeness/village_boundary_gate.py
"${U[@]}" verify/completeness/village_strict_campaign_gate.py
"${U[@]}" verify/completeness/special_mob_boundary_gate.py
"${U[@]}" verify/completeness/special_mob_strict_campaign_gate.py
"${U[@]}" verify/completeness/redstone_boundary_gate.py
"${U[@]}" verify/completeness/redstone_strict_topology_gate.py
"${U[@]}" verify/completeness/piston_strict_campaign_gate.py
"${U[@]}" verify/completeness/strict_closure_gate.py
"${U[@]}" verify/completeness/world_system_boundary_gate.py
"${U[@]}" verify/completeness/spawner_tile_gate.py
"${U[@]}" verify/completeness/spawner_strict_campaign_gate.py
"${U[@]}" verify/completeness/dimension_boundary_gate.py
"${U[@]}" verify/completeness/structure_request_order_gate.py
"${U[@]}" verify/completeness/advanced_item_boundary_gate.py
"${U[@]}" verify/completeness/item_strict_campaign_gate.py
"${U[@]}" verify/completeness/container_slot_gate.py
"${U[@]}" verify/completeness/client_shell_boundary_gate.py
"${U[@]}" verify/completeness/mode_strict_campaign_gate.py
"${U[@]}" verify/completeness/visual_boundary_gate.py
"${U[@]}" verify/completeness/visual_strict_campaign_gate.py
"${U[@]}" verify/completeness/audio_boundary_gate.py
"${U[@]}" verify/completeness/performance_boundary_gate.py
"${U[@]}" verify/completeness/native_parallel_soak_gate.py
"${U[@]}" verify/completeness/smelting_registry_gate.py
"${U[@]}" verify/completeness/furnace_fuel_gate.py
"${U[@]}" verify/completeness/recorder_private_gate.py
"${U[@]}" verify/completeness/save_order_gate.py
"${U[@]}" verify/completeness/mixed_order_java_gate.py
"${U[@]}" verify/completeness/horse_family_gate.py
"${U[@]}" verify/completeness/llama_family_gate.py
"${U[@]}" verify/completeness/bat_family_gate.py
"${U[@]}" verify/completeness/squid_family_gate.py
"${U[@]}" verify/completeness/mooshroom_family_gate.py
"${U[@]}" verify/completeness/snowman_family_gate.py
"${U[@]}" verify/completeness/endermite_family_gate.py
"${U[@]}" verify/completeness/giant_family_gate.py
"${U[@]}" verify/completeness/minecart_variant_gate.py
"${U[@]}" verify/completeness/spawner_tile_gate.py
"${U[@]}" verify/completeness/structure_block_gate.py
"${U[@]}" verify/completeness/skull_tile_gate.py
"${U[@]}" verify/completeness/decorative_tile_gate.py
"${U[@]}" verify/completeness/command_block_gate.py
"${U[@]}" verify/completeness/command_registry_gate.py
"${U[@]}" verify/completeness/husk_family_gate.py
"${U[@]}" verify/completeness/stray_family_gate.py
"${U[@]}" verify/completeness/polar_bear_family_gate.py
"${U[@]}" verify/completeness/rabbit_family_gate.py
"${U[@]}" verify/completeness/spectral_arrow_family_gate.py
"${U[@]}" verify/completeness/armor_stand_gate.py
"${U[@]}" verify/completeness/hanging_family_gate.py
bash magma/game/test_boat_oracle.sh
bash magma/game/test_food_oracle.sh
bash magma/game/test_item_block_oracle.sh
bash magma/game/test_tool_callback_oracle.sh
bash magma/game/test_special_item_use_oracle.sh
bash magma/game/test_container_click_oracle.sh
make -C magma game/test_anvil_oracle
"${U[@]}" magma/trace/test_anvil.py --port "${QRL_PORT:-25600}"
bash magma/game/test_command_block_tick.sh
bash magma/game/test_map_color_registry.sh
bash magma/game/test_map_update_oracle.sh
bash magma/game/test_audio_selector_oracle.sh
bash magma/game/test_audio_source_oracle.sh
make -C magma game/test_piston_capacity
magma/game/test_piston_capacity
make -C magma game/test_piston_reentrant
magma/game/test_piston_reentrant
"${U[@]}" verify/completeness/save_fork.py selftest
"${U[@]}" verify/completeness/anvil_semantic.py selftest
"${U[@]}" magma/trace/state_capsule.py selftest
"${U[@]}" verify/completeness/comparator_gate.py selftest
"${U[@]}" verify/completeness/fixture_contract.py selftest
"${U[@]}" verify/completeness/reduce_failure.py selftest
make -C magma game
"${U[@]}" verify/completeness/test_chunk_bundle.py
"${U[@]}" verify/completeness/test_cold_chunk_store.py
"${U[@]}" verify/completeness/test_native_checkpoint.py
"${U[@]}" verify/completeness/test_mixed_checkpoint_campaign.py
"${U[@]}" verify/completeness/test_native_save_slot.py
bash verify/completeness/test_native_save_ui.sh
make -C magma game/test_horse_runtime
make -C magma game/test_llama_runtime
make -C magma game/test_bat_runtime
make -C magma game/test_squid_runtime
make -C magma game/test_mooshroom_runtime
make -C magma game/test_snowman_runtime
make -C magma game/test_endermite_runtime game/test_giant_runtime \
    game/test_husk_runtime game/test_stray_runtime \
    game/test_polar_bear_runtime \
    game/test_rabbit_runtime \
    game/test_hostile_death_live
make -C magma game/test_armor_stand_runtime
make -C magma game/test_hanging_runtime
"${U[@]}" blaze/oracle/runner.py --cpu-only llama_genetics
"${U[@]}" blaze/oracle/runner.py --cpu-only llama_loot
"${U[@]}" magma/trace/test_husk_melee.py --port "${QRL_PORT:-25600}"
"${U[@]}" magma/trace/test_stray_arrow.py --port "${QRL_PORT:-25600}"
"${U[@]}" magma/trace/test_polar_bear_melee.py --port "${QRL_PORT:-25600}"
"${U[@]}" magma/trace/test_rabbit.py --port "${QRL_PORT:-25600}"
"${U[@]}" verify/completeness/skeleton_trap_gate.py
magma/game/test_horse_runtime
magma/game/test_llama_runtime
magma/game/test_bat_runtime
magma/game/test_squid_runtime
magma/game/test_mooshroom_runtime
magma/game/test_snowman_runtime
magma/game/test_endermite_runtime
magma/game/test_giant_runtime
magma/game/test_husk_runtime
magma/game/test_stray_runtime
magma/game/test_polar_bear_runtime
magma/game/test_rabbit_runtime
magma/game/test_hostile_death_live
magma/game/test_armor_stand_runtime
magma/game/test_hanging_runtime
bash magma/game/test_hanging_script.sh
