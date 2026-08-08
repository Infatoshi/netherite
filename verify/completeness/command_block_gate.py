#!/usr/bin/env python3
"""Lock the measured MODE-02 live-bounded command-block boundary."""

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def source_has(path, tokens):
    source = path.read_text(encoding="utf-8")
    for token in tokens:
        require(token in source, f"{path.relative_to(ROOT)} lost {token!r}")


def main():
    manifest = json.loads((HERE / "command_block_manifest.json")
                          .read_text(encoding="utf-8"))
    require(manifest["schema"] == "netherite.command_block_gate"
            and manifest["version"] == 69
            and manifest["todo"] == "MODE-02"
            and manifest["classification"] == "live_bounded",
            "invalid command-block manifest identity")
    require(manifest["state"]["block_variants"] == [137, 210, 211]
            and manifest["state"]["command"] == [
                "", "Searge",
                "time set day|night|<nonnegative int32>",
                "time add <nonnegative int32>",
                "time query daytime|day|gametime",
                "weather clear|rain|thunder [1..1000000 seconds]",
                "gamerule <ten represented simulation rules> <exact typed value>",
                "toggledownfall", "seed",
                "setblock <absolute|relative xyz> <registered block> [meta] [replace|keep] plus exact bounded non-tile destroy families",
                "testforblock <absolute|relative xyz> <registered block> [meta|*]",
                "testforblocks <source box> <destination> [all|masked]",
                "fill <opposite corners> <registered block> [meta] [replace|keep|outline|hollow]",
                "clone <source box> <destination> replace|masked|filtered [normal|force|move] [filter block/meta]",
                "say <non-selector printable literal>",
                "me <non-selector printable literal>",
                "particle smoke ~ ~ ~ 0 0 0 0 1 normal",
                "xp <nonnegative points|signed levels> @p",
                "clear @p [all 391 non-air registry items] [-1..32767 metadata] [-1..int32 max count] including zero-count test",
                "give @p <all 391 non-air registry items> [1..64 count] [0..32767 metadata]",
                "enchant @p <all 30 enchantment names|numeric ids> [valid level] with exact applicability and compatibility",
                "replaceitem entity @p <all player hotbar/inventory/armor/hand slots> <all 391 non-air registry items> [1..64 count] [0..32767 metadata]",
                "kill @p",
                "execute @p ~ ~ ~ setblock ~6 ~ ~ minecraft:gold_block 0 replace",
                "setworldspawn <absolute|relative integer x y z>",
                "spawnpoint @p <absolute|relative integer x y z>",
                "summon minecraft:lightning_bolt 14 78 8",
                "blockdata 12 78 8 {OutputSignal:7}",
                "entitydata @e[type=minecraft:item_frame,c=1] {ItemRotation:3b}",
                "help with externally pinned command RNG seed 0",
                "scoreboard objectives list on the empty objective registry",
                "stats entity @p clear SuccessCount on an empty player mapping",
                "execute @p ~ ~ ~ trigger qrl add 2 from saved score 5 unlocked",
                "achievement give achievement.openInventory @p from saved absent state",
                "spreadplayers 8 8 0 4 false @p with externally pinned constructor RNG seed 0",
                "gamemode survival|creative|adventure|spectator|0|1|2|3 @p",
                "testfor @p",
                "title @p clear|reset|times <signed int32 triplet>|title|subtitle|actionbar {\"text\":\"bounded\"}",
                "stopsound @p [all ten sound categories] [individual sound]",
                "effect @p <all 27 potion names|numeric ids> [0..1000000 seconds] [0..255 amplifier] [hideParticles] plus remove/clear",
                "playsound minecraft:block.note.harp master @p",
                "tellraw @p {\"text\":\"bounded\"}",
                "tell @p bounded",
                "locate Stronghold|Monument|Village|Mansion|EndCity|Fortress|Temple|Mineshaft",
                "difficulty peaceful|easy|normal|hard|p|e|n|h|0|1|2|3",
                "defaultgamemode survival|creative|adventure|spectator|0|1|2|3",
                "worldborder get|set 1000|set 1|add -100|center 12.5 -7.5|damage buffer 3|damage amount 0.5|warning time 20|warning distance 7",
                "tp|teleport @p <absolute|relative xyz> [absolute|relative yaw pitch]"]
            and manifest["state"]["powered_automatic_condition_met"]
                == "exact"
            and manifest["state"]["pending_update"] == "exact"
            and manifest["behavior"] == {
                "searge_easter_egg": "direct_java_native_exact",
                "time_set_family": "direct_java_native_exact",
                "time_add_family": "direct_java_native_exact",
                "time_query_family": "direct_java_native_exact",
                "weather_family": "direct_java_native_exact",
                "represented_gamerule_setters":
                    "10 direct_java_native_exact including isolated causal randomTickSpeed=17 selection",
                "toggledownfall": "direct_java_native_exact",
                "seed": "direct_java_native_exact",
                "setblock_replace_keep":
                    "direct_java_native_exact_all_236_registry_names_explicit_or_default_meta",
                "setblock_destroy_stone":
                    "direct_java_native_exact_drop_rng_entity_order_blocks_light_and_continuation",
                "setblock_destroy_nontile_drop_families":
                    "reused_exact_piston_drop_engine_with_direct_java_native_torch_receipt",
                "setblock_destroy_solid_and_snow_families":
                    "direct_java_native_exact_gravel_and_layered_snow_drop_rng_entity_order_blocks_light_and_continuation",
                "testforblock_positive":
                    "direct_java_native_exact_registered_block_numeric_or_wildcard_meta",
                "testforblocks_positive":
                    "direct_java_native_exact_raw_state_all_or_masked",
                "fill_replace_keep_shells":
                    "direct_java_native_exact_all_236_registry_names_explicit_or_default_meta",
                "clone_all_non_tile_modes":
                    "direct_java_native_exact_filtered_snapshot_overlap_move_and_raw_state",
                "say_literal":
                    "direct_java_native_exact_server_state_chat_delivery_ui_tail",
                "me_literal":
                    "direct_java_native_exact_server_state_chat_delivery_ui_tail",
                "particle_smoke_canonical":
                    "direct_java_native_exact_server_state_packet_render_tail",
                "xp_single_player":
                    "direct_java_native_exact_nonnegative_points_and_signed_levels",
                "clear_registry_single_player":
                    "all_391_nonair_registry_items_native_with_direct_java_native_exact_partial_metadata_test_only_clear_all_and_held_attack_cooldown_partitions",
                "give_registry_single_player":
                    "all_391_nonair_registry_items_native_with_direct_java_native_exact_ordinary_damageable_limited_stack_and_block_item_partitions",
                "enchant_full_registry_single_player":
                    "all_30_names_and_numeric_aliases_valid_levels_applicability_and_compatibility_with_five_direct_java_native_sword_partitions",
                "replaceitem_registry_single_player":
                    "all_391_nonair_registry_items_and_all_player_slot_classes_native_with_five_direct_java_native_partitions_and_long_command_persistence",
                "kill_single_player":
                    "direct_java_native_exact_damage_death_lifecycle_world_continuation_and_output",
                "execute_single_player_setblock":
                    "direct_java_native_exact_player_context_nested_dispatch_and_output",
                "setworldspawn_integer_coordinates":
                    "direct_java_native_exact_persisted_world_spawn_and_output",
                "spawnpoint_single_player_integer_coordinates":
                    "direct_java_native_exact_persisted_forced_player_spawn_and_output",
                "summon_lightning_bounded":
                    "direct_java_native_exact_weather_entity_rng_fire_callbacks_and_output",
                "blockdata_comparator_bounded":
                    "direct_java_native_exact_nbt_merge_transient_output_and_scheduled_recompute",
                "entitydata_item_frame_bounded":
                    "direct_java_native_exact_nbt_merge_rotation_persistence_and_output",
                "help_seed_zero_bounded":
                    "direct_java_native_exact_command_rng_choice_and_nested_text_output",
                "scoreboard_empty_objective_list":
                    "direct_java_native_exact_zero_success_query_and_output",
                "stats_clear_empty_player_mapping":
                    "direct_java_native_exact_idempotent_mapping_clear_and_output",
                "trigger_qrl_add_two":
                    "direct_java_native_exact_saved_score_add_lock_and_empty_feedback",
                "achievement_open_inventory":
                    "direct_java_native_exact_saved_stat_award_and_hover_output",
                "spreadplayers_single_player_seed_zero":
                    "direct_java_native_exact_server_destination_output_and_client_correction_tail",
                "gamemode_all_modes_single_player":
                    "direct_java_native_exact_saved_server_state_spectator_motion_and_empty_feedback",
                "testfor_single_player":
                    "direct_java_native_exact_server_state_and_output",
                "title_bounded_single_player":
                    "all_seven_bounded_actions_native_with_four_direct_java_native_exact_partitions",
                "stopsound_bounded_single_player":
                    "all_ten_categories_and_individual_sound_native_with_three_direct_java_native_exact_partitions",
                "effect_full_registry_single_player":
                    "all_27_names_and_numeric_aliases_defaults_duration_amplifier_particles_remove_and_clear_with_four_direct_java_native_partitions",
                "playsound_harp_single_player":
                    "direct_java_native_exact_server_state_packet_tail",
                "tellraw_literal_single_player":
                    "direct_java_native_exact_server_state_chat_tail",
                "tell_literal_single_player":
                    "direct_java_native_exact_styled_output_incoming_chat_tail",
                "locate_all_eight_structures":
                    "direct_java_native_exact_spacing_rng_biome_terrain_and_dimension_predicates",
                "server_modes":
                    "direct_java_native_exact_all_saved_difficulties_by_name_short_or_number_and_named_or_numeric_default_game_modes",
                "bounded_worldborder":
                    "direct_java_native_exact_stationary_mutations_saved_state_and_outside_damage",
                "teleport_coordinates_single_player":
                    "direct_java_native_exact_absolute_centering_player_or_source_relative_coordinates_rotation_output_server_state_and_captured_client_packet_phase",
                "comparator_update": "exact",
                "impulse_repeating_chain_update_tick":
                    "pinned_java_source_native_regression",
                "chain_propagation_order":
                    "pinned_java_source_native_regression",
            }, "command-block bounded behavior changed")
    require(manifest["receipts"]["represented_gamerules"]
            == "magma/command_gamerule_10_exact_v2/summary.md",
            "represented gamerule campaign receipt changed")
    summary = (ROOT / manifest["receipts"]["represented_gamerules"])
    summary_text = summary.read_text(encoding="utf-8")
    require(summary_text.count("| 25600 | 1 | pass | pass | pass | pass |")
            == 10,
            "represented gamerule campaign is not 10/10 exact")
    seed_summary = ROOT / manifest["receipts"]["seed"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in seed_summary.read_text(encoding="utf-8"),
            "seed command receipt is not exact")
    setblock_summary = ROOT / manifest["receipts"]["setblock"]
    require(setblock_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass | pass | pass |") == 3,
            "setblock command receipt is not 3/3 exact")
    setblock_destroy_summary = ROOT / manifest["receipts"][
        "setblock_destroy_stone"]
    require("| 25700 | 1 | pass | pass | pass | pass |"
            in setblock_destroy_summary.read_text(encoding="utf-8"),
            "setblock destroy-stone receipt is not exact")
    setblock_destroy_family_summary = ROOT / manifest["receipts"][
        "setblock_destroy_nontile_drop_families"]
    require("| 25700 | 1 | pass | pass | pass | pass |"
            in setblock_destroy_family_summary.read_text(encoding="utf-8"),
            "setblock destroy non-tile drop-family receipt is not exact")
    setblock_destroy_solid_summary = ROOT / manifest["receipts"][
        "setblock_destroy_solid_and_snow_families"]
    require(setblock_destroy_solid_summary.read_text(encoding="utf-8").count(
                "| 25708 | 1 | pass | pass | pass | pass |") == 2,
            "setblock destroy solid/snow receipt is not 2/2 exact")
    testforblock_summary = ROOT / manifest["receipts"]["testforblock"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in testforblock_summary.read_text(encoding="utf-8"),
            "testforblock command receipt is not exact")
    compare_summary = ROOT / manifest["receipts"]["testforblocks"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in compare_summary.read_text(encoding="utf-8"),
            "testforblocks command receipt is not exact")
    fill_summary = ROOT / manifest["receipts"]["fill"]
    require(fill_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass |") == 5,
            "fill command receipt is not 5/5 exact")
    clone_summary = ROOT / manifest["receipts"]["clone"]
    require(clone_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass |") == 5,
            "clone command receipt is not 5/5 exact")
    chat_summary = ROOT / manifest["receipts"]["literal_chat"]
    require(chat_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass | pass | pass |") == 2,
            "literal chat command receipt is not 2/2 exact")
    particle_summary = ROOT / manifest["receipts"]["particle"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in particle_summary.read_text(encoding="utf-8"),
            "particle command receipt is not exact")
    xp_summary = ROOT / manifest["receipts"]["xp"]
    require(xp_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass | pass | pass |") == 2,
            "xp command receipt is not 2/2 exact")
    xp_negative_summary = ROOT / manifest["receipts"]["xp_negative_levels"]
    require("| 25708 | 1 | pass | pass | pass | pass |"
            in xp_negative_summary.read_text(encoding="utf-8"),
            "negative xp-level command receipt is not exact")
    clear_summary = ROOT / manifest["receipts"]["clear"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in clear_summary.read_text(encoding="utf-8"),
            "clear command receipt is not exact")
    clear_registry_summary = ROOT / manifest["receipts"]["clear_registry"]
    clear_registry_text = clear_registry_summary.read_text(encoding="utf-8")
    require("clear_test" in clear_registry_text
            and "clear_all" in clear_registry_text
            and clear_registry_text.count(
                "| 25608 | 1 | pass | pass | pass | pass |") == 2,
            "clear registry test/all receipt is not 2/2 exact")
    clear_sword_summary = ROOT / manifest["receipts"]["clear_held_sword"]
    require("| 25608 | 1 | pass | pass | pass | pass |"
            in clear_sword_summary.read_text(encoding="utf-8"),
            "clear held-sword receipt is not exact")
    give_summary = ROOT / manifest["receipts"]["give"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in give_summary.read_text(encoding="utf-8"),
            "give command receipt is not exact")
    give_registry_summary = ROOT / manifest["receipts"]["give_registry"]
    give_registry_text = give_registry_summary.read_text(encoding="utf-8")
    require(give_registry_text.count(
                "| 25608 | 1 | pass | pass | pass | pass |") == 3
            and "give_sword" in give_registry_text
            and "give_pearl" in give_registry_text
            and "give_shulker" in give_registry_text,
            "give registry receipt is not 3/3 exact")
    enchant_summary = ROOT / manifest["receipts"]["enchant"]
    enchant_text = enchant_summary.read_text(encoding="utf-8")
    require(enchant_text.count(
                "| 25608 | 1 | pass | pass | pass | pass |") == 5
            and "enchant_smite" in enchant_text
            and "enchant_unbreaking" in enchant_text
            and "enchant_mending" in enchant_text
            and "enchant_vanishing" in enchant_text,
            "enchant command receipt is not 5/5 exact")
    replaceitem_summary = ROOT / manifest["receipts"]["replaceitem"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in replaceitem_summary.read_text(encoding="utf-8"),
            "replaceitem command receipt is not exact")
    replace_registry_summary = ROOT / manifest["receipts"]["replaceitem_registry"]
    replace_registry_text = replace_registry_summary.read_text(encoding="utf-8")
    require(replace_registry_text.count(
                "| 25608 | 1 | pass | pass | pass | pass |") == 3
            and "replace_hotbar" in replace_registry_text
            and "replace_armor" in replace_registry_text
            and "replace_offhand" in replace_registry_text,
            "replaceitem registry receipt is not 3/3 exact")
    replace_long_summary = ROOT / manifest["receipts"]["replaceitem_long"]
    require("| 25608 | 1 | pass | pass | pass | pass |"
            in replace_long_summary.read_text(encoding="utf-8"),
            "replaceitem long-command receipt is not exact")
    kill_summary = ROOT / manifest["receipts"]["kill"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in kill_summary.read_text(encoding="utf-8"),
            "kill command receipt is not exact")
    execute_summary = ROOT / manifest["receipts"]["execute"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in execute_summary.read_text(encoding="utf-8"),
            "execute command receipt is not exact")
    setworldspawn_summary = ROOT / manifest["receipts"]["setworldspawn"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in setworldspawn_summary.read_text(encoding="utf-8"),
            "setworldspawn command receipt is not exact")
    spawnpoint_summary = ROOT / manifest["receipts"]["spawnpoint"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in spawnpoint_summary.read_text(encoding="utf-8"),
            "spawnpoint command receipt is not exact")
    summon_summary = ROOT / manifest["receipts"]["summon"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in summon_summary.read_text(encoding="utf-8"),
            "summon command receipt is not exact")
    blockdata_summary = ROOT / manifest["receipts"]["blockdata"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in blockdata_summary.read_text(encoding="utf-8"),
            "blockdata command receipt is not exact")
    entitydata_summary = ROOT / manifest["receipts"]["entitydata"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in entitydata_summary.read_text(encoding="utf-8"),
            "entitydata command receipt is not exact")
    help_summary = ROOT / manifest["receipts"]["help"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in help_summary.read_text(encoding="utf-8"),
            "help command receipt is not exact")
    scoreboard_summary = ROOT / manifest["receipts"]["scoreboard"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in scoreboard_summary.read_text(encoding="utf-8"),
            "scoreboard command receipt is not exact")
    stats_summary = ROOT / manifest["receipts"]["stats"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in stats_summary.read_text(encoding="utf-8"),
            "stats command receipt is not exact")
    trigger_summary = ROOT / manifest["receipts"]["trigger"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in trigger_summary.read_text(encoding="utf-8"),
            "trigger command receipt is not exact")
    achievement_summary = ROOT / manifest["receipts"]["achievement"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in achievement_summary.read_text(encoding="utf-8"),
            "achievement command receipt is not exact")
    spread_summary = ROOT / manifest["receipts"]["spreadplayers"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in spread_summary.read_text(encoding="utf-8"),
            "spreadplayers command receipt is not exact")
    gamemode_summary = ROOT / manifest["receipts"]["gamemode"]
    require(gamemode_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass | pass | pass |") == 4,
            "gamemode command receipt is not 4/4 exact")
    testfor_summary = ROOT / manifest["receipts"]["testfor"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in testfor_summary.read_text(encoding="utf-8"),
            "testfor command receipt is not exact")
    packet_summary = ROOT / manifest["receipts"]["packet_controls"]
    require(packet_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass | pass | pass |") == 2,
            "packet-control command receipt is not 2/2 exact")
    title_sound_summary = ROOT / manifest["receipts"][
        "title_stopsound_breadth"]
    title_sound_text = title_sound_summary.read_text(encoding="utf-8")
    require(title_sound_text.count(
                "| 25608 | 1 | pass | pass | pass | pass |") == 7
            and "title_text" in title_sound_text
            and "title_actionbar" in title_sound_text
            and "title_times" in title_sound_text
            and "stopsound_music" in title_sound_text
            and "stopsound_one" in title_sound_text,
            "title/stopsound breadth receipt is not 7/7 exact")
    effect_summary = ROOT / manifest["receipts"]["effect"]
    effect_text = effect_summary.read_text(encoding="utf-8")
    require(effect_text.count("| 25608 | 1 | pass | pass | pass | pass |") == 4
            and "effect_fatigue" in effect_text
            and "effect_instant" in effect_text
            and "effect_luck" in effect_text,
            "effect command receipt is not 4/4 exact")
    playsound_summary = ROOT / manifest["receipts"]["playsound"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in playsound_summary.read_text(encoding="utf-8"),
            "playsound command receipt is not exact")
    tellraw_summary = ROOT / manifest["receipts"]["tellraw"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in tellraw_summary.read_text(encoding="utf-8"),
            "tellraw command receipt is not exact")
    tell_summary = ROOT / manifest["receipts"]["tell"]
    require("| 25600 | 1 | pass | pass | pass | pass |"
            in tell_summary.read_text(encoding="utf-8"),
            "tell command receipt is not exact")
    locate_summary = ROOT / manifest["receipts"]["locate"]
    require(locate_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass | pass | pass |") == 8,
            "locate command receipt is not 8/8 exact")
    server_mode_summary = ROOT / manifest["receipts"]["server_modes"]
    require(server_mode_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass | pass | pass |") == 20,
            "server-mode receipt is not 20/20 exact")
    worldborder_summary = ROOT / manifest["receipts"]["worldborder"]
    require(worldborder_summary.read_text(encoding="utf-8").count(
                "| 25600 | 1 | pass | pass | pass | pass |") == 9,
            "worldborder receipt is not 9/9 exact")
    teleport_summary = ROOT / manifest["receipts"]["teleport"]
    require(teleport_summary.read_text(encoding="utf-8").count(
                "| 25608 | 1 | pass | pass | pass | pass |") == 3,
            "tp/teleport command receipt is not 3/3 exact")
    require(manifest["continuation"]["trigger_boundary"]
            == "same_pre_tick_capsule_input"
            and manifest["continuation"]["wall_clock_input"]
            == "java_hour_minute_second_replayed_exactly",
            "command trigger is not a deterministic same-input fork")
    require(manifest["continuation"]["scheduled_callback_capsule"]
            == "exact", "pending command callback is not retained")
    registry = json.loads((HERE / "registry_manifest.json")
                          .read_text(encoding="utf-8"))
    row = next(row for row in registry["tile_entities"]
               if row["class"] == "TileEntityCommandBlock")
    require(row["status"] == "live_bounded" and row["todo"] == "MODE-02",
            "command-block registry row is not live-bounded")
    source_has(ROOT / "magma/trace/state_capsule.py", (
        '"tile_entities.command_block_bounded_commands": "exact"',
        '"type": "set_command_block_state"'))
    source_has(ROOT / "magma/game/runtime.c", (
        "gm_runtime_command_block_set_state",
        "gm_runtime_command_block_trigger_at_clock", "#itzlipofutzli",
        "commands.seed.success",
        "runtime_tick_command_block", "runtime_command_block_propagate"))
    source_has(ROOT / "magma/game/test_command_block_tick.c", (
        "first unconditional impulse callback",
        "repeating callback schedules itself and its chain successor"))
    source_has(ROOT / "magma/game/test_runtime.c", (
        "bounded Searge command tile", "Searge execution preserves"))
    source_has(ROOT / "magma/trace/run_oracle_matrix.py", (
        "redstone_comparator_saved_command_block_searge_seed_0",
        "redstone_comparator_saved_command_block_time_day_seed_0",
        "redstone_comparator_saved_command_block_gamerule_",
        "redstone_command_random_tick.blocks",
        "redstone_comparator_saved_command_block_toggledownfall_seed_0",
        "redstone_comparator_saved_command_block_seed_seed_0",
        "redstone_comparator_saved_command_block_setblock_seed_0",
        "redstone_comparator_saved_command_block_setblock_default_seed_0",
        "redstone_comparator_saved_command_block_setblock_keep_seed_0",
        "redstone_comparator_saved_command_block_setblock_destroy_seed_0",
        "redstone_comparator_saved_command_block_setblock_destroy_torch_seed_0",
        "redstone_comparator_saved_command_block_setblock_destroy_gravel_seed_0",
        "redstone_comparator_saved_command_block_setblock_destroy_snow_seed_0",
        "redstone_comparator_saved_command_block_testforblock_seed_0",
        "redstone_comparator_saved_command_block_compare_seed_0",
        "redstone_comparator_saved_command_block_fill_seed_0",
        "redstone_comparator_saved_command_block_fill_keep_seed_0",
        "redstone_comparator_saved_command_block_fill_default_seed_0",
        "redstone_comparator_saved_command_block_fill_outline_seed_0",
        "redstone_comparator_saved_command_block_fill_hollow_seed_0",
        "redstone_comparator_saved_command_block_clone_seed_0",
        "redstone_comparator_saved_command_block_clone_masked_seed_0",
        "redstone_comparator_saved_command_block_clone_force_seed_0",
        "redstone_comparator_saved_command_block_clone_move_seed_0",
        "redstone_comparator_saved_command_block_clone_filtered_seed_0",
        "redstone_comparator_saved_command_block_say_seed_0",
        "redstone_comparator_saved_command_block_me_seed_0",
        "redstone_comparator_saved_command_block_particle_seed_0",
        "redstone_comparator_saved_command_block_xp_points_seed_0",
        "redstone_comparator_saved_command_block_xp_levels_seed_0",
        "redstone_comparator_saved_command_block_xp_negative_levels_seed_0",
        "redstone_comparator_saved_command_block_clear_seed_0",
        "redstone_comparator_saved_command_block_kill_player_seed_0",
        "redstone_comparator_saved_command_block_execute_setblock_seed_0",
        "redstone_comparator_saved_command_block_setworldspawn_seed_0",
        "redstone_comparator_saved_command_block_spawnpoint_seed_0",
        "redstone_comparator_saved_command_block_setworldspawn_arbitrary_seed_0",
        "redstone_comparator_saved_command_block_spawnpoint_relative_seed_0",
        "redstone_comparator_saved_command_block_summon_lightning_seed_0",
        "redstone_comparator_saved_command_block_blockdata_comparator_seed_0",
        "redstone_comparator_saved_command_block_entitydata_item_frame_seed_0",
        "redstone_comparator_saved_command_block_help_seed_0",
        "redstone_comparator_saved_command_block_scoreboard_list_empty_seed_0",
        "redstone_comparator_saved_command_block_stats_clear_seed_0",
        "redstone_comparator_saved_command_block_execute_trigger_seed_0",
        "redstone_comparator_saved_command_block_achievement_open_inventory_seed_0",
        "redstone_comparator_saved_command_block_spreadplayers_seed_0",
        "redstone_comparator_saved_command_block_gamemode_seed_0",
        "redstone_comparator_saved_command_block_gamemode_creative_seed_0",
        "redstone_comparator_saved_command_block_gamemode_adventure_seed_0",
        "redstone_comparator_saved_command_block_gamemode_spectator_seed_0",
        "redstone_comparator_saved_command_block_testfor_seed_0",
        "redstone_comparator_saved_command_block_title_clear_seed_0",
        "redstone_comparator_saved_command_block_title_text_seed_0",
        "redstone_comparator_saved_command_block_title_actionbar_seed_0",
        "redstone_comparator_saved_command_block_title_times_seed_0",
        "redstone_comparator_saved_command_block_stopsound_seed_0",
        "redstone_comparator_saved_command_block_stopsound_music_seed_0",
        "redstone_comparator_saved_command_block_stopsound_one_seed_0",
        "redstone_comparator_saved_command_block_effect_seed_0",
        "redstone_comparator_saved_command_block_effect_fatigue_seed_0",
        "redstone_comparator_saved_command_block_effect_instant_seed_0",
        "redstone_comparator_saved_command_block_effect_luck_seed_0",
        "redstone_comparator_saved_command_block_give_sword_seed_0",
        "redstone_comparator_saved_command_block_give_pearl_seed_0",
        "redstone_comparator_saved_command_block_give_shulker_seed_0",
        "redstone_comparator_saved_command_block_clear_sword_seed_0",
        "redstone_comparator_saved_command_block_clear_test_seed_0",
        "redstone_comparator_saved_command_block_clear_all_seed_0",
        "redstone_comparator_saved_command_block_replace_long_seed_0",
        "redstone_comparator_saved_command_block_replace_hotbar_seed_0",
        "redstone_comparator_saved_command_block_replace_armor_seed_0",
        "redstone_comparator_saved_command_block_replace_offhand_seed_0",
        "redstone_comparator_saved_command_block_enchant_sharpness_seed_0",
        "redstone_comparator_saved_command_block_enchant_smite_seed_0",
        "redstone_comparator_saved_command_block_enchant_unbreaking_seed_0",
        "redstone_comparator_saved_command_block_enchant_mending_seed_0",
        "redstone_comparator_saved_command_block_enchant_vanishing_seed_0",
        "redstone_comparator_saved_command_block_playsound_seed_0",
        "redstone_comparator_saved_command_block_tellraw_seed_0",
        "redstone_comparator_saved_command_block_tell_seed_0",
        "redstone_comparator_saved_command_block_locate_seed_0",
        "redstone_comparator_saved_command_block_locate_village_seed_0",
        "redstone_comparator_saved_command_block_locate_temple_seed_0",
        "redstone_comparator_saved_command_block_locate_mineshaft_seed_0",
        "redstone_comparator_saved_command_block_locate_mansion_seed_0",
        "redstone_comparator_saved_command_block_locate_monument_seed_0",
        "redstone_comparator_saved_command_block_locate_fortress_seed_0",
        "redstone_comparator_saved_command_block_locate_endcity_seed_0",
        'f"redstone_comparator_saved_command_block_{name}_seed_0"',
        '("difficulty_peaceful", "difficulty peaceful")',
        '("difficulty_easy", "difficulty easy")',
        '("difficulty_normal", "difficulty normal")',
        '("difficulty_hard", "difficulty hard")',
        '("difficulty_p", "difficulty p")',
        '("difficulty_e", "difficulty e")',
        '("difficulty_n", "difficulty n")',
        '("difficulty_h", "difficulty h")',
        '("difficulty_0", "difficulty 0")',
        '("difficulty_1", "difficulty 1")',
        '("difficulty_2", "difficulty 2")',
        '("difficulty_3", "difficulty 3")',
        '("defaultgamemode_survival", "defaultgamemode survival")',
        '("defaultgamemode_creative", "defaultgamemode creative")',
        '("defaultgamemode_adventure", "defaultgamemode adventure")',
        '("defaultgamemode_spectator", "defaultgamemode spectator")',
        'f"redstone_comparator_saved_command_block_{name}_seed_0"',
        'for name in ("tp", "teleport")',
        'for query in ("daytime", "day", "gametime")',
        'for mode in ("clear", "rain", "thunder")',
        'f"{mode}_default_seed_0"',
        "mixed_command_sign_support_teardown_seed_0",
        "command_searge_trigger=True"))
    source_has(ROOT / "magma/trace/trace_java.py", (
        "deferred_command_fixture", "command_trigger_clock_out"))
    source_has(ROOT / "magma/trace/trace_runtime.py", (
        "trigger_command_block_from_capsule", "command_trigger_clock"))
    source_has(ROOT / "java/Minecraft/src/main/java/qrl/Recorder.java", (
        "Exact bounded command-block state", "logic.trigger(world)"))
    print("PASS command block: Searge, complete time, default/explicit weather, "
          "represented gamerules, toggledownfall, seed, setblock replace/keep, "
          "testforblock positive, "
          "testforblocks positive, fill replace/keep/shells, all non-tile clone modes, "
          "literal say/me, bounded particle, xp points/levels, clear/give, "
          "full-registry enchant with exact applicability/compatibility, hotbar replaceitem, kill/death lifecycle, "
          "player-context nested execute/setblock, "
          "persisted setworldspawn, "
          "persisted singleton spawnpoint, "
          "bounded summon lightning with exact weather state/fire, "
          "comparator blockdata NBT merge/recompute, "
          "item-frame entitydata NBT merge/rotation, "
          "seeded help RNG/text, "
          "empty scoreboard objective query, "
          "empty player command-stat clear, "
          "saved trigger score add/lock, "
          "saved achievement award, "
          "seeded spreadplayers, "
          "all four gamemodes, singleton testfor, title clear, stopsound, "
          "full-registry singleton effect, playsound, literal tellraw/tell, all eight structure locators, "
          "all saved difficulties by name/short/number and defaultgamemodes, stationary worldborder, "
          "idempotent tp/teleport, "
          "same-input pre-tick fork, exact external clock/output, comparator, "
          "loaded order, capsule, checkpoint, and direct Java parity")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, StopIteration, ValueError, RuntimeError) as error:
        print(f"FAIL command block: {error}")
        raise SystemExit(1)
