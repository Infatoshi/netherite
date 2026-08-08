#!/usr/bin/env python3
"""Create and validate neutral Java-vs-magma pre-tick state capsules.

The capsule is deliberately separate from blaze's ``.bsnp`` training ABI.  A
capsule is an oracle fixture, not a resumable training environment:

* ``manifest.json`` contains a versioned canonical state vector and an explicit
  capability ledger.
* ``blocks.u16le`` contains an inclusive cuboid in y/z/x order, one little-
  endian ``block_id << 4 | metadata`` value per cell.
* optional ``sky_light.u8`` contains the saved Chunk SkyLight nibble value for
  the same cells and order, expanded to one validated byte per cell.
* optional ``*.nbt`` sidecars retain complete player-profile and shulker
  ItemStack compounds, and ``map_colors_item_frame_*.u8`` sidecars retain
  exact 128x128 filled-map planes, without forcing large or nested values
  through the script parser.
* every payload is length- and SHA-256-checked before it can be emitted as
  magma script events.

Version 2 is intentionally incomplete.  It restores the fields named ``exact``
in CAPABILITIES_V2 and refuses ``--require-complete`` while general entities,
scheduled ticks, tile entities, and RNG cursors remain unsupported.  This
prevents a partial checkpoint from being mistaken for a full save/reload proof.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import copy
import functools
import hashlib
import json
import math
import pathlib
import re
import shutil
import struct
import sys
import tempfile
import uuid

import nbt_codec


SCHEMA = "netherite.state_capsule"
VERSION = 2
BLOCK_FILE = "blocks.u16le"
SKY_LIGHT_FILE = "sky_light.u8"
BLOCK_LIGHT_FILE = "block_light.u8"
MANIFEST_FILE = "manifest.json"
BLOCK_ENCODING = "u16le:id<<4|meta:y-z-x"
SKY_LIGHT_ENCODING = "u8:nibble:y-z-x"
BLOCK_LIGHT_ENCODING = "u8:nibble:y-z-x"
NBT_ENCODING = "minecraft:nbt-uncompressed-root-compound"
MAP_COLORS_ENCODING = "u8:map-colors:128x128"
MAX_NBT_PAYLOAD_TOTAL = 16 << 20
SPAWNER_ENTITY_TYPES = {
    "minecraft:zombie": 2,
    "minecraft:skeleton": 3,
    "minecraft:creeper": 4,
    "minecraft:spider": 5,
    "minecraft:enderman": 6,
    "minecraft:blaze": 7,
    "minecraft:sheep": 10,
    "minecraft:pig": 11,
    "minecraft:cow": 12,
    "minecraft:chicken": 13,
    "minecraft:squid": 14,
    "minecraft:zombie_pigman": 15,
    "minecraft:wolf": 16,
    "minecraft:ocelot": 17,
    "minecraft:villager": 40,
    "minecraft:witch": 23,
    "minecraft:bat": 24,
    "minecraft:llama": 25,
    "minecraft:ghast": 26,
    "minecraft:magma_cube": 27,
    "minecraft:wither_skeleton": 32,
    "minecraft:slime": 35,
    "minecraft:silverfish": 36,
    "minecraft:cave_spider": 39,
    "minecraft:zombie_villager": 41,
    "minecraft:vindication_illager": 51,
    "minecraft:evocation_illager": 52,
    "minecraft:vex": 53,
    "minecraft:guardian": 55,
    "minecraft:elder_guardian": 56,
    "minecraft:villager_golem": 57,
    "minecraft:stray": 58,
    "minecraft:husk": 59,
    "minecraft:mooshroom": 60,
    "minecraft:rabbit": 61,
    "minecraft:polar_bear": 62,
    "minecraft:endermite": 63,
    "minecraft:snowman": 64,
    "minecraft:giant": 65,
    "minecraft:horse": 68,
    "minecraft:donkey": 69,
    "minecraft:mule": 70,
    "minecraft:skeleton_horse": 71,
    "minecraft:zombie_horse": 72,
}
BLOCKSTATE_PROPS_FILE = pathlib.Path(__file__).with_name(
    "blockstate_props_1_11_2.json"
)
BLOCKSTATE_PROPS_SHA256 = (
    "57911f7d3a42bd1e585d3cca75283fe500fbfcb2308c3f15127837a2c56433f6"
)

# "exact" means the v2 magma emitter actively restores the field.  The other
# values are first-class contract states, not comments: validation and
# --require-complete consume them.
CAPABILITIES_V2 = {
    "player.pose_motion": "exact",
    "player.health_food": "exact",
    "player.saturation": "exact",
    "player.food_hidden": "exact",
    "player.main_inventory.enchantment_subset": "exact",
    "player.inventory.anvil_metadata_subset": "exact",
    "player.ender_inventory": "exact",
    "player.selected_slot": "exact",
    "world.block_cuboid": "exact",
    "world.light.sky_nibbles": "captured_only",
    "world.light.block_nibbles": "captured_only",
    "world.dimension": "exact",
    "world.time": "exact",
    "world.gamerule.doMobSpawning": "exact",
    "world.weather_clock_isolated": "exact",
    "world.weather_chunk_side_effects": "captured_only",
    "world.weather_steady_rain_fire_slice": "exact",
    "world.weather_steady_thunder_fire_slice": "exact",
    "world.villages.saved_state": "exact",
    "world.villages.transient_task_ai": "captured_only",
    "player.air": "exact",
    "player.fire": "exact",
    "client.move_packet_cursor": "exact",
    "player.xp": "exact",
    "player.combat_timers": "exact",
    "player.potions": "exact",
    "player.armor_offhand.enchantment_subset": "exact",
    "player.inventory_arbitrary_nbt": "unavailable",
    "entities": "captured_only",
    # The fork oracle splits this process-global counter from client-only
    # constructors at the parked boundary, then magma restores the server
    # continuation cursor.
    "entities.next_id": "exact",
    "entities.no_ai_pig": "exact",
    "entities.no_ai_plain_living": "exact",
    "entities.no_ai_unopened_villager": "exact",
    "entities.no_ai_initialized_villager_economy": "exact",
    "entities.active_fresh_nbt_villager": "exact",
    "entities.no_ai_tameable": "exact",
    "entities.no_ai_horse_family": "exact",
    "entities.no_ai_llama": "exact",
    "entities.llama_spit": "exact",
    "entities.no_ai_shulker": "exact",
    "entities.player_targeted_shulker_bullet": "exact",
    "entities.xp_orb": "exact",
    "entities.default_thrown_potion": "exact",
    "entities.default_area_effect_cloud": "exact",
    "entities.custom_thrown_potion_payload": "exact",
    "entities.custom_area_effect_cloud_payload": "exact",
    "entities.area_effect_cloud_extended_state": "exact",
    "entities.area_effect_cloud_reapplication_graph": "exact",
    "entities.area_effect_cloud_identity_owner": "exact",
    "entities.area_effect_cloud_common_entity_state": "exact",
    "entities.player_normal_arrow": "exact",
    "entities.player_tipped_spectral_arrow": "exact",
    "entities.proof_fenced_plain_item_merge": "exact",
    "entities.proof_fenced_plain_item_environment": "exact",
    "entities.item_frame_comparator_source": "exact",
    "entities.hidden_state": "unavailable",
    "tile_entities": "unavailable",
    "tile_entities.comparator_output": "exact",
    "tile_entities.flower_pot": "exact",
    "tile_entities.note_block": "exact",
    "tile_entities.skull_ownerless": "exact",
    "tile_entities.skull_player_profile": "exact",
    "tile_entities.sign_persistent_nbt": "exact",
    "tile_entities.banner_persistent_nbt": "exact",
    "entities.banner_item_nbt": "exact",
    "tile_entities.moving_piston": "exact",
    "tile_entities.single_chest_inventory": "exact",
    "tile_entities.single_trapped_chest_inventory": "exact",
    "tile_entities.double_trapped_chest_inventory": "exact",
    "tile_entities.double_chest_inventory": "exact",
    "tile_entities.chest_transient_animation": "exact",
    "tile_entities.furnace_inventory": "exact",
    "tile_entities.brewing_stand_inventory": "exact",
    "tile_entities.dispenser_dropper_inventory": "exact",
    "tile_entities.shulker_box_plain_inventory": "exact",
    "tile_entities.shulker_box_transient_animation": "exact",
    "entities.shulker_box_plain_item_payload": "exact",
    "tile_entities.shulker_box_persistent_nbt": "exact",
    "entities.shulker_box_item_nbt": "exact",
    "tile_entities.jukebox_record": "exact",
    "tile_entities.command_block_bounded_commands": "exact",
    "tile_entities.mob_spawner.default_spawn_data": "exact",
    "world.scheduled_ticks": "captured_only",
    "world.scheduled_ticks.inert_stone_in_cuboid": "exact",
    "world.scheduled_ticks.water_on_flat_stone_plane": "exact",
    "world.scheduled_ticks.lava_source_on_flat_stone_plane": "exact",
    "world.scheduled_ticks.lava_down_into_enclosed_water": "exact",
    "world.scheduled_ticks.falling_sand_clear_column": "exact",
    "world.scheduled_ticks.falling_gravel_clear_column": "exact",
    "world.scheduled_ticks.falling_failed_placement_drop": "exact",
    "world.scheduled_ticks.dragon_egg_scheduled_fall": "exact",
    "world.scheduled_ticks.anvil_supported_callback": "exact",
    "world.scheduled_ticks.fire_dry_nonhumid_normal": "exact",
    "world.scheduled_ticks.fire_dry_humid_normal": "exact",
    "world.scheduled_ticks.fire_do_tick_disabled": "exact",
    "world.scheduled_ticks.fire_rain_age15_exposed": "exact",
    "world.scheduled_ticks.fire_rain_direct_target": "exact",
    "world.scheduled_ticks.redstone_lamp_callback_proof_region": "exact",
    "world.scheduled_ticks.stone_button_floor_lamp_release": "exact",
    "world.scheduled_ticks.wooden_button_unoccupied_release": "exact",
    "world.scheduled_ticks.stone_pressure_plate_unoccupied_release": "exact",
    "world.scheduled_ticks.weighted_pressure_plate_unoccupied_release": "exact",
    "world.scheduled_ticks.redstone_torch_floor_inverter": "exact",
    "world.scheduled_ticks.redstone_torch_wall_inverter": "exact",
    "world.scheduled_ticks.redstone_repeater_proof_region": "exact",
    "world.scheduled_ticks.redstone_comparator_proof_region": "exact",
    "world.scheduled_ticks.redstone_observer_proof_region": "exact",
    "world.scheduled_ticks.tripwire_hook_proof_region": "exact",
    "world.scheduled_ticks.tripwire_wire_lone_powered": "exact",
    "world.redstone_torch_toggle_history": "exact",
    "world.rng.java_random_seed48": "exact",
    "world.rng.math_random_seed48": "exact",
    "world.rng.block_random_seed48": "exact",
    "world.rng.update_lcg": "exact",
    "world.rng_cursors": "captured_only",
}


def entity_payload_is_restorable(value: dict[str, Any]) -> bool:
    """Return whether ``emit_magma`` reconstructs this complete entity row.

    Keep the capability predicate public and side-effect free so the Anvil
    importer can distinguish an exact active entity from a persisted entity
    that would otherwise be silently omitted. Validation remains responsible
    for proving that a row carrying one of these markers is internally valid.
    """
    entity_type = value.get("type")
    return (
        value.get("no_ai_plain_exact") is True
        or (entity_type == "EntityBat"
            and value.get("bat_active_exact") is True)
        or (entity_type == "EntitySquid"
            and value.get("squid_active_exact") is True)
        or (entity_type == "EntityPig"
            and value.get("no_ai_pig_exact") is True)
        or (entity_type == "EntityVillager"
            and value.get("villager_exact") is True)
        or (entity_type in ("EntityWolf", "EntityOcelot")
            and value.get("tameable_exact") is True)
        or (entity_type in {
                "EntityHorse", "EntityDonkey", "EntityMule",
                "EntitySkeletonHorse", "EntityZombieHorse", "EntityLlama"}
            and value.get("horse_exact") is True)
        or (entity_type == "EntityArmorStand"
            and value.get("armor_stand_exact") is True)
        or (entity_type == "EntityShulker"
            and value.get("shulker_exact") is True)
        or (entity_type == "EntityShulkerBullet"
            and value.get("shulker_bullet_exact") is True)
        or (entity_type == "EntityWither"
            and value.get("wither_exact") is True)
        or (entity_type == "EntityWitherSkull"
            and value.get("wither_skull_exact") is True)
        or (entity_type == "EntityLlamaSpit"
            and value.get("llama_spit_exact") is True)
        or (entity_type == "EntityXPOrb"
            and value.get("xp_box_exact") is True)
        or (entity_type == "EntityItem"
            and value.get("item_exact") is True)
        or (entity_type in {"EntityTippedArrow", "EntitySpectralArrow"}
            and value.get("arrow_exact") is True)
        or (entity_type == "EntityPotion"
            and value.get("potion_exact") is True)
        or (entity_type in {
                "EntityEgg", "EntitySnowball", "EntityExpBottle",
                "EntityEnderPearl", "EntityPotion"}
            and value.get("throwable_exact") is True)
        or (entity_type == "EntityAreaEffectCloud"
            and value.get("cloud_exact") is True)
        or (entity_type == "EntityFireworkRocket"
            and value.get("firework_exact") is True)
        or (entity_type == "EntityFallingBlock"
            and value.get("falling_exact") is True)
        or (entity_type == "EntityTNTPrimed"
            and value.get("primed_tnt_exact") is True)
        or (entity_type == "EntityEnderCrystal"
            and value.get("end_crystal_exact") is True)
        or entity_type in {
            "EntityMinecartEmpty", "EntityMinecartChest",
            "EntityMinecartFurnace", "EntityMinecartTNT",
            "EntityMinecartMobSpawner", "EntityMinecartHopper",
            "EntityFishHook",
        }
    )


def entity_payload_is_living_restorable(value: dict[str, Any]) -> bool:
    """Return whether a cloud deadline target restores as living state."""
    entity_type = value.get("type")
    return (
        value.get("no_ai_plain_exact") is True
        or (entity_type == "EntityBat"
            and value.get("bat_active_exact") is True)
        or (entity_type == "EntitySquid"
            and value.get("squid_active_exact") is True)
        or (entity_type == "EntityPig"
            and value.get("no_ai_pig_exact") is True)
        or (entity_type == "EntityVillager"
            and value.get("villager_exact") is True)
        or (entity_type in ("EntityWolf", "EntityOcelot")
            and value.get("tameable_exact") is True)
        or (entity_type in {
                "EntityHorse", "EntityDonkey", "EntityMule",
                "EntitySkeletonHorse", "EntityZombieHorse", "EntityLlama"}
            and value.get("horse_exact") is True)
        or (entity_type == "EntityArmorStand"
            and value.get("armor_stand_exact") is True)
        or (entity_type == "EntityShulker"
            and value.get("shulker_exact") is True)
        or (entity_type == "EntityWither"
            and value.get("wither_exact") is True)
    )


class CapsuleError(ValueError):
    """A capsule violates the on-disk contract."""


def _validate_no_ai_base(
        entity: dict, label: str, *, maximum_health: float,
        require_no_ai: bool = True,
        exact_field: str = "no_ai_base_exact",
        minimum_ticks: int = 0,
        maximum_ticks: int = 2147483647,
        allow_detached_box_y: bool = False,
        allow_dead: bool = False) -> None:
    for field in (
            "air", "fire", "ticks_existed", "base_living_sound_time",
            "base_entity_seed48"):
        value = entity.get(field)
        if isinstance(value, bool) or not isinstance(value, int):
            raise CapsuleError(f"{label}.{field} must be an integer")
    for field in (
            "on_ground", "in_water", "base_entity_have_gaussian",
            "mob_potions_empty", "mob_equipment_empty"):
        if entity.get(field) not in (0, 1, False, True):
            raise CapsuleError(f"{label}.{field} must be boolean")
    for field in (
            "fall_distance", "base_last_damage", "base_entity_gaussian",
            "max_health", "absorption", "base_box_min_x",
            "base_box_min_y", "base_box_min_z", "base_box_max_x",
            "base_box_max_y", "base_box_max_z"):
        _finite_number(entity.get(field), f"{label}.{field}")
    effects = entity.get("mob_effects", [])
    if not isinstance(effects, list) or len(effects) > 16:
        raise CapsuleError(f"{label}.mob_effects must contain at most 16 effects")
    seen_effects = set()
    health_boost = 0.0
    for effect_index, effect in enumerate(effects):
        effect_label = f"{label}.mob_effects[{effect_index}]"
        if not isinstance(effect, dict) or set(effect) != {
                "id", "amp", "dur", "ambient", "show_particles"}:
            raise CapsuleError(f"{effect_label} is invalid")
        if any(isinstance(effect[field], bool)
               or not isinstance(effect[field], int)
               for field in ("id", "amp", "dur")) \
                or not 1 <= effect["id"] <= 27 \
                or not 0 <= effect["amp"] <= 255 \
                or not 1 <= effect["dur"] <= 2147483647 \
                or effect["ambient"] not in (False, True) \
                or effect["show_particles"] not in (False, True) \
                or effect["id"] in seen_effects:
            raise CapsuleError(f"{effect_label} is invalid or duplicate")
        seen_effects.add(effect["id"])
        if effect["id"] == 21:
            health_boost += 4.0 * (effect["amp"] + 1)
    represented_maximum = maximum_health + health_boost
    if bool(entity.get("no_ai")) != require_no_ai \
            or entity.get(exact_field) is not True \
            or not ((allow_dead and float(entity["health"]) == 0.0)
                    or 0 < float(entity["health"]) <= represented_maximum) \
            or abs(float(entity["max_health"])
                   - represented_maximum) > 1e-6 \
            or float(entity["absorption"]) < 0.0 \
            or bool(entity["mob_potions_empty"]) != (len(effects) == 0) \
            or entity["mob_equipment_empty"] is not True \
            or not -20 <= entity["air"] <= 300 \
            or not -20 <= entity["fire"] <= 32767 \
            or float(entity["fall_distance"]) < 0.0 \
            or not minimum_ticks <= entity["ticks_existed"] \
                <= maximum_ticks \
            or not -1000000 <= entity["base_living_sound_time"] <= 1000000 \
            or not 0.0 <= float(entity["base_last_damage"]) <= 1000000.0 \
            or not 0 <= entity["base_entity_seed48"] < (1 << 48) \
            or abs(float(entity["pitch"])) > 1e-12:
        raise CapsuleError(f"{label} has invalid exact living base state")
    if entity["base_box_min_x"] > entity["base_box_max_x"] \
            or entity["base_box_min_y"] > entity["base_box_max_y"] \
            or entity["base_box_min_z"] > entity["base_box_max_z"] \
            or (entity["base_box_min_x"] + entity["base_box_max_x"]) \
                * 0.5 != entity["x"] \
            or (not allow_detached_box_y
                and entity["base_box_min_y"] != entity["y"]) \
            or (entity["base_box_min_z"] + entity["base_box_max_z"]) \
                * 0.5 != entity["z"]:
        raise CapsuleError(f"{label} has an invalid exact bounding box")


def _validate_villager_stack(
        stack: object, label: str, *, allow_empty: bool) -> None:
    if not isinstance(stack, dict):
        raise CapsuleError(f"{label} must be an object")
    for field in ("id", "count", "meta"):
        value = stack.get(field)
        if isinstance(value, bool) or not isinstance(value, int):
            raise CapsuleError(f"{label}.{field} must be an integer")
    item_id = stack["id"]
    count = stack["count"]
    meta = stack["meta"]
    empty = item_id == 0 and count == 0
    if (not allow_empty and empty) \
            or (empty and meta != 0) \
            or (not empty and (not 1 <= item_id <= 4095
                               or not 1 <= count <= 64
                               or not 0 <= meta <= 32767)) \
            or ((item_id == 0) != (count == 0)):
        raise CapsuleError(f"{label} has an invalid stack")
    enchants = stack.get("enchants")
    if not isinstance(enchants, list) or len(enchants) > 8:
        raise CapsuleError(f"{label}.enchants must contain at most 8 pairs")
    for index, enchantment in enumerate(enchants):
        if not isinstance(enchantment, list) or len(enchantment) != 2 \
                or any(isinstance(value, bool) or not isinstance(value, int)
                       for value in enchantment) \
                or not 0 <= enchantment[0] <= 32767 \
                or not 1 <= enchantment[1] <= 32767:
            raise CapsuleError(
                f"{label}.enchants[{index}] is not an id/level pair"
            )
    payload = _validate_item_stack_payload(
        stack.get("stack_payload"), f"{label}.stack_payload")
    if (stack.get("repair_cost", 0) != 0
            or stack.get("custom_name", "") != "") and payload is None:
        raise CapsuleError(f"{label} has unsupported merchant ItemStack NBT")
    if stack.get("nbt_subset_exact", True) is not True and payload is None:
        raise CapsuleError(f"{label} has unsupported merchant ItemStack NBT")


def _validate_horse_inventory(
        entity: dict, label: str, *, entity_type: str) -> None:
    inventory = entity.get("horse_inventory")
    if not isinstance(inventory, list) or len(inventory) > 17:
        raise CapsuleError(f"{label}.horse_inventory must be an array")
    chested = bool(entity["horse_chested"])
    inventory_size = (
        2 + 3 * int(entity["llama_strength"])
        if entity_type == "EntityLlama" and chested
        else 17 if chested else 2
    )
    seen_slots = set()
    for index, stack in enumerate(inventory):
        stack_label = f"{label}.horse_inventory[{index}]"
        _validate_villager_stack(stack, stack_label, allow_empty=False)
        slot = stack.get("slot")
        if isinstance(slot, bool) or not isinstance(slot, int) \
                or not 0 <= slot < inventory_size or slot in seen_slots:
            raise CapsuleError(f"{stack_label}.slot is invalid or duplicate")
        seen_slots.add(slot)
        if slot == 0 and (entity_type == "EntityLlama"
                          or stack["id"] != 329):
            raise CapsuleError(f"{stack_label} is not a saddle")
        if slot == 1 and (
                entity_type == "EntityLlama"
                and (stack["id"] != 171
                     or not 0 <= stack["meta"] <= 15)
                or entity_type != "EntityLlama"
                and (entity_type != "EntityHorse"
                     or stack["id"] not in (417, 418, 419))):
            raise CapsuleError(f"{stack_label} is not legal horse armor")
    saddle_present = any(stack["slot"] == 0 for stack in inventory)
    armor_stack = next(
        (stack for stack in inventory if stack["slot"] == 1), None)
    armor = (0 if armor_stack is None or entity_type == "EntityLlama"
             else armor_stack["id"] - 416)
    if saddle_present != bool(entity["horse_saddled"]) \
            or armor != entity["horse_armor"]:
        raise CapsuleError(f"{label} horse equipment flags are inconsistent")
    if entity_type == "EntityLlama":
        decor = -1 if armor_stack is None else armor_stack["meta"]
        if decor != entity["llama_decor"]:
            raise CapsuleError(
                f"{label} llama decor and inventory are inconsistent")


def _validate_armor_stand_equipment(entity: dict, label: str) -> None:
    equipment = entity.get("armor_stand_equipment")
    if not isinstance(equipment, list) or len(equipment) > 6:
        raise CapsuleError(f"{label}.armor_stand_equipment must be an array")
    seen_slots = set()
    for index, stack in enumerate(equipment):
        stack_label = f"{label}.armor_stand_equipment[{index}]"
        _validate_villager_stack(stack, stack_label, allow_empty=False)
        slot = stack.get("slot")
        if isinstance(slot, bool) or not isinstance(slot, int) \
                or not 0 <= slot < 6 or slot in seen_slots:
            raise CapsuleError(f"{stack_label}.slot is invalid or duplicate")
        seen_slots.add(slot)


def _validate_item_stack_payload(value: object, label: str) -> bytes | None:
    """Return a lossless root-compound ItemStack tag, or None for no tag."""
    if value is None:
        return None
    if not isinstance(value, dict) or set(value) != {"kind", "nbt"} \
            or value.get("kind") != "item_tag":
        raise CapsuleError(f"{label} must be an item_tag payload")
    encoded = value.get("nbt")
    try:
        if isinstance(encoded, str):
            raw = bytes.fromhex(encoded)
            document = nbt_codec.decode(raw)
        elif isinstance(encoded, dict):
            raw = nbt_codec.encode(encoded)
            document = nbt_codec.decode(raw)
        else:
            raise nbt_codec.NbtError(
                "nbt must be hexadecimal or canonical typed NBT")
    except (ValueError, nbt_codec.NbtError) as exc:
        raise CapsuleError(f"{label} is invalid NBT: {exc}") from exc
    if document.get("name") != "" \
            or document.get("tag", {}).get("type") != "compound":
        raise CapsuleError(
            f"{label} must be an empty-name root TAG_Compound")
    # Compound member order is not part of NBTTagCompound equality.  Store a
    # canonical encoding so two semantically equal tags intern to the same
    # native handle and therefore retain vanilla split/merge behavior.
    return nbt_codec.encode(document)


def _validate_root_nbt_payload(value: object, label: str) -> bytes:
    """Return one validated empty-name root compound without byte reordering."""
    try:
        if isinstance(value, str):
            raw = bytes.fromhex(value)
            document = nbt_codec.decode(raw)
        elif isinstance(value, dict):
            document = value
            raw = nbt_codec.encode(document)
        else:
            raise nbt_codec.NbtError(
                "payload must be hexadecimal or canonical typed NBT")
        # Encoded compound member order is observable in a byte-exact save
        # continuation even though NBTTagCompound equality ignores it. Hex
        # captures therefore remain byte-preserving; typed author input gets
        # the encoder's deterministic order.
        document = nbt_codec.decode(raw)
    except (ValueError, nbt_codec.NbtError) as exc:
        raise CapsuleError(f"{label} is invalid NBT: {exc}") from exc
    if document.get("name") != "" \
            or document.get("tag", {}).get("type") != "compound":
        raise CapsuleError(
            f"{label} must be an empty-name root TAG_Compound")
    return raw


def _spawner_nbt_identity(value: object, label: str) \
        -> tuple[bytes, str, bool]:
    raw = _validate_root_nbt_payload(value, label)
    compound = nbt_codec.decode(raw)["tag"]["value"]
    identity = compound.get("id")
    if not isinstance(identity, dict) \
            or identity.get("type") != "string" \
            or not isinstance(identity.get("value"), str):
        raise CapsuleError(f"{label} is missing string id")
    entity_id = identity["value"]
    if ":" not in entity_id:
        entity_id = "minecraft:" + entity_id.lower()
    supported = {
        "id", "Pos", "Motion", "Rotation", "FallDistance", "Fire",
        "Air", "OnGround", "PortalCooldown",
        "AbsorptionAmount", "Health", "HurtTime", "DeathTime",
        "CanPickUpLoot", "PersistenceRequired", "LeftHanded", "NoAI",
        "Age", "ForcedAge", "InLove", "Saddle",
    }
    unsupported = sorted(set(compound) - supported)
    if unsupported:
        raise CapsuleError(
            f"{label} has unsupported represented tags: {unsupported}")
    if "Saddle" in compound and entity_id != "minecraft:pig":
        raise CapsuleError(f"{label}.Saddle is only represented for pigs")
    return raw, entity_id, set(compound) == {"id"}


def _validate_map_colors_b64(value: object, label: str) -> bytes:
    if not isinstance(value, str):
        raise CapsuleError(f"{label} must be canonical base64")
    try:
        raw = base64.b64decode(value, validate=True)
    except (ValueError, binascii.Error) as exc:
        raise CapsuleError(f"{label} is invalid base64") from exc
    if len(raw) not in (0, 128 * 128):
        raise CapsuleError(f"{label} must contain exactly 16384 bytes")
    if any(color > 143 for color in raw):
        raise CapsuleError(f"{label} contains an undefined 1.11.2 MapColor")
    if base64.b64encode(raw).decode("ascii") != value:
        raise CapsuleError(f"{label} is not canonical base64")
    return raw


def _validate_game_profile_nbt(value: object, label: str) -> bytes:
    if not isinstance(value, str):
        raise CapsuleError(f"{label} must be hexadecimal NBT")
    try:
        document = nbt_codec.decode_hex(value)
    except nbt_codec.NbtError as exc:
        raise CapsuleError(f"{label} is invalid NBT: {exc}") from exc
    if document["name"] != "":
        raise CapsuleError(f"{label} must have an empty root name")
    root = document["tag"]
    fields = root["value"]
    if set(fields) - {"Name", "Id", "Properties"}:
        raise CapsuleError(f"{label} contains a non-GameProfile field")

    def string_field(name: str, *, required: bool = False) -> str | None:
        node = fields.get(name)
        if node is None:
            if required:
                raise CapsuleError(f"{label}.{name} is required")
            return None
        if node.get("type") != "string" or not isinstance(
                node.get("value"), str):
            raise CapsuleError(f"{label}.{name} must be TAG_String")
        return node["value"]

    string_field("Name")
    owner_id = string_field("Id")
    if owner_id is not None:
        try:
            uuid.UUID(owner_id)
        except ValueError as exc:
            raise CapsuleError(
                f"{label}.Id is not a canonical UUID") from exc
    properties = fields.get("Properties")
    if properties is not None:
        if properties.get("type") != "compound":
            raise CapsuleError(f"{label}.Properties must be TAG_Compound")
        for property_name, values in properties["value"].items():
            property_label = f"{label}.Properties[{property_name!r}]"
            if values.get("type") != "list" \
                    or values.get("element_type") != "compound":
                raise CapsuleError(
                    f"{property_label} must be a TAG_Compound list")
            for index, entry in enumerate(values["value"]):
                entry_label = f"{property_label}[{index}]"
                if entry.get("type") != "compound" \
                        or set(entry["value"]) not in (
                            {"Value"}, {"Value", "Signature"}):
                    raise CapsuleError(
                        f"{entry_label} must contain Value and optional "
                        "Signature")
                for field_name, node in entry["value"].items():
                    if node.get("type") != "string" \
                            or not isinstance(node.get("value"), str):
                        raise CapsuleError(
                            f"{entry_label}.{field_name} must be TAG_String")
    return bytes.fromhex(value)


def _validate_shulker_item_tag_nbt(
        value: object, label: str, container: dict) -> bytes:
    """Validate the exact tag produced by BlockShulkerBox.breakBlock."""
    try:
        if isinstance(value, str):
            document = nbt_codec.decode_hex(value)
            raw = bytes.fromhex(value)
        elif isinstance(value, dict):
            raw = nbt_codec.encode(value)
            document = nbt_codec.decode(raw)
        else:
            raise nbt_codec.NbtError(
                "value must be hexadecimal or canonical typed NBT")
    except nbt_codec.NbtError as exc:
        raise CapsuleError(f"{label} is invalid NBT: {exc}") from exc
    if document["name"] != "":
        raise CapsuleError(f"{label} must have an empty root name")
    outer = document["tag"]["value"]
    if set(outer) not in ({"BlockEntityTag"},
                          {"BlockEntityTag", "display"}):
        raise CapsuleError(
            f"{label} must contain BlockEntityTag and optional display")
    block_entity = outer["BlockEntityTag"]
    if block_entity.get("type") != "compound":
        raise CapsuleError(f"{label}.BlockEntityTag must be TAG_Compound")
    fields = block_entity["value"]
    allowed = {"Items", "CustomName", "Lock", "LootTable",
               "LootTableSeed"}
    if set(fields) - allowed:
        raise CapsuleError(
            f"{label}.BlockEntityTag contains unsupported saved state")

    def saved_string(name: str) -> str | None:
        node = fields.get(name)
        if node is None:
            return None
        if node.get("type") != "string" \
                or not isinstance(node.get("value"), str) \
                or not node["value"]:
            raise CapsuleError(
                f"{label}.BlockEntityTag.{name} must be nonempty TAG_String")
        return node["value"]

    custom_name = saved_string("CustomName")
    saved_string("Lock")
    loot_table = saved_string("LootTable")
    loot_seed = fields.get("LootTableSeed")
    if loot_seed is not None and (
            loot_table is None or loot_seed.get("type") != "long"
            or not isinstance(loot_seed.get("value"), int)
            or loot_seed["value"] == 0):
        raise CapsuleError(
            f"{label}.BlockEntityTag.LootTableSeed must be a nonzero "
            "TAG_Long paired with LootTable")
    if loot_table is not None and "Items" in fields:
        raise CapsuleError(
            f"{label}.BlockEntityTag cannot save Items with LootTable")

    display = outer.get("display")
    if custom_name is None:
        if display is not None:
            raise CapsuleError(
                f"{label}.display is present without CustomName")
    elif display is None or display.get("type") != "compound" \
            or set(display.get("value", {})) != {"Name"} \
            or display["value"]["Name"].get("type") != "string" \
            or display["value"]["Name"].get("value") != custom_name:
        raise CapsuleError(
            f"{label}.display.Name must duplicate CustomName exactly")

    encoded_items = []
    items = fields.get("Items")
    if items is not None:
        if items.get("type") != "list" \
                or items.get("element_type") != "compound":
            raise CapsuleError(
                f"{label}.BlockEntityTag.Items must be a compound list")
        seen_slots = set()
        for index, entry in enumerate(items["value"]):
            item_label = f"{label}.BlockEntityTag.Items[{index}]"
            if entry.get("type") != "compound":
                raise CapsuleError(f"{item_label} must be TAG_Compound")
            values = entry["value"]
            required = {"Slot", "id", "Count", "Damage"}
            if not required <= set(values) \
                    or set(values) - required - {"tag", "ForgeCaps"}:
                raise CapsuleError(
                    f"{item_label} has incomplete ItemStack NBT")
            typed = (("Slot", "byte"), ("id", "string"),
                     ("Count", "byte"), ("Damage", "short"))
            if any(values[name].get("type") != kind
                   for name, kind in typed):
                raise CapsuleError(
                    f"{item_label} has incorrect ItemStack tag widths")
            slot = values["Slot"]["value"]
            item_id = values["id"]["value"]
            count = values["Count"]["value"]
            damage = values["Damage"]["value"]
            if (isinstance(slot, bool) or not isinstance(slot, int)
                    or slot in seen_slots or not 0 <= slot < 27
                    or not isinstance(item_id, str) or not item_id
                    or isinstance(count, bool) or not isinstance(count, int)
                    or not 1 <= count <= 64
                    or isinstance(damage, bool) or not isinstance(damage, int)
                    or not 0 <= damage <= 32767):
                raise CapsuleError(f"{item_label} has invalid ItemStack state")
            for optional in ("tag", "ForgeCaps"):
                if optional in values \
                        and values[optional].get("type") != "compound":
                    raise CapsuleError(
                        f"{item_label}.{optional} must be TAG_Compound")
            seen_slots.add(slot)
            encoded_items.append((slot, count, damage))
    structured_items = sorted(
        (item["slot"], item["count"], item["meta"])
        for item in container["items"])
    if sorted(encoded_items) != structured_items:
        raise CapsuleError(
            f"{label}.BlockEntityTag.Items differs from structured slots")
    return raw


@functools.lru_cache(maxsize=1)
def blockstate_predicate_masks() -> tuple[
        tuple[int, ...], tuple[int, ...], tuple[int, ...]]:
    """Load the provenance-locked Java 1.11.2 block-state registry capture."""
    try:
        raw = BLOCKSTATE_PROPS_FILE.read_bytes()
    except OSError as exc:
        raise CapsuleError(
            f"missing block-state registry capture: {BLOCKSTATE_PROPS_FILE}"
        ) from exc
    digest = hashlib.sha256(raw).hexdigest()
    if digest != BLOCKSTATE_PROPS_SHA256:
        raise CapsuleError(
            "block-state registry capture sha256 differs: "
            f"expected {BLOCKSTATE_PROPS_SHA256}, got {digest}"
        )
    try:
        payload = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CapsuleError("invalid block-state registry JSON") from exc
    if (
        payload.get("schema") != "qrl.blockstate_props.v4"
        or payload.get("minecraft") != "1.11.2"
    ):
        raise CapsuleError("unexpected block-state registry identity")
    normal = [-1] * 256
    providers = [-1] * 256
    fully_opaque = [-1] * 256
    for row in payload.get("blocks", []):
        block_id = row.get("id")
        normal_mask = row.get("normal_cube_mask")
        provider_mask = row.get("can_provide_power_mask")
        fully_opaque_mask = row.get("fully_opaque_mask")
        if (
            not isinstance(block_id, int)
            or not 0 <= block_id < 256
            or normal[block_id] != -1
            or not isinstance(normal_mask, int)
            or not 0 <= normal_mask <= 0xFFFF
            or not isinstance(provider_mask, int)
            or not 0 <= provider_mask <= 0xFFFF
            or not isinstance(fully_opaque_mask, int)
            or not 0 <= fully_opaque_mask <= 0xFFFF
        ):
            raise CapsuleError("invalid block-state registry row")
        normal[block_id] = normal_mask
        providers[block_id] = provider_mask
        fully_opaque[block_id] = fully_opaque_mask
    if any(value < 0 for value in normal + providers + fully_opaque):
        raise CapsuleError("incomplete block-state registry ID coverage")
    return tuple(normal), tuple(providers), tuple(fully_opaque)


def cell_count(box: list[int] | tuple[int, ...]) -> int:
    if len(box) != 6:
        raise CapsuleError("block box must contain six integers")
    x0, y0, z0, x1, y1, z1 = box
    if any(isinstance(value, bool) or not isinstance(value, int) for value in box):
        raise CapsuleError("block box values must be integers")
    if x1 < x0 or z1 < z0 or y1 < y0 or y0 < 0 or y1 > 255:
        raise CapsuleError(f"invalid inclusive block box: {box}")
    return (x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1)


def coordinate(index: int, box: list[int] | tuple[int, ...]) -> tuple[int, int, int]:
    x0, y0, z0, x1, _y1, z1 = box
    nx = x1 - x0 + 1
    nz = z1 - z0 + 1
    return x0 + index % nx, y0 + index // (nx * nz), z0 + (index // nx) % nz


def sha256(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _finite_number(value, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CapsuleError(f"{label} must be a number")
    numeric = float(value)
    if not math.isfinite(numeric):
        raise CapsuleError(f"{label} must be finite")
    return numeric


def _validate_potion_effect_payload(value: object, label: str) -> list[dict]:
    if not isinstance(value, list) or len(value) > 16:
        raise CapsuleError(f"{label} must contain at most 16 effects")
    for index, effect in enumerate(value):
        effect_label = f"{label}[{index}]"
        if not isinstance(effect, dict) \
                or set(effect) != {"id", "amp", "dur", "flags"}:
            raise CapsuleError(
                f"{effect_label} must contain id/amp/dur/flags")
        if any(isinstance(effect[field], bool)
               or not isinstance(effect[field], int)
               for field in ("id", "amp", "dur", "flags")) \
                or not 1 <= effect["id"] <= 27 \
                or not 0 <= effect["amp"] <= 255 \
                or not 1 <= effect["dur"] <= 2147483647 \
                or not 0 <= effect["flags"] <= 3:
            raise CapsuleError(f"{effect_label} has invalid state")
    return value


def _read_state(path: pathlib.Path) -> dict:
    text = path.read_text(encoding="utf-8")
    try:
        parsed = json.loads(text)
        if isinstance(parsed, dict):
            return parsed
    except json.JSONDecodeError:
        pass
    rows = []
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            raise CapsuleError(f"{path}:{line_no}: {exc}") from exc
        if not isinstance(row, dict):
            raise CapsuleError(f"{path}:{line_no}: state row must be an object")
        rows.append(row)
    if len(rows) != 1:
        raise CapsuleError(
            f"{path}: expected one pre-tick state object, found {len(rows)} rows"
        )
    return rows[0]


def _validate_state(state: dict) -> None:
    if not isinstance(state, dict):
        raise CapsuleError("state must be an object")
    do_entity_drops = state.get("do_entity_drops", True)
    if not isinstance(do_entity_drops, bool):
        raise CapsuleError("state.do_entity_drops must be boolean")
    do_mob_spawning = state.get("do_mob_spawning", True)
    if not isinstance(do_mob_spawning, bool):
        raise CapsuleError("state.do_mob_spawning must be boolean")
    do_mob_loot = state.get("do_mob_loot", True)
    if not isinstance(do_mob_loot, bool):
        raise CapsuleError("state.do_mob_loot must be boolean")
    random_tick_speed = state.get("random_tick_speed", 3)
    if isinstance(random_tick_speed, bool) \
            or not isinstance(random_tick_speed, int) \
            or not -(1 << 31) <= random_tick_speed < (1 << 31):
        raise CapsuleError(
            "state.random_tick_speed must be a signed 32-bit integer")
    world_spawn = state.get("world_spawn", {"x": 0, "y": 64, "z": 0})
    if not isinstance(world_spawn, dict) or set(world_spawn) != {"x", "y", "z"}:
        raise CapsuleError("state.world_spawn must contain x/y/z")
    for field in ("x", "y", "z"):
        value = world_spawn[field]
        if isinstance(value, bool) or not isinstance(value, int) \
                or not -(1 << 31) <= value < (1 << 31):
            raise CapsuleError(f"state.world_spawn.{field} must be int32")
    if not 0 <= world_spawn["y"] <= 255:
        raise CapsuleError("state.world_spawn.y must be in 0..255")
    ticking_chunks = state.get("ticking_chunks")
    if ticking_chunks is None and "ticking_chunks_complete" not in state:
        ticking_chunks = []
    elif not isinstance(ticking_chunks, list) \
            or state.get("ticking_chunks_complete") is not True:
        raise CapsuleError(
            "state.ticking_chunks must be a complete array")
    if len(ticking_chunks) > 4225:
        raise CapsuleError("state.ticking_chunks exceeds 4225 entries")
    seen_tick_chunks = set()
    for index, chunk in enumerate(ticking_chunks):
        if not isinstance(chunk, dict) or set(chunk) != {
                "order", "x", "z", "random_tick_mask"}:
            raise CapsuleError(
                f"state.ticking_chunks[{index}] has an incomplete schema")
        if chunk["order"] != index:
            raise CapsuleError(
                f"state.ticking_chunks[{index}] has a noncanonical order")
        for field in ("order", "x", "z", "random_tick_mask"):
            if isinstance(chunk[field], bool) \
                    or not isinstance(chunk[field], int):
                raise CapsuleError(
                    f"state.ticking_chunks[{index}].{field} must be integer")
        if not -134217728 <= chunk["x"] <= 134217727 \
                or not -134217728 <= chunk["z"] <= 134217727 \
                or not 0 <= chunk["random_tick_mask"] <= 0xFFFF:
            raise CapsuleError(
                f"state.ticking_chunks[{index}] is outside native bounds")
        position = (chunk["x"], chunk["z"])
        if position in seen_tick_chunks:
            raise CapsuleError("state.ticking_chunks contains a duplicate")
        seen_tick_chunks.add(position)
    weather_effects = state.get("weather_effects", [])
    if not isinstance(weather_effects, list) or len(weather_effects) > 16:
        raise CapsuleError("state.weather_effects must be an array of at most 16")
    for index, effect in enumerate(weather_effects):
        if not isinstance(effect, dict) or set(effect) != {
                "eid", "x", "y", "z", "ticks_existed",
                "lightning_state", "living_time", "effect_only",
                "bolt_vertex", "entity_seed48"}:
            raise CapsuleError(
                f"state.weather_effects[{index}] has an incomplete schema")
        for field in ("x", "y", "z"):
            _finite_number(effect[field],
                           f"state.weather_effects[{index}].{field}")
        if not isinstance(effect["effect_only"], bool):
            raise CapsuleError(
                f"state.weather_effects[{index}].effect_only must be boolean")
        for field in ("eid", "ticks_existed", "lightning_state",
                      "living_time", "bolt_vertex", "entity_seed48"):
            if isinstance(effect[field], bool) or not isinstance(effect[field], int):
                raise CapsuleError(
                    f"state.weather_effects[{index}].{field} must be integer")
        if effect["eid"] < 0 or effect["ticks_existed"] < 0 \
                or not -1000 <= effect["lightning_state"] <= 2 \
                or not 0 <= effect["living_time"] <= 3 \
                or not 0 <= effect["entity_seed48"] < (1 << 48):
            raise CapsuleError(
                f"state.weather_effects[{index}] is outside native bounds")
    for key in ("player", "inventory", "entities", "time", "world_rng"):
        if key not in state:
            raise CapsuleError(f"state is missing {key!r}")
    default_game_mode = state.get("default_game_mode", 0)
    if isinstance(default_game_mode, bool) \
            or not isinstance(default_game_mode, int) \
            or not 0 <= default_game_mode <= 3:
        raise CapsuleError("state.default_game_mode must be in 0..3")
    difficulty = state.get("difficulty", 2)
    if isinstance(difficulty, bool) or not isinstance(difficulty, int) \
            or not 0 <= difficulty <= 3:
        raise CapsuleError("state.difficulty must be in 0..3")
    world_border = state.get("world_border", {})
    if not isinstance(world_border, dict):
        raise CapsuleError("state.world_border must be an object")
    border_defaults = {
        "center_x": 0.0, "center_z": 0.0,
        "diameter": 60000000.0, "target_diameter": 60000000.0,
        "time_until_target": 0, "damage_amount": 0.2,
        "damage_buffer": 5.0, "warning_time": 15,
        "warning_distance": 5,
    }
    if set(world_border) - set(border_defaults):
        raise CapsuleError("state.world_border has unknown fields")
    for field in ("center_x", "center_z", "diameter", "target_diameter",
                  "damage_amount", "damage_buffer"):
        value = world_border.get(field, border_defaults[field])
        if isinstance(value, bool) or not isinstance(value, (int, float)) \
                or not math.isfinite(value):
            raise CapsuleError(f"state.world_border.{field} must be finite")
    if world_border.get("diameter", 60000000.0) < 1.0 \
            or world_border.get("target_diameter", 60000000.0) < 1.0 \
            or world_border.get("damage_amount", 0.2) < 0.0 \
            or world_border.get("damage_buffer", 5.0) < 0.0:
        raise CapsuleError("state.world_border has an out-of-range value")
    for field in ("time_until_target", "warning_time", "warning_distance"):
        value = world_border.get(field, border_defaults[field])
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise CapsuleError(
                f"state.world_border.{field} must be a nonnegative integer")
    entity_id_cursor = state.get("entity_id_cursor")
    if isinstance(entity_id_cursor, bool) \
            or not isinstance(entity_id_cursor, int) \
            or not 0 <= entity_id_cursor <= 2147483647:
        raise CapsuleError(
            "state.entity_id_cursor must be an integer in 0..2147483647"
        )
    player = state["player"]
    if not isinstance(player, dict):
        raise CapsuleError("state.player must be an object")
    player_name = player.get("name", "Player")
    if not isinstance(player_name, str) \
            or not re.fullmatch(r"[A-Za-z0-9_]{1,16}", player_name):
        raise CapsuleError(
            "state.player.name must be a 1..16 character vanilla username")
    player_eid = player.get("eid", 1)
    if isinstance(player_eid, bool) or not isinstance(player_eid, int) \
            or not 0 <= player_eid <= 2147483647:
        raise CapsuleError(
            "state.player.eid must be an integer in 0..2147483647")
    for field in ("x", "y", "z", "yaw", "pitch", "vx", "vy", "vz",
                  "health", "max_health", "absorption", "food",
                  "saturation", "xp_frac", "attack_cooldown",
                  "fall_distance"):
        _finite_number(player.get(field), f"state.player.{field}")
    on_ground = player.get("on_ground")
    if on_ground not in (0, 1, False, True):
        raise CapsuleError("state.player.on_ground must be 0 or 1")
    if not isinstance(player.get("creative", False), bool):
        raise CapsuleError("state.player.creative must be boolean")
    game_mode = player.get(
        "game_mode", 1 if player.get("creative", False) else 0)
    if isinstance(game_mode, bool) or not isinstance(game_mode, int) \
            or not 0 <= game_mode <= 3:
        raise CapsuleError("state.player.game_mode must be in 0..3")
    if bool(player.get("creative", False)) != (game_mode == 1):
        raise CapsuleError(
            "state.player.creative must agree with state.player.game_mode")
    dimension = player.get("dim")
    if isinstance(dimension, bool) or not isinstance(dimension, int) \
            or dimension not in (-1, 0, 1):
        raise CapsuleError("state.player.dim must be -1, 0, or 1")
    spawn_present = player.get("spawn_present", False)
    spawn_forced = player.get("spawn_forced", False)
    if not isinstance(spawn_present, bool) or not isinstance(spawn_forced, bool):
        raise CapsuleError("state.player spawn flags must be boolean")
    for field in ("spawn_x", "spawn_y", "spawn_z"):
        value = player.get(field, 0)
        if isinstance(value, bool) or not isinstance(value, int) \
                or not -(1 << 31) <= value < (1 << 31):
            raise CapsuleError(f"state.player.{field} must be int32")
    if spawn_present and not 0 <= player.get("spawn_y", 0) <= 255:
        raise CapsuleError("state.player.spawn_y must be in 0..255")
    if not spawn_present and spawn_forced:
        raise CapsuleError("state.player spawn cannot be forced when absent")
    trigger_present = player.get("trigger_qrl_present", False)
    trigger_locked = player.get("trigger_qrl_locked", False)
    trigger_score = player.get("trigger_qrl_score", 0)
    if not isinstance(trigger_present, bool) \
            or not isinstance(trigger_locked, bool):
        raise CapsuleError("state.player trigger qrl flags must be boolean")
    if isinstance(trigger_score, bool) or not isinstance(trigger_score, int) \
            or not -(1 << 31) <= trigger_score < (1 << 31):
        raise CapsuleError("state.player.trigger_qrl_score must be int32")
    if not trigger_present and (trigger_locked or trigger_score != 0):
        raise CapsuleError("absent trigger qrl score must be zero and unlocked")
    if not isinstance(player.get("achievement_open_inventory", False), bool):
        raise CapsuleError(
            "state.player.achievement_open_inventory must be boolean")
    for field, default in (
            ("uuid_most_hex", "a01e3843e5213998"),
            ("uuid_least_hex", "958af459800e4d11")):
        value = player.get(field, default)
        if not isinstance(value, str) or len(value) != 16 \
                or any(character not in "0123456789abcdef"
                       for character in value):
            raise CapsuleError(
                f"state.player.{field} must be lowercase hex64")
    held_slot = player.get("held_slot")
    if isinstance(held_slot, bool) or not isinstance(held_slot, int) \
            or not 0 <= held_slot <= 8:
        raise CapsuleError("state.player.held_slot must be in 0..8")
    if float(player["max_health"]) <= 0 \
            or float(player["max_health"]) > 1024:
        raise CapsuleError("state.player.max_health must be in (0,1024]")
    if float(player["health"]) < 0 \
            or float(player["health"]) > float(player["max_health"]):
        raise CapsuleError("state.player.health must be in 0..max_health")
    if float(player["absorption"]) < 0 \
            or float(player["absorption"]) > 1024:
        raise CapsuleError("state.player.absorption must be in 0..1024")
    if float(player["food"]) < 0 or float(player["food"]) > 20:
        raise CapsuleError("state.player.food must be in 0..20")
    if float(player["saturation"]) < 0 or float(player["saturation"]) > 20:
        raise CapsuleError("state.player.saturation must be in 0..20")
    food_exhaustion = _finite_number(
        player.get("food_exhaustion"), "state.player.food_exhaustion"
    )
    if not 0 <= food_exhaustion <= 40:
        raise CapsuleError("state.player.food_exhaustion must be in 0..40")
    food_timer = player.get("food_timer")
    if isinstance(food_timer, bool) or not isinstance(food_timer, int) \
            or not 0 <= food_timer <= 1000000:
        raise CapsuleError(
            "state.player.food_timer must be an integer in 0..1000000"
        )
    if float(player["fall_distance"]) < 0:
        raise CapsuleError("state.player.fall_distance may not be negative")
    air = player.get("air")
    if isinstance(air, bool) or not isinstance(air, int) or not -20 <= air <= 300:
        raise CapsuleError("state.player.air must be an integer in -20..300")
    fire = player.get("fire")
    if isinstance(fire, bool) or not isinstance(fire, int) \
            or not -20 <= fire <= 32767:
        raise CapsuleError("state.player.fire must be an integer in -20..32767")
    packet_ticks = player.get("position_update_ticks")
    if isinstance(packet_ticks, bool) or not isinstance(packet_ticks, int) \
            or not 0 <= packet_ticks <= 19:
        raise CapsuleError(
            "state.player.position_update_ticks must be an integer in 0..19"
        )
    packet_pending = player.get("position_packet_pending")
    if packet_pending not in (0, 1, False, True):
        raise CapsuleError("state.player.position_packet_pending must be 0 or 1")
    riding_eid = player.get("riding_eid", -1)
    if isinstance(riding_eid, bool) or not isinstance(riding_eid, int) \
            or riding_eid < -1:
        raise CapsuleError(
            "state.player.riding_eid must be -1 or a non-negative integer")
    for field, maximum in (
        ("xp_level", 21863),
        ("xp_total", 2147483647),
        ("attack_ticks", 1000000000),
        ("hurt_time", 20),
        ("hurt_resistant_time", 20),
        ("death_time", 20),
        ("deaths", 2147483647),
    ):
        value = player.get(field)
        if isinstance(value, bool) or not isinstance(value, int) \
                or not 0 <= value <= maximum:
            raise CapsuleError(
                f"state.player.{field} must be an integer in 0..{maximum}"
            )
    if not 0 <= float(player["xp_frac"]) < 1:
        raise CapsuleError("state.player.xp_frac must be in [0,1)")
    if not 0 <= float(player["attack_cooldown"]) <= 1:
        raise CapsuleError("state.player.attack_cooldown must be in [0,1]")
    dead = player.get("dead")
    if dead not in (0, 1, False, True):
        raise CapsuleError("state.player.dead must be 0 or 1")
    if player["health"] > 0 and player["death_time"] != 0:
        raise CapsuleError("a positive-health player must have death_time 0")
    if dead and player["health"] > 0:
        raise CapsuleError("a dead player must have zero health")
    potions = player.get("potions")
    if not isinstance(potions, list) or len(potions) > 32:
        raise CapsuleError("state.player.potions must contain at most 32 effects")
    seen_potions = set()
    health_boost = 0
    for index, effect in enumerate(potions):
        label = f"state.player.potions[{index}]"
        if not isinstance(effect, dict) or set(effect) != {
                "id", "amp", "dur", "ambient", "show_particles"}:
            raise CapsuleError(
                f"{label} must contain id/amp/dur/ambient/show_particles")
        potion_id = effect["id"]
        amplifier = effect["amp"]
        duration = effect["dur"]
        if any(isinstance(value, bool) or not isinstance(value, int)
               for value in (potion_id, amplifier, duration)) \
                or not 1 <= potion_id <= 255 \
                or not 0 <= amplifier <= 255 \
                or not 1 <= duration <= 2147483647 \
                or effect["ambient"] not in (False, True) \
                or effect["show_particles"] not in (False, True) \
                or potion_id in seen_potions:
            raise CapsuleError(f"{label} has an invalid or duplicate effect")
        seen_potions.add(potion_id)
        if potion_id == 21:
            health_boost += 4 * (amplifier + 1)
    if abs(float(player["max_health"]) - (20.0 + health_boost)) > 1e-6:
        raise CapsuleError(
            "state.player.max_health is not explained by represented potions"
        )

    inventory = state["inventory"]
    if not isinstance(inventory, list):
        raise CapsuleError("state.inventory must be an array")
    seen_slots = set()
    for index, item in enumerate(inventory):
        if not isinstance(item, dict):
            raise CapsuleError(f"state.inventory[{index}] must be an object")
        try:
            slot = item["slot"]
            item_id = item["id"]
            count = item["count"]
            meta = item["meta"]
        except KeyError as exc:
            raise CapsuleError(
                f"state.inventory[{index}] is missing {exc.args[0]!r}"
            ) from exc
        if any(isinstance(value, bool) or not isinstance(value, int)
               for value in (slot, item_id, count, meta)):
            raise CapsuleError(f"state.inventory[{index}] values must be integers")
        if slot in seen_slots or not 0 <= slot <= 40:
            raise CapsuleError(
                f"state.inventory[{index}].slot must be unique and in 0..40"
            )
        if not 1 <= item_id <= 4095 or not 1 <= count <= 64 \
                or not 0 <= meta <= 32767:
            raise CapsuleError(f"state.inventory[{index}] has an invalid stack")
        enchants = item.get("enchants")
        if not isinstance(enchants, list) or len(enchants) > 8:
            raise CapsuleError(
                f"state.inventory[{index}].enchants must contain at most 8 pairs"
            )
        for enchant_index, enchantment in enumerate(enchants):
            if not isinstance(enchantment, list) or len(enchantment) != 2 \
                    or any(isinstance(value, bool)
                           or not isinstance(value, int)
                           for value in enchantment) \
                    or not 0 <= enchantment[0] <= 32767 \
                    or not 1 <= enchantment[1] <= 32767:
                raise CapsuleError(
                    f"state.inventory[{index}].enchants[{enchant_index}] "
                    "must be an enchantment-id/positive-level pair"
                )
        repair_cost = item.get("repair_cost", 0)
        custom_name = item.get("custom_name", "")
        subset_exact = item.get("nbt_subset_exact", True)
        if isinstance(repair_cost, bool) or not isinstance(repair_cost, int) \
                or not 0 <= repair_cost <= 2147483647:
            raise CapsuleError(
                f"state.inventory[{index}].repair_cost must be a "
                "non-negative integer"
            )
        payload = _validate_item_stack_payload(
            item.get("stack_payload"),
            f"state.inventory[{index}].stack_payload")
        if not isinstance(custom_name, str) \
                or len(custom_name.encode("utf-8")) > (65535 if payload else 31):
            raise CapsuleError(
                f"state.inventory[{index}].custom_name is too large"
            )
        if subset_exact is not True and payload is None:
            raise CapsuleError(
                f"state.inventory[{index}] has unsupported ItemStack NBT"
            )
        seen_slots.add(slot)
    ender_inventory = state.get("ender_inventory", [])
    if not isinstance(ender_inventory, list) or len(ender_inventory) > 27:
        raise CapsuleError("state.ender_inventory must contain at most 27 stacks")
    seen_ender_slots = set()
    for index, item in enumerate(ender_inventory):
        label = f"state.ender_inventory[{index}]"
        if not isinstance(item, dict):
            raise CapsuleError(f"{label} must be an object")
        _validate_villager_stack(item, label, allow_empty=False)
        slot = item.get("slot")
        if isinstance(slot, bool) or not isinstance(slot, int) \
                or not 0 <= slot < 27 or slot in seen_ender_slots:
            raise CapsuleError(f"{label}.slot is invalid or duplicate")
        seen_ender_slots.add(slot)
    entities = state["entities"]
    if not isinstance(entities, list):
        raise CapsuleError("state.entities must be an array")
    seen_eids = set()
    seen_entity_uuids = set()
    seen_loaded_orders = set()
    loaded_order_count = 0
    fish_hooks = []
    for index, entity in enumerate(entities):
        label = f"state.entities[{index}]"
        if not isinstance(entity, dict):
            raise CapsuleError(f"{label} must be an object")
        eid = entity.get("eid")
        entity_type = entity.get("type")
        if isinstance(eid, bool) or not isinstance(eid, int) or eid < 0:
            raise CapsuleError(f"{label}.eid must be a non-negative integer")
        if eid in seen_eids:
            raise CapsuleError(f"{label}.eid must be unique")
        seen_eids.add(eid)
        uuid_fields = ("uuid_most", "uuid_least")
        if any(field in entity for field in uuid_fields):
            if not all(field in entity for field in uuid_fields):
                raise CapsuleError(f"{label} has an incomplete UUID")
            uuid = tuple(entity[field] for field in uuid_fields)
            if any(isinstance(value, bool) or not isinstance(value, int)
                   or not -(1 << 63) <= value < (1 << 63)
                   for value in uuid):
                raise CapsuleError(f"{label} UUID is outside signed int64")
            if uuid in seen_entity_uuids:
                raise CapsuleError(f"{label} UUID must be unique")
            seen_entity_uuids.add(uuid)
        if not isinstance(entity_type, str) or not entity_type:
            raise CapsuleError(f"{label}.type must be a non-empty string")
        if "loaded_order" in entity:
            loaded_order = entity["loaded_order"]
            if isinstance(loaded_order, bool) \
                    or not isinstance(loaded_order, int) \
                    or loaded_order < 0:
                raise CapsuleError(
                    f"{label}.loaded_order must be a non-negative integer"
                )
            if loaded_order in seen_loaded_orders:
                raise CapsuleError(
                    f"{label}.loaded_order must be unique"
                )
            seen_loaded_orders.add(loaded_order)
            loaded_order_count += 1
        for field in ("x", "y", "z", "dx", "dy", "dz",
                      "vx", "vy", "vz", "yaw", "pitch", "health"):
            _finite_number(entity.get(field), f"{label}.{field}")
        plain_no_ai_health = {
            "EntityZombie": 20.0,
            "EntitySkeleton": 20.0,
            "EntityWitherSkeleton": 20.0,
            "EntityCreeper": 20.0,
            "EntitySpider": 16.0,
            "EntityCaveSpider": 12.0,
            "EntityEnderman": 40.0,
            "EntityBlaze": 20.0,
            "EntityGhast": 10.0,
            "EntityWitch": 26.0,
            "EntityZombieVillager": 20.0,
            "EntityVindicator": 24.0,
            "EntityEvoker": 24.0,
            "EntityVex": 14.0,
            "EntityGuardian": 30.0,
            "EntityElderGuardian": 80.0,
            "EntitySheep": 8.0,
            "EntityChicken": 4.0,
            "EntitySquid": 10.0,
            "EntitySlime": None,
            "EntityMagmaCube": None,
            "EntityPigZombie": 20.0,
            "EntitySilverfish": 8.0,
            "EntityCow": 10.0,
            "EntityIronGolem": 100.0,
            "EntityStray": 20.0,
            "EntityHusk": 20.0,
            "EntityMooshroom": 10.0,
            "EntityRabbit": 3.0,
            "EntityPolarBear": 30.0,
            "EntityBat": 6.0,
            "EntityEndermite": 8.0,
            "EntitySnowman": 4.0,
            "EntityGiantZombie": 100.0,
        }
        if entity.get("no_ai_plain_exact") is True:
            maximum_health = plain_no_ai_health.get(entity_type)
            if entity_type in ("EntitySlime", "EntityMagmaCube"):
                size = entity.get("slime_size")
                if isinstance(size, bool) or size not in (1, 2, 4):
                    raise CapsuleError(
                        f"{label}.slime_size must be 1, 2, or 4")
                maximum_health = float(size * size)
            elif maximum_health is None:
                raise CapsuleError(
                    f"{label}.type is not a plain NoAI living class")
            _validate_no_ai_base(
                entity, label, maximum_health=maximum_health,
                allow_detached_box_y=entity_type == "EntityBat")
            if entity.get("death_time") != 0:
                raise CapsuleError(
                    f"{label}.death_time must be zero for a plain NoAI mob")
            if entity_type == "EntitySheep":
                fleece = entity.get("sheep_fleece_color")
                if isinstance(fleece, bool) or not isinstance(fleece, int) \
                        or not 0 <= fleece <= 15 \
                        or not isinstance(entity.get("sheep_sheared"), bool):
                    raise CapsuleError(
                        f"{label} has invalid exact sheep state")
            if entity_type == "EntityChicken":
                egg_time = entity.get("chicken_egg_time")
                if isinstance(egg_time, bool) or not isinstance(egg_time, int) \
                        or egg_time <= 0:
                    raise CapsuleError(
                        f"{label}.chicken_egg_time must be positive")
                for field in (
                        "chicken_wing_rotation", "chicken_dest_pos",
                        "chicken_old_flap_speed", "chicken_old_flap",
                        "chicken_wing_rot_delta"):
                    _finite_number(entity.get(field), f"{label}.{field}")
            if entity_type in ("EntitySlime", "EntityMagmaCube"):
                for field in (
                        "slime_squish_amount", "slime_squish_factor",
                        "slime_prev_squish_factor"):
                    _finite_number(entity.get(field), f"{label}.{field}")
                if entity.get("slime_was_on_ground") not in (
                        False, True) \
                        or bool(entity["slime_was_on_ground"]) \
                        != bool(entity["on_ground"]):
                    raise CapsuleError(
                        f"{label} has invalid exact slime ground state")
            if entity_type == "EntityIronGolem":
                for field in (
                        "golem_home_timer", "golem_attack_timer",
                        "golem_rose_timer"):
                    value = entity.get(field)
                    if isinstance(value, bool) or not isinstance(value, int):
                        raise CapsuleError(
                            f"{label}.{field} must be an integer")
                if entity.get("golem_player_created") not in (
                        False, True) \
                        or not 0 <= entity["golem_attack_timer"] <= 10 \
                        or not 0 <= entity["golem_rose_timer"] <= 400:
                    raise CapsuleError(
                        f"{label} has invalid exact iron-golem state")
            if entity_type == "EntityBat" \
                    and entity.get("bat_hanging") not in (False, True):
                raise CapsuleError(
                    f"{label}.bat_hanging must be boolean")
            if entity_type == "EntityEndermite":
                lifetime = entity.get("endermite_lifetime")
                if isinstance(lifetime, bool) \
                        or not isinstance(lifetime, int) \
                        or not 0 <= lifetime < 2400 \
                        or entity.get("endermite_player_spawned") \
                            not in (False, True) \
                        or entity.get("endermite_persistence_required") \
                            not in (False, True):
                    raise CapsuleError(
                        f"{label} has invalid exact endermite state")
            if entity_type == "EntitySquid":
                for field in (
                        "squid_pitch", "squid_prev_pitch", "squid_yaw",
                        "squid_prev_yaw", "squid_rotation",
                        "squid_prev_rotation", "squid_tentacle_angle",
                        "squid_last_tentacle_angle",
                        "squid_random_motion_speed",
                        "squid_rotation_velocity", "squid_rotate_speed",
                        "squid_random_motion_x", "squid_random_motion_y",
                        "squid_random_motion_z",
                        "squid_render_yaw_offset", "squid_head_yaw",
                        "squid_body_prev_head_yaw"):
                    _finite_number(entity.get(field), f"{label}.{field}")
                body_tick = entity.get("squid_body_rotation_tick_counter")
                if isinstance(body_tick, bool) \
                        or not isinstance(body_tick, int) \
                        or not 0 <= body_tick <= 2147483647:
                    raise CapsuleError(
                        f"{label}.squid_body_rotation_tick_counter is invalid")
            if entity_type == "EntitySnowman" \
                    and entity.get("snowman_pumpkin") not in (False, True):
                raise CapsuleError(
                    f"{label}.snowman_pumpkin must be boolean")
        if entity_type == "EntityBat" \
                and entity.get("bat_active_exact") is True:
            _validate_no_ai_base(
                entity, label, maximum_health=6.0,
                require_no_ai=False, exact_field="bat_active_exact",
                allow_detached_box_y=True,
            )
            if entity.get("death_time") != 0 \
                    or entity.get("bat_hanging") not in (False, True) \
                    or entity.get("bat_spawn_valid") not in (False, True) \
                    or entity.get("bat_persistence_required") \
                        not in (False, True):
                raise CapsuleError(
                    f"{label} has invalid active Bat lifetime state")
            entity_age = entity.get("bat_entity_age")
            if isinstance(entity_age, bool) \
                    or not isinstance(entity_age, int) \
                    or not -(1 << 31) <= entity_age < (1 << 31):
                raise CapsuleError(
                    f"{label}.bat_entity_age must be a signed int32")
            body_tick = entity.get("bat_body_rotation_tick_counter")
            if isinstance(body_tick, bool) \
                    or not isinstance(body_tick, int) \
                    or not 0 <= body_tick <= 2147483647:
                raise CapsuleError(
                    f"{label}.bat_body_rotation_tick_counter is invalid")
            for field in (
                    "bat_head_yaw", "bat_render_yaw_offset",
                    "bat_body_prev_head_yaw", "bat_move_forward",
                    "bat_move_strafing"):
                _finite_number(entity.get(field), f"{label}.{field}")
            if abs(float(entity["bat_move_forward"])) > 1e-12 \
                    or abs(float(entity["bat_move_strafing"])) > 1e-12:
                raise CapsuleError(
                    f"{label} has pending Bat move-helper input")
            if entity["bat_spawn_valid"]:
                for field in ("bat_spawn_x", "bat_spawn_y", "bat_spawn_z"):
                    value = entity.get(field)
                    if isinstance(value, bool) \
                            or not isinstance(value, int) \
                            or not -(1 << 31) <= value < (1 << 31):
                        raise CapsuleError(
                            f"{label}.{field} must be a signed int32")
        if entity_type == "EntityPig" \
                and entity.get("no_ai_pig_exact") is True:
            if not 0 < float(entity["health"]) <= 10:
                raise CapsuleError(
                    f"{label}.health must be in (0,10] for an exact NoAI pig"
                )
            for field, maximum in (
                ("hurt_time", 10),
                ("death_time", 19),
                ("hurt_resistant_time", 20),
            ):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int) \
                        or not 0 <= value <= maximum:
                    raise CapsuleError(
                        f"{label}.{field} must be an integer in 0..{maximum}"
                    )
            if abs(float(entity["pitch"])) > 1e-12:
                raise CapsuleError(
                    f"{label}.pitch must be zero for an exact NoAI pig"
                )
            _validate_no_ai_base(entity, label, maximum_health=10.0)
        horse_subtypes = {
            "EntityHorse": "horse",
            "EntityDonkey": "donkey",
            "EntityMule": "mule",
            "EntitySkeletonHorse": "skeleton",
            "EntityZombieHorse": "zombie",
            "EntityLlama": "llama",
        }
        if entity_type in horse_subtypes \
                and entity.get("horse_exact") is True:
            for field in (
                    "horse_growing_age", "horse_forced_age",
                    "horse_forced_age_timer", "horse_in_love",
                    "horse_temper", "horse_variant", "horse_armor",
                    "horse_trap_time", "horse_eating_counter",
                    "horse_owner_uuid_most", "horse_owner_uuid_least",
                    "horse_open_mouth_counter",
                    "horse_jump_rearing_counter", "horse_tail_counter",
                    "horse_sprint_counter", "horse_gallop_time",
                    "hurt_time", "death_time", "hurt_resistant_time"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in (
                    "horse_tame", "horse_saddled", "horse_bred",
                    "horse_eating", "horse_rearing", "horse_mouth_open",
                    "horse_chested", "horse_trap", "horse_jumping",
                    "horse_allow_stand_sliding", "horse_owner_present"):
                if not isinstance(entity.get(field), bool):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            for field in (
                    "horse_max_health_base", "horse_movement_speed_base",
                    "horse_jump_strength", "horse_jump_power",
                    "horse_head_lean", "horse_prev_head_lean",
                    "horse_rearing_amount", "horse_prev_rearing_amount",
                    "horse_mouth_openness",
                    "horse_prev_mouth_openness",
                    "horse_prev_limb_amount", "horse_limb_amount",
                    "horse_limb_swing"):
                _finite_number(entity.get(field), f"{label}.{field}")
            maximum_health = float(entity["horse_max_health_base"])
            if entity.get("horse_subtype") != horse_subtypes[entity_type] \
                    or not 1.0 <= maximum_health <= 1024.0 \
                    or not 0.0 <= float(
                        entity["horse_movement_speed_base"]) <= 16.0 \
                    or not 0.0 <= float(entity["horse_jump_strength"]) <= 16.0 \
                    or not 0 <= entity["horse_forced_age_timer"] <= 40 \
                    or not 0 <= entity["horse_in_love"] <= 600 \
                    or not 0 <= entity["horse_temper"] <= (
                        30 if entity_type == "EntityLlama" else 100) \
                    or not 0 <= entity["horse_armor"] <= 3 \
                    or (entity_type != "EntityHorse"
                        and entity["horse_armor"] != 0) \
                    or entity["horse_trap_time"] < 0 \
                    or (entity["horse_trap"]
                        and entity_type != "EntitySkeletonHorse") \
                    or (entity["horse_chested"]
                        and entity_type not in (
                            "EntityDonkey", "EntityMule", "EntityLlama")) \
                    or (not entity["horse_owner_present"]
                        and (entity["horse_owner_uuid_most"] != 0
                            or entity["horse_owner_uuid_least"] != 0)) \
                    or any(entity[field] < 0 for field in (
                        "horse_eating_counter", "horse_open_mouth_counter",
                        "horse_jump_rearing_counter", "horse_tail_counter",
                        "horse_sprint_counter", "horse_gallop_time")) \
                    or not 0.0 <= float(entity["horse_jump_power"]) <= 1.0 \
                    or any(not 0.0 <= float(entity[field]) <= 1.0 for field in (
                        "horse_head_lean", "horse_prev_head_lean",
                        "horse_rearing_amount", "horse_prev_rearing_amount",
                        "horse_mouth_openness",
                        "horse_prev_mouth_openness")) \
                    or entity["death_time"] != 0 \
                    or not 0 <= entity["hurt_time"] <= 10 \
                    or not 0 <= entity["hurt_resistant_time"] <= 20:
                raise CapsuleError(f"{label} has invalid horse-family state")
            if entity_type == "EntityLlama":
                for field in (
                        "llama_strength", "llama_decor",
                        "llama_leash_holder_kind",
                        "llama_leash_holder_eid",
                        "llama_leash_pending_x",
                        "llama_leash_pending_y",
                        "llama_leash_pending_z",
                        "llama_caravan_head_eid",
                        "llama_caravan_tail_eid",
                        "llama_caravan_dist_counter"):
                    value = entity.get(field)
                    if isinstance(value, bool) or not isinstance(value, int):
                        raise CapsuleError(
                            f"{label}.{field} must be an integer")
                for field in (
                        "llama_leash_holder_uuid_most",
                        "llama_leash_holder_uuid_least"):
                    value = entity.get(field)
                    if isinstance(value, bool) or not isinstance(value, int) \
                            or not -(1 << 63) <= value < (1 << 63):
                        raise CapsuleError(
                            f"{label}.{field} must be signed int64")
                _finite_number(
                    entity.get("llama_caravan_speed"),
                    f"{label}.llama_caravan_speed")
                for field in (
                        "llama_did_spit", "llama_leashed",
                        "llama_leash_pending"):
                    if not isinstance(entity.get(field), bool):
                        raise CapsuleError(
                            f"{label}.{field} must be boolean")
                if not 1 <= entity["llama_strength"] <= 5 \
                        or not -1 <= entity["llama_decor"] <= 15 \
                        or entity["horse_saddled"] \
                        or entity["horse_armor"] != 0 \
                        or not 0 <= entity["llama_leash_holder_kind"] <= 3 \
                        or entity["llama_leash_holder_eid"] < -1 \
                        or not 0 <= entity["llama_leash_pending_y"] <= 255 \
                        or entity["llama_caravan_head_eid"] < -1 \
                        or entity["llama_caravan_tail_eid"] < -1 \
                        or not 0.0 < float(
                            entity["llama_caravan_speed"]) <= 16.0 \
                        or not 0 <= entity[
                            "llama_caravan_dist_counter"] <= 40:
                    raise CapsuleError(
                        f"{label} has invalid exact llama state")
                # This schema restores a NoAI horse-family continuation, not
                # active Llama goals/navigation.  Only their constructor-inert
                # values are safe to omit from the native event stream.
                navigation = entity.get(
                    "llama_navigation", {"path": None, "target": None})
                if entity.get("llama_attack_target_eid", -1) != -1 \
                        or entity.get("llama_ranged_attack_time", -1) != -1 \
                        or entity.get("llama_ranged_see_time", 0) != 0 \
                        or entity.get("llama_task_mask", 0) != 0 \
                        or navigation != {"path": None, "target": None} \
                        or ("llama_entity_seed48" in entity
                            and entity["llama_entity_seed48"]
                                != entity.get("base_entity_seed48")) \
                        or ("llama_on_ground" in entity
                            and entity["llama_on_ground"]
                                != entity.get("on_ground")) \
                        or ("llama_fall_distance" in entity
                            and float(entity["llama_fall_distance"])
                                != float(entity.get("fall_distance", -2.0))):
                    raise CapsuleError(
                        f"{label} has active Llama state in NoAI schema")
            _validate_no_ai_base(
                entity, label, maximum_health=maximum_health)
            _validate_horse_inventory(
                entity, label, entity_type=entity_type)
        if entity_type == "EntityArmorStand" \
                and entity.get("armor_stand_exact") is True:
            for field in (
                    "hurt_time", "death_time", "hurt_resistant_time",
                    "armor_stand_disabled_slots", "armor_stand_air",
                    "armor_stand_fire", "armor_stand_ticks_existed",
                    "armor_stand_punch_cooldown",
                    "armor_stand_entity_seed48",
                    "armor_stand_revenge_timer",
                    "armor_stand_portal_cooldown",
                    "armor_stand_vehicle_eid"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in (
                    "armor_stand_small", "armor_stand_show_arms",
                    "armor_stand_no_base_plate", "armor_stand_marker",
                    "armor_stand_no_gravity", "armor_stand_invisible",
                    "armor_stand_in_water", "armor_stand_on_ground",
                    "armor_stand_entity_have_gaussian",
                    "armor_stand_custom_name_visible",
                    "armor_stand_silent", "armor_stand_glowing",
                    "armor_stand_invulnerable",
                    "armor_stand_update_blocked",
                    "armor_stand_fall_flying"):
                if entity.get(field) not in (False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            for field in (
                    "armor_stand_fall_distance",
                    "armor_stand_last_damage",
                    "armor_stand_entity_gaussian",
                    "armor_stand_absorption",
                    "armor_stand_max_health",
                    "armor_stand_max_health_base"):
                _finite_number(entity.get(field), f"{label}.{field}")
            custom_name = entity.get("armor_stand_custom_name")
            if not isinstance(custom_name, str):
                raise CapsuleError(
                    f"{label}.armor_stand_custom_name must be a string")
            tags = entity.get("armor_stand_tags")
            if not isinstance(tags, list) or len(tags) > 1024 \
                    or any(not isinstance(tag, str) or not tag
                           for tag in tags) \
                    or len(tags) != len(set(tags)):
                raise CapsuleError(f"{label}.armor_stand_tags is invalid")
            effects = entity.get("armor_stand_effects")
            if not isinstance(effects, list) or len(effects) > 32:
                raise CapsuleError(f"{label}.armor_stand_effects is invalid")
            effect_ids = set()
            for effect_index, effect in enumerate(effects):
                effect_label = (
                    f"{label}.armor_stand_effects[{effect_index}]")
                if not isinstance(effect, dict) \
                        or set(effect) != {
                            "id", "amp", "dur", "ambient",
                            "show_particles"}:
                    raise CapsuleError(f"{effect_label} is invalid")
                if any(isinstance(effect[field], bool)
                       or not isinstance(effect[field], int)
                       for field in ("id", "amp", "dur")) \
                        or not 1 <= effect["id"] <= 27 \
                        or not 0 <= effect["amp"] <= 255 \
                        or effect["dur"] <= 0 \
                        or effect["ambient"] not in (False, True) \
                        or effect["show_particles"] not in (False, True) \
                        or effect["id"] in effect_ids:
                    raise CapsuleError(f"{effect_label} is invalid")
                effect_ids.add(effect["id"])
            health_boost = sum(
                4.0 * (effect["amp"] + 1)
                for effect in effects if effect["id"] == 21)
            if abs(float(entity["armor_stand_max_health"])
                   - float(entity["armor_stand_max_health_base"])
                   - health_boost) > 1e-6:
                raise CapsuleError(
                    f"{label}.armor_stand_max_health is not explained "
                    "by base health and represented potions")
            pose = entity.get("armor_stand_pose")
            if not isinstance(pose, list) or len(pose) != 6:
                raise CapsuleError(
                    f"{label}.armor_stand_pose must contain six rotations")
            for part, rotation in enumerate(pose):
                if not isinstance(rotation, list) or len(rotation) != 3:
                    raise CapsuleError(
                        f"{label}.armor_stand_pose[{part}] is invalid")
                for axis, value in enumerate(rotation):
                    _finite_number(
                        value,
                        f"{label}.armor_stand_pose[{part}][{axis}]")
            if not 0.0 < float(entity["health"]) <= 1024.0 \
                    or not 0 <= entity["hurt_time"] <= 10 \
                    or entity["death_time"] != 0 \
                    or not 0 <= entity["hurt_resistant_time"] <= 20 \
                    or not -32768 <= entity["armor_stand_air"] <= 32767 \
                    or not -20 <= entity["armor_stand_fire"] <= 32767 \
                    or entity["armor_stand_ticks_existed"] < 0 \
                    or entity["armor_stand_punch_cooldown"] < 0 \
                    or not 0 <= entity["armor_stand_entity_seed48"] \
                        < (1 << 48) \
                    or float(entity["armor_stand_fall_distance"]) < 0.0 \
                    or float(entity["armor_stand_last_damage"]) < 0.0 \
                    or float(entity["armor_stand_absorption"]) < 0.0 \
                    or not 0.0 < float(
                        entity["armor_stand_max_health"]) <= 1024.0 \
                    or not 0.0 < float(
                        entity["armor_stand_max_health_base"]) <= 1024.0 \
                    or float(entity["health"]) > float(
                        entity["armor_stand_max_health"]) \
                    or entity["armor_stand_revenge_timer"] < 0 \
                    or entity["armor_stand_portal_cooldown"] < 0 \
                    or entity["armor_stand_vehicle_eid"] < -1:
                raise CapsuleError(f"{label} has invalid armor-stand state")
            _validate_armor_stand_equipment(entity, label)
        if entity_type == "EntityVillager" \
                and entity.get("villager_exact") is True:
            active_fresh = entity.get(
                "active_fresh_villager_exact", False)
            if active_fresh not in (False, True):
                raise CapsuleError(
                    f"{label}.active_fresh_villager_exact must be boolean")
            for field in (
                    "hurt_time", "death_time", "hurt_resistant_time",
                    "profession", "growing_age", "career", "career_level",
                    "living_sound_time", "entity_seed48"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in ("entity_have_gaussian", "offers_initialized"):
                if entity.get(field) not in (0, 1, False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            _finite_number(
                entity.get("entity_gaussian"),
                f"{label}.entity_gaussian",
            )
            initialized = entity.get("offers_initialized") is True
            if not 0 < float(entity["health"]) <= 20 \
                    or not 0 <= entity["hurt_time"] <= 10 \
                    or not 0 <= entity["death_time"] < 20 \
                    or not 0 <= entity["hurt_resistant_time"] <= 20 \
                    or not 0 <= entity["profession"] <= 5 \
                    or entity["growing_age"] != 0 \
                    or not -80 <= entity["living_sound_time"] <= 1000 \
                    or not 0 <= entity["entity_seed48"] < (1 << 48) \
                    or abs(float(entity["pitch"])) > 1e-12:
                raise CapsuleError(
                    f"{label} has invalid NoAI-villager state"
                )
            _validate_no_ai_base(
                entity, label, maximum_health=20.0,
                require_no_ai=not active_fresh,
                exact_field=("living_base_exact" if active_fresh
                             else "no_ai_base_exact"),
                minimum_ticks=0 if active_fresh else 2,
                maximum_ticks=0 if active_fresh else 2147483647,
            )
            if active_fresh \
                    and entity.get("villager_inventory_empty") is not True:
                raise CapsuleError(
                    f"{label} active fresh villager inventory must be empty")
            if entity["living_sound_time"] \
                    != entity["base_living_sound_time"] \
                    or entity["entity_seed48"] \
                    != entity["base_entity_seed48"] \
                    or bool(entity["entity_have_gaussian"]) \
                    != bool(entity["base_entity_have_gaussian"]) \
                    or float(entity["entity_gaussian"]) \
                    != float(entity["base_entity_gaussian"]):
                raise CapsuleError(
                    f"{label} has inconsistent living base state")
            if not initialized:
                if entity["career"] != 0 or entity["career_level"] != 0:
                    raise CapsuleError(
                        f"{label} has invalid unopened NoAI-villager state"
                    )
            else:
                careers = (4, 2, 1, 3, 2, 1)
                wealth = entity.get("wealth")
                willing = entity.get("willing")
                inventory_empty = entity.get("villager_inventory_empty")
                offers = entity.get("offers")
                if isinstance(wealth, bool) or not isinstance(wealth, int) \
                        or wealth < 0 \
                        or willing not in (0, 1, False, True) \
                        or inventory_empty is not True \
                        or not 1 <= entity["career"] \
                            <= careers[entity["profession"]] \
                        or not 1 <= entity["career_level"] <= 64 \
                        or not isinstance(offers, list) \
                        or len(offers) > 20:
                    raise CapsuleError(
                        f"{label} has invalid initialized merchant state"
                    )
                for offer_index, offer in enumerate(offers):
                    offer_label = f"{label}.offers[{offer_index}]"
                    if not isinstance(offer, dict):
                        raise CapsuleError(f"{offer_label} must be an object")
                    uses = offer.get("uses")
                    max_uses = offer.get("max_uses")
                    rewards_exp = offer.get("rewards_exp")
                    if isinstance(uses, bool) or not isinstance(uses, int) \
                            or isinstance(max_uses, bool) \
                            or not isinstance(max_uses, int) \
                            or uses < 0 or max_uses <= 0 or uses > max_uses \
                            or rewards_exp not in (0, 1, False, True):
                        raise CapsuleError(
                            f"{offer_label} has invalid counters"
                        )
                    _validate_villager_stack(
                        offer.get("buy_a"), f"{offer_label}.buy_a",
                        allow_empty=False,
                    )
                    _validate_villager_stack(
                        offer.get("buy_b"), f"{offer_label}.buy_b",
                        allow_empty=True,
                    )
                    _validate_villager_stack(
                        offer.get("sell"), f"{offer_label}.sell",
                        allow_empty=False,
                    )
        if entity_type in ("EntityWolf", "EntityOcelot") \
                and entity.get("tameable_exact") is True:
            for field in (
                    "hurt_time", "death_time", "hurt_resistant_time",
                    "variant", "growing_age", "living_sound_time",
                    "entity_seed48"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in (
                    "tamed", "sitting", "player_owner",
                    "entity_have_gaussian"):
                if entity.get(field) not in (0, 1, False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            _finite_number(
                entity.get("entity_gaussian"),
                f"{label}.entity_gaussian",
            )
            maximum_health = 20 if (
                entity_type == "EntityWolf" and entity["tamed"]
            ) else 8 if entity_type == "EntityWolf" else 10
            maximum_variant = 15 if entity_type == "EntityWolf" else 3
            if entity.get("no_ai") is not True \
                    or not 0 < float(entity["health"]) <= maximum_health \
                    or not 0 <= entity["hurt_time"] <= 10 \
                    or not 0 <= entity["death_time"] < 20 \
                    or not 0 <= entity["hurt_resistant_time"] <= 20 \
                    or not 0 <= entity["variant"] <= maximum_variant \
                    or not -(1 << 31) <= entity["growing_age"] < (1 << 31) \
                    or not -80 <= entity["living_sound_time"] <= 1000 \
                    or not 0 <= entity["entity_seed48"] < (1 << 48) \
                    or abs(float(entity["pitch"])) > 1e-12 \
                    or (entity["player_owner"] and not entity["tamed"]) \
                    or (entity["sitting"] and not entity["tamed"]):
                raise CapsuleError(
                    f"{label} has invalid exact NoAI-tameable state"
                )
            _validate_no_ai_base(
                entity, label, maximum_health=float(maximum_health))
            if entity["living_sound_time"] \
                    != entity["base_living_sound_time"] \
                    or entity["entity_seed48"] \
                    != entity["base_entity_seed48"] \
                    or bool(entity["entity_have_gaussian"]) \
                    != bool(entity["base_entity_have_gaussian"]) \
                    or float(entity["entity_gaussian"]) \
                    != float(entity["base_entity_gaussian"]):
                raise CapsuleError(
                    f"{label} has inconsistent NoAI base state")
        wither_exact = entity.get("wither_exact", False)
        if wither_exact not in (False, True):
            raise CapsuleError(f"{label}.wither_exact must be boolean")
        if wither_exact is True and entity_type != "EntityWither":
            raise CapsuleError(
                f"{label}.wither_exact requires EntityWither")
        if entity_type == "EntityWither" and wither_exact is True:
            for field in (
                    "invul_time", "block_break_counter", "recently_hit",
                    "attack_target_eid",
                    "revenge_eid", "revenge_timer", "goal_task_tick",
                    "hurt_target_eid", "hurt_revenge_timer_old",
                    "hurt_target_unseen_ticks",
                    "target_task_tick", "ranged_attack_time",
                    "ranged_see_time", "head_target_0", "head_target_1",
                    "head_target_2", "head_next_0", "head_next_1",
                    "head_idle_0", "head_idle_1",
                    "body_rotation_tick_counter"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in (
                    "no_gravity", "attacking_player", "revenge_is_player",
                    "attack_target_is_player",
                    "hurt_target_task_active", "hurt_target_is_player",
                    "nearest_target_task_active",
                    "invul_task_active", "ranged_task_active",
                    "head_target_player_0",
                    "head_target_player_1", "head_target_player_2"):
                if entity.get(field) not in (0, 1, False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            for side in range(2):
                for field in (
                        f"head_yaw_{side}", f"head_pitch_{side}",
                        f"head_prev_yaw_{side}",
                        f"head_prev_pitch_{side}"):
                    _finite_number(entity.get(field), f"{label}.{field}")
            _validate_no_ai_base(
                entity, label, maximum_health=300.0,
                require_no_ai=bool(entity["no_ai"]),
                minimum_ticks=0,
                allow_dead=True,
            )
            if not 0 <= entity.get("death_time", -1) < 20 \
                    or (float(entity["health"]) > 0.0
                        and entity["death_time"] != 0) \
                    or not 0 <= entity["invul_time"] <= 2147483647 \
                    or not 0 <= entity["block_break_counter"] <= 20 \
                    or not 0 <= entity["recently_hit"] <= 100 \
                    or (entity["attacking_player"]
                        and entity["recently_hit"] == 0) \
                    or any(entity[field] < 0 for field in (
                        "attack_target_eid", "revenge_eid", "revenge_timer",
                        "hurt_target_eid", "hurt_revenge_timer_old",
                        "hurt_target_unseen_ticks", "goal_task_tick",
                        "target_task_tick", "head_next_0", "head_next_1",
                        "head_idle_0", "head_idle_1",
                        "head_target_0", "head_target_1",
                        "head_target_2")) \
                    or (entity["attack_target_eid"] == 0
                        and entity["attack_target_is_player"] is not False) \
                    or (entity["revenge_eid"] == 0
                        and entity["revenge_is_player"] is not False) \
                    or (entity["hurt_target_eid"] == 0
                        and entity["hurt_target_is_player"] is not False) \
                    or any(entity[f"head_target_{head}"] == 0
                           and entity[f"head_target_player_{head}"] is not False
                           for head in range(3)) \
                    or (entity["hurt_target_task_active"]
                        and entity["nearest_target_task_active"]) \
                    or (entity["hurt_target_task_active"]
                        and entity["hurt_target_eid"] == 0) \
                    or (entity["nearest_target_task_active"]
                        and entity["attack_target_eid"] == 0) \
                    or (entity["ranged_task_active"]
                        and entity["attack_target_eid"] == 0) \
                    or (not entity["ranged_task_active"]
                        and (entity["ranged_attack_time"] != -1
                             or entity["ranged_see_time"] != 0)):
                raise CapsuleError(
                    f"{label} has invalid exact Wither state")
            if entity["no_ai"] is True:
                if entity["invul_task_active"] is not False \
                        or entity["hurt_target_task_active"] is not False \
                        or entity["nearest_target_task_active"] is not False \
                        or entity["ranged_task_active"] is not False:
                    raise CapsuleError(
                        f"{label} NoAI Wither cannot run AI tasks")
            elif entity["invul_time"] <= 0 \
                    and entity["invul_task_active"] is True:
                raise CapsuleError(
                    f"{label} active Wither cannot run AIDoNothing")

        if entity_type == "EntitySquid" \
                and entity.get("squid_active_exact") is True:
            _validate_no_ai_base(
                entity, label, maximum_health=10.0,
                require_no_ai=False, exact_field="squid_active_exact",
            )
            if entity.get("death_time") != 0 \
                    or entity.get("squid_persistence_required") \
                        not in (False, True):
                raise CapsuleError(
                    f"{label} has invalid active Squid lifetime state")
            entity_age = entity.get("squid_entity_age")
            if isinstance(entity_age, bool) \
                    or not isinstance(entity_age, int) \
                    or not -(1 << 31) <= entity_age < (1 << 31):
                raise CapsuleError(
                    f"{label}.squid_entity_age must be a signed int32")
            for field in (
                    "squid_pitch", "squid_prev_pitch", "squid_yaw",
                    "squid_prev_yaw", "squid_rotation",
                    "squid_prev_rotation", "squid_tentacle_angle",
                    "squid_last_tentacle_angle",
                    "squid_random_motion_speed",
                    "squid_rotation_velocity", "squid_rotate_speed",
                    "squid_random_motion_x", "squid_random_motion_y",
                    "squid_random_motion_z",
                    "squid_render_yaw_offset", "squid_head_yaw",
                    "squid_body_prev_head_yaw"):
                _finite_number(entity.get(field), f"{label}.{field}")
            body_tick = entity.get("squid_body_rotation_tick_counter")
            if isinstance(body_tick, bool) \
                    or not isinstance(body_tick, int) \
                    or not 0 <= body_tick <= 2147483647:
                raise CapsuleError(
                    f"{label}.squid_body_rotation_tick_counter is invalid")

        wither_skull_exact = entity.get("wither_skull_exact", False)
        if wither_skull_exact not in (False, True):
            raise CapsuleError(
                f"{label}.wither_skull_exact must be boolean")
        if wither_skull_exact is True \
                and entity_type != "EntityWitherSkull":
            raise CapsuleError(
                f"{label}.wither_skull_exact requires EntityWitherSkull")
        if entity_type == "EntityWitherSkull" \
                and wither_skull_exact is True:
            for field in ("shooter_eid", "ticks_in_air", "life"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int) \
                        or value < 0:
                    raise CapsuleError(
                        f"{label}.{field} must be a non-negative integer")
            if entity.get("invulnerable") not in (0, 1, False, True):
                raise CapsuleError(f"{label}.invulnerable must be boolean")
            for field in ("ax", "ay", "az"):
                _finite_number(entity.get(field), f"{label}.{field}")
            if entity["shooter_eid"] != 0 \
                    and not any(
                        other.get("eid") == entity["shooter_eid"]
                        for other in entities
                    ):
                raise CapsuleError(
                    f"{label}.shooter_eid must name a loaded entity")

        llama_spit_exact = entity.get("llama_spit_exact", False)
        if llama_spit_exact not in (False, True):
            raise CapsuleError(
                f"{label}.llama_spit_exact must be boolean")
        if llama_spit_exact is True \
                and entity_type != "EntityLlamaSpit":
            raise CapsuleError(
                f"{label}.llama_spit_exact requires EntityLlamaSpit")
        if entity_type == "EntityLlamaSpit" \
                and llama_spit_exact is True:
            for field in (
                    "llama_spit_owner_eid",
                    "llama_spit_owner_uuid_most",
                    "llama_spit_owner_uuid_least",
                    "ticks_existed"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(
                        f"{label}.{field} must be an integer")
            if entity["ticks_existed"] < 0:
                raise CapsuleError(
                    f"{label}.ticks_existed must be nonnegative")
            for field in (
                    "llama_spit_owner_uuid_present",
                    "llama_spit_no_gravity"):
                if not isinstance(entity.get(field), bool):
                    raise CapsuleError(
                        f"{label}.{field} must be boolean")
            owner_eid = entity["llama_spit_owner_eid"]
            owner_uuid_present = entity[
                "llama_spit_owner_uuid_present"]
            if owner_eid < -1 \
                    or (not owner_uuid_present and (
                        entity["llama_spit_owner_uuid_most"] != 0
                        or entity["llama_spit_owner_uuid_least"] != 0)) \
                    or (owner_eid >= 0 and not any(
                        other.get("eid") == owner_eid
                        and other.get("type") == "EntityLlama"
                        for other in entities)):
                raise CapsuleError(
                    f"{label} has invalid llama-spit owner state")

        shulker_exact = entity.get("shulker_exact", False)
        if shulker_exact not in (False, True):
            raise CapsuleError(f"{label}.shulker_exact must be boolean")
        if shulker_exact is True and entity_type != "EntityShulker":
            raise CapsuleError(
                f"{label}.shulker_exact requires EntityShulker")
        if entity_type == "EntityShulker" and shulker_exact is True:
            for field in (
                    "attach_x", "attach_y", "attach_z", "face",
                    "peek_tick", "peek_time", "attack_time",
                    "watch_time", "idle_look_time", "living_sound_time",
                    "ticks_existed", "hurt_time", "hurt_resistant_time",
                    "death_time", "entity_seed48"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in ("no_ai", "has_player_target"):
                if entity.get(field) not in (0, 1, False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            for field in (
                    "last_damage", "prev_peek_amount", "peek_amount",
                    "head_yaw", "head_pitch"):
                _finite_number(entity.get(field), f"{label}.{field}")
            if entity["no_ai"] is not True \
                    or entity["has_player_target"] is not False \
                    or not 1 <= entity["attach_y"] <= 254 \
                    or not 0 <= entity["face"] <= 5 \
                    or not 0 <= entity["peek_tick"] <= 100 \
                    or any(entity[field] != 0 for field in (
                        "peek_time", "attack_time", "watch_time",
                        "idle_look_time")) \
                    or not -80 <= entity["living_sound_time"] <= 1000 \
                    or entity["ticks_existed"] < 0 \
                    or not 0 <= entity["hurt_time"] <= 10 \
                    or not 0 <= entity["hurt_resistant_time"] <= 20 \
                    or not 0 <= entity["death_time"] < 20 \
                    or not 0 < float(entity["health"]) <= 30 \
                    or float(entity["last_damage"]) < 0 \
                    or not 0 <= float(entity["prev_peek_amount"]) <= 1 \
                    or not 0 <= float(entity["peek_amount"]) <= 1 \
                    or not 0 <= entity["entity_seed48"] < (1 << 48) \
                    or abs(float(entity["x"])
                           - (entity["attach_x"] + 0.5)) > 1e-12 \
                    or abs(float(entity["y"])
                           - entity["attach_y"]) > 1e-12 \
                    or abs(float(entity["z"])
                           - (entity["attach_z"] + 0.5)) > 1e-12:
                raise CapsuleError(
                    f"{label} has invalid exact NoAI-shulker state")
        bullet_exact = entity.get("shulker_bullet_exact", False)
        if bullet_exact not in (False, True):
            raise CapsuleError(
                f"{label}.shulker_bullet_exact must be boolean")
        if bullet_exact is True \
                and entity_type != "EntityShulkerBullet":
            raise CapsuleError(
                f"{label}.shulker_bullet_exact requires "
                "EntityShulkerBullet")
        if entity_type == "EntityShulkerBullet" and bullet_exact is True:
            for field in (
                    "owner_eid", "direction", "steps", "ticks_existed",
                    "entity_seed48"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in ("target_dx", "target_dy", "target_dz"):
                _finite_number(entity.get(field), f"{label}.{field}")
            if entity.get("target_player") is not True \
                    or not -1 <= entity["direction"] <= 5 \
                    or entity["steps"] < 0 \
                    or entity["ticks_existed"] < 0 \
                    or not 0 <= entity["entity_seed48"] < (1 << 48) \
                    or any(abs(float(entity[field])) > 1.0 for field in (
                        "target_dx", "target_dy", "target_dz")):
                raise CapsuleError(
                    f"{label} has invalid exact shulker-bullet state")
        if entity_type == "EntityXPOrb":
            for field in (
                "value", "age", "pickup_delay", "color", "target_color"
            ):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            if not 1 <= entity["value"] <= 32767 \
                    or not 0 <= entity["age"] < 6000 \
                    or entity["pickup_delay"] < 0 or entity["color"] < 0:
                raise CapsuleError(f"{label} has invalid XP-orb state")
            if entity.get("xp_box_exact") is not True:
                raise CapsuleError(
                    f"{label} lacks an exact XP-orb bounding box")
            for field in (
                    "xp_box_min_x", "xp_box_min_y", "xp_box_min_z",
                    "xp_box_max_x", "xp_box_max_y", "xp_box_max_z"):
                _finite_number(entity.get(field), f"{label}.{field}")
            if entity["xp_box_min_x"] > entity["xp_box_max_x"] \
                    or entity["xp_box_min_y"] > entity["xp_box_max_y"] \
                    or entity["xp_box_min_z"] > entity["xp_box_max_z"]:
                raise CapsuleError(
                    f"{label} has an invalid XP-orb bounding box")
        item_exact = entity.get("item_exact", False)
        if item_exact not in (False, True):
            raise CapsuleError(f"{label}.item_exact must be boolean")
        if item_exact is True and entity_type != "EntityItem":
            raise CapsuleError(f"{label}.item_exact requires EntityItem")
        if entity_type == "EntityItem" and item_exact is True:
            for field in (
                    "item", "count", "meta", "age", "ticks_existed",
                    "pickup_delay",
                    "health", "lifespan", "fire", "entity_seed48"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in (
                    "on_ground", "no_gravity", "in_water", "first_update"):
                if entity.get(field) not in (0, 1, False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            _finite_number(
                entity.get("hover_start"), f"{label}.hover_start")
            if not 1 <= entity["item"] <= 4095 \
                    or not 1 <= entity["count"] <= 64 \
                    or not 0 <= entity["meta"] <= 32767 \
                    or entity["age"] < -32768 \
                    or entity["ticks_existed"] < 0 \
                    or not 0 <= entity["pickup_delay"] <= 32767 \
                    or not 1 <= entity["health"] <= 5 \
                    or entity["lifespan"] <= entity["age"] \
                    or not -1 <= entity["fire"] <= 32767 \
                    or not 0 <= entity["entity_seed48"] < (1 << 48):
                raise CapsuleError(
                    f"{label} has invalid exact plain-item state")
            _validate_item_stack_payload(
                entity.get("stack_payload"), f"{label}.stack_payload")
        falling_exact = entity.get("falling_exact", False)
        if falling_exact not in (False, True):
            raise CapsuleError(f"{label}.falling_exact must be boolean")
        if falling_exact is True and entity_type != "EntityFallingBlock":
            raise CapsuleError(
                f"{label}.falling_exact requires EntityFallingBlock")
        if entity_type == "EntityFallingBlock" and falling_exact is True:
            for field in (
                    "block", "meta", "fall_time",
                    "origin_x", "origin_y", "origin_z"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            if entity.get("uuid_most") is None \
                    or entity.get("uuid_least") is None \
                    or entity["eid"] < 0 \
                    or entity["block"] not in (12, 13) \
                    or entity["meta"] != 0 \
                    or not 1 <= entity["fall_time"] <= 600 \
                    or any(not -(1 << 31) <= entity[field] < (1 << 31)
                           for field in (
                               "origin_x", "origin_y", "origin_z")) \
                    or abs(float(entity["yaw"])) > 1e-12 \
                    or abs(float(entity["pitch"])) > 1e-12 \
                    or float(entity["health"]) != -1.0:
                raise CapsuleError(
                    f"{label} has invalid exact falling-block state")
        primed_tnt_exact = entity.get("primed_tnt_exact", False)
        if primed_tnt_exact not in (False, True):
            raise CapsuleError(
                f"{label}.primed_tnt_exact must be boolean")
        if primed_tnt_exact is True and entity_type != "EntityTNTPrimed":
            raise CapsuleError(
                f"{label}.primed_tnt_exact requires EntityTNTPrimed")
        if entity_type == "EntityTNTPrimed" and primed_tnt_exact is True:
            fuse = entity.get("fuse")
            if isinstance(fuse, bool) or not isinstance(fuse, int) \
                    or entity.get("uuid_most") is None \
                    or entity.get("uuid_least") is None \
                    or entity["eid"] < 0 \
                    or not 1 <= fuse <= 32767 \
                    or abs(float(entity["yaw"])) > 1e-12 \
                    or abs(float(entity["pitch"])) > 1e-12 \
                    or float(entity["health"]) != -1.0:
                raise CapsuleError(
                    f"{label} has invalid exact primed-TNT state")
        end_crystal_exact = entity.get("end_crystal_exact", False)
        if end_crystal_exact not in (False, True):
            raise CapsuleError(
                f"{label}.end_crystal_exact must be boolean")
        if end_crystal_exact is True \
                and entity_type != "EntityEnderCrystal":
            raise CapsuleError(
                f"{label}.end_crystal_exact requires EntityEnderCrystal")
        if entity_type == "EntityEnderCrystal" \
                and end_crystal_exact is True:
            for field in (
                    "inner_rotation", "show_bottom", "has_beam",
                    "beam_x", "beam_y", "beam_z"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            if entity.get("uuid_most") is None \
                    or entity.get("uuid_least") is None \
                    or entity["eid"] <= 0 \
                    or entity["inner_rotation"] < 0 \
                    or entity["show_bottom"] not in (0, 1) \
                    or entity["has_beam"] not in (0, 1) \
                    or abs(float(entity["vx"])) > 1e-15 \
                    or abs(float(entity["vy"])) > 1e-15 \
                    or abs(float(entity["vz"])) > 1e-15 \
                    or abs(float(entity["yaw"])) > 1e-12 \
                    or abs(float(entity["pitch"])) > 1e-12 \
                    or float(entity["health"]) != -1.0:
                raise CapsuleError(
                    f"{label} has invalid exact End-crystal state")
        arrow_exact = entity.get("arrow_exact", False)
        if arrow_exact not in (False, True):
            raise CapsuleError(f"{label}.arrow_exact must be boolean")
        arrow_types = {"EntityTippedArrow", "EntitySpectralArrow"}
        if arrow_exact is True and entity_type not in arrow_types:
            raise CapsuleError(
                f"{label}.arrow_exact requires a supported arrow")
        if entity_type in arrow_types and arrow_exact is True:
            for field in (
                    "ticks_in_air", "fire_ticks", "knockback",
                    "pickup_status", "shake", "ticks_in_ground",
                    "time_in_ground",
                    "tile_x", "tile_y", "tile_z", "tile_block",
                    "tile_meta", "entity_seed48", "arrow_kind",
                    "potion_type", "spectral_duration", "arrow_color",
                    "pickup_item", "pickup_meta"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in (
                    "critical", "in_ground", "entity_have_gaussian",
                    "arrow_custom_color"):
                if entity.get(field) not in (0, 1, False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            for field in ("damage", "entity_gaussian"):
                _finite_number(entity.get(field), f"{label}.{field}")
            effects = entity.get("arrow_effects")
            if not isinstance(effects, list) or len(effects) > 16:
                raise CapsuleError(
                    f"{label}.arrow_effects must contain at most 16 effects")
            effect_ids = set()
            for effect_index, effect in enumerate(effects):
                effect_label = f"{label}.arrow_effects[{effect_index}]"
                if not isinstance(effect, dict) \
                        or set(effect) != {"id", "amp", "dur", "flags"}:
                    raise CapsuleError(
                        f"{effect_label} must contain id/amp/dur/flags")
                if any(isinstance(effect[field], bool)
                       or not isinstance(effect[field], int)
                       for field in ("id", "amp", "dur", "flags")) \
                        or not 1 <= effect["id"] <= 27 \
                        or not 0 <= effect["amp"] <= 255 \
                        or not 1 <= effect["dur"] <= 2147483647 \
                        or not 0 <= effect["flags"] <= 3 \
                        or effect["id"] in effect_ids:
                    raise CapsuleError(
                        f"{effect_label} has invalid or duplicate state")
                effect_ids.add(effect["id"])
            stack_payload = _validate_item_stack_payload(
                entity.get("stack_payload"), f"{label}.stack_payload")
            if entity["ticks_in_air"] < 0 \
                    or entity["fire_ticks"] < -1 \
                    or float(entity["damage"]) < 0.0 \
                    or entity["knockback"] < 0 \
                    or not 0 <= entity["pickup_status"] <= 2 \
                    or not 0 <= entity["shake"] <= 255 \
                    or not 0 <= entity["ticks_in_ground"] <= 1200 \
                    or not 0 <= entity["time_in_ground"] <= 2147483647 \
                    or not 0 <= entity["tile_block"] <= 255 \
                    or not 0 <= entity["tile_meta"] <= 15 \
                    or not 0 <= entity["entity_seed48"] < (1 << 48) \
                    or not 0 <= entity["arrow_kind"] <= 2 \
                    or not 0 <= entity["potion_type"] <= 36 \
                    or entity["spectral_duration"] < 0 \
                    or entity["arrow_color"] < -1 \
                    or entity["pickup_item"] not in (262, 439, 440) \
                    or not 0 <= entity["pickup_meta"] <= 32767 \
                    or (entity_type == "EntitySpectralArrow"
                        and (entity["arrow_kind"] != 2
                             or entity["potion_type"] != 0
                             or effects
                             or entity["arrow_color"] != -1
                             or bool(entity["arrow_custom_color"])
                             or entity["pickup_item"] != 439
                             or entity["pickup_meta"] != 0
                             or stack_payload is not None)) \
                    or (entity_type == "EntityTippedArrow"
                        and entity["arrow_kind"] == 2) \
                    or (entity["arrow_kind"] == 0
                        and (entity["potion_type"] != 0
                             or effects
                             or entity["arrow_color"] != -1
                             or bool(entity["arrow_custom_color"])
                             or entity["pickup_item"] != 262
                             or entity["pickup_meta"] != 0
                             or stack_payload is not None)) \
                    or (entity["arrow_kind"] == 1
                        and (entity["pickup_item"] not in (262, 440)
                             or (entity["pickup_item"] == 262
                                 and (entity["potion_type"] != 0
                                      or effects
                                      or entity["pickup_meta"] != 0
                                      or stack_payload is not None))
                             or (entity["pickup_item"] == 440
                                 and (not effects
                                      and entity["potion_type"] == 0
                                      or stack_payload is None)))):
                raise CapsuleError(f"{label} has invalid exact arrow state")
        throwable_exact = entity.get("throwable_exact", False)
        throwable_types = {
            "EntityEgg", "EntitySnowball", "EntityExpBottle",
            "EntityEnderPearl", "EntityPotion",
        }
        if throwable_exact not in (False, True):
            raise CapsuleError(
                f"{label}.throwable_exact must be boolean")
        if throwable_exact is True and entity_type not in throwable_types:
            raise CapsuleError(
                f"{label}.throwable_exact requires a supported throwable")
        if entity_type in throwable_types and throwable_exact is True:
            for field in (
                    "age", "ticks_in_air", "ignore_player_time",
                    "throwable_shake", "ticks_in_ground",
                    "tile_x", "tile_y", "tile_z", "tile_block",
                    "portal_counter", "portal_cooldown",
                    "last_portal_x", "last_portal_y", "last_portal_z",
                    "teleport_direction",
                    "client_entity_seed48", "entity_seed48",
                    "uuid_most", "uuid_least"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(
                        f"{label}.{field} must be an integer")
            for field in (
                    "player_thrower", "thrower_player_pending",
                    "ignore_player",
                    "pearl_private_thrower", "in_ground", "in_portal",
                    "last_portal_pos_valid",
                    "client_random_valid", "entity_have_gaussian"):
                if entity.get(field) not in (False, True):
                    raise CapsuleError(
                        f"{label}.{field} must be boolean")
            for field in (
                    "prev_yaw", "prev_pitch", "last_portal_vec_x",
                    "last_portal_vec_y", "entity_gaussian"):
                _finite_number(entity.get(field), f"{label}.{field}")
            if entity["age"] < 0 or entity["ticks_in_air"] < 0 \
                    or not -1 <= entity["ignore_player_time"] <= 2 \
                    or not 0 <= entity["throwable_shake"] <= 255 \
                    or not 0 <= entity["ticks_in_ground"] < 1200 \
                    or not 0 <= entity["tile_block"] <= 255 \
                    or not 0 <= entity["portal_counter"] <= 1 \
                    or entity["in_portal"] \
                    or entity["portal_cooldown"] < 0 \
                    or not 0 <= entity["teleport_direction"] <= 3 \
                    or (not entity["last_portal_pos_valid"]
                        and (entity["last_portal_x"] != 0
                            or entity["last_portal_y"] != 0
                            or entity["last_portal_z"] != 0
                            or float(entity["last_portal_vec_x"]) != 0.0
                            or float(entity["last_portal_vec_y"]) != 0.0
                            or entity["teleport_direction"] != 0)) \
                    or not 0 <= entity["client_entity_seed48"] < (1 << 48) \
                    or not 0 <= entity["entity_seed48"] < (1 << 48) \
                    or (entity["player_thrower"]
                        and entity["thrower_player_pending"]) \
                    or (entity["ignore_player"]
                        and not entity["player_thrower"]) \
                    or (entity["pearl_private_thrower"]
                        and entity_type != "EntityEnderPearl"):
                raise CapsuleError(
                    f"{label} has invalid exact throwable state")
        potion_exact = entity.get("potion_exact", False)
        if potion_exact not in (False, True):
            raise CapsuleError(f"{label}.potion_exact must be boolean")
        if potion_exact is True and entity_type != "EntityPotion":
            raise CapsuleError(
                f"{label}.potion_exact requires EntityPotion")
        if entity_type == "EntityPotion" and potion_exact is True:
            for field in (
                    "potion_item", "potion_type", "age",
                    "ignore_player_time", "potion_color"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in (
                    "player_thrower", "ignore_player",
                    "potion_custom_color"):
                if entity.get(field) not in (False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            _validate_potion_effect_payload(
                entity.get("potion_effects"), f"{label}.potion_effects")
            _validate_item_stack_payload(
                entity.get("stack_payload"), f"{label}.stack_payload")
            if entity["potion_item"] not in (438, 441) \
                    or not 0 <= entity["potion_type"] <= 36 \
                    or not 0 <= entity["age"] < 1200 \
                    or not -1 <= entity["ignore_player_time"] <= 2 \
                    or (entity["ignore_player"]
                        and not entity["player_thrower"]):
                raise CapsuleError(
                    f"{label} has invalid exact thrown-potion state")
        cloud_exact = entity.get("cloud_exact", False)
        if cloud_exact not in (False, True):
            raise CapsuleError(f"{label}.cloud_exact must be boolean")
        if cloud_exact is True and entity_type != "EntityAreaEffectCloud":
            raise CapsuleError(
                f"{label}.cloud_exact requires EntityAreaEffectCloud")
        if entity_type == "EntityAreaEffectCloud" and cloud_exact is True:
            if entity.get("cloud_common_exact") is not True:
                raise CapsuleError(
                    f"{label} lacks exact common Entity state")
            for field in (
                    "potion_type", "age", "duration", "wait_time",
                    "reapplication_delay", "next_application",
                    "potion_color", "duration_on_use", "particle",
                    "particle_param1", "particle_param2",
                    "uuid_most", "uuid_least", "owner_eid",
                    "owner_uuid_most", "owner_uuid_least",
                    "dimension", "air", "fire", "portal_cooldown",
                    "server_entity_seed48"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in (
                    "player_owner", "potion_custom_color", "ignore_radius",
                    "owner_present", "on_ground", "no_gravity",
                    "invulnerable", "silent", "glowing", "update_blocked",
                    "in_water", "first_update",
                    "server_entity_have_gaussian"):
                if entity.get(field) not in (False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            _validate_potion_effect_payload(
                entity.get("potion_effects"), f"{label}.potion_effects")
            deadlines = entity.get("reapplication_deadlines")
            if not isinstance(deadlines, list) or len(deadlines) > 97:
                raise CapsuleError(
                    f"{label}.reapplication_deadlines must be an array "
                    "of at most 97 entries")
            previous_target = -1
            player_deadline = 0
            for deadline_index, deadline in enumerate(deadlines):
                deadline_label = (
                    f"{label}.reapplication_deadlines[{deadline_index}]")
                if not isinstance(deadline, dict) \
                        or set(deadline) != {"eid", "deadline"}:
                    raise CapsuleError(
                        f"{deadline_label} must contain eid and deadline")
                target_eid = deadline["eid"]
                target_tick = deadline["deadline"]
                if isinstance(target_eid, bool) \
                        or not isinstance(target_eid, int) \
                        or not 0 <= target_eid <= 2147483647 \
                        or isinstance(target_tick, bool) \
                        or not isinstance(target_tick, int) \
                        or not 1 <= target_tick <= 2147483647 \
                        or target_eid <= previous_target:
                    raise CapsuleError(
                        f"{deadline_label} has invalid or non-canonical state")
                previous_target = target_eid
                if target_eid == player_eid:
                    player_deadline = target_tick
            if player_deadline != entity["next_application"]:
                raise CapsuleError(
                    f"{label}.next_application disagrees with its "
                    "player deadline")
            if not -(1 << 63) <= entity["uuid_most"] < (1 << 63) \
                    or not -(1 << 63) <= entity["uuid_least"] < (1 << 63) \
                    or not -(1 << 63) <= entity["owner_uuid_most"] \
                        < (1 << 63) \
                    or not -(1 << 63) <= entity["owner_uuid_least"] \
                        < (1 << 63) \
                    or not -1 <= entity["owner_eid"] <= 2147483647 \
                    or (not entity["owner_present"] and (
                        entity["owner_eid"] != -1
                        or entity["owner_uuid_most"] != 0
                        or entity["owner_uuid_least"] != 0)) \
                    or (entity["owner_present"]
                        and entity["owner_eid"] < 0) \
                    or entity["player_owner"] != (
                        entity["owner_present"]
                        and entity["owner_eid"] == player_eid):
                raise CapsuleError(
                    f"{label} has invalid exact cloud identity/owner state")
            for field in ("radius", "radius_on_use", "radius_per_tick"):
                _finite_number(entity.get(field), f"{label}.{field}")
            for field in ("prev_yaw", "prev_pitch"):
                _finite_number(entity.get(field), f"{label}.{field}")
            for field in (
                    "fall_distance", "prev_x", "prev_y", "prev_z",
                    "last_tick_x", "last_tick_y", "last_tick_z",
                    "server_entity_gaussian"):
                _finite_number(entity.get(field), f"{label}.{field}")
            if not 0 <= entity["potion_type"] <= 36 \
                    or entity["age"] < 0 \
                    or entity["duration"] <= 0 \
                    or entity["wait_time"] < 0 \
                    or entity["reapplication_delay"] < 0 \
                    or entity["next_application"] < 0 \
                    or not 0 <= entity["particle"] <= 48 \
                    or entity["dimension"] != dimension \
                    or not -32768 <= entity["air"] <= 32767 \
                    or not -32768 <= entity["fire"] <= 32767 \
                    or not 0 <= entity["portal_cooldown"] <= 2147483647 \
                    or float(entity["fall_distance"]) < 0.0 \
                    or not 0 <= entity["server_entity_seed48"] < (1 << 48) \
                    or entity["age"] >= (
                        entity["wait_time"] + entity["duration"]) \
                    or float(entity["radius"]) < 0.5:
                raise CapsuleError(
                    f"{label} has invalid exact area-effect-cloud state")
        firework_exact = entity.get("firework_exact", False)
        if firework_exact not in (False, True):
            raise CapsuleError(f"{label}.firework_exact must be boolean")
        if firework_exact is True \
                and entity_type != "EntityFireworkRocket":
            raise CapsuleError(
                f"{label}.firework_exact requires EntityFireworkRocket")
        if entity_type == "EntityFireworkRocket" \
                and firework_exact is True:
            for field in (
                    "firework_age", "lifetime", "ticks_existed", "flight",
                    "explosion_count", "firework_item", "firework_count",
                    "firework_meta", "entity_seed48", "uuid_most",
                    "uuid_least"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in (
                    "attached_player", "large_blast", "twinkle",
                    "firework_item_present", "entity_have_gaussian"):
                if entity.get(field) not in (False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            for field in ("prev_yaw", "prev_pitch", "entity_gaussian"):
                _finite_number(entity.get(field), f"{label}.{field}")
            item_present = entity["firework_item_present"]
            if entity["firework_age"] < 0 \
                    or entity["lifetime"] < entity["firework_age"] \
                    or entity["ticks_existed"] < 0 \
                    or not 0 <= entity["flight"] <= 3 \
                    or not 0 <= entity["explosion_count"] <= 8 \
                    or not 0 <= entity["entity_seed48"] < (1 << 48) \
                    or (item_present and (
                        entity["firework_item"] != 401
                        or entity["firework_count"] != 1
                        or entity["firework_meta"] != 0)) \
                    or (not item_present and (
                        entity["firework_item"] != 0
                        or entity["firework_count"] != 0
                        or entity["firework_meta"] != 0
                        or entity["flight"] != 0
                        or entity["explosion_count"] != 0
                        or entity["large_blast"]
                        or entity["twinkle"]
                        or entity.get("stack_payload") is not None)) \
                    or (entity["explosion_count"] == 0
                        and (entity["large_blast"] or entity["twinkle"])):
                raise CapsuleError(
                    f"{label} has invalid exact firework state")
            _validate_item_stack_payload(
                entity.get("stack_payload"), f"{label}.stack_payload")
        if entity_type == "EntityFishHook":
            fish_hooks.append(entity)
            for field in (
                    "fish_state", "ticks_in_ground", "ticks_in_air",
                    "ticks_catchable", "ticks_caught_delay",
                    "ticks_catchable_delay", "lure", "luck",
                    "caught_eid", "entity_seed48"):
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in ("fish_approach_angle", "entity_gaussian"):
                _finite_number(entity.get(field), f"{label}.{field}")
            for field in ("in_ground", "entity_have_gaussian"):
                if entity.get(field) not in (0, 1, False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            if entity["fish_state"] not in (0, 1, 2) \
                    or entity["ticks_in_ground"] < 0 \
                    or entity["ticks_in_air"] < 0 \
                    or entity["ticks_catchable"] < 0 \
                    or entity["ticks_catchable_delay"] < 0 \
                    or not 0 <= entity["lure"] <= 3 \
                    or not 0 <= entity["luck"] <= 3 \
                    or entity["caught_eid"] < 0 \
                    or not 0 <= entity["entity_seed48"] < (1 << 48):
                raise CapsuleError(f"{label} has invalid fishing-hook state")
        minecart_types = {
            "EntityMinecartEmpty": (0, 0),
            "EntityMinecartChest": (1, 27),
            "EntityMinecartFurnace": (2, 0),
            "EntityMinecartTNT": (3, 0),
            "EntityMinecartMobSpawner": (4, 0),
            "EntityMinecartHopper": (5, 5),
        }
        if entity_type in minecart_types:
            expected_kind, inventory_size = minecart_types[entity_type]
            integer_fields = (
                "minecart_kind", "rolling_amplitude",
                "rolling_direction", "fuel", "tnt_fuse",
                "transfer_cooldown", "entity_seed48",
            )
            for field in integer_fields:
                value = entity.get(field)
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            for field in ("damage", "push_x", "push_z", "entity_gaussian"):
                _finite_number(entity.get(field), f"{label}.{field}")
            for field in ("reverse", "hopper_enabled", "entity_have_gaussian"):
                if entity.get(field) not in (0, 1, False, True):
                    raise CapsuleError(f"{label}.{field} must be boolean")
            if entity["minecart_kind"] != expected_kind \
                    or entity["rolling_amplitude"] < 0 \
                    or entity["rolling_direction"] == 0 \
                    or float(entity["damage"]) < 0.0 \
                    or not -32768 <= entity["fuel"] <= 32767 \
                    or entity["tnt_fuse"] < -1 \
                    or not -(1 << 31) <= entity["transfer_cooldown"] \
                        < (1 << 31) \
                    or not 0 <= entity["entity_seed48"] < (1 << 48):
                raise CapsuleError(f"{label} has invalid minecart state")
            items = entity.get("items")
            if not isinstance(items, list):
                raise CapsuleError(f"{label}.items must be an array")
            cart_slots = set()
            for item_index, item in enumerate(items):
                item_label = f"{label}.items[{item_index}]"
                if not isinstance(item, dict) or set(item) not in ({
                        "slot", "id", "count", "meta"}, {
                        "slot", "id", "count", "meta", "stack_payload"}):
                    raise CapsuleError(
                        f"{item_label} must contain slot/id/count/meta and "
                        "optional stack_payload")
                values = tuple(item[field]
                               for field in ("slot", "id", "count", "meta"))
                if any(isinstance(value, bool) or not isinstance(value, int)
                       for value in values):
                    raise CapsuleError(f"{item_label} values must be integers")
                if item["slot"] in cart_slots \
                        or not 0 <= item["slot"] < inventory_size \
                        or not 1 <= item["id"] <= 4095 \
                        or not 1 <= item["count"] <= 64 \
                        or not 0 <= item["meta"] <= 32767:
                    raise CapsuleError(f"{item_label} has an invalid stack")
                _validate_item_stack_payload(
                    item.get("stack_payload"),
                    f"{item_label}.stack_payload")
                cart_slots.add(item["slot"])
            if entity_type == "EntityMinecartMobSpawner":
                for field in (
                        "spawner_delay", "spawner_min_delay",
                        "spawner_max_delay", "spawner_spawn_count",
                        "spawner_max_nearby", "spawner_activate_range",
                        "spawner_spawn_range"):
                    value = entity.get(field)
                    if isinstance(value, bool) or not isinstance(value, int):
                        raise CapsuleError(
                            f"{label}.{field} must be an integer")
                if not -1 <= entity["spawner_delay"] <= 32767 \
                        or any(not 0 <= entity[field] <= 32767 for field in (
                            "spawner_min_delay", "spawner_max_delay",
                            "spawner_spawn_count", "spawner_max_nearby",
                            "spawner_activate_range", "spawner_spawn_range")):
                    raise CapsuleError(
                        f"{label} has an invalid spawner scalar range")
                entity_id = entity.get("spawner_entity_id")
                if isinstance(entity_id, str) and ":" not in entity_id:
                    entity_id = "minecraft:" + entity_id.lower()
                _raw, nbt_id, is_default = _spawner_nbt_identity(
                    entity.get("spawner_spawn_data_nbt"),
                    f"{label}.spawner_spawn_data_nbt")
                if entity_id not in SPAWNER_ENTITY_TYPES \
                        or nbt_id != entity_id \
                        or not isinstance(
                            entity.get("spawner_default_entity_nbt"), bool) \
                        or entity["spawner_default_entity_nbt"] \
                            is not is_default:
                    raise CapsuleError(
                        f"{label} has inconsistent minecart SpawnData")
                potentials = entity.get("spawner_potentials")
                if not isinstance(potentials, list) \
                        or len(potentials) > 16:
                    raise CapsuleError(
                        f"{label}.spawner_potentials exceeds the exact "
                        "16-entry bound")
                total_weight = 0
                for potential_index, potential in enumerate(potentials):
                    potential_label = (
                        f"{label}.spawner_potentials[{potential_index}]")
                    if not isinstance(potential, dict) \
                            or set(potential) != {
                                "weight", "entity_id", "entity_nbt",
                                "default_entity_nbt"}:
                        raise CapsuleError(
                            f"{potential_label} has an incomplete schema")
                    potential_id = potential["entity_id"]
                    if isinstance(potential_id, str) \
                            and ":" not in potential_id:
                        potential_id = (
                            "minecraft:" + potential_id.lower())
                    weight = potential["weight"]
                    _raw, nbt_id, nbt_default = _spawner_nbt_identity(
                        potential["entity_nbt"],
                        f"{potential_label}.entity_nbt")
                    if potential_id not in SPAWNER_ENTITY_TYPES \
                            or nbt_id != potential_id \
                            or not isinstance(
                                potential["default_entity_nbt"], bool) \
                            or potential["default_entity_nbt"] \
                                is not nbt_default \
                            or isinstance(weight, bool) \
                            or not isinstance(weight, int) or weight <= 0:
                        raise CapsuleError(
                            f"{potential_label} has inconsistent "
                            "entity NBT/weight")
                    total_weight += weight
                    if total_weight > 2147483647:
                        raise CapsuleError(
                            f"{label}.spawner_potentials total weight "
                            "overflows Java int")
    if loaded_order_count not in (0, len(entities)):
        raise CapsuleError(
            "state.entities must either all include loaded_order or all omit it"
        )
    entity_by_eid = {entity["eid"]: entity for entity in entities}
    player_uuid_for_cloud = tuple(
        value - (1 << 64) if value >= (1 << 63) else value
        for value in (
            int(player.get(
                "uuid_most_hex", "a01e3843e5213998"), 16),
            int(player.get(
                "uuid_least_hex", "958af459800e4d11"), 16),
        )
    )
    for index, cloud in enumerate(entities):
        if cloud["type"] != "EntityAreaEffectCloud" \
                or cloud.get("cloud_exact") is not True:
            continue
        for deadline in cloud["reapplication_deadlines"]:
            target_eid = deadline["eid"]
            if target_eid == player_eid:
                continue
            target = entity_by_eid.get(target_eid)
            if target is None \
                    or not entity_payload_is_living_restorable(target):
                raise CapsuleError(
                    f"state.entities[{index}] cloud deadline target "
                    f"{target_eid} is not an exact restored living entity")
        if not cloud["owner_present"]:
            continue
        owner_eid = cloud["owner_eid"]
        owner_uuid = (
            cloud["owner_uuid_most"], cloud["owner_uuid_least"])
        if owner_eid == player_eid:
            owner_exact = owner_uuid == player_uuid_for_cloud
        else:
            owner = entity_by_eid.get(owner_eid)
            owner_exact = owner is not None \
                and entity_payload_is_living_restorable(owner) \
                and owner_uuid == (
                    owner.get("uuid_most"), owner.get("uuid_least"))
        if not owner_exact:
            raise CapsuleError(
                f"state.entities[{index}] cloud owner is not an exact "
                "restored living identity")
    if riding_eid >= 0:
        vehicle = entity_by_eid.get(riding_eid)
        if vehicle is None or vehicle.get("type") != "EntityMinecartEmpty":
            raise CapsuleError(
                "state.player.riding_eid requires an exact rideable minecart")
    for index, entity in enumerate(entities):
        if entity["type"] != "EntityShulkerBullet" \
                or entity.get("shulker_bullet_exact") is not True:
            continue
        owner = entity_by_eid.get(entity["owner_eid"])
        if owner is None or owner["type"] != "EntityShulker" \
                or owner.get("shulker_exact") is not True:
            raise CapsuleError(
                f"state.entities[{index}] exact shulker bullet requires "
                "its exact restored shulker owner")
    exact_llamas = {
        entity["eid"]: entity for entity in entities
        if entity["type"] == "EntityLlama"
        and entity.get("horse_exact") is True
    }
    player_uuid = tuple(
        value - (1 << 64) if value >= (1 << 63) else value
        for value in (
            int(player.get("uuid_most_hex", "a01e3843e5213998"), 16),
            int(player.get("uuid_least_hex", "958af459800e4d11"), 16),
        )
    )
    raw_knots = state.get("leash_knots")
    knot_by_eid = {
        knot.get("eid"): knot for knot in raw_knots
        if isinstance(knot, dict) and isinstance(knot.get("eid"), int)
    } if isinstance(raw_knots, list) else {}
    for eid, llama in exact_llamas.items():
        holder_kind = llama["llama_leash_holder_kind"]
        holder_eid = llama["llama_leash_holder_eid"]
        holder_uuid = (
            llama["llama_leash_holder_uuid_most"],
            llama["llama_leash_holder_uuid_least"],
        )
        pending = llama["llama_leash_pending"]
        if holder_kind == 0:
            if (pending and (not llama["llama_leashed"]
                    or holder_eid != -1 or holder_uuid != (0, 0))) \
                    or (not pending and (llama["llama_leashed"]
                        or holder_eid != -1 or holder_uuid != (0, 0))):
                raise CapsuleError(
                    f"exact llama {eid} has an inconsistent empty leash")
        elif holder_kind == 1:
            if pending or not llama["llama_leashed"] \
                    or holder_eid != player_eid \
                    or holder_uuid != player_uuid:
                raise CapsuleError(
                    f"exact llama {eid} has an inconsistent player leash")
        elif holder_kind == 2:
            holder = entity_by_eid.get(holder_eid)
            if pending or not llama["llama_leashed"] or holder_eid == eid \
                    or holder is None \
                    or not entity_payload_is_restorable(holder) \
                    or float(holder.get("health", -1.0)) < 0.0 \
                    or holder_uuid != (
                        holder.get("uuid_most"), holder.get("uuid_least")):
                raise CapsuleError(
                    f"exact llama {eid} requires its exact living "
                    "leash holder")
        else:
            knot = knot_by_eid.get(holder_eid)
            if pending or not llama["llama_leashed"] or holder_eid == eid \
                    or knot is None \
                    or holder_uuid != (
                        knot.get("uuid_most"), knot.get("uuid_least")):
                raise CapsuleError(
                    f"exact llama {eid} requires its exact leash knot")
        head_eid = llama["llama_caravan_head_eid"]
        tail_eid = llama["llama_caravan_tail_eid"]
        if head_eid == eid or tail_eid == eid:
            raise CapsuleError(f"exact llama {eid} links to itself")
        if head_eid >= 0:
            head = exact_llamas.get(head_eid)
            if head is None or head["llama_caravan_tail_eid"] != eid:
                raise CapsuleError(
                    f"exact llama {eid} has a non-reciprocal caravan head")
        if tail_eid >= 0:
            tail = exact_llamas.get(tail_eid)
            if tail is None or tail["llama_caravan_head_eid"] != eid:
                raise CapsuleError(
                    f"exact llama {eid} has a non-reciprocal caravan tail")
        seen_caravan = {eid}
        cursor = head_eid
        while cursor >= 0:
            if cursor in seen_caravan or len(seen_caravan) > 8:
                raise CapsuleError(
                    f"exact llama {eid} has a cyclic or overlong caravan")
            seen_caravan.add(cursor)
            cursor = exact_llamas[cursor]["llama_caravan_head_eid"]
    if len(fish_hooks) > 1:
        raise CapsuleError("state.entities contains multiple fishing hooks")
    if fish_hooks:
        hook = fish_hooks[0]
        if state["player"].get("held_id") != 346:
            raise CapsuleError(
                "an exact fishing hook requires the selected fishing rod")
        caught_eid = hook["caught_eid"]
        caught = next(
            (entity for entity in entities if entity["eid"] == caught_eid),
            None,
        ) if caught_eid else None
        if hook["fish_state"] == 2 and caught_eid:
            raise CapsuleError("a bobbing fishing hook cannot hold an entity")
        if hook["fish_state"] == 1 and caught is None:
            raise CapsuleError("a hooked fishing hook requires its target")
        if caught is not None and not (
                caught["type"] == "EntityPig"
                and caught.get("no_ai_pig_exact") is True):
            raise CapsuleError(
                "fishing-hook target is not an exact restorable NoAI pig")
    moving_pistons = state.get("moving_pistons")
    if not isinstance(moving_pistons, list):
        raise CapsuleError("state.moving_pistons must be an array")
    if state.get("moving_pistons_complete") is not True:
        raise CapsuleError(
            "state.moving_pistons_complete must be true for a capsule"
        )
    if len(moving_pistons) > 64:
        raise CapsuleError(
            "state.moving_pistons exceeds the exact 64-entry runtime bound"
        )
    piston_fields = {
        "x", "y", "z", "moved_block", "moved_meta", "facing",
        "extending", "source", "progress_bits", "last_progress_bits",
    }
    seen_pistons = set()
    for index, piston in enumerate(moving_pistons):
        label = f"state.moving_pistons[{index}]"
        if not isinstance(piston, dict) or set(piston) != piston_fields:
            raise CapsuleError(
                f"{label} must contain exactly "
                + ", ".join(sorted(piston_fields))
            )
        for field in (
                "x", "y", "z", "moved_block", "moved_meta", "facing",
                "progress_bits", "last_progress_bits"):
            value = piston[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not isinstance(piston["extending"], bool) \
                or not isinstance(piston["source"], bool):
            raise CapsuleError(
                f"{label}.extending and source must be booleans"
            )
        if not 0 <= piston["y"] <= 255 \
                or not 1 <= piston["moved_block"] <= 4095 \
                or not 0 <= piston["moved_meta"] <= 15 \
                or not 0 <= piston["facing"] <= 5:
            raise CapsuleError(f"{label} has invalid block or facing state")
        position = (piston["x"], piston["y"], piston["z"])
        if position in seen_pistons:
            raise CapsuleError(f"{label} duplicates a moving-piston position")
        seen_pistons.add(position)
        progress_values = []
        for field in ("progress_bits", "last_progress_bits"):
            bits = piston[field]
            if not 0 <= bits <= 0xFFFFFFFF:
                raise CapsuleError(f"{label}.{field} is outside uint32")
            value = struct.unpack("<f", struct.pack("<I", bits))[0]
            if not math.isfinite(value) or not 0.0 <= value <= 1.0:
                raise CapsuleError(f"{label}.{field} is not in [0,1]")
            progress_values.append(value)
        progress, last_progress = progress_values
        if last_progress > progress or progress - last_progress > 0.5:
            raise CapsuleError(f"{label} has an impossible progress interval")
    item_frames = state.get("item_frames")
    if not isinstance(item_frames, list):
        raise CapsuleError("state.item_frames must be an array")
    if state.get("item_frames_complete") is not True:
        raise CapsuleError(
            "state.item_frames_complete must be true for a capsule"
        )
    if len(item_frames) > 256:
        raise CapsuleError(
            "state.item_frames exceeds the exact 256-entry runtime bound"
        )
    seen_hanging_positions = set()
    seen_hanging_orders = set()
    frame_fields = {
        "eid", "x", "y", "z",
        "hanging_x", "hanging_y", "hanging_z",
        "facing", "item", "count", "meta", "rotation",
        "tick_counter", "item_drop_chance",
        "entity_seed48", "entity_have_gaussian", "entity_gaussian",
        "uuid_most", "uuid_least", "loaded_order",
        "repair_cost", "custom_name", "enchants",
        "tracker_update_counter", "map_data_present", "map_colors_b64",
        "map_dimension",
        "map_x_center", "map_z_center", "map_scale",
        "map_tracking_position", "map_unlimited_tracking",
        "map_decoration_present", "map_decoration_type",
        "map_decoration_x", "map_decoration_z",
        "map_decoration_rotation",
    }
    for index, frame in enumerate(item_frames):
        label = f"state.item_frames[{index}]"
        if not isinstance(frame, dict) or set(frame) not in (
                frame_fields, frame_fields | {"stack_payload"}):
            raise CapsuleError(
                f"{label} must contain exactly "
                + ", ".join(sorted(frame_fields))
                + " and optional stack_payload"
            )
        eid = frame["eid"]
        if isinstance(eid, bool) or not isinstance(eid, int) or eid < 0:
            raise CapsuleError(f"{label}.eid must be a nonnegative integer")
        if eid in seen_eids:
            raise CapsuleError(f"{label}.eid must be globally unique")
        seen_eids.add(eid)
        for field in ("x", "y", "z"):
            _finite_number(frame[field], f"{label}.{field}")
        for field in (
            "hanging_x", "hanging_y", "hanging_z",
            "facing", "item", "count", "meta", "rotation",
            "tick_counter", "entity_seed48", "loaded_order",
            "repair_cost", "uuid_most", "uuid_least",
            "tracker_update_counter", "map_dimension",
            "map_x_center", "map_z_center", "map_scale",
            "map_decoration_type", "map_decoration_x",
            "map_decoration_z", "map_decoration_rotation",
        ):
            value = frame[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        hanging = (
            frame["hanging_x"], frame["hanging_y"], frame["hanging_z"])
        if hanging in seen_hanging_positions:
            raise CapsuleError(f"{label} duplicates a hanging position")
        seen_hanging_positions.add(hanging)
        uuid = (frame["uuid_most"], frame["uuid_least"])
        if any(not -(1 << 63) <= value < (1 << 63) for value in uuid) \
                or uuid in seen_entity_uuids:
            raise CapsuleError(f"{label} has an invalid or duplicate UUID")
        seen_entity_uuids.add(uuid)
        if not 0 <= frame["hanging_y"] <= 255 \
                or not 2 <= frame["facing"] <= 5 \
                or not 0 <= frame["rotation"] <= 7 \
                or not 0 <= frame["tick_counter"] <= 100 \
                or not 0 <= frame["entity_seed48"] < 1 << 48 \
                or frame["loaded_order"] < 0 \
                or frame["repair_cost"] < 0 \
                or frame["tracker_update_counter"] < 0 \
                or not 0 <= frame["map_scale"] <= 4 \
                or not 0 <= frame["map_decoration_type"] <= 9 \
                or not -128 <= frame["map_decoration_x"] <= 127 \
                or not -128 <= frame["map_decoration_z"] <= 127 \
                or not -128 <= frame["map_decoration_rotation"] <= 127:
            raise CapsuleError(f"{label} has invalid hanging state")
        if frame["loaded_order"] in seen_hanging_orders:
            raise CapsuleError(f"{label}.loaded_order must be unique")
        seen_hanging_orders.add(frame["loaded_order"])
        _finite_number(
            frame["item_drop_chance"], f"{label}.item_drop_chance")
        _finite_number(
            frame["entity_gaussian"], f"{label}.entity_gaussian")
        if not isinstance(frame["entity_have_gaussian"], bool):
            raise CapsuleError(
                f"{label}.entity_have_gaussian must be a boolean")
        for field in (
                "map_data_present", "map_tracking_position",
                "map_unlimited_tracking", "map_decoration_present"):
            if not isinstance(frame[field], bool):
                raise CapsuleError(f"{label}.{field} must be a boolean")
        if frame["map_decoration_present"] \
                and not frame["map_data_present"]:
            raise CapsuleError(
                f"{label} has a decoration without map data")
        if not frame["map_data_present"] and any((
                frame["map_dimension"], frame["map_x_center"],
                frame["map_z_center"], frame["map_scale"],
                frame["map_tracking_position"],
                frame["map_unlimited_tracking"],
                frame["map_decoration_present"],
                frame["map_decoration_type"],
                frame["map_decoration_x"], frame["map_decoration_z"],
                frame["map_decoration_rotation"])):
            raise CapsuleError(f"{label} has state for an absent map")
        if frame["map_data_present"] and frame["item"] != 358:
            raise CapsuleError(f"{label} has map data without a filled map")
        map_colors = _validate_map_colors_b64(
            frame["map_colors_b64"], f"{label}.map_colors_b64")
        if bool(map_colors) != frame["map_data_present"]:
            raise CapsuleError(
                f"{label}.map_colors_b64 presence disagrees with map data")
        if not isinstance(frame["custom_name"], str):
            raise CapsuleError(f"{label}.custom_name must be a string")
        enchants = frame["enchants"]
        if not isinstance(enchants, list) or len(enchants) > 8:
            raise CapsuleError(f"{label}.enchants must have at most 8 rows")
        for enchant_index, enchant in enumerate(enchants):
            if not isinstance(enchant, list) or len(enchant) != 2 \
                    or any(isinstance(value, bool)
                           or not isinstance(value, int)
                           or not -(1 << 15) <= value < 1 << 15
                           for value in enchant):
                raise CapsuleError(
                    f"{label}.enchants[{enchant_index}] is invalid")
        if not 0 <= frame["item"] <= 4095 \
                or not 0 <= frame["meta"] <= 32767 \
                or not ((frame["item"] == 0
                         and frame["count"] == 0
                         and frame["meta"] == 0
                         and frame["repair_cost"] == 0
                         and frame["custom_name"] == ""
                         and not enchants
                         and "stack_payload" not in frame)
                        or (frame["item"] > 0
                            and frame["count"] == 1)):
            raise CapsuleError(f"{label} has an invalid displayed stack")
        if "stack_payload" in frame:
            _validate_item_stack_payload(
                frame["stack_payload"], f"{label}.stack_payload")
    paintings = state.get("paintings")
    if not isinstance(paintings, list):
        raise CapsuleError("state.paintings must be an array")
    if state.get("paintings_complete") is not True:
        raise CapsuleError("state.paintings_complete must be true for a capsule")
    if len(paintings) > 256:
        raise CapsuleError(
            "state.paintings exceeds the exact 256-entry runtime bound")
    painting_fields = {
        "eid", "uuid_most", "uuid_least", "x", "y", "z",
        "hanging_x", "hanging_y", "hanging_z",
        "facing", "art", "tick_counter", "loaded_order",
    }
    art_sizes = (
        *((16, 16),) * 7,
        *((32, 16),) * 5,
        *((16, 32),) * 2,
        *((32, 32),) * 6,
        (64, 32),
        *((64, 64),) * 3,
        *((64, 48),) * 2,
    )
    facing_offsets = {
        2: (0, -1), 3: (0, 1), 4: (-1, 0), 5: (1, 0),
    }
    side_offsets = {
        2: (-1, 0), 3: (1, 0), 4: (0, 1), 5: (0, -1),
    }
    for index, painting in enumerate(paintings):
        label = f"state.paintings[{index}]"
        if not isinstance(painting, dict) or set(painting) != painting_fields:
            raise CapsuleError(
                f"{label} must contain exactly "
                + ", ".join(sorted(painting_fields)))
        for field in (
                "eid", "uuid_most", "uuid_least",
                "hanging_x", "hanging_y", "hanging_z",
                "facing", "art", "tick_counter", "loaded_order"):
            value = painting[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if painting["eid"] < 0 or painting["eid"] in seen_eids:
            raise CapsuleError(
                f"{label}.eid {painting['eid']} must be globally unique")
        seen_eids.add(painting["eid"])
        uuid = (painting["uuid_most"], painting["uuid_least"])
        if any(not -(1 << 63) <= value < (1 << 63) for value in uuid) \
                or uuid in seen_entity_uuids:
            raise CapsuleError(f"{label} has an invalid or duplicate UUID")
        seen_entity_uuids.add(uuid)
        for field in ("x", "y", "z"):
            _finite_number(painting[field], f"{label}.{field}")
        if not 0 <= painting["hanging_y"] <= 255 \
                or painting["facing"] not in facing_offsets \
                or not 0 <= painting["art"] < len(art_sizes) \
                or not 0 <= painting["tick_counter"] <= 100 \
                or painting["loaded_order"] < 0:
            raise CapsuleError(f"{label} has invalid hanging state")
        if painting["loaded_order"] in seen_hanging_orders:
            raise CapsuleError(f"{label}.loaded_order must be unique")
        seen_hanging_orders.add(painting["loaded_order"])
        hanging = (
            painting["hanging_x"], painting["hanging_y"],
            painting["hanging_z"])
        if hanging in seen_hanging_positions:
            raise CapsuleError(f"{label} duplicates a hanging position")
        seen_hanging_positions.add(hanging)
        width, height = art_sizes[painting["art"]]
        face_x, face_z = facing_offsets[painting["facing"]]
        side_x, side_z = side_offsets[painting["facing"]]
        expected = (
            painting["hanging_x"] + 0.5 - face_x * 0.46875
                + (0.5 if width % 32 == 0 else 0.0) * side_x,
            painting["hanging_y"] + 0.5
                + (0.5 if height % 32 == 0 else 0.0),
            painting["hanging_z"] + 0.5 - face_z * 0.46875
                + (0.5 if width % 32 == 0 else 0.0) * side_z,
        )
        if tuple(painting[field] for field in ("x", "y", "z")) != expected:
            raise CapsuleError(f"{label} has a non-canonical pose")
    leash_knots = state.get("leash_knots")
    if not isinstance(leash_knots, list):
        raise CapsuleError("state.leash_knots must be an array")
    if state.get("leash_knots_complete") is not True:
        raise CapsuleError(
            "state.leash_knots_complete must be true for a capsule")
    if len(leash_knots) > 256:
        raise CapsuleError(
            "state.leash_knots exceeds the exact 256-entry runtime bound")
    knot_fields = {
        "eid", "uuid_most", "uuid_least", "x", "y", "z",
        "hanging_x", "hanging_y", "hanging_z", "tick_counter",
        "loaded_order",
    }
    for index, knot in enumerate(leash_knots):
        label = f"state.leash_knots[{index}]"
        if not isinstance(knot, dict) or set(knot) != knot_fields:
            raise CapsuleError(
                f"{label} must contain exactly "
                + ", ".join(sorted(knot_fields)))
        for field in (
                "eid", "uuid_most", "uuid_least",
                "hanging_x", "hanging_y", "hanging_z", "tick_counter",
                "loaded_order"):
            value = knot[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if knot["eid"] < 0 or knot["eid"] in seen_eids:
            raise CapsuleError(
                f"{label}.eid {knot['eid']} must be globally unique")
        seen_eids.add(knot["eid"])
        uuid = (knot["uuid_most"], knot["uuid_least"])
        if any(not -(1 << 63) <= value < (1 << 63) for value in uuid) \
                or uuid in seen_entity_uuids:
            raise CapsuleError(f"{label} has an invalid or duplicate UUID")
        seen_entity_uuids.add(uuid)
        for field in ("x", "y", "z"):
            _finite_number(knot[field], f"{label}.{field}")
        if not 0 <= knot["hanging_y"] <= 255 \
                or not 0 <= knot["tick_counter"] <= 100 \
                or knot["loaded_order"] < 0:
            raise CapsuleError(f"{label} has invalid hanging state")
        if knot["loaded_order"] in seen_hanging_orders:
            raise CapsuleError(f"{label}.loaded_order must be unique")
        seen_hanging_orders.add(knot["loaded_order"])
        hanging = (
            knot["hanging_x"], knot["hanging_y"], knot["hanging_z"])
        if hanging in seen_hanging_positions:
            raise CapsuleError(f"{label} duplicates a hanging position")
        seen_hanging_positions.add(hanging)
        expected = tuple(value + 0.5 for value in hanging)
        if tuple(knot[field] for field in ("x", "y", "z")) != expected:
            raise CapsuleError(f"{label} has a non-canonical pose")
    living_leashes = state.get("living_leashes")
    if not isinstance(living_leashes, list):
        raise CapsuleError("state.living_leashes must be an array")
    if state.get("living_leashes_complete") is not True:
        raise CapsuleError(
            "state.living_leashes_complete must be true for a capsule")
    if len(living_leashes) > 256:
        raise CapsuleError(
            "state.living_leashes exceeds the exact 256-entry runtime bound")
    living_leash_fields = {
        "eid", "uuid_most", "uuid_least", "leashed",
        "holder_kind", "holder_eid", "holder_uuid_most",
        "holder_uuid_least", "pending", "pending_x", "pending_y",
        "pending_z", "wolf_angry",
    }
    leashable_types = {
        "EntityPig", "EntitySheep", "EntityCow", "EntityChicken",
        "EntitySquid", "EntityWolf", "EntityMooshroom", "EntitySnowman",
        "EntityOcelot", "EntityIronGolem", "EntityHorse", "EntityDonkey",
        "EntityMule", "EntityRabbit", "EntityPolarBear", "EntityLlama",
    }
    entity_by_eid = {entity["eid"]: entity for entity in state["entities"]}
    knot_by_eid = {knot["eid"]: knot for knot in leash_knots}
    player_eid = state["player"]["eid"]
    player_uuid = tuple(
        value - (1 << 64) if value >= (1 << 63) else value
        for value in (
            int(state["player"].get(
                "uuid_most_hex", "a01e3843e5213998"), 16),
            int(state["player"].get(
                "uuid_least_hex", "958af459800e4d11"), 16),
        )
    )
    seen_living_leashes = set()
    for index, leash in enumerate(living_leashes):
        label = f"state.living_leashes[{index}]"
        if not isinstance(leash, dict) or set(leash) != living_leash_fields:
            raise CapsuleError(
                f"{label} must contain exactly "
                + ", ".join(sorted(living_leash_fields)))
        for field in (
                "eid", "uuid_most", "uuid_least", "holder_kind",
                "holder_eid", "holder_uuid_most", "holder_uuid_least",
                "pending_x", "pending_y", "pending_z"):
            if isinstance(leash[field], bool) \
                    or not isinstance(leash[field], int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        for field in ("leashed", "pending", "wolf_angry"):
            if not isinstance(leash[field], bool):
                raise CapsuleError(f"{label}.{field} must be a boolean")
        eid = leash["eid"]
        entity = entity_by_eid.get(eid)
        if eid in seen_living_leashes or entity is None \
                or entity.get("type") not in leashable_types:
            raise CapsuleError(
                f"{label}.eid must name one unique leashable living entity")
        seen_living_leashes.add(eid)
        if (leash["uuid_most"], leash["uuid_least"]) != (
                entity.get("uuid_most"), entity.get("uuid_least")):
            raise CapsuleError(f"{label} UUID does not match its entity")
        if not 0 <= leash["holder_kind"] <= 3 \
                or leash["holder_eid"] < -1 \
                or not 0 <= leash["pending_y"] <= 255 \
                or (leash["wolf_angry"]
                    and entity.get("type") != "EntityWolf"):
            raise CapsuleError(f"{label} has invalid leash state")
        holder_uuid = (
            leash["holder_uuid_most"], leash["holder_uuid_least"])
        if leash["holder_kind"] == 0:
            if leash["holder_eid"] != -1 or holder_uuid != (0, 0) \
                    or leash["leashed"] != leash["pending"]:
                raise CapsuleError(f"{label} has an inconsistent empty leash")
        elif leash["holder_kind"] == 1:
            if leash["pending"] or not leash["leashed"] \
                    or leash["holder_eid"] != player_eid \
                    or holder_uuid != player_uuid:
                raise CapsuleError(f"{label} has an inconsistent player leash")
        elif leash["holder_kind"] == 2:
            holder = entity_by_eid.get(leash["holder_eid"])
            if leash["pending"] or not leash["leashed"] \
                    or leash["holder_eid"] == eid or holder is None \
                    or holder_uuid != (
                        holder.get("uuid_most"), holder.get("uuid_least")):
                raise CapsuleError(
                    f"{label} requires its exact living leash holder")
        else:
            holder = knot_by_eid.get(leash["holder_eid"])
            if leash["pending"] or not leash["leashed"] or holder is None \
                    or holder_uuid != (
                        holder.get("uuid_most"), holder.get("uuid_least")):
                raise CapsuleError(f"{label} requires its exact leash knot")
        if entity.get("type") == "EntityLlama" \
                and entity.get("horse_exact") is True:
            legacy = (
                entity["llama_leashed"],
                entity["llama_leash_holder_kind"],
                entity["llama_leash_holder_eid"],
                entity["llama_leash_holder_uuid_most"],
                entity["llama_leash_holder_uuid_least"],
                entity["llama_leash_pending"],
                entity["llama_leash_pending_x"],
                entity["llama_leash_pending_y"],
                entity["llama_leash_pending_z"],
            )
            current = (
                leash["leashed"], leash["holder_kind"],
                leash["holder_eid"], leash["holder_uuid_most"],
                leash["holder_uuid_least"], leash["pending"],
                leash["pending_x"], leash["pending_y"],
                leash["pending_z"],
            )
            if legacy != current:
                raise CapsuleError(
                    f"{label} disagrees with the exact llama schema")
    for entity in state["entities"]:
        if entity.get("type") == "EntityLlama" \
                and entity.get("horse_exact") is True \
                and (entity["llama_leashed"]
                     or entity["llama_leash_pending"]) \
                and entity["eid"] not in seen_living_leashes:
            raise CapsuleError(
                "state.living_leashes is missing an active llama leash")
    if seen_hanging_orders != set(range(
            len(item_frames) + len(paintings) + len(leash_knots))):
        raise CapsuleError(
            "state hanging loaded_order must be contiguous from zero")
    collection_tick = state.get("village_collection_tick")
    if isinstance(collection_tick, bool) \
            or not isinstance(collection_tick, int) \
            or not -(1 << 31) <= collection_tick < (1 << 31):
        raise CapsuleError(
            "state.village_collection_tick must be a signed 32-bit integer"
        )
    villages = state.get("villages")
    if not isinstance(villages, list):
        raise CapsuleError("state.villages must be an array")
    if state.get("villages_complete") is not True:
        raise CapsuleError("state.villages_complete must be true for a capsule")
    if len(villages) > 16:
        raise CapsuleError(
            "state.villages exceeds the exact 16-entry runtime bound"
        )
    village_fields = {
        "population", "radius", "golems", "stable", "state_tick",
        "mating_tick", "center_x", "center_y", "center_z",
        "helper_x", "helper_y", "helper_z", "doors", "reputations",
    }
    signed_fields = {
        "stable", "state_tick", "mating_tick",
        "center_x", "center_y", "center_z",
        "helper_x", "helper_y", "helper_z",
    }
    for index, village in enumerate(villages):
        label = f"state.villages[{index}]"
        if not isinstance(village, dict) or set(village) != village_fields:
            raise CapsuleError(f"{label} has an incomplete saved-state schema")
        for field in signed_fields | {"population", "radius", "golems"}:
            value = village[field]
            if isinstance(value, bool) or not isinstance(value, int) \
                    or not -(1 << 31) <= value < (1 << 31):
                raise CapsuleError(f"{label}.{field} must be signed int32")
        if village["population"] < 0 or village["radius"] < 0 \
                or village["golems"] < 0:
            raise CapsuleError(f"{label} has a negative village counter")
        doors = village["doors"]
        if not isinstance(doors, list) or len(doors) > 256:
            raise CapsuleError(f"{label}.doors exceeds the 256-entry bound")
        seen_doors = set()
        for door_index, door in enumerate(doors):
            door_label = f"{label}.doors[{door_index}]"
            if not isinstance(door, dict) or set(door) != {
                    "x", "y", "z", "inside_dx", "inside_dz", "timestamp"}:
                raise CapsuleError(f"{door_label} has an invalid schema")
            if any(isinstance(value, bool) or not isinstance(value, int)
                   or not -(1 << 31) <= value < (1 << 31)
                   for value in door.values()):
                raise CapsuleError(f"{door_label} values must be signed int32")
            if (door["inside_dx"], door["inside_dz"]) not in (
                    (-2, 0), (2, 0), (0, -2), (0, 2)):
                raise CapsuleError(f"{door_label} has an invalid inside offset")
            position = (door["x"], door["y"], door["z"])
            if position in seen_doors:
                raise CapsuleError(f"{door_label} duplicates a door position")
            seen_doors.add(position)
        reputations = village["reputations"]
        if not isinstance(reputations, list) or len(reputations) > 32:
            raise CapsuleError(
                f"{label}.reputations exceeds the 32-entry bound"
            )
        seen_reputations = set()
        previous_uuid = None
        for reputation_index, reputation in enumerate(reputations):
            reputation_label = (
                f"{label}.reputations[{reputation_index}]")
            if not isinstance(reputation, dict) or set(reputation) != {
                    "uuid_most_hex", "uuid_least_hex", "score"}:
                raise CapsuleError(f"{reputation_label} has an invalid schema")
            most = reputation["uuid_most_hex"]
            least = reputation["uuid_least_hex"]
            if not isinstance(most, str) or not isinstance(least, str) \
                    or len(most) != 16 or len(least) != 16 \
                    or any(character not in "0123456789abcdef"
                           for character in most + least):
                raise CapsuleError(
                    f"{reputation_label} UUID halves must be lowercase hex64"
                )
            score = reputation["score"]
            if isinstance(score, bool) or not isinstance(score, int) \
                    or not -30 <= score <= 10:
                raise CapsuleError(f"{reputation_label}.score is outside -30..10")
            uuid = (most, least)
            if uuid in seen_reputations or (
                    previous_uuid is not None and uuid < previous_uuid):
                raise CapsuleError(
                    f"{reputation_label} is duplicate or not sorted")
            seen_reputations.add(uuid)
            previous_uuid = uuid
    scheduled = state.get("scheduled_ticks")
    if not isinstance(scheduled, list):
        raise CapsuleError("state.scheduled_ticks must be an array")
    if state.get("scheduled_ticks_complete") is not True:
        raise CapsuleError(
            "state.scheduled_ticks_complete must be true for a capsule"
        )
    comparators = state.get("comparators")
    if not isinstance(comparators, list):
        raise CapsuleError("state.comparators must be an array")
    if state.get("comparators_complete") is not True:
        raise CapsuleError(
            "state.comparators_complete must be true for a capsule"
        )
    if len(comparators) > 64:
        raise CapsuleError(
            "state.comparators exceeds the exact 64-entry runtime bound"
        )
    seen_comparators = set()
    for index, comparator in enumerate(comparators):
        label = f"state.comparators[{index}]"
        if not isinstance(comparator, dict) or set(comparator) != {
                "x", "y", "z", "output_signal"}:
            raise CapsuleError(
                f"{label} must contain exactly x, y, z, output_signal"
            )
        for field in ("x", "y", "z", "output_signal"):
            value = comparator[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not 0 <= comparator["y"] <= 255 \
                or not 0 <= comparator["output_signal"] <= 15:
            raise CapsuleError(f"{label} has invalid tile state")
        key = (comparator["x"], comparator["y"], comparator["z"])
        if key in seen_comparators:
            raise CapsuleError(f"{label} duplicates a comparator position")
        seen_comparators.add(key)
    loaded_tiles_present = "loaded_tiles" in state
    loaded_tiles = state.get("loaded_tiles", [])
    if not isinstance(loaded_tiles, list):
        raise CapsuleError("state.loaded_tiles must be an array")
    if loaded_tiles_present \
            and state.get("loaded_tiles_complete") is not True:
        raise CapsuleError(
            "state.loaded_tiles_complete must be true for a capsule")
    if len(loaded_tiles) > 4096:
        raise CapsuleError(
            "state.loaded_tiles exceeds the exact 4096-entry runtime bound")
    loaded_tile_order_by_position = {}
    seen_loaded_tile_orders = set()
    seen_tickable_tile_orders = set()
    for index, tile in enumerate(loaded_tiles):
        label = f"state.loaded_tiles[{index}]"
        if not isinstance(tile, dict) or set(tile) != {
                "x", "y", "z", "loaded_order", "update_order", "class",
                "tickable", "block", "meta"}:
            raise CapsuleError(
                f"{label} must contain exactly x, y, z, loaded_order, "
                "update_order, class, tickable, block, meta")
        for field in (
                "x", "y", "z", "loaded_order", "update_order",
                "block", "meta"):
            value = tile[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not isinstance(tile["class"], str) or not tile["class"] \
                or not isinstance(tile["tickable"], bool) \
                or not 0 <= tile["y"] <= 255 \
                or not 0 <= tile["block"] <= 255 \
                or not 0 <= tile["meta"] <= 15:
            raise CapsuleError(f"{label} has invalid tile identity/state")
        position = (tile["x"], tile["y"], tile["z"])
        order = tile["loaded_order"]
        if position in loaded_tile_order_by_position \
                or order in seen_loaded_tile_orders:
            raise CapsuleError(f"{label} duplicates a position or order")
        loaded_tile_order_by_position[position] = order
        seen_loaded_tile_orders.add(order)
        update_order = tile["update_order"]
        if tile["tickable"]:
            if update_order < 0 or update_order in seen_tickable_tile_orders:
                raise CapsuleError(
                    f"{label}.update_order must be unique and nonnegative")
            seen_tickable_tile_orders.add(update_order)
        elif update_order != -1:
            raise CapsuleError(
                f"{label}.update_order must be -1 for a non-tickable tile")
    if seen_loaded_tile_orders != set(range(len(loaded_tiles))):
        raise CapsuleError(
            "state.loaded_tiles.loaded_order must be contiguous from zero")
    if seen_tickable_tile_orders != set(range(
            sum(bool(tile["tickable"]) for tile in loaded_tiles))):
        raise CapsuleError(
            "state.loaded_tiles.update_order must be contiguous from zero")
    spawners_present = "spawners" in state
    spawners = state.get("spawners", [])
    if not isinstance(spawners, list):
        raise CapsuleError("state.spawners must be an array")
    if spawners_present and state.get("spawners_complete") is not True:
        raise CapsuleError(
            "state.spawners_complete must be true for a capsule")
    if len(spawners) > 64:
        raise CapsuleError("state.spawners exceeds the exact 64-entry bound")
    seen_spawners = set()
    spawner_fields = {
        "x", "y", "z", "delay", "min_delay", "max_delay",
        "spawn_count", "max_nearby", "activate_range", "spawn_range",
        "entity_id", "spawn_data_nbt", "default_entity_nbt", "potentials",
    }
    for index, spawner in enumerate(spawners):
        label = f"state.spawners[{index}]"
        if not isinstance(spawner, dict) or set(spawner) != spawner_fields:
            raise CapsuleError(
                f"{label} has an unsupported or incomplete spawner schema")
        for field in (
                "x", "y", "z", "delay", "min_delay", "max_delay",
                "spawn_count", "max_nearby", "activate_range",
                "spawn_range"):
            value = spawner[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not 0 <= spawner["y"] <= 255 \
                or not -1 <= spawner["delay"] <= 32767 \
                or any(not 0 <= spawner[field] <= 32767 for field in (
                    "min_delay", "max_delay", "spawn_count", "max_nearby",
                    "activate_range", "spawn_range")):
            raise CapsuleError(f"{label} has an invalid scalar range")
        position = (spawner["x"], spawner["y"], spawner["z"])
        if position in seen_spawners:
            raise CapsuleError(f"{label} duplicates a spawner position")
        seen_spawners.add(position)
        entity_id = spawner["entity_id"]
        if isinstance(entity_id, str) and ":" not in entity_id:
            entity_id = "minecraft:" + entity_id.lower()
        _spawn_raw, spawn_nbt_id, spawn_is_default = _spawner_nbt_identity(
            spawner["spawn_data_nbt"], f"{label}.spawn_data_nbt")
        if entity_id not in SPAWNER_ENTITY_TYPES \
                or spawn_nbt_id != entity_id \
                or not isinstance(spawner["default_entity_nbt"], bool) \
                or spawner["default_entity_nbt"] is not spawn_is_default:
            raise CapsuleError(
                f"{label} has inconsistent SpawnData identity")
        potentials = spawner["potentials"]
        if not isinstance(potentials, list) or len(potentials) > 16:
            raise CapsuleError(
                f"{label}.potentials exceeds the exact 16-entry bound")
        total_weight = 0
        for potential_index, potential in enumerate(potentials):
            potential_label = (
                f"{label}.potentials[{potential_index}]")
            if not isinstance(potential, dict) or set(potential) != {
                    "weight", "entity_id", "entity_nbt",
                    "default_entity_nbt"}:
                raise CapsuleError(
                    f"{potential_label} has an incomplete schema")
            potential_id = potential["entity_id"]
            if isinstance(potential_id, str) and ":" not in potential_id:
                potential_id = "minecraft:" + potential_id.lower()
            weight = potential["weight"]
            _potential_raw, nbt_id, nbt_is_default = _spawner_nbt_identity(
                potential["entity_nbt"],
                f"{potential_label}.entity_nbt")
            if potential_id not in SPAWNER_ENTITY_TYPES \
                    or nbt_id != potential_id \
                    or not isinstance(
                        potential["default_entity_nbt"], bool) \
                    or potential["default_entity_nbt"] is not nbt_is_default \
                    or isinstance(weight, bool) \
                    or not isinstance(weight, int) or weight <= 0:
                raise CapsuleError(
                    f"{potential_label} has inconsistent entity NBT/weight")
            total_weight += weight
            if total_weight > 2147483647:
                raise CapsuleError(
                    f"{label}.potentials total weight overflows Java int")
    if spawners_present and loaded_tiles_present:
        loaded_spawners = {
            (tile["x"], tile["y"], tile["z"])
            for tile in loaded_tiles
            if tile["class"] == "TileEntityMobSpawner"
        }
        if loaded_spawners != seen_spawners:
            raise CapsuleError(
                "state.spawners does not exactly cover loaded spawner tiles")
        for tile in loaded_tiles:
            position = (tile["x"], tile["y"], tile["z"])
            if position in seen_spawners and (
                    tile["class"] != "TileEntityMobSpawner"
                    or not tile["tickable"] or tile["block"] != 52):
                raise CapsuleError(
                    f"state.spawners identity disagrees at {position}")
    containers = state.get("containers")
    if not isinstance(containers, list):
        raise CapsuleError("state.containers must be an array")
    if state.get("containers_complete") is not True:
        raise CapsuleError(
            "state.containers_complete must be true for a capsule"
        )
    if len(containers) > 4096:
        raise CapsuleError(
            "state.containers exceeds the exact 4096-entry bound"
        )
    seen_containers = set()
    seen_container_orders = set()
    container_order_count = 0
    represented_container_rows = len(containers)
    represented_furnaces = 0
    represented_static_containers = 0
    represented_command_blocks = 0
    for index, container in enumerate(containers):
        label = f"state.containers[{index}]"
        if not isinstance(container, dict):
            raise CapsuleError(f"{label} must be an object")
        container_type = container.get("type")
        common_fields = {"type", "x", "y", "z", "size", "items"}
        if "loaded_order" in container:
            common_fields.add("loaded_order")
        chest_fields = {
            "num_players_using", "lid_angle_bits",
            "prev_lid_angle_bits", "ticks_since_sync",
        }
        furnace_fields = {
            "burn_time", "current_burn_time",
            "cook_time", "total_cook_time", "custom_name",
        }
        brewing_fields = {"brew_time", "fuel", "ingredient_id"}
        hopper_fields = {"transfer_cooldown", "ticked_game_time"}
        command_fields = {
            "success_count", "command", "last_output",
            "powered", "automatic", "condition_met",
        }
        shulker_fields = {
            "block", "facing", "item_tag_nbt", "open_count",
            "animation_status", "progress_bits", "progress_old_bits",
        }
        double_chest_fields = {"pair_x", "pair_y", "pair_z"}
        expected_fields = (
            common_fields | chest_fields
            if container_type in (
                "single_chest", "single_trapped_chest")
            else common_fields | chest_fields | double_chest_fields
            if container_type in (
                "double_chest_half", "double_trapped_chest_half")
            else common_fields | furnace_fields
            if container_type == "furnace"
            else common_fields | brewing_fields
            if container_type == "brewing_stand"
            else common_fields | hopper_fields
            if container_type == "hopper"
            else common_fields
            if container_type in (
                "dispenser", "dropper", "jukebox")
            else common_fields | shulker_fields
            if container_type == "shulker_box"
            else common_fields | command_fields
            if container_type in (
                "command_block", "repeating_command_block",
                "chain_command_block")
            else set()
        )
        if not expected_fields or set(container) != expected_fields:
            raise CapsuleError(
                f"{label} has an unsupported or incomplete container schema"
            )
        if container_type in (
                "single_chest", "single_trapped_chest",
                "double_chest_half", "double_trapped_chest_half") \
                and container["size"] != 27:
            raise CapsuleError(
                f"{label} is not an exact 27-slot chest tile"
            )
        if container_type in (
                "single_chest", "single_trapped_chest",
                "double_chest_half", "double_trapped_chest_half"):
            for field in chest_fields:
                value = container[field]
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(
                        f"{label}.{field} must be an integer")
            if not 0 <= container["num_players_using"] <= 2147483647:
                raise CapsuleError(
                    f"{label}.num_players_using is outside 0..2^31-1")
            ticks_since_sync = container["ticks_since_sync"]
            if isinstance(ticks_since_sync, bool) \
                    or not isinstance(ticks_since_sync, int) \
                    or not -(1 << 31) <= ticks_since_sync < (1 << 31):
                raise CapsuleError(
                    f"{label}.ticks_since_sync is outside signed int32")
            for field in ("lid_angle_bits", "prev_lid_angle_bits"):
                if not 0 <= container[field] <= 0xFFFFFFFF:
                    raise CapsuleError(
                        f"{label}.{field} is outside unsigned 32-bit range")
            lid_angle = struct.unpack(
                "<f", struct.pack("<I", container["lid_angle_bits"]))[0]
            prev_lid_angle = struct.unpack(
                "<f",
                struct.pack("<I", container["prev_lid_angle_bits"]))[0]
            viewers = container["num_players_using"]
            if not math.isfinite(lid_angle) \
                    or not math.isfinite(prev_lid_angle) \
                    or not 0.0 <= lid_angle <= 1.0 \
                    or not 0.0 <= prev_lid_angle <= 1.0 \
                    or viewers > 0 and (
                        lid_angle < prev_lid_angle
                        or lid_angle - prev_lid_angle > 0.100001) \
                    or viewers == 0 and (
                        lid_angle > prev_lid_angle
                        or prev_lid_angle - lid_angle > 0.100001):
                raise CapsuleError(
                    f"{label} has an invalid chest animation state")
        if container_type == "furnace" and container["size"] != 3:
            raise CapsuleError(
                f"{label} is not an exact three-slot furnace"
            )
        if container_type == "brewing_stand":
            if container["size"] != 5:
                raise CapsuleError(
                    f"{label} is not an exact five-slot brewing stand"
                )
            for field, maximum in (("brew_time", 400), ("fuel", 20)):
                value = container[field]
                if isinstance(value, bool) or not isinstance(value, int) \
                        or not 0 <= value <= maximum:
                    raise CapsuleError(
                        f"{label}.{field} must be an integer in 0..{maximum}"
                    )
            ingredient_id = container["ingredient_id"]
            if isinstance(ingredient_id, bool) \
                    or not isinstance(ingredient_id, int) \
                    or not 0 <= ingredient_id <= 4095:
                raise CapsuleError(
                    f"{label}.ingredient_id must be an item id in 0..4095")
        if container_type in ("dispenser", "dropper") \
                and container["size"] != 9:
            raise CapsuleError(
                f"{label} is not an exact nine-slot inventory tile"
            )
        if container_type == "hopper" and container["size"] != 5:
            raise CapsuleError(
                f"{label} is not an exact five-slot hopper tile"
            )
        if container_type == "hopper":
            cooldown = container["transfer_cooldown"]
            ticked_time = container["ticked_game_time"]
            if isinstance(cooldown, bool) or not isinstance(cooldown, int) \
                    or not -(1 << 31) <= cooldown < (1 << 31):
                raise CapsuleError(
                    f"{label}.transfer_cooldown must be a signed 32-bit integer"
                )
            if isinstance(ticked_time, bool) \
                    or not isinstance(ticked_time, int) \
                    or not -(1 << 63) <= ticked_time < (1 << 63):
                raise CapsuleError(
                    f"{label}.ticked_game_time must be a signed 64-bit integer"
                )
        if container_type == "jukebox" and container["size"] != 1:
            raise CapsuleError(
                f"{label} is not an exact one-record jukebox tile"
            )
        if container_type == "shulker_box":
            if container["size"] != 27:
                raise CapsuleError(
                    f"{label} is not an exact 27-slot shulker tile"
                )
            for field in ("block", "facing"):
                value = container[field]
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            if not 219 <= container["block"] <= 234 \
                    or not 0 <= container["facing"] <= 5:
                raise CapsuleError(
                    f"{label} has invalid shulker block/facing state"
                )
            for field in (
                    "open_count", "animation_status",
                    "progress_bits", "progress_old_bits"):
                value = container[field]
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            if not -(1 << 31) <= container["open_count"] < (1 << 31) \
                    or not 0 <= container["animation_status"] <= 3:
                raise CapsuleError(
                    f"{label} has invalid shulker transient counters")
            for field in ("progress_bits", "progress_old_bits"):
                if not 0 <= container[field] <= 0xFFFFFFFF:
                    raise CapsuleError(
                        f"{label}.{field} is outside unsigned 32-bit range")
            progress = struct.unpack(
                "<f", struct.pack("<I", container["progress_bits"]))[0]
            progress_old = struct.unpack(
                "<f", struct.pack("<I", container["progress_old_bits"]))[0]
            status = container["animation_status"]
            if not math.isfinite(progress) \
                    or not math.isfinite(progress_old) \
                    or not 0.0 <= progress <= 1.0 \
                    or not 0.0 <= progress_old <= 1.0 \
                    or status == 0 and progress != 0.0 \
                    or status == 2 and progress != 1.0 \
                    or status == 1 and (
                        progress < progress_old
                        or progress - progress_old > 0.100001) \
                    or status == 3 and (
                        progress > progress_old
                        or progress_old - progress > 0.100001):
                raise CapsuleError(
                    f"{label} has an invalid shulker animation state")
        if container_type in (
                "command_block", "repeating_command_block",
                "chain_command_block"):
            success_count = container["success_count"]
            if container["size"] != 0 or container["items"] != []:
                raise CapsuleError(
                    f"{label} is not an exact inventory-free command tile"
                )
            if isinstance(success_count, bool) \
                    or not isinstance(success_count, int) \
                    or not 0 <= success_count <= 15:
                raise CapsuleError(
                    f"{label}.success_count must be an integer in 0..15"
                )
            for field in ("powered", "automatic", "condition_met"):
                if not isinstance(container[field], bool):
                    raise CapsuleError(f"{label}.{field} must be a boolean")
            command = container["command"]
            last_output = container["last_output"]
            command_kind = command.lower() if isinstance(command, str) else None
            time_match = re.fullmatch(
                r"time (set|add) (day|night|[0-9]+)",
                command_kind or "")
            query_match = re.fullmatch(
                r"time query (daytime|day|gametime)",
                command_kind or "")
            weather_match = re.fullmatch(
                r"weather (clear|rain|thunder)(?: ([0-9]+))?",
                command_kind or "")
            gamerule_match = re.fullmatch(
                r"gamerule (doDaylightCycle|doMobSpawning|doFireTick|"
                r"randomTickSpeed|mobGriefing|keepInventory|doTileDrops|"
                r"naturalRegeneration|doWeatherCycle|maxEntityCramming) "
                r"(true|false|-?[0-9]+)",
                command or "")
            setblock_match = re.fullmatch(
                r"setblock ((?:~(?:-?[0-9]+)?)|-?[0-9]+) "
                r"((?:~(?:-?[0-9]+)?)|-?[0-9]+) "
                r"((?:~(?:-?[0-9]+)?)|-?[0-9]+) "
                r"minecraft:[a-z0-9_]+(?: ([0-9]+)(?: (?:replace|keep|destroy))?)?",
                command_kind or "")
            if setblock_match and setblock_match.group(4) is not None \
                    and not 0 <= int(setblock_match.group(4)) <= 15:
                setblock_match = None
            testforblock_match = re.fullmatch(
                r"testforblock ((?:~(?:-?[0-9]+)?)|-?[0-9]+) "
                r"((?:~(?:-?[0-9]+)?)|-?[0-9]+) "
                r"((?:~(?:-?[0-9]+)?)|-?[0-9]+) "
                r"minecraft:[a-z0-9_]+(?: ([0-9]+|\*))?",
                command_kind or "")
            if testforblock_match \
                    and testforblock_match.group(4) not in (None, "*") \
                    and not 0 <= int(testforblock_match.group(4)) <= 15:
                testforblock_match = None
            compare_match = re.fullmatch(
                r"testforblocks"
                + 9 * r" ((?:~(?:-?[0-9]+)?)|-?[0-9]+)"
                + r"(?: (all|masked))?", command_kind or "")
            fill_match = re.fullmatch(
                r"fill"
                + 6 * r" ((?:~(?:-?[0-9]+)?)|-?[0-9]+)"
                + r" minecraft:[a-z0-9_]+(?: ([0-9]+))?"
                + r"(?: (?:replace|keep|outline|hollow))?",
                command_kind or "")
            if fill_match and fill_match.group(7) is not None \
                    and not 0 <= int(fill_match.group(7)) <= 15:
                fill_match = None
            clone_match = re.fullmatch(
                r"clone"
                + 9 * r" ((?:~(?:-?[0-9]+)?)|-?[0-9]+)"
                + r"(?:(?: (?:replace|masked)"
                + r"(?: (?:normal|force|move))?)|"
                + r"(?: filtered (?:normal|force|move)"
                + r" minecraft:[a-z0-9_]+(?: ([0-9]+))?))?",
                command_kind or "")
            if clone_match and clone_match.group(10) is not None \
                    and not 0 <= int(clone_match.group(10)) <= 15:
                clone_match = None
            literal_chat_match = re.fullmatch(
                r"(?:say|me) [ -~]+", command_kind or "")
            if literal_chat_match and "@" in (command_kind or ""):
                literal_chat_match = None
            particle_match = command_kind \
                == "particle smoke ~ ~ ~ 0 0 0 0 1 normal"
            xp_match = re.fullmatch(
                r"xp (-?)([0-9]+)(l)? @p", command_kind or "")
            if xp_match and int(xp_match.group(2)) > 2147483647:
                xp_match = None
            if xp_match and xp_match.group(1) and not xp_match.group(3):
                xp_match = None
            clear_match = command_kind in {
                "clear @p minecraft:stone -1 -1",
                "clear @p minecraft:diamond_sword 17 2",
                "clear @p minecraft:ender_pearl 4 0",
                "clear @p",
            }
            single_player_gamemode_match = command_kind in (
                "gamemode survival @p", "gamemode creative @p",
                "gamemode adventure @p", "gamemode spectator @p",
                "gamemode 0 @p", "gamemode 1 @p",
                "gamemode 2 @p", "gamemode 3 @p")
            single_player_testfor_match = command_kind == "testfor @p"
            single_player_title_match = command_kind in {
                "title @p clear", "title @p reset",
                "title @p times 10 70 20", "title @p times -1 -2 -3",
                'title @p title {"text":"bounded"}',
                'title @p subtitle {"text":"bounded"}',
                'title @p actionbar {"text":"bounded"}',
            }
            stopsound_fields = (command_kind or "").split(" ")
            stopsound_categories = {
                "master", "music", "record", "weather", "block",
                "hostile", "neutral", "player", "ambient", "voice",
            }
            single_player_stopsound_match = (
                len(stopsound_fields) in (2, 3, 4)
                and stopsound_fields[:2] == ["stopsound", "@p"]
                and (len(stopsound_fields) == 2
                     or stopsound_fields[2] in stopsound_categories)
                and (len(stopsound_fields) < 4
                     or stopsound_fields[3]
                     == "minecraft:block.note.harp"))
            single_player_effect_specs = {
                "effect @p minecraft:speed 10 0 true":
                    ("effect.moveSpeed", 1, 0, 10),
                "effect @p minecraft:mining_fatigue 2 3 true":
                    ("effect.digSlowDown", 4, 3, 2),
                "effect @p minecraft:instant_health 2 3 false":
                    ("effect.heal", 6, 3, 2),
                "effect @p minecraft:luck 1":
                    ("effect.luck", 26, 0, 1),
            }
            single_player_effect_match = single_player_effect_specs.get(
                command_kind or "")
            single_player_playsound_match = command_kind \
                == "playsound minecraft:block.note.harp master @p"
            single_player_tellraw_match = command \
                == 'tellraw @p {"text":"bounded"}'
            single_player_tell_match = command_kind == "tell @p bounded"
            stronghold_locate_match = command == "locate Stronghold"
            village_locate_match = command == "locate Village"
            temple_locate_match = command == "locate Temple"
            mineshaft_locate_match = command == "locate Mineshaft"
            mansion_locate_match = command == "locate Mansion"
            monument_locate_match = command == "locate Monument"
            fortress_locate_match = command == "locate Fortress"
            end_city_locate_match = command == "locate EndCity"
            idempotent_server_mode_match = command_kind in (
                "difficulty peaceful", "difficulty easy",
                "difficulty normal", "difficulty hard",
                "difficulty p", "difficulty e", "difficulty n",
                "difficulty h", "difficulty 0", "difficulty 1",
                "difficulty 2", "difficulty 3",
                "defaultgamemode survival",
                "defaultgamemode creative", "defaultgamemode adventure",
                "defaultgamemode spectator", "defaultgamemode 0",
                "defaultgamemode 1", "defaultgamemode 2",
                "defaultgamemode 3")
            bounded_worldborder_commands = {
                "worldborder get": (
                    r"commands\.worldborder\.get\.success", r'"60000000"'),
                "worldborder set 1000": (
                    r"commands\.worldborder\.set\.success",
                    r'"1000\.0","60000000\.0"'),
                "worldborder add -100": (
                    r"commands\.worldborder\.set\.success",
                    r'"59999900\.0","60000000\.0"'),
                "worldborder center 12.5 -7.5": (
                    r"commands\.worldborder\.center\.success",
                    r'"12\.5","-7\.5"'),
                "worldborder damage buffer 3": (
                    r"commands\.worldborder\.damage\.buffer\.success",
                    r'"3\.0","5\.0"'),
                "worldborder damage amount 0.5": (
                    r"commands\.worldborder\.damage\.amount\.success",
                    r'"0\.50","0\.20"'),
                "worldborder warning time 20": (
                    r"commands\.worldborder\.warning\.time\.success",
                    r'"20","15"'),
                "worldborder warning distance 7": (
                    r"commands\.worldborder\.warning\.distance\.success",
                    r'"7","5"'),
                "worldborder set 1": (
                    r"commands\.worldborder\.set\.success",
                    r'"1\.0","60000000\.0"'),
            }
            bounded_worldborder_match = command_kind in bounded_worldborder_commands
            single_player_give_match = command_kind in {
                "give @p minecraft:stone 1 0",
                "give @p minecraft:diamond_sword 3 17",
                "give @p minecraft:ender_pearl 64 0",
                "give @p minecraft:white_shulker_box 2 0",
            }
            single_player_enchant_match = command_kind in {
                "enchant @p minecraft:sharpness 1",
                "enchant @p minecraft:smite 5",
                "enchant @p minecraft:unbreaking 3",
                "enchant @p minecraft:mending 1",
                "enchant @p minecraft:vanishing_curse 1",
            }
            single_player_replaceitem_match = command_kind in {
                "replaceitem entity @p slot.hotbar.0 minecraft:stone 1 0",
                "replaceitem entity @p slot.hotbar.0 minecraft:brown_mushroom_block",
                "replaceitem entity @p slot.hotbar.8 minecraft:apple 7 4",
                "replaceitem entity @p slot.armor.head minecraft:diamond_helmet",
                "replaceitem entity @p slot.weapon.offhand minecraft:shield",
            }
            single_player_kill_match = command_kind == "kill @p"
            single_player_execute_setblock_match = command_kind == (
                "execute @p ~ ~ ~ setblock ~6 ~ ~ "
                "minecraft:gold_block 0 replace")
            block_coord = r"(?:~-?[0-9]+|-?[0-9]+)"
            setworldspawn_match = re.fullmatch(
                rf"setworldspawn {block_coord} {block_coord} {block_coord}",
                command_kind or "")
            spawnpoint_match = re.fullmatch(
                rf"spawnpoint @p {block_coord} {block_coord} {block_coord}",
                command_kind or "")
            summon_lightning_match = command_kind == (
                "summon minecraft:lightning_bolt 14 78 8")
            comparator_blockdata_match = command_kind == (
                "blockdata 12 78 8 {outputsignal:7}")
            item_frame_entitydata_match = command_kind == (
                "entitydata @e[type=minecraft:item_frame,c=1] "
                "{itemrotation:3b}")
            help_match = command_kind == "help"
            empty_scoreboard_list_match = command_kind == (
                "scoreboard objectives list")
            clear_player_success_stat_match = command_kind == (
                "stats entity @p clear successcount")
            execute_trigger_match = command_kind == (
                "execute @p ~ ~ ~ trigger qrl add 2")
            open_inventory_achievement_match = command_kind == (
                "achievement give achievement.openinventory @p")
            single_player_spread_match = command_kind == (
                "spreadplayers 8 8 0 4 false @p")
            single_player_teleport_specs = {
                "tp @p 8.5 78 8.5": ("tp", "8.5", "78.0", "8.5"),
                "teleport @p 8.5 78 8.5": (
                    "teleport", "8.5", "78.0", "8.5"),
                "tp @p -1 80 4": ("tp", "-0.5", "80.0", "4.5"),
                "tp @p ~1 ~2 ~-3 450 -80": (
                    "tp", "9.5", "80.0", "5.5"),
                "teleport @p ~ ~2 ~": (
                    "teleport", "11.5", "80.5", "8.5"),
            }
            single_player_teleport_match = single_player_teleport_specs.get(
                command_kind or "")
            if gamerule_match:
                boolean_rules = {
                    "doDaylightCycle", "doMobSpawning", "doFireTick",
                    "mobGriefing", "keepInventory", "doTileDrops",
                    "naturalRegeneration", "doWeatherCycle",
                }
                name, setting = gamerule_match.groups()
                if name in boolean_rules and setting not in ("true", "false"):
                    gamerule_match = None
                elif name not in boolean_rules:
                    try:
                        parsed_rule = int(setting)
                    except ValueError:
                        gamerule_match = None
                    else:
                        if not -(1 << 31) <= parsed_rule < (1 << 31):
                            gamerule_match = None
            if weather_match and weather_match.group(2) is not None \
                    and not 1 <= int(weather_match.group(2)) <= 1000000:
                weather_match = None
            time_operation = None
            time_value = None
            if time_match:
                time_operation = time_match.group(1)
                argument = time_match.group(2)
                if time_operation == "add" and not argument.isdigit():
                    time_match = None
                else:
                    time_value = (1000 if argument == "day"
                                  else 13000 if argument == "night"
                                  else int(argument))
                    if time_value > 2147483647:
                        time_match = None
            elif query_match:
                time_operation = "query"
            if command_kind not in ("", "searge", "toggledownfall", "seed") \
                    and not time_match and not query_match \
                    and not weather_match and not gamerule_match \
                    and not setblock_match and not testforblock_match \
                    and not compare_match and not fill_match \
                    and not clone_match and not literal_chat_match \
                    and not particle_match and not xp_match \
                    and not clear_match and not single_player_gamemode_match \
                    and not single_player_testfor_match \
                    and not single_player_title_match \
                    and not single_player_stopsound_match \
                    and not single_player_effect_match \
                    and not single_player_playsound_match \
                    and not single_player_tellraw_match \
                    and not single_player_tell_match \
                    and not stronghold_locate_match \
                    and not village_locate_match \
                    and not temple_locate_match \
                    and not mineshaft_locate_match \
                    and not mansion_locate_match \
                    and not monument_locate_match \
                    and not fortress_locate_match \
                    and not end_city_locate_match \
                    and not idempotent_server_mode_match \
                    and not bounded_worldborder_match \
                    and not single_player_give_match \
                    and not single_player_enchant_match \
                    and not single_player_replaceitem_match \
                    and not single_player_kill_match \
                    and not single_player_execute_setblock_match \
                    and not setworldspawn_match \
                    and not spawnpoint_match \
                    and not summon_lightning_match \
                    and not comparator_blockdata_match \
                    and not item_frame_entitydata_match \
                    and not help_match \
                    and not empty_scoreboard_list_match \
                    and not clear_player_success_stat_match \
                    and not execute_trigger_match \
                    and not open_inventory_achievement_match \
                    and not single_player_spread_match \
                    and not single_player_teleport_match:
                raise CapsuleError(
                    f"{label}.command is outside the bounded command set")
            time_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.time\.'
                + ("set" if time_operation == "set"
                   else "added" if time_operation == "add" else "query")
                + r'","with":\["'
                + (r'-?[0-9]+' if query_match else str(time_value))
                + r'"\]\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if time_match or query_match else False
            weather_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.weather\.'
                + (weather_match.group(1) if weather_match else "")
                + r'"\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if weather_match else False
            gamerule_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.gamerule\.success",'
                r'"with":\["' + (gamerule_match.group(1)
                                   if gamerule_match else "")
                + r'","' + (gamerule_match.group(2)
                              if gamerule_match else "")
                + r'"\]\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if gamerule_match else False
            downfall_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.downfall\.success"'
                r'\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if command_kind == "toggledownfall" else False
            seed_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.seed\.success",'
                r'"with":\["-?[0-9]+"\]\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if command_kind == "seed" else False
            setblock_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.setblock\.success"'
                r'\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if setblock_match else False
            testforblock_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.testforblock\.success",'
                r'"with":\["-?[0-9]+","-?[0-9]+","-?[0-9]+"\]\}\],'
                r'"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                r'[0-5][0-9]\] "\}', last_output) \
                if testforblock_match else False
            compare_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.compare\.success",'
                r'"with":\["[0-9]+"\]\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if compare_match else False
            fill_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.fill\.success",'
                r'"with":\["[0-9]+"\]\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if fill_match else False
            clone_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.clone\.success",'
                r'"with":\["[0-9]+"\]\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if clone_match else False
            particle_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.particle\.success",'
                r'"with":\["smoke","1"\]\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if particle_match else False
            xp_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.xp\.success'
                + (r'\.negative\.levels' if xp_match and xp_match.group(1)
                   else r'\.levels' if xp_match and xp_match.group(3) else '')
                + r'","with":\["'
                + (xp_match.group(2) if xp_match else '')
                + r'","[A-Za-z0-9_]{1,16}"\]\}\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if xp_match else False
            clear_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[\{"translate":"commands\.clear\.'
                + ("testing" if command_kind
                   == "clear @p minecraft:ender_pearl 4 0" else "success")
                + r'",'
                r'"with":\["[A-Za-z0-9_]{1,16}","[0-9]+"\]\}\],'
                r'"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                r'[0-5][0-9]\] "\}', last_output) if clear_match else False
            single_player_testfor_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"color":"red","translate":'
                    r'"commands\.testfor\.success","with":\['
                    r'"[A-Za-z0-9_]{1,16}"\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if single_player_testfor_match else False
            single_player_title_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.title\.success"\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if single_player_title_match else False
            single_player_stopsound_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.stopsound\.success\.'
                    + ("individualSound" if len(stopsound_fields) == 4
                       else "soundSource" if len(stopsound_fields) == 3
                       else "all")
                    + r'","with":\['
                    + ((r'"' + re.escape(stopsound_fields[3]) + r'","'
                        + re.escape(stopsound_fields[2]) + r'","')
                       if len(stopsound_fields) == 4 else
                       (r'"' + re.escape(stopsound_fields[2]) + r'","')
                       if len(stopsound_fields) == 3 else r'"')
                    + r'[A-Za-z0-9_]{1,16}"\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if single_player_stopsound_match else False
            single_player_effect_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.effect\.success","with":\['
                    r'\{"translate":"'
                    + re.escape(single_player_effect_match[0])
                    + r'"\},"' + str(single_player_effect_match[1])
                    + r'","' + str(single_player_effect_match[2])
                    + r'","[A-Za-z0-9_]{1,16}","'
                    + str(single_player_effect_match[3])
                    + r'"\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if single_player_effect_match else False
            single_player_playsound_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.playsound\.success","with":\['
                    r'"minecraft:block\.note\.harp",'
                    r'"[A-Za-z0-9_]{1,16}"\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if single_player_playsound_match else False
            single_player_teleport_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":"commands\.'
                    + (single_player_teleport_match[0]
                       if single_player_teleport_match else '')
                    + r'\.success\.coordinates","with":\['
                    r'"[A-Za-z0-9_]{1,16}","'
                    + re.escape(single_player_teleport_match[1]) + r'","'
                    + re.escape(single_player_teleport_match[2]) + r'","'
                    + re.escape(single_player_teleport_match[3]) + r'"\]'
                    r'\}\],"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] "\}', last_output) \
                if single_player_teleport_match else False
            single_player_tell_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"italic":true,"color":"gray",'
                    r'"translate":"commands\.message\.display\.outgoing",'
                    r'.*\}\],"text":"\[(?:[01][0-9]|2[0-3]):'
                    r'[0-5][0-9]:[0-5][0-9]\] "\}', last_output) \
                if single_player_tell_match else False
            stronghold_locate_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.locate\.success","with":\['
                    r'"Stronghold","-?[0-9]+","-?[0-9]+"\]\}\],'
                    r'"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] "\}', last_output) \
                if stronghold_locate_match else False
            village_locate_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.locate\.success","with":\['
                    r'"Village","-?[0-9]+","-?[0-9]+"\]\}\],'
                    r'"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] "\}', last_output) \
                if village_locate_match else False
            temple_locate_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{\"extra\":\[\{\"translate\":'
                    r'\"commands\.locate\.success\",\"with\":\['
                    r'\"Temple\",\"-?[0-9]+\",\"-?[0-9]+\"\]\}\],'
                    r'\"text\":\"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] \"\}', last_output) \
                if temple_locate_match else False
            mineshaft_locate_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{\"extra\":\[\{\"translate\":'
                    r'\"commands\.locate\.success\",\"with\":\['
                    r'\"Mineshaft\",\"-?[0-9]+\",\"-?[0-9]+\"\]\}\],'
                    r'\"text\":\"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] \"\}', last_output) \
                if mineshaft_locate_match else False
            mansion_locate_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{\"extra\":\[\{\"translate\":'
                    r'\"commands\.locate\.success\",\"with\":\['
                    r'\"Mansion\",\"-?[0-9]+\",\"-?[0-9]+\"\]\}\],'
                    r'\"text\":\"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] \"\}', last_output) \
                if mansion_locate_match else False
            monument_locate_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{\"extra\":\[\{\"translate\":'
                    r'\"commands\.locate\.success\",\"with\":\['
                    r'\"Monument\",\"-?[0-9]+\",\"-?[0-9]+\"\]\}\],'
                    r'\"text\":\"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] \"\}', last_output) \
                if monument_locate_match else False
            fortress_locate_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{\"extra\":\[\{\"translate\":'
                    r'\"commands\.locate\.success\",\"with\":\['
                    r'\"Fortress\",\"-?[0-9]+\",\"-?[0-9]+\"\]\}\],'
                    r'\"text\":\"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] \"\}', last_output) \
                if fortress_locate_match else False
            end_city_locate_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{\"extra\":\[\{\"translate\":'
                    r'\"commands\.locate\.success\",\"with\":\['
                    r'\"EndCity\",\"-?[0-9]+\",\"-?[0-9]+\"\]\}\],'
                    r'\"text\":\"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] \"\}', last_output) \
                if end_city_locate_match else False
            idempotent_server_mode_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":"commands\.'
                    + ("difficulty" if command_kind.startswith("difficulty ")
                       else "defaultgamemode")
                    + r'\.success","with":\[\{"translate":"'
                    + ({
                        "difficulty peaceful": r'options\.difficulty\.peaceful',
                        "difficulty easy": r'options\.difficulty\.easy',
                        "difficulty normal": r'options\.difficulty\.normal',
                        "difficulty hard": r'options\.difficulty\.hard',
                        "difficulty p": r'options\.difficulty\.peaceful',
                        "difficulty e": r'options\.difficulty\.easy',
                        "difficulty n": r'options\.difficulty\.normal',
                        "difficulty h": r'options\.difficulty\.hard',
                        "difficulty 0": r'options\.difficulty\.peaceful',
                        "difficulty 1": r'options\.difficulty\.easy',
                        "difficulty 2": r'options\.difficulty\.normal',
                        "difficulty 3": r'options\.difficulty\.hard',
                        "defaultgamemode survival": r'gameMode\.survival',
                        "defaultgamemode creative": r'gameMode\.creative',
                        "defaultgamemode adventure": r'gameMode\.adventure',
                        "defaultgamemode spectator": r'gameMode\.spectator',
                        "defaultgamemode 0": r'gameMode\.survival',
                        "defaultgamemode 1": r'gameMode\.creative',
                        "defaultgamemode 2": r'gameMode\.adventure',
                        "defaultgamemode 3": r'gameMode\.spectator',
                    }.get(command_kind, ""))
                    + r'"\}\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if idempotent_server_mode_match else False
            bounded_worldborder_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":"'
                    + bounded_worldborder_commands[command_kind][0]
                    + r'","with":\['
                    + bounded_worldborder_commands[command_kind][1]
                    + r'\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if bounded_worldborder_match else False
            single_player_give_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.give\.success","with":\[.*\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if single_player_give_match else False
            single_player_enchant_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.enchant\.success"\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if single_player_enchant_match else False
            single_player_replaceitem_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.replaceitem\.success","with":\[.*\]\}\],'
                    r'"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] "\}', last_output) \
                if single_player_replaceitem_match else False
            single_player_kill_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.kill\.successful","with":\[.*\]\}\],'
                    r'"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] "\}', last_output) \
                if single_player_kill_match else False
            single_player_execute_setblock_output = isinstance(
                last_output, str) and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.setblock\.success"\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if single_player_execute_setblock_match \
                else False
            setworldspawn_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.setworldspawn\.success","with":\['
                    r'"-?[0-9]+","[0-9]+","-?[0-9]+"\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if setworldspawn_match else False
            spawnpoint_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.spawnpoint\.success","with":\['
                    r'"[A-Za-z0-9_]{1,16}","-?[0-9]+","[0-9]+",'
                    r'"-?[0-9]+"\]\}\],'
                    r'"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] "\}',
                    last_output) if spawnpoint_match else False
            summon_lightning_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.summon\.success"\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if summon_lightning_match else False
            comparator_blockdata_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.blockdata\.success","with":\['
                    r'".*OutputSignal:7.*"\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if comparator_blockdata_match else False
            item_frame_entitydata_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.entitydata\.success","with":\['
                    r'".*ItemRotation:3b.*"\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if item_frame_entitydata_match else False
            help_output = isinstance(last_output, str) and re.fullmatch(
                r'\{"extra":\[.*Searge says: .*Yolo.*\],"text":"\['
                r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                last_output) if help_match else False
            empty_scoreboard_list_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.scoreboard\.objectives\.list\.empty"\}\],'
                    r'"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] "\}', last_output) \
                if empty_scoreboard_list_match else False
            clear_player_success_stat_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":"commands\.stats\.cleared",'
                    r'"with":\["SuccessCount"\]\}\],"text":"\['
                    r'(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]\] "\}',
                    last_output) if clear_player_success_stat_match else False
            single_player_spread_output = isinstance(last_output, str) \
                and re.fullmatch(
                    r'\{"extra":\[\{"translate":'
                    r'"commands\.spreadplayers\.success\.players",'
                    r'"with":\["1","8\.5","8\.5"\]\}\],'
                    r'"text":"\[(?:[01][0-9]|2[0-3]):[0-5][0-9]:'
                    r'[0-5][0-9]\] "\}', last_output) \
                if single_player_spread_match else False
            if not isinstance(last_output, str) or not (
                    last_output == ""
                    or (command_kind == "searge"
                        and last_output == '{"text":"#itzlipofutzli"}')
                    or ((time_match or query_match) and time_output)
                    or (weather_match and weather_output)
                    or (gamerule_match and gamerule_output)
                    or (command_kind == "toggledownfall"
                        and downfall_output)
                    or (command_kind == "seed" and seed_output)
                    or (setblock_match and setblock_output)
                    or (testforblock_match and testforblock_output)
                    or (compare_match and compare_output)
                    or (fill_match and fill_output)
                    or (clone_match and clone_output)
                    or (particle_match and particle_output)
                    or (xp_match and xp_output)
                    or (clear_match and clear_output)
                    or (single_player_testfor_match
                        and single_player_testfor_output)
                    or (single_player_title_match
                        and single_player_title_output)
                    or (single_player_stopsound_match
                        and single_player_stopsound_output)
                    or (single_player_effect_match
                        and single_player_effect_output)
                    or (single_player_playsound_match
                        and single_player_playsound_output)
                    or (single_player_tell_match
                        and single_player_tell_output)
                    or (stronghold_locate_match
                        and stronghold_locate_output)
                    or (village_locate_match and village_locate_output)
                    or (temple_locate_match and temple_locate_output)
                    or (mineshaft_locate_match and mineshaft_locate_output)
                    or (mansion_locate_match and mansion_locate_output)
                    or (monument_locate_match and monument_locate_output)
                    or (fortress_locate_match and fortress_locate_output)
                    or (end_city_locate_match and end_city_locate_output)
                    or (idempotent_server_mode_match
                        and idempotent_server_mode_output)
                    or (bounded_worldborder_match
                        and bounded_worldborder_output)
                    or (single_player_give_match
                        and single_player_give_output)
                    or (single_player_enchant_match
                        and single_player_enchant_output)
                    or (single_player_replaceitem_match
                        and single_player_replaceitem_output)
                    or (single_player_kill_match
                        and single_player_kill_output)
                    or (single_player_execute_setblock_match
                        and single_player_execute_setblock_output)
                    or (setworldspawn_match and setworldspawn_output)
                    or (spawnpoint_match and spawnpoint_output)
                    or (summon_lightning_match and summon_lightning_output)
                    or (comparator_blockdata_match
                        and comparator_blockdata_output)
                    or (item_frame_entitydata_match
                        and item_frame_entitydata_output)
                    or (help_match and help_output)
                    or (empty_scoreboard_list_match
                        and empty_scoreboard_list_output)
                    or (clear_player_success_stat_match
                        and clear_player_success_stat_output)
                    or (execute_trigger_match and last_output == "")
                    or (open_inventory_achievement_match
                        and isinstance(last_output, str)
                        and "commands.achievement.give.success.one"
                            in last_output
                        and "achievement.openInventory" in last_output)
                    or (single_player_spread_match
                        and single_player_spread_output)
                    or (single_player_teleport_match
                        and single_player_teleport_output)):
                raise CapsuleError(
                    f"{label}.last_output is outside the bounded output set")
        for field in ("x", "y", "z", "size"):
            value = container[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not 0 <= container["y"] <= 255:
            raise CapsuleError(f"{label}.y must be in 0..255")
        if "loaded_order" in container:
            loaded_order = container["loaded_order"]
            if isinstance(loaded_order, bool) \
                    or not isinstance(loaded_order, int) \
                    or loaded_order < 0 \
                    or loaded_order in seen_container_orders:
                raise CapsuleError(
                    f"{label}.loaded_order must be a unique "
                    "non-negative integer")
            seen_container_orders.add(loaded_order)
            container_order_count += 1
        if container_type in (
                "double_chest_half", "double_trapped_chest_half"):
            for field in double_chest_fields:
                value = container[field]
                if isinstance(value, bool) or not isinstance(value, int):
                    raise CapsuleError(f"{label}.{field} must be an integer")
            if not 0 <= container["pair_y"] <= 255:
                raise CapsuleError(
                    f"{label}.pair_y must be in 0..255")
        if container_type == "furnace":
            represented_furnaces += 1
            if represented_furnaces > 16:
                raise CapsuleError(
                    "state.containers exceeds the exact 16-furnace "
                    "runtime bound"
                )
            for field in furnace_fields:
                value = container[field]
                if field == "custom_name":
                    if not isinstance(value, str) \
                            or len(value.encode("utf-8")) >= 32:
                        raise CapsuleError(
                            f"{label}.custom_name must be a UTF-8 string "
                            "shorter than 32 bytes"
                        )
                elif isinstance(value, bool) or not isinstance(value, int) \
                        or not 0 <= value <= 32767:
                    raise CapsuleError(
                        f"{label}.{field} must be an integer in 0..32767"
                    )
        if container_type in (
                "brewing_stand", "dispenser", "dropper", "hopper", "jukebox",
                "shulker_box"):
            represented_static_containers += 1
            if represented_static_containers > 256:
                raise CapsuleError(
                    "state.containers exceeds the exact 256 static-container "
                    "runtime bound"
                )
        if container_type in (
                "command_block", "repeating_command_block",
                "chain_command_block"):
            represented_command_blocks += 1
            if represented_command_blocks > 256:
                raise CapsuleError(
                    "state.containers exceeds the exact 256-command-block "
                    "runtime bound"
                )
        key = (container["x"], container["y"], container["z"])
        if key in seen_containers:
            raise CapsuleError(f"{label} duplicates a container position")
        seen_containers.add(key)
        items = container["items"]
        if not isinstance(items, list):
            raise CapsuleError(f"{label}.items must be an array")
        represented_container_rows += len(items)
        if represented_container_rows > 4096:
            raise CapsuleError(
                "state.containers exceeds the exact 4096-row bound"
            )
        seen_container_slots = set()
        for item_index, item in enumerate(items):
            item_label = f"{label}.items[{item_index}]"
            if not isinstance(item, dict) or set(item) not in ({
                    "slot", "id", "count", "meta"}, {
                    "slot", "id", "count", "meta", "stack_payload"}):
                raise CapsuleError(
                    f"{item_label} must contain exactly "
                    "slot, id, count, meta and optional stack_payload"
                )
            values = tuple(
                item[field] for field in ("slot", "id", "count", "meta")
            )
            if any(
                    isinstance(value, bool) or not isinstance(value, int)
                    for value in values):
                raise CapsuleError(
                    f"{item_label} values must be integers"
            )
            slot, item_id, count, meta = values
            if slot in seen_container_slots \
                    or not 0 <= slot < container["size"]:
                raise CapsuleError(
                    f"{item_label}.slot must be unique and inside the "
                    "container"
                )
            if not 1 <= item_id <= 4095 \
                    or not 1 <= count <= 64 \
                    or not 0 <= meta <= 32767:
                raise CapsuleError(
                    f"{item_label} has an invalid stack"
                )
            if count > (1 if item_id in (373, 403, 438, 441)
                        or 219 <= item_id <= 234
                        else 64):
                raise CapsuleError(
                    f"{item_label} exceeds the represented item stack limit"
                )
            _validate_item_stack_payload(
                item.get("stack_payload"),
                f"{item_label}.stack_payload")
            if container_type == "jukebox" and (
                    slot != 0 or not 2256 <= item_id <= 2267
                    or count != 1 or meta != 0):
                raise CapsuleError(
                    f"{item_label} is not one exact vanilla music record"
                )
            if container_type == "brewing_stand":
                potion_items = (373, 438, 441)
                reagents = {
                    289, 331, 348, 353, 370, 372, 375, 376, 377,
                    378, 382, 396, 414, 437,
                }
                valid = (
                    slot <= 2 and item_id in potion_items
                    and count == 1 and 0 <= meta <= 36
                ) or (
                    slot == 3 and (
                        item_id in reagents
                        or (item_id == 349 and meta == 3)
                        or (item_id == 374 and count == 1 and meta == 0)
                    )
                ) or (
                    slot == 4 and item_id == 377
                )
                if not valid:
                    raise CapsuleError(
                        f"{item_label} is invalid for its brewing slot"
                    )
            seen_container_slots.add(slot)
        if container_type == "shulker_box":
            _validate_shulker_item_tag_nbt(
                container["item_tag_nbt"],
                f"{label}.item_tag_nbt", container)
    if container_order_count not in (0, len(containers)):
        raise CapsuleError(
            "state.containers must either all include loaded_order or all omit it"
        )
    if container_order_count and not loaded_tiles_present \
            and seen_container_orders != set(range(len(containers))):
        raise CapsuleError(
            "state.containers.loaded_order must be contiguous from zero"
        )
    if loaded_tiles_present:
        for index, container in enumerate(containers):
            position = (container["x"], container["y"], container["z"])
            if loaded_tile_order_by_position.get(position) \
                    != container.get("loaded_order"):
                raise CapsuleError(
                    f"state.containers[{index}].loaded_order does not match "
                    "state.loaded_tiles")
    flower_pots = state.get("flower_pots")
    if not isinstance(flower_pots, list):
        raise CapsuleError("state.flower_pots must be an array")
    if state.get("flower_pots_complete") is not True:
        raise CapsuleError(
            "state.flower_pots_complete must be true for a capsule"
        )
    if len(flower_pots) > 256:
        raise CapsuleError(
            "state.flower_pots exceeds the exact 256-entry runtime bound"
        )
    seen_flower_pots = set()
    for index, flower_pot in enumerate(flower_pots):
        label = f"state.flower_pots[{index}]"
        if not isinstance(flower_pot, dict) or set(flower_pot) != {
                "x", "y", "z", "item", "meta"}:
            raise CapsuleError(
                f"{label} must contain exactly x, y, z, item, meta"
            )
        for field in ("x", "y", "z", "item", "meta"):
            value = flower_pot[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not 0 <= flower_pot["y"] <= 255 \
                or not 0 <= flower_pot["item"] <= 4095 \
                or not 0 <= flower_pot["meta"] <= 32767 \
                or (flower_pot["item"] == 0 and flower_pot["meta"] != 0):
            raise CapsuleError(f"{label} has invalid flower-pot tile state")
        key = (flower_pot["x"], flower_pot["y"], flower_pot["z"])
        if key in seen_flower_pots:
            raise CapsuleError(f"{label} duplicates a flower-pot position")
        seen_flower_pots.add(key)
    note_blocks = state.get("note_blocks")
    if not isinstance(note_blocks, list):
        raise CapsuleError("state.note_blocks must be an array")
    if state.get("note_blocks_complete") is not True:
        raise CapsuleError(
            "state.note_blocks_complete must be true for a capsule"
        )
    if len(note_blocks) > 256:
        raise CapsuleError(
            "state.note_blocks exceeds the exact 256-entry runtime bound"
        )
    seen_note_blocks = set()
    for index, note_block in enumerate(note_blocks):
        label = f"state.note_blocks[{index}]"
        if not isinstance(note_block, dict) or set(note_block) != {
                "x", "y", "z", "note", "powered"}:
            raise CapsuleError(
                f"{label} must contain exactly x, y, z, note, powered"
            )
        for field in ("x", "y", "z", "note"):
            value = note_block[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not isinstance(note_block["powered"], bool):
            raise CapsuleError(f"{label}.powered must be a boolean")
        if not 0 <= note_block["y"] <= 255 \
                or not 0 <= note_block["note"] <= 24:
            raise CapsuleError(f"{label} has invalid note-block tile state")
        key = (note_block["x"], note_block["y"], note_block["z"])
        if key in seen_note_blocks:
            raise CapsuleError(f"{label} duplicates a note-block position")
        seen_note_blocks.add(key)
    skulls = state.get("skulls")
    if not isinstance(skulls, list):
        raise CapsuleError("state.skulls must be an array")
    if state.get("skulls_complete") is not True:
        raise CapsuleError(
            "state.skulls_complete must be true for a capsule"
        )
    if len(skulls) > 256:
        raise CapsuleError(
            "state.skulls exceeds the exact 256-entry runtime bound"
        )
    seen_skulls = set()
    skull_nbt_bytes = 0
    for index, skull in enumerate(skulls):
        label = f"state.skulls[{index}]"
        if not isinstance(skull, dict):
            raise CapsuleError(f"{label} must be an object")
        has_owner = skull.get("has_owner")
        expected_fields = {
            "x", "y", "z", "type", "rotation", "has_owner",
        } | ({"owner_nbt"} if has_owner is True else set())
        if set(skull) != expected_fields:
            raise CapsuleError(
                f"{label} has an incomplete skull/profile schema"
            )
        for field in ("x", "y", "z", "type", "rotation"):
            value = skull[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not isinstance(has_owner, bool):
            raise CapsuleError(f"{label}.has_owner must be boolean")
        if not 0 <= skull["y"] <= 255 \
                or not 0 <= skull["type"] <= 5 \
                or not 0 <= skull["rotation"] <= 15:
            raise CapsuleError(f"{label} has invalid skull tile state")
        if has_owner:
            if skull["type"] != 3:
                raise CapsuleError(
                    f"{label} has a profile but is not player skull type 3")
            owner_raw = _validate_game_profile_nbt(
                skull["owner_nbt"], f"{label}.owner_nbt")
            skull_nbt_bytes += len(owner_raw)
            if skull_nbt_bytes > MAX_NBT_PAYLOAD_TOTAL:
                raise CapsuleError(
                    "state.skulls exceeds the 16 MiB NBT payload bound")
        key = (skull["x"], skull["y"], skull["z"])
        if key in seen_skulls:
            raise CapsuleError(f"{label} duplicates a skull position")
        seen_skulls.add(key)
    decorative_tiles = state.get("decorative_tiles")
    if not isinstance(decorative_tiles, list):
        raise CapsuleError("state.decorative_tiles must be an array")
    if state.get("decorative_tiles_complete") is not True:
        raise CapsuleError(
            "state.decorative_tiles_complete must be true for a capsule")
    if len(decorative_tiles) > 512:
        raise CapsuleError(
            "state.decorative_tiles exceeds the exact 512-entry runtime bound")
    seen_decorative_tiles = set()
    decorative_nbt_bytes = 0
    for index, tile in enumerate(decorative_tiles):
        label = f"state.decorative_tiles[{index}]"
        if not isinstance(tile, dict):
            raise CapsuleError(f"{label} must be an object")
        expected = {
            "x", "y", "z", "block", "tile_nbt", "drop_item", "drop_meta",
        } | ({"drop_nbt"} if "drop_nbt" in tile else set())
        if set(tile) != expected:
            raise CapsuleError(f"{label} has an incomplete tile/drop schema")
        for field in ("x", "y", "z", "block", "drop_item", "drop_meta"):
            value = tile[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not 0 <= tile["y"] <= 255 \
                or tile["block"] not in (63, 68, 176, 177) \
                or not 0 <= tile["drop_meta"] <= 32767:
            raise CapsuleError(f"{label} has invalid decorative tile state")
        if tile["block"] in (63, 68):
            if tile["drop_item"] != 323 or tile["drop_meta"] != 0 \
                    or "drop_nbt" in tile:
                raise CapsuleError(f"{label} has an invalid sign drop")
        elif tile["drop_item"] != 425:
            raise CapsuleError(f"{label} has an invalid banner drop")
        tile_raw = _validate_root_nbt_payload(
            tile["tile_nbt"], f"{label}.tile_nbt")
        drop_raw = b""
        if "drop_nbt" in tile:
            drop_raw = _validate_root_nbt_payload(
                tile["drop_nbt"], f"{label}.drop_nbt")
        decorative_nbt_bytes += len(tile_raw) + len(drop_raw)
        if decorative_nbt_bytes > MAX_NBT_PAYLOAD_TOTAL:
            raise CapsuleError(
                "state.decorative_tiles exceeds the 16 MiB NBT payload bound")
        key = (tile["x"], tile["y"], tile["z"])
        if key in seen_decorative_tiles:
            raise CapsuleError(f"{label} duplicates a tile position")
        seen_decorative_tiles.add(key)
    world_rng = state["world_rng"]
    if not isinstance(world_rng, dict):
        raise CapsuleError("state.world_rng must be an object")
    java_seed48 = world_rng.get("java_seed48")
    if isinstance(java_seed48, bool) or not isinstance(java_seed48, int) \
            or not 0 <= java_seed48 < (1 << 48):
        raise CapsuleError(
            "state.world_rng.java_seed48 must be an integer in 0..2^48-1"
        )
    java_have_gaussian = world_rng.get("java_have_gaussian", False)
    java_gaussian = world_rng.get("java_gaussian", 0.0)
    if not isinstance(java_have_gaussian, bool):
        raise CapsuleError(
            "state.world_rng.java_have_gaussian must be a boolean")
    _finite_number(java_gaussian, "state.world_rng.java_gaussian")
    math_seed48 = world_rng.get("math_seed48")
    if isinstance(math_seed48, bool) or not isinstance(math_seed48, int) \
            or not 0 <= math_seed48 < (1 << 48):
        raise CapsuleError(
            "state.world_rng.math_seed48 must be an integer in 0..2^48-1"
        )
    collections_seed48 = world_rng.get("collections_seed48")
    if collections_seed48 is not None and (
            isinstance(collections_seed48, bool)
            or not isinstance(collections_seed48, int)
            or not 0 <= collections_seed48 < (1 << 48)):
        raise CapsuleError(
            "state.world_rng.collections_seed48 must be an integer in "
            "0..2^48-1")
    server_uuid_seed48 = world_rng.get("server_uuid_seed48")
    if server_uuid_seed48 is not None and (
            isinstance(server_uuid_seed48, bool)
            or not isinstance(server_uuid_seed48, int)
            or not 0 <= server_uuid_seed48 < (1 << 48)):
        raise CapsuleError(
            "state.world_rng.server_uuid_seed48 must be an integer in "
            "0..2^48-1"
        )
    entity_seed_generator_seed48 = world_rng.get(
        "entity_seed_generator_seed48")
    if entity_seed_generator_seed48 is not None and (
            isinstance(entity_seed_generator_seed48, bool)
            or not isinstance(entity_seed_generator_seed48, int)
            or not 0 <= entity_seed_generator_seed48 < (1 << 48)):
        raise CapsuleError(
            "state.world_rng.entity_seed_generator_seed48 must be an "
            "integer in 0..2^48-1"
        )
    block_seed48 = world_rng.get("block_seed48")
    if isinstance(block_seed48, bool) or not isinstance(block_seed48, int) \
            or not 0 <= block_seed48 < (1 << 48):
        raise CapsuleError(
            "state.world_rng.block_seed48 must be an integer in 0..2^48-1"
        )
    inventory_helper_seed48 = world_rng.get("inventory_helper_seed48")
    inventory_helper_have_gaussian = world_rng.get(
        "inventory_helper_have_gaussian")
    inventory_helper_gaussian = world_rng.get("inventory_helper_gaussian")
    inventory_helper_values = (
        inventory_helper_seed48,
        inventory_helper_have_gaussian,
        inventory_helper_gaussian,
    )
    if any(value is not None for value in inventory_helper_values):
        if any(value is None for value in inventory_helper_values):
            raise CapsuleError(
                "state.world_rng InventoryHelper state must include seed, "
                "Gaussian-cache flag, and Gaussian value")
        if isinstance(inventory_helper_seed48, bool) \
                or not isinstance(inventory_helper_seed48, int) \
                or not 0 <= inventory_helper_seed48 < (1 << 48):
            raise CapsuleError(
                "state.world_rng.inventory_helper_seed48 must be an "
                "integer in 0..2^48-1")
        if not isinstance(inventory_helper_have_gaussian, bool):
            raise CapsuleError(
                "state.world_rng.inventory_helper_have_gaussian must be "
                "a boolean")
        _finite_number(
            inventory_helper_gaussian,
            "state.world_rng.inventory_helper_gaussian")
    update_lcg = world_rng.get("update_lcg")
    if isinstance(update_lcg, bool) or not isinstance(update_lcg, int) \
            or not -(1 << 31) <= update_lcg < (1 << 31):
        raise CapsuleError(
            "state.world_rng.update_lcg must be a signed 32-bit integer"
        )
    seen_scheduled = set()
    previous_order = None
    for index, entry in enumerate(scheduled):
        label = f"state.scheduled_ticks[{index}]"
        if not isinstance(entry, dict):
            raise CapsuleError(f"{label} must be an object")
        values = []
        for field in (
            "x", "y", "z", "block", "time", "priority", "order"
        ):
            value = entry.get(field)
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
            values.append(value)
        x, y, z, block, due, priority, order = values
        if not 0 <= y <= 255 or not 1 <= block <= 4095 \
                or due < 0 or not -128 <= priority <= 127 or order < 0:
            raise CapsuleError(f"{label} has invalid pending-update state")
        key = (x, y, z, block)
        if key in seen_scheduled:
            raise CapsuleError(f"{label} duplicates a position/block key")
        seen_scheduled.add(key)
        sort_key = (due, priority, order)
        if previous_order is not None and sort_key <= previous_order:
            raise CapsuleError(
                "state.scheduled_ticks must be strictly ordered by "
                "time/priority/order"
            )
        previous_order = sort_key
    torch_toggles = state.get("redstone_torch_toggles")
    if not isinstance(torch_toggles, list):
        raise CapsuleError("state.redstone_torch_toggles must be an array")
    if state.get("redstone_torch_toggles_complete") is not True:
        raise CapsuleError(
            "state.redstone_torch_toggles_complete must be true"
        )
    if len(torch_toggles) > 4096:
        raise CapsuleError(
            "state.redstone_torch_toggles exceeds the exact 4096-entry bound"
        )
    previous_toggle_time = None
    total_time = int(state["time"]["total_time"])
    for index, toggle in enumerate(torch_toggles):
        label = f"state.redstone_torch_toggles[{index}]"
        if not isinstance(toggle, dict) \
                or set(toggle) != {"x", "y", "z", "time"}:
            raise CapsuleError(
                f"{label} must contain exactly x, y, z, time"
            )
        for field in ("x", "y", "z", "time"):
            value = toggle[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not 0 <= toggle["y"] <= 255 \
                or not 0 <= toggle["time"] <= total_time:
            raise CapsuleError(f"{label} has invalid position/time")
        if previous_toggle_time is not None \
                and toggle["time"] < previous_toggle_time:
            raise CapsuleError(
                "state.redstone_torch_toggles must be chronological"
            )
        previous_toggle_time = toggle["time"]
    scheduled_context = state.get("scheduled_tick_context", [])
    if not isinstance(scheduled_context, list):
        raise CapsuleError("state.scheduled_tick_context must be an array")
    seen_context = set()
    fire_tick_values = set()
    for index, context in enumerate(scheduled_context):
        label = f"state.scheduled_tick_context[{index}]"
        if not isinstance(context, dict):
            raise CapsuleError(f"{label} must be an object")
        key_values = []
        for field in ("x", "y", "z", "block"):
            value = context.get(field)
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
            key_values.append(value)
        key = tuple(key_values)
        if key in seen_context:
            raise CapsuleError(f"{label} duplicates a position/block key")
        seen_context.add(key)
        if context["block"] != 51:
            raise CapsuleError(f"{label}.block must be fire (51)")
        if not isinstance(context.get("high_humidity"), bool) \
                or not isinstance(context.get("do_fire_tick"), bool) \
                or not isinstance(context.get("raining"), bool):
            raise CapsuleError(f"{label} fire booleans are incomplete")
        rain_probe_fields = (
            "raining_at", "raining_at_west", "raining_at_east",
            "raining_at_north", "raining_at_south",
        )
        if context["raining"]:
            for field in ("rain_time", "thunder_time"):
                value = context.get(field)
                if isinstance(value, bool) or not isinstance(value, int) \
                        or not 0 <= value <= 2147483647:
                    raise CapsuleError(
                        f"{label}.{field} must be a non-negative integer")
            if any(not isinstance(context.get(field), bool)
                   for field in rain_probe_fields):
                raise CapsuleError(
                    f"{label} rain-exposure booleans are incomplete")
            if not isinstance(
                    context.get("rain_can_die_west_candidate"), bool):
                raise CapsuleError(
                    f"{label} west-candidate canDie boolean is incomplete")
        else:
            for field in ("rain_time", "thunder_time"):
                value = context.get(field)
                if value is not None and (
                        isinstance(value, bool) or not isinstance(value, int)
                        or not 0 <= value <= 2147483647):
                    raise CapsuleError(f"{label}.{field} is invalid")
            for field in rain_probe_fields:
                value = context.get(field)
                if value is not None and not isinstance(value, bool):
                    raise CapsuleError(f"{label}.{field} must be boolean")
        fire_tick_values.add(context["do_fire_tick"])
        difficulty = context.get("difficulty")
        if isinstance(difficulty, bool) or not isinstance(difficulty, int) \
                or not 0 <= difficulty <= 3:
            raise CapsuleError(f"{label}.difficulty must be in 0..3")
    if len(fire_tick_values) > 1:
        raise CapsuleError(
            "state.scheduled_tick_context has inconsistent doFireTick values"
        )
    time = state["time"]
    if not isinstance(time, dict):
        raise CapsuleError("state.time must be an object")
    for field in ("world_time", "total_time"):
        value = time.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise CapsuleError(f"state.time.{field} must be a non-negative integer")
    for field in ("raining", "thundering"):
        if not isinstance(time.get(field), bool):
            raise CapsuleError(f"state.time.{field} must be boolean")
    for field in ("rain_time", "thunder_time", "clean_weather_time"):
        value = time.get(field)
        if isinstance(value, bool) or not isinstance(value, int) \
                or not 0 <= value <= 2147483647:
            raise CapsuleError(
                f"state.time.{field} must be a non-negative integer")
    for field in ("do_weather_cycle", "do_daylight_cycle"):
        if not isinstance(time.get(field), bool):
            raise CapsuleError(f"state.time.{field} must be boolean")
    for field in (
            "prev_rain_strength", "rain_strength",
            "prev_thunder_strength", "thunder_strength"):
        value = _finite_number(time.get(field), f"state.time.{field}")
        if not 0.0 <= value <= 1.0:
            raise CapsuleError(f"state.time.{field} must be in [0,1]")
    no_ai_exact = [
        entity for entity in entities
        if entity.get("no_ai_plain_exact") is True
        or (entity["type"] == "EntityPig"
            and entity.get("no_ai_pig_exact") is True)
        or (entity["type"] == "EntityVillager"
            and entity.get("villager_exact") is True)
        or (entity["type"] in ("EntityWolf", "EntityOcelot")
            and entity.get("tameable_exact") is True)
        or (entity["type"] in {
                "EntityHorse", "EntityDonkey", "EntityMule",
                "EntitySkeletonHorse", "EntityZombieHorse", "EntityLlama"}
            and entity.get("horse_exact") is True)
    ]
    solar_types = {
        "EntityZombie", "EntityZombieVillager", "EntitySkeleton",
        "EntityWitherSkeleton",
    }
    daytime = int(time["world_time"]) % 24000 < 12000
    if player["dim"] == 0 and daytime and any(
            entity["type"] in solar_types for entity in no_ai_exact):
        raise CapsuleError(
            "plain NoAI zombie/skeleton continuation requires night or a "
            "non-Overworld dimension until daylight equipment is represented"
        )
    # Loaded-order living/living and player/living pushes are represented by
    # the runtime. Do not hide dense or colliding saves behind the historical
    # four-block isolation fence; the continuation comparator owns them.


def create_capsule(
    state_path: pathlib.Path,
    blocks_path: pathlib.Path,
    box: list[int],
    output_dir: pathlib.Path,
    *,
    sky_light_path: pathlib.Path | None = None,
    block_light_path: pathlib.Path | None = None,
    seed: int,
    source_engine: str,
    source_version: str,
) -> pathlib.Path:
    state = copy.deepcopy(_read_state(state_path))
    _validate_state(state)
    cells = cell_count(box)
    raw = blocks_path.read_bytes()
    if len(raw) != cells * 2:
        raise CapsuleError(
            f"{blocks_path}: expected {cells * 2} bytes for {cells} cells, "
            f"got {len(raw)}"
        )
    states = struct.unpack(f"<{cells}H", raw)
    invalid = next((index for index, value in enumerate(states)
                    if (value >> 4) > 4095), None)
    if invalid is not None:
        raise CapsuleError(
            f"{blocks_path}: invalid packed state at {coordinate(invalid, box)}"
        )
    sky_raw = None
    if sky_light_path is not None:
        sky_raw = sky_light_path.read_bytes()
        if len(sky_raw) != cells:
            raise CapsuleError(
                f"{sky_light_path}: expected {cells} bytes for {cells} "
                f"skylight cells, got {len(sky_raw)}"
            )
        invalid = next(
            (index for index, value in enumerate(sky_raw) if value > 15), None
        )
        if invalid is not None:
            raise CapsuleError(
                f"{sky_light_path}: invalid skylight value {sky_raw[invalid]} "
                f"at {coordinate(invalid, box)}"
            )
    block_light_raw = None
    if block_light_path is not None:
        block_light_raw = block_light_path.read_bytes()
        if len(block_light_raw) != cells:
            raise CapsuleError(
                f"{block_light_path}: expected {cells} bytes for {cells} "
                f"block-light cells, got {len(block_light_raw)}"
            )
        invalid = next(
            (index for index, value in enumerate(block_light_raw)
             if value > 15), None
        )
        if invalid is not None:
            raise CapsuleError(
                f"{block_light_path}: invalid block-light value "
                f"{block_light_raw[invalid]} at {coordinate(invalid, box)}"
            )
    # Version 2 promotes only proof-safe scheduled-update subsets: inert stone,
    # scheduled dynamic water in a bounded two-air-layer basin, a deterministic
    # level-0 lava source over a flat stone plane, metadata-0 sand above a
    # clear air column ending at stone, and supported or proof-fenced
    # metadata-0 dragon-egg callbacks, and supported canonical anvil callbacks.
    # Falling anvil callbacks need an Entity.rand cursor that world saves do
    # not contain, so they remain captured-only. Fire additionally requires a dry
    # NORMAL context and an air/stone/planks/logs/bookshelves/wool/grass/TNT/
    # fire proof neighborhood;
    # one exact source-humidity predicate can be transported with that proof.
    # Lit-lamp callbacks require an unpowered proof neighborhood including
    # every adjacent normal cube's strong-power inputs. Redstone-torch and
    # repeater callbacks require registry-backed support plus bounded,
    # represented redstone neighborhoods. Observer callbacks require a valid
    # six-way facing and a bounded inert/observer/lamp notification region.
    # Other pending states remain captured-only.
    x0, y0, z0, x1, y1, z1 = box
    exact_scheduled = []
    nx = x1 - x0 + 1
    nz = z1 - z0 + 1
    fire_context = {
        (row["x"], row["y"], row["z"], row["block"]): row
        for row in state.get("scheduled_tick_context", [])
    }
    raining_fire_context_count = sum(
        context.get("raining") is True for context in fire_context.values()
    )
    humid_fire_context_count = sum(
        context.get("high_humidity") is True
        and context.get("raining") is not True
        for context in fire_context.values()
    )

    def packed_at(x, y, z):
        if not (x0 <= x <= x1 and y0 <= y <= y1
                and z0 <= z <= z1):
            return None
        offset = ((y - y0) * nz + (z - z0)) * nx + (x - x0)
        return states[offset]

    for village_index, village in enumerate(state["villages"]):
        for door_index, door in enumerate(village["doors"]):
            packed = packed_at(door["x"], door["y"], door["z"])
            if packed is None or packed >> 4 not in (64, 193, 194, 195, 196, 197):
                raise CapsuleError(
                    f"state.villages[{village_index}].doors[{door_index}] "
                    "is not a represented wooden door block "
                    f"(packed={packed!r})"
                )

    # EntityItem's air/water/lava/fire update, ordinary full-cube landing, and
    # represented tagless-stack merge are exact in magma. Fence the saved state
    # away from pickup, shaped collision, wall collision, non-default
    # slipperiness, and interactions with unrepresented item payloads.
    # The pass-through halo covers the current and post-gravity swept
    # 0.25-wide AABB. Water additionally proves every cell used by getFlow.
    exact_items = [
        entity for entity in state["entities"]
        if entity["type"] == "EntityItem"
        and entity.get("item_exact") is True
    ]
    all_items = [
        entity for entity in state["entities"]
        if entity["type"] == "EntityItem"
    ]
    player = state["player"]
    for entity in exact_items:
        if abs(entity["x"] - player["x"]) <= 2.0 \
                and abs(entity["z"] - player["z"]) <= 2.0 \
                and player["y"] - 1.0 <= entity["y"] \
                    <= player["y"] + 4.0:
            raise CapsuleError(
                "exact plain EntityItem intersects the conservative player "
                "pickup fence")
        next_vy = entity["vy"] - (
            0.0 if entity["no_gravity"] else 0.03999999910593033)
        end_x = entity["x"] + entity["vx"]
        end_y = entity["y"] + next_vy
        end_z = entity["z"] + entity["vz"]
        if abs(end_x - player["x"]) <= 2.0 \
                and abs(end_z - player["z"]) <= 2.0 \
                and player["y"] - 1.0 <= end_y \
                    <= player["y"] + 4.0:
            raise CapsuleError(
                "exact plain EntityItem enters the conservative player "
                "pickup fence")
        footprint_x = range(
            math.floor(min(entity["x"], end_x) - 0.125),
            math.floor(max(entity["x"], end_x) + 0.125) + 1)
        footprint_z = range(
            math.floor(min(entity["z"], end_z) - 0.125),
            math.floor(max(entity["z"], end_z) + 0.125) + 1)
        normal_masks, _provider_masks, _opaque_masks = (
            blockstate_predicate_masks())

        def default_slip_normal_cube(x, y, z):
            packed = packed_at(x, y, z)
            if packed is None:
                return False
            block_id = packed >> 4
            meta = packed & 15
            # Ice, packed/frosted ice, and slime are normal cubes with a
            # non-default slipperiness. They remain outside this proof.
            return block_id not in (79, 165, 174, 212) \
                and bool(normal_masks[block_id] & (1 << meta))

        def item_pass_through(x, y, z):
            packed = packed_at(x, y, z)
            return packed is not None \
                and packed >> 4 in (0, 8, 9, 10, 11, 51, 81)

        def water_flow_proven(x, y, z):
            packed = packed_at(x, y, z)
            if packed is None or packed >> 4 not in (8, 9):
                return True
            for dx, dz in ((0, 1), (-1, 0), (0, -1), (1, 0)):
                neighbor = packed_at(x + dx, y, z + dz)
                above = packed_at(x + dx, y + 1, z + dz)
                below = packed_at(x + dx, y - 1, z + dz)
                if neighbor is None or above is None or below is None:
                    return False
                neighbor_id = neighbor >> 4
                if neighbor_id not in (0, 8, 9) \
                        and not default_slip_normal_cube(
                            x + dx, y, z + dz):
                    return False
                if above >> 4 not in (0, 8, 9) \
                        and not default_slip_normal_cube(
                            x + dx, y + 1, z + dz):
                    return False
                if neighbor_id == 0 and below >> 4 not in (0, 8, 9) \
                        and not default_slip_normal_cube(
                            x + dx, y - 1, z + dz):
                    return False
            return True

        start_box = (
            entity["x"] - 0.125, entity["y"], entity["z"] - 0.125,
            entity["x"] + 0.125, entity["y"] + 0.25,
            entity["z"] + 0.125,
        )
        start_full_cube_collision = False
        for bx in range(math.floor(start_box[0]), math.ceil(start_box[3])):
            for by in range(
                    math.floor(start_box[1]), math.ceil(start_box[4])):
                for bz in range(
                        math.floor(start_box[2]), math.ceil(start_box[5])):
                    packed = packed_at(bx, by, bz)
                    if packed is None:
                        raise CapsuleError(
                            "exact plain EntityItem start-box proof is "
                            f"missing at {(bx, by, bz)}")
                    if default_slip_normal_cube(bx, by, bz):
                        start_full_cube_collision = True
                    elif packed >> 4 not in (0, 8, 9, 10, 11, 51):
                        raise CapsuleError(
                            "exact plain EntityItem starts in an unsupported "
                            f"shaped block at {(bx, by, bz)}")
        if start_full_cube_collision:
            center_x = math.floor(entity["x"])
            center_y = math.floor(entity["y"] + 0.125)
            center_z = math.floor(entity["z"])
            if abs(entity["x"] - player["x"]) <= 3.0 \
                    and abs(entity["z"] - player["z"]) <= 3.0 \
                    and player["y"] - 2.0 <= entity["y"] \
                        <= player["y"] + 5.0:
                raise CapsuleError(
                    "exact pushed-out EntityItem enters the conservative "
                    "player pickup fence")
            for bx in range(center_x - 1, center_x + 2):
                for by in range(center_y - 1, center_y + 2):
                    for bz in range(center_z - 1, center_z + 2):
                        packed = packed_at(bx, by, bz)
                        if packed is None or (packed >> 4 != 0
                                and not default_slip_normal_cube(
                                    bx, by, bz)):
                            raise CapsuleError(
                                "exact plain EntityItem full-cube push-out "
                                f"proof is missing at {(bx, by, bz)}")
            continue

        support_y = None
        air_min_y = math.floor(min(entity["y"], end_y))
        if entity["on_ground"]:
            ground_y = round(entity["y"])
            if entity["no_gravity"] \
                    or abs(entity["y"] - ground_y) > 1e-12 \
                    or next_vy > 0.0 or end_y >= entity["y"]:
                raise CapsuleError(
                    "exact grounded EntityItem has an unsupported motion "
                    "or gravity boundary")
            support_y = ground_y - 1
            air_min_y = ground_y
        elif next_vy <= 0.0:
            candidate_y = math.floor(end_y)
            candidate_top = candidate_y + 1
            if end_y < candidate_top <= entity["y"] and all(
                    default_slip_normal_cube(bx, candidate_y, bz)
                    for bx in footprint_x for bz in footprint_z):
                support_y = candidate_y
                air_min_y = candidate_top

        if support_y is not None and not all(
                default_slip_normal_cube(bx, support_y, bz)
                for bx in footprint_x for bz in footprint_z):
            raise CapsuleError(
                "exact plain EntityItem full-cube support proof is missing")

        def cactus_start_clear(x, y, z):
            packed = packed_at(x, y, z)
            if packed is None or packed >> 4 != 81:
                return True
            cactus = (
                x + 0.0625, y, z + 0.0625,
                x + 0.9375, y + 0.9375, z + 0.9375,
            )
            return start_box[3] <= cactus[0] \
                or start_box[0] >= cactus[3] \
                or start_box[4] <= cactus[1] \
                or start_box[1] >= cactus[4] \
                or start_box[5] <= cactus[2] \
                or start_box[2] >= cactus[5]

        for bx in footprint_x:
            for by in range(
                    air_min_y,
                    math.floor(max(entity["y"], end_y) + 0.25) + 1):
                for bz in footprint_z:
                    if not item_pass_through(bx, by, bz):
                        raise CapsuleError(
                            "exact plain EntityItem swept pass-through proof is "
                            f"missing at {(bx, by, bz)}")
                    if not cactus_start_clear(bx, by, bz):
                        raise CapsuleError(
                            "exact plain EntityItem starts inside an "
                            f"unsupported cactus push-out at {(bx, by, bz)}")
        for bx in footprint_x:
            for by in range(
                    math.floor(min(entity["y"], end_y) - 0.15),
                    math.floor(max(entity["y"], end_y) + 0.4) + 1):
                for bz in footprint_z:
                    if packed_at(bx, by, bz) is None:
                        raise CapsuleError(
                            "exact plain EntityItem environment proof is "
                            f"missing at {(bx, by, bz)}")
                    if not water_flow_proven(bx, by, bz):
                        raise CapsuleError(
                            "exact plain EntityItem water-flow proof is "
                            f"missing at {(bx, by, bz)}")
    def item_pair_close(first, second):
        return abs(first["x"] - second["x"]) <= 1.0 \
            and abs(first["y"] - second["y"]) <= 1.0 \
            and abs(first["z"] - second["z"]) <= 1.0

    # A represented item may never silently interact with a captured-only item
    # that emit_magma cannot restore.
    for exact in exact_items:
        for other in all_items:
            if other is exact or other.get("item_exact") is True:
                continue
            if item_pair_close(exact, other):
                raise CapsuleError(
                    "exact plain EntityItem intersects a captured-only item "
                    "merge fence")

    comparator_state = {
        (entry["x"], entry["y"], entry["z"]): entry["output_signal"]
        for entry in state["comparators"]
    }
    for position in comparator_state:
        packed = packed_at(*position)
        if packed is None or packed >> 4 not in (149, 150):
            raise CapsuleError(
                "captured comparator tile state is outside the block cuboid "
                f"or does not match a comparator block at {position}"
            )
    for piston in state["moving_pistons"]:
        position = (piston["x"], piston["y"], piston["z"])
        packed = packed_at(*position)
        if packed is None or packed >> 4 != 36 \
                or (packed & 7) != piston["facing"]:
            raise CapsuleError(
                "captured moving-piston tile state is outside the block "
                "cuboid or does not match a facing-compatible moving block "
                f"at {position}"
            )
    for spawner in state.get("spawners", []):
        position = (spawner["x"], spawner["y"], spawner["z"])
        packed = packed_at(*position)
        if packed is None or packed >> 4 != 52:
            raise CapsuleError(
                "captured spawner state is outside the block cuboid or "
                f"does not match a spawner block at {position}")
    for flower_pot in state["flower_pots"]:
        position = (
            flower_pot["x"], flower_pot["y"], flower_pot["z"]
        )
        packed = packed_at(*position)
        if packed is None or packed >> 4 != 140:
            raise CapsuleError(
                "captured flower-pot tile state is outside the block cuboid "
                f"or does not match a flower-pot block at {position}"
            )
    for note_block in state["note_blocks"]:
        position = (note_block["x"], note_block["y"], note_block["z"])
        packed = packed_at(*position)
        if packed is None or packed >> 4 != 25:
            raise CapsuleError(
                "captured note-block tile state is outside the block cuboid "
                f"or does not match a note block at {position}"
            )
    for skull in state["skulls"]:
        position = (skull["x"], skull["y"], skull["z"])
        packed = packed_at(*position)
        if packed is None or packed >> 4 != 144:
            raise CapsuleError(
                "captured skull tile state is outside the block cuboid or "
                f"does not match a skull block at {position}"
            )
    for tile in state["decorative_tiles"]:
        position = (tile["x"], tile["y"], tile["z"])
        packed = packed_at(*position)
        if packed is None or packed >> 4 != tile["block"]:
            raise CapsuleError(
                "captured decorative tile is outside the block cuboid or "
                f"does not match its block at {position}")
    container_state = {
        (entry["x"], entry["y"], entry["z"]): entry
        for entry in state["containers"]
    }
    for position in container_state:
        container = container_state[position]
        packed = packed_at(*position)
        block_id = None if packed is None else packed >> 4
        expected_ids = (
            (54,)
            if container["type"] in (
                "single_chest", "double_chest_half")
            else (146,)
            if container["type"] in (
                "single_trapped_chest", "double_trapped_chest_half")
            else (23,)
            if container["type"] == "dispenser"
            else (158,)
            if container["type"] == "dropper"
            else (154,)
            if container["type"] == "hopper"
            else (117,)
            if container["type"] == "brewing_stand"
            else (84,)
            if container["type"] == "jukebox"
            else tuple(range(219, 235))
            if container["type"] == "shulker_box"
            else (137,)
            if container["type"] == "command_block"
            else (210,)
            if container["type"] == "repeating_command_block"
            else (211,)
            if container["type"] == "chain_command_block"
            else (61, 62)
        )
        if block_id not in expected_ids:
            raise CapsuleError(
                "captured container state is outside the block cuboid or "
                f"does not match its block type at {position}: "
                f"expected {expected_ids}, got {block_id}"
            )
        if container["type"] == "jukebox":
            expected_meta = 1 if container["items"] else 0
            if (packed & 15) != expected_meta:
                raise CapsuleError(
                    f"captured jukebox record/meta disagree at {position}"
                )
        if container["type"] == "brewing_stand":
            bottle_bits = sum(
                1 << slot
                for slot in range(3)
                if any(item["slot"] == slot for item in container["items"])
            )
            if (packed & 7) != bottle_bits:
                raise CapsuleError(
                    f"captured brewing bottles/meta disagree at {position}"
                )
        if container["type"] == "shulker_box":
            if block_id != container["block"] \
                    or (packed & 15) != container["facing"]:
                raise CapsuleError(
                    f"captured shulker block/facing disagree at {position}"
                )
        if container["type"] in (
                "furnace", "brewing_stand", "dispenser", "dropper",
                "hopper", "jukebox",
                "shulker_box",
                "command_block", "repeating_command_block",
                "chain_command_block"):
            continue
        x, y, z = position
        if container["type"] in (
                "double_chest_half", "double_trapped_chest_half"):
            pair_position = (
                container["pair_x"],
                container["pair_y"],
                container["pair_z"],
            )
            pair = container_state.get(pair_position)
            adjacent_chests = {
                (x + dx, y, z + dz)
                for dx, dz in ((0, -1), (1, 0), (0, 1), (-1, 0))
                if (
                    (neighbor := packed_at(x + dx, y, z + dz))
                    is not None
                    and neighbor >> 4 == (
                        146
                        if container["type"]
                            == "double_trapped_chest_half"
                        else 54
                    )
                )
            }
            if (
                pair is None
                or pair.get("type") != container["type"]
                or (
                    pair.get("pair_x"),
                    pair.get("pair_y"),
                    pair.get("pair_z"),
                ) != position
                or pair_position[1] != y
                or abs(pair_position[0] - x)
                    + abs(pair_position[2] - z) != 1
                or adjacent_chests != {pair_position}
            ):
                raise CapsuleError(
                    f"captured double chest at {position} lacks an exact "
                    "reciprocal horizontal pair"
                )
            continue
        chest_block_id = (
            146 if container["type"] == "single_trapped_chest" else 54)
        for dx, dz in ((0, -1), (1, 0), (0, 1), (-1, 0)):
            adjacent = packed_at(x + dx, y, z + dz)
            if adjacent is None:
                raise CapsuleError(
                    "captured single chest lacks a represented horizontal "
                    f"neighbor at {(x + dx, y, z + dz)}"
                )
            if adjacent >> 4 == chest_block_id:
                raise CapsuleError(
                    f"captured chest at {position} is a double chest"
                )

    item_frame_state = {
        (
            entry["hanging_x"],
            entry["hanging_y"],
            entry["hanging_z"],
        ): entry
        for entry in state["item_frames"]
    }
    normal_masks, _provider_masks, _opaque_masks = (
        blockstate_predicate_masks())
    frame_offsets = {
        2: (0, -1), 3: (0, 1), 4: (-1, 0), 5: (1, 0),
    }
    art_sizes = (
        *((16, 16),) * 7,
        *((32, 16),) * 5,
        *((16, 32),) * 2,
        *((32, 32),) * 6,
        (64, 32),
        *((64, 64),) * 3,
        *((64, 48),) * 2,
    )
    facing_offsets = frame_offsets
    side_offsets = {
        2: (-1, 0), 3: (1, 0), 4: (0, 1), 5: (0, -1),
    }
    for frame in state["item_frames"]:
        hanging = (
            frame["hanging_x"], frame["hanging_y"], frame["hanging_z"])
        packed = packed_at(*hanging)
        if packed != 0:
            raise CapsuleError(
                "captured item frame hanging cell is outside the block "
                f"cuboid or not air at {hanging}"
            )
        face_dx, face_dz = frame_offsets[frame["facing"]]
        support = (
            hanging[0] - face_dx, hanging[1], hanging[2] - face_dz)
        support_packed = packed_at(*support)
        if support_packed is None:
            raise CapsuleError(
                f"captured item frame support is outside the cuboid at {support}"
            )
        support_id = support_packed >> 4
        support_meta = support_packed & 15
        if not (
            0 <= support_id < 256
            and normal_masks[support_id] & (1 << support_meta)
        ):
            raise CapsuleError(
                f"captured item frame lacks an exact normal-cube support "
                f"at {support}"
            )
        expected_pose = (
            hanging[0] + 0.5 - face_dx * 0.46875,
            hanging[1] + 0.5,
            hanging[2] + 0.5 - face_dz * 0.46875,
        )
        actual_pose = (frame["x"], frame["y"], frame["z"])
        if any(
                float(actual).hex() != float(expected).hex()
                for actual, expected in zip(actual_pose, expected_pose)):
            raise CapsuleError(
                f"captured item frame pose disagrees with its hanging state "
                f"at {hanging}: {actual_pose} vs {expected_pose}"
            )

    for painting in state["paintings"]:
        hanging = (
            painting["hanging_x"], painting["hanging_y"],
            painting["hanging_z"])
        width, height = art_sizes[painting["art"]]
        columns = width // 16
        rows = height // 16
        column_start = -((columns - 1) // 2)
        row_start = -((rows - 1) // 2)
        face_dx, face_dz = facing_offsets[painting["facing"]]
        side_dx, side_dz = side_offsets[painting["facing"]]
        for column in range(columns):
            for row in range(rows):
                side = column + column_start
                front = (
                    hanging[0] + side_dx * side,
                    hanging[1] + row + row_start,
                    hanging[2] + side_dz * side,
                )
                support = (
                    front[0] - face_dx,
                    front[1],
                    front[2] - face_dz,
                )
                if packed_at(*front) != 0:
                    raise CapsuleError(
                        "captured painting front is outside the cuboid or "
                        f"not air at {front}")
                support_packed = packed_at(*support)
                if support_packed is None:
                    raise CapsuleError(
                        "captured painting support is outside the cuboid at "
                        f"{support}")
                support_id = support_packed >> 4
                support_meta = support_packed & 15
                if not (
                        0 <= support_id < 256
                        and normal_masks[support_id] & (1 << support_meta)):
                    raise CapsuleError(
                        "captured painting lacks exact normal-cube support "
                        f"at {support}")

    fence_ids = {85, 113, 188, 189, 190, 191, 192}
    for knot in state["leash_knots"]:
        position = (
            knot["hanging_x"], knot["hanging_y"], knot["hanging_z"])
        packed = packed_at(*position)
        if packed is None or packed >> 4 not in fence_ids:
            raise CapsuleError(
                f"captured leash knot lacks a fence at {position}")

    for entry in state["scheduled_ticks"]:
        if entry["block"] not in (
            1, 8, 10, 12, 13, 23, 51, 70, 75, 76, 77, 93, 94, 122, 124, 143,
            137, 145, 210, 211,
            131, 132, 147, 148, 149, 150, 158, 218
        ):
            continue
        if not (x0 <= entry["x"] <= x1 and y0 <= entry["y"] <= y1
                and z0 <= entry["z"] <= z1):
            continue
        index = (
            ((entry["y"] - y0) * nz + (entry["z"] - z0)) * nx
            + (entry["x"] - x0)
        )
        block_state = states[index]
        if block_state >> 4 != entry["block"]:
            raise CapsuleError(
                "scheduled entry does not match packed block state at "
                f"{(entry['x'], entry['y'], entry['z'])}"
            )
        if entry["block"] in (
            8, 10, 12, 13, 23, 51, 70, 75, 76, 77, 93, 94, 122, 124, 143,
            145,
            131, 132, 147, 148, 149, 150, 158, 218
        ):
            x, y, z = entry["x"], entry["y"], entry["z"]

        if entry["block"] == 8:

            def exact_basin(floor_y):
                for dz in range(-5, 6):
                    for dx in range(-5, 6):
                        if abs(dx) + abs(dz) > 5:
                            continue
                        floor = packed_at(x + dx, floor_y, z + dz)
                        lower = packed_at(x + dx, floor_y + 1, z + dz)
                        upper = packed_at(x + dx, floor_y + 2, z + dz)
                        cap = packed_at(x + dx, floor_y + 3, z + dz)
                        lower_id = None if lower is None else lower >> 4
                        upper_id = None if upper is None else upper >> 4
                        if (
                            floor != (1 << 4)
                            or lower_id not in (0, 8, 9)
                            or upper_id not in (0, 8, 9)
                            or cap != 0
                        ):
                            return False
                return True

            enclosed_below_lava = (
                block_state == (8 << 4)
                and packed_at(x, y - 1, z) == (1 << 4)
                and packed_at(x, y + 1, z) == (10 << 4)
                and all(
                    packed_at(x + dx, y, z + dz) == (1 << 4)
                    for dx, dz in ((0, -1), (0, 1), (-1, 0), (1, 0))
                )
            )
            if not (
                exact_basin(y - 1)
                or exact_basin(y - 2)
                or enclosed_below_lava
            ):
                continue
        elif entry["block"] == 10:
            if block_state != (10 << 4):
                continue
            exact_plane = True
            for dz in range(-5, 6):
                for dx in range(-5, 6):
                    if abs(dx) + abs(dz) > 5:
                        continue
                    middle = packed_at(x + dx, y, z + dz)
                    middle_id = None if middle is None else middle >> 4
                    if (
                        packed_at(x + dx, y - 1, z + dz) != (1 << 4)
                        or middle_id not in (0, 10, 11)
                        or packed_at(x + dx, y + 1, z + dz) != 0
                    ):
                        exact_plane = False
                        break
                if not exact_plane:
                    break
            above_enclosed_water = (
                packed_at(x, y - 1, z) in ((8 << 4), (9 << 4))
                and packed_at(x, y - 2, z) == (1 << 4)
                and all(
                    packed_at(x + dx, y - 1, z + dz) == (1 << 4)
                    for dx, dz in ((0, -1), (0, 1), (-1, 0), (1, 0))
                )
            )
            if not exact_plane and not above_enclosed_water:
                continue
        elif entry["block"] in (12, 13):
            falling_block = entry["block"]
            below = packed_at(x, y - 1, z)
            below_id = None if below is None else below >> 4
            fall_through = (0, 8, 9, 10, 11, 51)
            # BlockFalling.checkFallable is an exact no-op when the block
            # below is any represented non-fall-through state.  Retain these
            # callbacks too: a due entry remains visible in Java's
            # pendingTickListEntriesThisTick until the server tick returns,
            # and dropping it from the capsule loses observable queue order
            # even though it performs no world mutation.
            supported_noop = below is not None and below_id not in fall_through
            stacked_tie = below_id == falling_block and any(
                candidate["x"] == x
                and candidate["y"] == y - 1
                and candidate["z"] == z
                and candidate["block"] == falling_block
                and candidate["time"] == entry["time"]
                and candidate["priority"] == entry["priority"]
                for candidate in state["scheduled_ticks"]
            )
            if block_state != (falling_block << 4) \
                    or (below_id not in fall_through
                        and not stacked_tie and not supported_noop):
                continue
            if supported_noop:
                exact_scheduled.append(entry)
                continue
            support_found = False
            for support_y in range(y - 2, y0 - 1, -1):
                packed = packed_at(x, support_y, z)
                block_id = None if packed is None else packed >> 4
                if packed == 0 or block_id in (
                        8, 9, 10, 11, 51, 70, 72, 132, 147, 148):
                    continue
                support_found = packed in (
                    (1 << 4), (44 << 4), (44 << 4) | 8,
                    (88 << 4), (116 << 4),
                    (171 << 4), (208 << 4)) \
                    or (
                        packed in (
                            (60 << 4), (60 << 4) | 7,
                            (78 << 4), (78 << 4) | 7,
                            (92 << 4),
                        )
                        and packed_at(x, support_y - 1, z) == (1 << 4)
                    )
                break
            if not support_found:
                continue
        elif entry["block"] == 122:
            if block_state != (122 << 4):
                continue
            # BlockDragonEgg checks isAirBlock below before it considers the
            # falling path. Any represented non-air block is therefore an
            # exact supported no-op callback. Air requires the same bounded
            # landing-surface proof used by the active falling entity.
            below = packed_at(x, y - 1, z)
            if below is None:
                continue
            if below == 0:
                support_found = False
                for support_y in range(y - 2, y0 - 1, -1):
                    packed = packed_at(x, support_y, z)
                    block_id = None if packed is None else packed >> 4
                    if packed == 0 or block_id in (
                            8, 9, 10, 11, 51, 70, 72, 132, 147, 148):
                        continue
                    support_found = packed in (
                        (1 << 4), (44 << 4), (44 << 4) | 8,
                        (88 << 4), (116 << 4),
                        (171 << 4), (208 << 4)) or (
                            packed in (
                                (60 << 4), (60 << 4) | 7,
                                (78 << 4), (78 << 4) | 7,
                                (92 << 4),
                            )
                            and packed_at(
                                x, support_y - 1, z) == (1 << 4)
                        )
                    break
                if not support_found:
                    continue
        elif entry["block"] == 145:
            # A supported BlockFalling callback drains before entity creation,
            # so it needs no unavailable clock-seeded Entity.rand cursor.
            below = packed_at(x, y - 1, z)
            if block_state is None or block_state >> 4 != 145 \
                    or (block_state & 15) > 11 \
                    or below is None \
                    or below >> 4 in (0, 8, 9, 10, 11, 51):
                continue
        elif entry["block"] == 51:
            context = fire_context.get((x, y, z, 51))
            if context is None or block_state >> 4 != 51:
                continue
            if context["do_fire_tick"]:
                fire_dimension = int(state["player"]["dim"])
                fire_support = packed_at(x, y - 1, z)
                rain_probes = (
                    context.get("raining_at"),
                    context.get("raining_at_west"),
                    context.get("raining_at_east"),
                    context.get("raining_at_north"),
                    context.get("raining_at_south"),
                )
                rainy = context["raining"] or state["time"]["raining"]
                if rainy:
                    rain_direct_target = (
                        fire_support == (87 << 4)
                        and (block_state & 15) == 0
                        and packed_at(x + 1, y, z) == ((31 << 4) | 1)
                        and packed_at(x + 1, y + 1, z) == 0
                        and packed_at(x + 1, y + 2, z)
                            in (0, (1 << 4))
                    )
                    west_roofs = tuple(
                        packed_at(x + dx, y + 2, z + dz)
                        for dx, dz in (
                            (-1, 0), (-2, 0), (0, 0),
                            (-1, -1), (-1, 1),
                        )
                    )
                    rain_volume_west = (
                        fire_support == (87 << 4)
                        and (block_state & 15) == 0
                        and packed_at(x - 1, y, z) == 0
                        and packed_at(x - 2, y, z) == (171 << 4)
                        and all(
                            packed_at(x + dx, y + 1, z + dz) == 0
                            for dx, dz in (
                                (-1, 0), (-2, 0), (0, 0),
                                (-1, -1), (-1, 1),
                            )
                        )
                        and (
                            all(value == 0 for value in west_roofs)
                            or all(value == (1 << 4)
                                   for value in west_roofs)
                        )
                    )
                    rain_age15_source = (
                        fire_support == (1 << 4)
                        and (block_state & 15) == 15
                    )
                    if context["high_humidity"] \
                            or context["difficulty"] != 2 \
                            or context["raining"] is not True \
                            or state["time"]["raining"] is not True \
                            or fire_dimension != 0 \
                            or not (
                                rain_age15_source
                                or rain_direct_target
                                or rain_volume_west
                            ) \
                            or raining_fire_context_count != 1 \
                            or not any(probe is True for probe in rain_probes) \
                            or context.get("rain_time") \
                                != state["time"].get("rain_time") \
                            or context.get("thunder_time") \
                                != state["time"].get("thunder_time"):
                        continue
                    fire_allowed_ids = (
                        (0, 1, 31, 51, 87, 171)
                        if rain_direct_target or rain_volume_west
                        else (0, 1, 51)
                    )
                else:
                    fire_supports = (
                        ((1 << 4), (87 << 4), (7 << 4))
                        if fire_dimension == 1
                        else ((1 << 4), (87 << 4))
                    )
                    if context["difficulty"] != 2 \
                            or fire_dimension not in (-1, 0, 1) \
                            or fire_support not in fire_supports \
                            or (
                                context["high_humidity"]
                                and humid_fire_context_count != 1
                            ):
                        continue
                    fire_allowed_ids = (
                        (0, 1, 5, 17, 31, 35, 46, 47, 51, 87, 170, 7)
                        if fire_dimension == 1
                        else (0, 1, 5, 17, 31, 35, 46, 47, 51, 87, 170)
                    )
                supported = True
                faces = (
                    (0, -1, 0), (0, 1, 0), (0, 0, -1),
                    (0, 0, 1), (-1, 0, 0), (1, 0, 0),
                )
                for face_dx, face_dy, face_dz in faces:
                    direct = packed_at(
                        x + face_dx, y + face_dy, z + face_dz)
                    if direct is None \
                            or direct >> 4 not in fire_allowed_ids:
                        supported = False
                        break
                for dy in range(-1, 5):
                    if not supported:
                        break
                    for dz in range(-1, 2):
                        for dx in range(-1, 2):
                            if dx == 0 and dy == 0 and dz == 0:
                                continue
                            packed = packed_at(x + dx, y + dy, z + dz)
                            if packed is None:
                                supported = False
                                break
                            packed_id = packed >> 4
                            if rainy and (
                                    rain_direct_target or rain_volume_west):
                                if packed_id in (31, 171) and not (
                                        rain_direct_target
                                        and packed_id == 31
                                        and dx == 1 and dy == 0 and dz == 0):
                                    supported = False
                                    break
                                if packed_id == 87 and not (
                                        dx == 0 and dy == -1 and dz == 0):
                                    supported = False
                                    break
                                if packed_id == 51:
                                    supported = False
                                    break
                            if packed_id != 0:
                                continue
                            for face_dx, face_dy, face_dz in faces:
                                neighbor = packed_at(
                                    x + dx + face_dx,
                                    y + dy + face_dy,
                                    z + dz + face_dz,
                                )
                                if neighbor is None \
                                        or neighbor >> 4 \
                                        not in fire_allowed_ids:
                                    supported = False
                                    break
                            if not supported:
                                break
                        if not supported:
                            break
                    if not supported:
                        break
                if not supported:
                    continue
        elif entry["block"] == 131:
            normal_masks, _provider_masks, _opaque_masks = (
                blockstate_predicate_masks())
            faces = (
                (0, -1, 0), (0, 1, 0), (0, 0, -1),
                (0, 0, 1), (-1, 0, 0), (1, 0, 0),
            )
            horizontal = (
                (0, 0, 1), (-1, 0, 0),
                (0, 0, -1), (1, 0, 0),
            )

            def is_normal(packed):
                if packed is None:
                    return False
                block_id, block_meta = packed >> 4, packed & 15
                return (
                    0 <= block_id < 256
                    and bool(normal_masks[block_id] & (1 << block_meta))
                )

            due_delay = entry["time"] - state["time"]["total_time"]
            dx, _dy, dz = horizontal[block_state & 3]
            endpoint = None
            supported = 0 <= due_delay <= 10
            for distance in range(1, 42):
                scanned = packed_at(x + dx * distance, y, z + dz * distance)
                if scanned is None:
                    supported = False
                    break
                scanned_id, scanned_meta = scanned >> 4, scanned & 15
                if scanned_id == 131:
                    endpoint = (x + dx * distance, y, z + dz * distance)
                    break
                if scanned_id == 132 and scanned_meta not in (
                        0, 1, 4, 5, 8, 9, 12, 13):
                    supported = False
                    break
                if scanned_id != 132:
                    break
            if not supported:
                continue
            centers = [(x, y, z), (x - dx, y, z - dz)]
            if endpoint is not None:
                centers.extend((endpoint, (
                    endpoint[0] + dx, y, endpoint[2] + dz)))
            for center_x, center_y, center_z in centers:
                for neighbor_dx, neighbor_dy, neighbor_dz in faces:
                    neighbor = packed_at(
                        center_x + neighbor_dx,
                        center_y + neighbor_dy,
                        center_z + neighbor_dz,
                    )
                    neighbor_id = -1 if neighbor is None else neighbor >> 4
                    if neighbor_id not in (0, 123, 124, 131, 132) \
                            and not is_normal(neighbor):
                        supported = False
                        break
                if not supported:
                    break
            if not supported:
                continue
        elif entry["block"] == 132:
            due_delay = entry["time"] - state["time"]["total_time"]
            faces = (
                (0, -1, 0), (0, 1, 0), (0, 0, -1),
                (0, 0, 1), (-1, 0, 0), (1, 0, 0),
            )
            if block_state != ((132 << 4) | 1) \
                    or not 0 <= due_delay <= 10:
                continue
            if any(
                    (neighbor := packed_at(
                        x + dx, y + dy, z + dz)) is None
                    or neighbor >> 4 not in (0, 1)
                    for dx, dy, dz in faces):
                continue
        elif entry["block"] in (70, 147, 148):
            normal_masks, _provider_masks, _opaque_masks = (
                blockstate_predicate_masks())
            faces = (
                (0, -1, 0), (0, 1, 0), (0, 0, -1),
                (0, 0, 1), (-1, 0, 0), (1, 0, 0),
            )

            def is_normal(packed):
                if packed is None:
                    return False
                block_id, block_meta = packed >> 4, packed & 15
                return (
                    0 <= block_id < 256
                    and bool(normal_masks[block_id] & (1 << block_meta))
                )

            # Promote only the captured unoccupied +3 release. General
            # entities are not reconstructed by capsule v1, so anything that
            # can trigger this plate but is not represented by magma must
            # begin outside a conservative 30-block prism. The exact player
            # and exact NoAI pig are restored and queried by the callback.
            plate_id = entry["block"]
            strength = block_state & 15
            expected_strength = 1 if plate_id == 70 else strength
            due_delay = entry["time"] - state["time"]["total_time"]
            if strength != expected_strength \
                    or not 1 <= strength <= 15 \
                    or not 0 <= due_delay <= 3 \
                    or not is_normal(packed_at(x, y - 1, z)):
                continue
            supported = True
            for center_x, center_y, center_z in (
                    (x, y, z), (x, y - 1, z)):
                for neighbor_dx, neighbor_dy, neighbor_dz in faces:
                    neighbor = packed_at(
                        center_x + neighbor_dx,
                        center_y + neighbor_dy,
                        center_z + neighbor_dz,
                    )
                    neighbor_id = (
                        -1 if neighbor is None else neighbor >> 4
                    )
                    if neighbor_id not in (
                            0, 55, 70, 123, 124, 147, 148) \
                            and not is_normal(neighbor):
                        supported = False
                        break
                if not supported:
                    break
            if not supported:
                continue
            ignored_by_stone = {
                "EntityArrow", "EntityBoat", "EntityEgg",
                "EntityEnderPearl", "EntityExpBottle",
                "EntityFallingBlock", "EntityFireball",
                "EntityFireworkRocket", "EntityFishHook",
                "EntityItem", "EntityLlamaSpit", "EntityPotion",
                "EntityShulkerBullet", "EntitySmallFireball",
                "EntitySnowball", "EntitySpectralArrow",
                "EntityTNTPrimed", "EntityThrownExpBottle",
                "EntityTippedArrow", "EntityWitherSkull",
                "EntityXPOrb",
            }
            for entity in state["entities"]:
                exact_counted_pig = (
                    entity["type"] == "EntityPig"
                    and entity.get("no_ai_pig_exact") is True
                )
                if exact_counted_pig or (
                        plate_id == 70
                        and entity["type"] in ignored_by_stone):
                    continue
                if (
                    x - 30.0 < float(entity["x"]) < x + 1.0 + 30.0
                    and y - 30.0 < float(entity["y"]) < y + 0.25 + 30.0
                    and z - 30.0 < float(entity["z"]) < z + 1.0 + 30.0
                ):
                    supported = False
                    break
            if not supported:
                continue
        elif entry["block"] in (75, 76):
            normal_masks, provider_masks, fully_opaque_masks = (
                blockstate_predicate_masks())
            faces = (
                (0, -1, 0), (0, 1, 0), (0, 0, -1),
                (0, 0, 1), (-1, 0, 0), (1, 0, 0),
            )

            def is_normal(packed):
                if packed is None:
                    return False
                block_id, block_meta = packed >> 4, packed & 15
                return (
                    0 <= block_id < 256
                    and bool(normal_masks[block_id] & (1 << block_meta))
                )

            def provider_supported(packed):
                if packed is None:
                    return False
                block_id, block_meta = packed >> 4, packed & 15
                if not 0 <= block_id < 256:
                    return False
                if not provider_masks[block_id] & (1 << block_meta):
                    return True
                return block_id in (
                    55, 69, 70, 72, 75, 76, 77, 93, 94, 131,
                    143, 146, 147, 148, 149, 150, 152, 218,
                )

            stair_ids = {
                53, 67, 108, 109, 114, 128, 134, 135, 136,
                156, 163, 164, 180, 203,
            }
            rotate_y = {2: 5, 3: 4, 4: 2, 5: 3}
            rotate_y_ccw = {2: 4, 3: 5, 4: 3, 5: 2}

            def stair_state(position):
                packed = packed_at(*position)
                if packed is None or packed >> 4 not in stair_ids:
                    return None
                return 5 - (packed & 3), bool(packed & 4)

            def stair_is_different(position, direction, facing, top):
                delta = faces[direction]
                neighbor = stair_state((
                    position[0] + delta[0],
                    position[1] + delta[1],
                    position[2] + delta[2],
                ))
                return neighbor is None or neighbor != (facing, top)

            def stair_shape(position, meta):
                facing = 5 - (meta & 3)
                top = bool(meta & 4)
                delta = faces[facing]
                neighbor = stair_state((
                    position[0] + delta[0],
                    position[1] + delta[1],
                    position[2] + delta[2],
                ))
                if neighbor is not None and neighbor[1] == top:
                    neighbor_facing = neighbor[0]
                    if ((neighbor_facing < 4) != (facing < 4)) \
                            and stair_is_different(
                                position, neighbor_facing ^ 1, facing, top):
                        return (
                            "outer_left"
                            if neighbor_facing == rotate_y_ccw[facing]
                            else "outer_right"
                        )
                opposite = facing ^ 1
                delta = faces[opposite]
                neighbor = stair_state((
                    position[0] + delta[0],
                    position[1] + delta[1],
                    position[2] + delta[2],
                ))
                if neighbor is not None and neighbor[1] == top:
                    neighbor_facing = neighbor[0]
                    if ((neighbor_facing < 4) != (facing < 4)) \
                            and stair_is_different(
                                position, neighbor_facing, facing, top):
                        return (
                            "inner_left"
                            if neighbor_facing == rotate_y_ccw[facing]
                            else "inner_right"
                        )
                return "straight"

            def side_solid(packed, position, side):
                if packed is None:
                    return False
                block_id, block_meta = packed >> 4, packed & 15
                if not 0 <= block_id < 256:
                    return False
                if fully_opaque_masks[block_id] & (1 << block_meta) \
                        and side == 1:
                    return True
                if block_id in (43, 125, 181, 204):
                    return True
                if block_id in (44, 126, 182, 205):
                    return (
                        bool(block_meta & 8) and side == 1
                    ) or (not bool(block_meta & 8) and side == 0)
                if block_id == 60:
                    return side not in (0, 1)
                if block_id in stair_ids:
                    top = bool(block_meta & 4)
                    if side == 1:
                        return top
                    if side == 0:
                        return not top
                    facing = 5 - (block_meta & 3)
                    if facing == side:
                        return True
                    shape = stair_shape(position, block_meta)
                    if shape == "inner_left":
                        expected = (
                            rotate_y_ccw[facing] if top else rotate_y[facing]
                        )
                        return side == expected
                    if shape == "inner_right":
                        expected = (
                            rotate_y[facing] if top else rotate_y_ccw[facing]
                        )
                        return side == expected
                    return False
                if block_id == 78:
                    return (block_meta & 7) == 7
                if block_id == 154 and side == 1:
                    return True
                if block_id == 152:
                    return True
                return is_normal(packed)

            torch_meta = block_state & 15
            support_delta = {
                1: (-1, 0, 0),
                2: (1, 0, 0),
                3: (0, 0, -1),
                4: (0, 0, 1),
                5: (0, -1, 0),
            }.get(torch_meta)
            if support_delta is None:
                continue
            support_position = (
                x + support_delta[0],
                y + support_delta[1],
                z + support_delta[2],
            )
            support = packed_at(*support_position)
            support_id = -1 if support is None else support >> 4
            support_side = {1: 5, 2: 4, 3: 3, 4: 2, 5: 1}[torch_meta]
            floor_special = support_id in (
                20, 85, 95, 113, 139, 188, 189, 190, 191, 192,
            )
            if not (
                        side_solid(
                            support, support_position, support_side)
                        or (torch_meta == 5 and floor_special)
                    ):
                continue
            if is_normal(support) and not all(
                    provider_supported(packed_at(
                        support_position[0] + dx,
                        support_position[1] + dy,
                        support_position[2] + dz,
                    ))
                    for dx, dy, dz in faces):
                continue
            supported = True
            for center_dx, center_dy, center_dz in faces:
                center = (x + center_dx, y + center_dy, z + center_dz)
                for neighbor_dx, neighbor_dy, neighbor_dz in faces:
                    neighbor = packed_at(
                        center[0] + neighbor_dx,
                        center[1] + neighbor_dy,
                        center[2] + neighbor_dz,
                    )
                    neighbor_position = (
                        center[0] + neighbor_dx,
                        center[1] + neighbor_dy,
                        center[2] + neighbor_dz,
                    )
                    if neighbor_position == support_position:
                        continue
                    neighbor_id = -1 if neighbor is None else neighbor >> 4
                    if neighbor_id not in (
                            0, 55, 75, 76, 93, 94, 123, 124,
                            149, 150, 218) and not is_normal(neighbor):
                        supported = False
                        break
                if not supported:
                    break
            if not supported:
                continue
        elif entry["block"] in (77, 143):
            meta = block_state & 15
            base_meta = meta & 7
            normal_masks, _provider_masks, _opaque_masks = (
                blockstate_predicate_masks())
            faces = (
                (0, -1, 0), (0, 1, 0), (0, 0, -1),
                (0, 0, 1), (-1, 0, 0), (1, 0, 0),
            )
            facing = {
                0: 0, 1: 5, 2: 4, 3: 3, 4: 2, 5: 1,
            }.get(base_meta)

            def is_normal(packed):
                if packed is None:
                    return False
                block_id, block_meta = packed >> 4, packed & 15
                return (
                    0 <= block_id < 256
                    and bool(
                        normal_masks[block_id] & (1 << block_meta)
                    )
                )

            if (meta & 8) == 0 or facing is None:
                continue
            if entry["block"] == 143:
                # The wooden callback rechecks EntityArrow occupancy. Capsule
                # v1 does not reconstruct arrows, so promote only an imminent
                # saved release with no captured arrow of any 1.11.2 subtype.
                due_delay = entry["time"] - state["time"]["total_time"]
                arrow_types = {
                    "EntityArrow", "EntitySpectralArrow",
                    "EntityTippedArrow",
                }
                if not 0 <= due_delay <= 3 or any(
                        entity["type"] in arrow_types
                        for entity in state["entities"]):
                    continue
            face_dx, face_dy, face_dz = faces[facing]
            support = (x - face_dx, y - face_dy, z - face_dz)
            if not is_normal(packed_at(*support)):
                continue
            supported = True
            for center_x, center_y, center_z in ((x, y, z), support):
                for neighbor_dx, neighbor_dy, neighbor_dz in faces:
                    neighbor = packed_at(
                        center_x + neighbor_dx,
                        center_y + neighbor_dy,
                        center_z + neighbor_dz,
                    )
                    neighbor_id = (
                        -1 if neighbor is None else neighbor >> 4
                    )
                    if neighbor_id not in (0, 77, 123, 124, 143) \
                            and not is_normal(neighbor):
                        supported = False
                        break
                if not supported:
                    break
            if not supported:
                continue
        elif entry["block"] in (93, 94):
            normal_masks, _provider_masks, _opaque_masks = (
                blockstate_predicate_masks())

            def is_normal(packed):
                if packed is None:
                    return False
                block_id, block_meta = packed >> 4, packed & 15
                return (
                    0 <= block_id < 256
                    and bool(normal_masks[block_id] & (1 << block_meta))
                )

            meta = block_state & 15
            input_offset, output_offset, side_offsets = {
                0: ((0, 1), (0, -1), ((-1, 0), (1, 0))),
                1: ((-1, 0), (1, 0), ((0, -1), (0, 1))),
                2: ((0, -1), (0, 1), ((-1, 0), (1, 0))),
                3: ((1, 0), (-1, 0), ((0, -1), (0, 1))),
            }[meta & 3]
            if not is_normal(packed_at(x, y - 1, z)) \
                    or packed_at(x, y + 1, z) != 0:
                continue
            input_state = packed_at(
                x + input_offset[0], y, z + input_offset[1])
            output_state = packed_at(
                x + output_offset[0], y, z + output_offset[1])
            input_id = -1 if input_state is None else input_state >> 4
            output_id = -1 if output_state is None else output_state >> 4
            input_supported = (
                input_id in (
                    0, 55, 69, 70, 72, 75, 76, 77, 93, 94, 143,
                    147, 148, 149, 150, 152, 218,
                )
                or is_normal(input_state)
            )
            output_supported = (
                output_id in (
                    0, 55, 93, 94, 123, 124, 149, 150, 218)
                or is_normal(output_state)
            )
            sides_supported = all(
                (
                    (side := packed_at(x + dx, y, z + dz)) is not None
                    and side >> 4 in (0, 93, 94, 218)
                )
                for dx, dz in side_offsets
            )
            if not (
                input_supported and output_supported and sides_supported
            ):
                continue
        elif entry["block"] in (149, 150):
            normal_masks, _provider_masks, _opaque_masks = (
                blockstate_predicate_masks())

            def is_normal(packed):
                if packed is None:
                    return False
                block_id, block_meta = packed >> 4, packed & 15
                return (
                    0 <= block_id < 256
                    and bool(normal_masks[block_id] & (1 << block_meta))
                )

            meta = block_state & 15
            input_offset, output_offset, side_offsets = {
                0: ((0, 1), (0, -1), ((-1, 0), (1, 0))),
                1: ((-1, 0), (1, 0), ((0, -1), (0, 1))),
                2: ((0, -1), (0, 1), ((-1, 0), (1, 0))),
                3: ((1, 0), (-1, 0), ((0, -1), (0, 1))),
            }[meta & 3]
            if (x, y, z) not in comparator_state \
                    or not is_normal(packed_at(x, y - 1, z)) \
                    or packed_at(x, y + 1, z) != 0:
                continue
            input_state = packed_at(
                x + input_offset[0], y, z + input_offset[1])
            input_position = (
                x + input_offset[0], y, z + input_offset[1])
            second_input_state = packed_at(
                x + 2 * input_offset[0],
                y,
                z + 2 * input_offset[1],
            )
            second_input_position = (
                x + 2 * input_offset[0],
                y,
                z + 2 * input_offset[1],
            )
            output_state = packed_at(
                x + output_offset[0], y, z + output_offset[1])
            input_id = -1 if input_state is None else input_state >> 4
            output_id = -1 if output_state is None else output_state >> 4

            def exact_override_supported(packed, position):
                if packed is None:
                    return False
                block_id, block_meta = packed >> 4, packed & 15
                container = container_state.get(position)
                return (
                    (block_id == 92 and block_meta <= 6)
                    or (block_id == 118 and block_meta <= 3)
                    or (block_id == 120 and block_meta <= 7)
                    or (
                        block_id == 54
                        and container is not None
                        and container["type"] in (
                            "single_chest", "double_chest_half")
                    )
                    or (
                        block_id == 146
                        and container is not None
                        and container["type"] in (
                            "single_trapped_chest",
                            "double_trapped_chest_half")
                    )
                    or (
                        block_id in (61, 62)
                        and container is not None
                        and container["type"] == "furnace"
                    )
                    or (
                        block_id == 23
                        and container is not None
                        and container["type"] == "dispenser"
                    )
                    or (
                        block_id == 158
                        and container is not None
                        and container["type"] == "dropper"
                    )
                    or (
                        block_id == 84
                        and container is not None
                        and container["type"] == "jukebox"
                    )
                    or (
                        block_id == 117
                        and container is not None
                        and container["type"] == "brewing_stand"
                    )
                    or (
                        block_id == 137
                        and container is not None
                        and container["type"] == "command_block"
                    )
                    or (
                        block_id == 210
                        and container is not None
                        and container["type"]
                            == "repeating_command_block"
                    )
                    or (
                        block_id == 211
                        and container is not None
                        and container["type"] == "chain_command_block"
                    )
                )

            input_supported = (
                exact_override_supported(input_state, input_position)
                or input_id in (
                    0, 55, 69, 70, 72, 75, 76, 77, 93, 94, 143,
                    147, 148, 149, 150, 152, 218,
                )
                or (
                    is_normal(input_state)
                    and (
                        exact_override_supported(
                            second_input_state, second_input_position)
                        or (
                            second_input_state == 0
                            and (
                                frame := item_frame_state.get(
                                    second_input_position)
                            ) is not None
                            and frame["facing"] == {
                                (0, -1): 2,
                                (0, 1): 3,
                                (-1, 0): 4,
                                (1, 0): 5,
                            }[input_offset]
                        )
                    )
                )
            )
            output_supported = (
                output_id in (
                    0, 55, 93, 94, 123, 124, 149, 150,
                    218,
                )
                or is_normal(output_state)
            )
            sides_supported = all(
                (
                    (side := packed_at(x + dx, y, z + dz)) is not None
                    and side >> 4 in (0, 55, 152, 218)
                )
                for dx, dz in side_offsets
            )
            if not (
                input_supported and output_supported and sides_supported
            ):
                continue
        elif entry["block"] in (23, 158):
            source = container_state.get((x, y, z))
            facing = block_state & 7
            faces = (
                (0, -1, 0), (0, 1, 0), (0, 0, -1),
                (0, 0, 1), (-1, 0, 0), (1, 0, 0),
            )
            expected_type = (
                "dispenser" if entry["block"] == 23 else "dropper")
            if source is None or source["type"] != expected_type \
                    or facing > 5 or len(source["items"]) != 1:
                continue
            item = source["items"][0]
            face_dx, face_dy, face_dz = faces[facing]
            target_position = (
                x + face_dx, y + face_dy, z + face_dz)
            if entry["block"] == 23:
                if item["id"] != 1 or item["meta"] != 0 \
                        or packed_at(*target_position) != 0 \
                        or sum(
                            entity["type"] == "EntityItem"
                            for entity in state["entities"]
                        ) >= 48:
                    continue
            else:
                target = container_state.get(target_position)
                if target is None or target["type"] != "dispenser":
                    continue
        elif entry["block"] == 218:
            meta = block_state & 15
            facing = meta & 7
            due_delay = entry["time"] - state["time"]["total_time"]
            faces = (
                (0, -1, 0), (0, 1, 0), (0, 0, -1),
                (0, 0, 1), (-1, 0, 0), (1, 0, 0),
            )
            if facing > 5 or not 0 <= due_delay <= 2:
                continue
            face_dx, face_dy, face_dz = faces[facing]
            output = (
                x - face_dx, y - face_dy, z - face_dz)
            supported = True
            for center_x, center_y, center_z in ((x, y, z), output):
                for neighbor_dx, neighbor_dy, neighbor_dz in faces:
                    neighbor = packed_at(
                        center_x + neighbor_dx,
                        center_y + neighbor_dy,
                        center_z + neighbor_dz,
                    )
                    if neighbor is None:
                        supported = False
                        break
                    neighbor_id = neighbor >> 4
                    neighbor_meta = neighbor & 15
                    if neighbor_id not in (0, 1, 5, 123, 124, 218):
                        supported = False
                        break
                    if neighbor_id == 218 and (neighbor_meta & 7) > 5:
                        supported = False
                        break
                if not supported:
                    break
            if not supported:
                continue
        elif entry["block"] == 124:
            if block_state != (124 << 4):
                continue
            normal_masks, provider_masks, _opaque_masks = (
                blockstate_predicate_masks())
            faces = (
                (0, -1, 0), (0, 1, 0), (0, 0, -1),
                (0, 0, 1), (-1, 0, 0), (1, 0, 0),
            )

            def predicate(mask_table, packed):
                if packed is None:
                    return False
                block_id, meta = packed >> 4, packed & 15
                return (
                    0 <= block_id < 256
                    and bool(mask_table[block_id] & (1 << meta))
                )

            def provider_supported(packed):
                if packed is None:
                    return False
                block_id = packed >> 4
                if not 0 <= block_id < 256:
                    return False
                if not predicate(provider_masks, packed):
                    return True
                return block_id in (
                    55, 69, 70, 72, 75, 76, 77, 93, 94, 143,
                    149, 150, 218,
                    147, 148, 152,
                )

            supported = True
            for face_dx, face_dy, face_dz in faces:
                neighbor_x = x + face_dx
                neighbor_y = y + face_dy
                neighbor_z = z + face_dz
                direct = packed_at(neighbor_x, neighbor_y, neighbor_z)
                if not provider_supported(direct):
                    supported = False
                    break
                if not predicate(normal_masks, direct):
                    continue
                for strong_dx, strong_dy, strong_dz in faces:
                    strong = packed_at(
                        neighbor_x + strong_dx,
                        neighbor_y + strong_dy,
                        neighbor_z + strong_dz,
                    )
                    if not provider_supported(strong):
                        supported = False
                        break
                if not supported:
                    break
            if not supported:
                continue
        exact_scheduled.append(entry)
    state["scheduled_ticks"] = exact_scheduled
    state["scheduled_ticks_complete"] = True

    output_dir.mkdir(parents=True, exist_ok=True)
    payload_path = output_dir / BLOCK_FILE
    shutil.copyfile(blocks_path, payload_path)
    capabilities = copy.deepcopy(CAPABILITIES_V2)
    if sky_raw is not None:
        shutil.copyfile(sky_light_path, output_dir / SKY_LIGHT_FILE)
        capabilities["world.light.sky_nibbles"] = "exact"
    if block_light_raw is not None:
        shutil.copyfile(block_light_path, output_dir / BLOCK_LIGHT_FILE)
        capabilities["world.light.block_nibbles"] = "exact"
    nbt_payloads = []
    for index, stack in enumerate(state["inventory"]):
        nbt_raw = _validate_item_stack_payload(
            stack.get("stack_payload"),
            f"state.inventory[{index}].stack_payload")
        if nbt_raw is None:
            continue
        filename = f"item_stack_inventory_{index:04d}.nbt"
        (output_dir / filename).write_bytes(nbt_raw)
        nbt_payloads.append({
            "kind": "item_stack",
            "owner": "inventory",
            "index": index,
            "slot": stack["slot"],
            "file": filename,
            "encoding": NBT_ENCODING,
            "bytes": len(nbt_raw),
            "sha256": sha256(nbt_raw),
        })
    for index, stack in enumerate(state.get("ender_inventory", [])):
        nbt_raw = _validate_item_stack_payload(
            stack.get("stack_payload"),
            f"state.ender_inventory[{index}].stack_payload")
        if nbt_raw is None:
            continue
        filename = f"item_stack_ender_inventory_{index:04d}.nbt"
        (output_dir / filename).write_bytes(nbt_raw)
        nbt_payloads.append({
            "kind": "item_stack",
            "owner": "ender_inventory",
            "index": index,
            "slot": stack["slot"],
            "file": filename,
            "encoding": NBT_ENCODING,
            "bytes": len(nbt_raw),
            "sha256": sha256(nbt_raw),
        })
    for index, entity in enumerate(state["entities"]):
        nbt_raw = _validate_item_stack_payload(
            entity.get("stack_payload"),
            f"state.entities[{index}].stack_payload")
        if nbt_raw is None:
            continue
        filename = f"item_stack_entity_{index:04d}.nbt"
        (output_dir / filename).write_bytes(nbt_raw)
        nbt_payloads.append({
            "kind": "item_stack",
            "owner": "entity",
            "index": index,
            "eid": entity["eid"],
            "file": filename,
            "encoding": NBT_ENCODING,
            "bytes": len(nbt_raw),
            "sha256": sha256(nbt_raw),
        })
    for index, frame in enumerate(state["item_frames"]):
        nbt_raw = _validate_item_stack_payload(
            frame.get("stack_payload"),
            f"state.item_frames[{index}].stack_payload")
        if nbt_raw is None:
            continue
        filename = f"item_stack_item_frame_{index:04d}.nbt"
        (output_dir / filename).write_bytes(nbt_raw)
        nbt_payloads.append({
            "kind": "item_stack",
            "owner": "item_frame",
            "index": index,
            "eid": frame["eid"],
            "file": filename,
            "encoding": NBT_ENCODING,
            "bytes": len(nbt_raw),
            "sha256": sha256(nbt_raw),
        })
    map_payloads = []
    for index, frame in enumerate(state["item_frames"]):
        map_raw = _validate_map_colors_b64(
            frame["map_colors_b64"],
            f"state.item_frames[{index}].map_colors_b64")
        if not map_raw:
            continue
        filename = f"map_colors_item_frame_{index:04d}.u8"
        (output_dir / filename).write_bytes(map_raw)
        map_payloads.append({
            "kind": "map_colors",
            "owner": "item_frame",
            "index": index,
            "eid": frame["eid"],
            "file": filename,
            "encoding": MAP_COLORS_ENCODING,
            "bytes": len(map_raw),
            "sha256": sha256(map_raw),
        })
    for entity_index, entity in enumerate(state["entities"]):
        if entity.get("horse_exact") is not True:
            continue
        for item_index, item in enumerate(entity["horse_inventory"]):
            nbt_raw = _validate_item_stack_payload(
                item.get("stack_payload"),
                f"state.entities[{entity_index}].horse_inventory["
                f"{item_index}].stack_payload")
            if nbt_raw is None:
                continue
            filename = (
                f"item_stack_horse_{entity_index:04d}_"
                f"{item['slot']:04d}.nbt")
            (output_dir / filename).write_bytes(nbt_raw)
            nbt_payloads.append({
                "kind": "item_stack",
                "owner": "horse",
                "entity_index": entity_index,
                "item_index": item_index,
                "eid": entity["eid"],
                "slot": item["slot"],
                "file": filename,
                "encoding": NBT_ENCODING,
                "bytes": len(nbt_raw),
                "sha256": sha256(nbt_raw),
            })
    for entity_index, entity in enumerate(state["entities"]):
        if entity.get("armor_stand_exact") is not True:
            continue
        for item_index, item in enumerate(
                entity["armor_stand_equipment"]):
            nbt_raw = _validate_item_stack_payload(
                item.get("stack_payload"),
                f"state.entities[{entity_index}].armor_stand_equipment["
                f"{item_index}].stack_payload")
            if nbt_raw is None:
                continue
            filename = (
                f"item_stack_armor_stand_{entity_index:04d}_"
                f"{item['slot']:04d}.nbt")
            (output_dir / filename).write_bytes(nbt_raw)
            nbt_payloads.append({
                "kind": "item_stack",
                "owner": "armor_stand",
                "entity_index": entity_index,
                "item_index": item_index,
                "eid": entity["eid"],
                "slot": item["slot"],
                "file": filename,
                "encoding": NBT_ENCODING,
                "bytes": len(nbt_raw),
                "sha256": sha256(nbt_raw),
            })
    for entity_index, entity in enumerate(state["entities"]):
        if entity["type"] not in {
                "EntityMinecartChest", "EntityMinecartHopper"}:
            continue
        for item_index, item in enumerate(entity["items"]):
            nbt_raw = _validate_item_stack_payload(
                item.get("stack_payload"),
                f"state.entities[{entity_index}].items[{item_index}]"
                ".stack_payload")
            if nbt_raw is None:
                continue
            filename = (
                f"item_stack_minecart_{entity_index:04d}_"
                f"{item['slot']:04d}.nbt")
            (output_dir / filename).write_bytes(nbt_raw)
            nbt_payloads.append({
                "kind": "item_stack",
                "owner": "minecart",
                "entity_index": entity_index,
                "item_index": item_index,
                "eid": entity["eid"],
                "slot": item["slot"],
                "file": filename,
                "encoding": NBT_ENCODING,
                "bytes": len(nbt_raw),
                "sha256": sha256(nbt_raw),
            })
    for entity_index, entity in enumerate(state["entities"]):
        if entity["type"] != "EntityMinecartMobSpawner":
            continue
        nbt_raw = _validate_root_nbt_payload(
            entity["spawner_spawn_data_nbt"],
            f"state.entities[{entity_index}].spawner_spawn_data_nbt")
        filename = (
            f"minecart_spawner_spawn_data_{entity_index:04d}.nbt")
        (output_dir / filename).write_bytes(nbt_raw)
        nbt_payloads.append({
            "kind": "minecart_spawner_spawn_data",
            "entity_index": entity_index,
            "eid": entity["eid"],
            "file": filename,
            "encoding": NBT_ENCODING,
            "bytes": len(nbt_raw),
            "sha256": sha256(nbt_raw),
        })
        for potential_index, potential in enumerate(
                entity["spawner_potentials"]):
            nbt_raw = _validate_root_nbt_payload(
                potential["entity_nbt"],
                f"state.entities[{entity_index}].spawner_potentials["
                f"{potential_index}].entity_nbt")
            filename = (
                f"minecart_spawner_potential_{entity_index:04d}_"
                f"{potential_index:04d}.nbt")
            (output_dir / filename).write_bytes(nbt_raw)
            nbt_payloads.append({
                "kind": "minecart_spawner_potential",
                "entity_index": entity_index,
                "potential_index": potential_index,
                "eid": entity["eid"],
                "file": filename,
                "encoding": NBT_ENCODING,
                "bytes": len(nbt_raw),
                "sha256": sha256(nbt_raw),
            })
    for container_index, container in enumerate(state["containers"]):
        for item_index, item in enumerate(container["items"]):
            nbt_raw = _validate_item_stack_payload(
                item.get("stack_payload"),
                f"state.containers[{container_index}].items[{item_index}]"
                ".stack_payload")
            if nbt_raw is None:
                continue
            filename = (
                f"item_stack_container_{container_index:04d}_"
                f"{item['slot']:04d}.nbt")
            (output_dir / filename).write_bytes(nbt_raw)
            nbt_payloads.append({
                "kind": "item_stack",
                "owner": "container",
                "container_index": container_index,
                "item_index": item_index,
                "slot": item["slot"],
                "file": filename,
                "encoding": NBT_ENCODING,
                "bytes": len(nbt_raw),
                "sha256": sha256(nbt_raw),
            })
    for index, skull in enumerate(state["skulls"]):
        if not skull["has_owner"]:
            continue
        nbt_raw = _validate_game_profile_nbt(
            skull["owner_nbt"],
            f"state.skulls[{index}].owner_nbt",
        )
        filename = f"skull_owner_{index:04d}.nbt"
        (output_dir / filename).write_bytes(nbt_raw)
        nbt_payloads.append({
            "kind": "skull_owner",
            "index": index,
            "file": filename,
            "encoding": NBT_ENCODING,
            "bytes": len(nbt_raw),
            "sha256": sha256(nbt_raw),
        })
    for index, tile in enumerate(state["decorative_tiles"]):
        tile_raw = _validate_root_nbt_payload(
            tile["tile_nbt"],
            f"state.decorative_tiles[{index}].tile_nbt")
        filename = f"decorative_tile_{index:04d}.nbt"
        (output_dir / filename).write_bytes(tile_raw)
        nbt_payloads.append({
            "kind": "decorative_tile", "index": index,
            "file": filename, "encoding": NBT_ENCODING,
            "bytes": len(tile_raw), "sha256": sha256(tile_raw),
        })
        if "drop_nbt" in tile:
            drop_raw = _validate_root_nbt_payload(
                tile["drop_nbt"],
                f"state.decorative_tiles[{index}].drop_nbt")
            filename = f"decorative_drop_{index:04d}.nbt"
            (output_dir / filename).write_bytes(drop_raw)
            nbt_payloads.append({
                "kind": "decorative_drop", "index": index,
                "file": filename, "encoding": NBT_ENCODING,
                "bytes": len(drop_raw), "sha256": sha256(drop_raw),
            })
    for spawner_index, spawner in enumerate(state.get("spawners", [])):
        nbt_raw = _validate_root_nbt_payload(
            spawner["spawn_data_nbt"],
            f"state.spawners[{spawner_index}].spawn_data_nbt")
        filename = f"spawner_spawn_data_{spawner_index:04d}.nbt"
        (output_dir / filename).write_bytes(nbt_raw)
        nbt_payloads.append({
            "kind": "spawner_spawn_data",
            "spawner_index": spawner_index,
            "file": filename,
            "encoding": NBT_ENCODING,
            "bytes": len(nbt_raw),
            "sha256": sha256(nbt_raw),
        })
        for potential_index, potential in enumerate(spawner["potentials"]):
            nbt_raw = _validate_root_nbt_payload(
                potential["entity_nbt"],
                f"state.spawners[{spawner_index}].potentials["
                f"{potential_index}].entity_nbt")
            filename = (
                f"spawner_potential_{spawner_index:04d}_"
                f"{potential_index:04d}.nbt")
            (output_dir / filename).write_bytes(nbt_raw)
            nbt_payloads.append({
                "kind": "spawner_potential",
                "spawner_index": spawner_index,
                "potential_index": potential_index,
                "file": filename,
                "encoding": NBT_ENCODING,
                "bytes": len(nbt_raw),
                "sha256": sha256(nbt_raw),
            })
    for index, container in enumerate(state["containers"]):
        if container["type"] != "shulker_box":
            continue
        nbt_raw = _validate_shulker_item_tag_nbt(
            container["item_tag_nbt"],
            f"state.containers[{index}].item_tag_nbt", container)
        filename = f"shulker_item_tag_{index:04d}.nbt"
        (output_dir / filename).write_bytes(nbt_raw)
        nbt_payloads.append({
            "kind": "shulker_item_tag",
            "index": index,
            "file": filename,
            "encoding": NBT_ENCODING,
            "bytes": len(nbt_raw),
            "sha256": sha256(nbt_raw),
        })
    manifest = {
        "schema": SCHEMA,
        "version": VERSION,
        "phase": "pre_tick",
        "source": {
            "engine": source_engine,
            "version": source_version,
            "seed": seed,
        },
        "state": state,
        "blocks": {
            "file": BLOCK_FILE,
            "encoding": BLOCK_ENCODING,
            "box": box,
            "cells": cells,
            "bytes": len(raw),
            "sha256": sha256(raw),
        },
        "nbt_payloads": nbt_payloads,
        "map_payloads": map_payloads,
        "capabilities": capabilities,
    }
    if sky_raw is not None:
        manifest["sky_light"] = {
            "file": SKY_LIGHT_FILE,
            "encoding": SKY_LIGHT_ENCODING,
            "box": box,
            "cells": cells,
            "bytes": len(sky_raw),
            "sha256": sha256(sky_raw),
        }
    if block_light_raw is not None:
        manifest["block_light"] = {
            "file": BLOCK_LIGHT_FILE,
            "encoding": BLOCK_LIGHT_ENCODING,
            "box": box,
            "cells": cells,
            "bytes": len(block_light_raw),
            "sha256": sha256(block_light_raw),
        }
    manifest_path = output_dir / MANIFEST_FILE
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    validate_capsule(output_dir)
    return manifest_path


def validate_capsule(
    capsule_dir: pathlib.Path, *, require_complete: bool = False
) -> tuple[dict, bytes]:
    manifest_path = capsule_dir / MANIFEST_FILE
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CapsuleError(f"{manifest_path}: {exc}") from exc
    if not isinstance(manifest, dict):
        raise CapsuleError("manifest root must be an object")
    if manifest.get("schema") != SCHEMA or manifest.get("version") != VERSION:
        raise CapsuleError(
            f"unsupported capsule schema/version: "
            f"{manifest.get('schema')!r}/{manifest.get('version')!r}"
        )
    if manifest.get("phase") != "pre_tick":
        raise CapsuleError("v1 capsules must describe a pre_tick boundary")
    source = manifest.get("source")
    if not isinstance(source, dict) or not isinstance(source.get("seed"), int):
        raise CapsuleError("manifest.source.seed must be an integer")
    _validate_state(manifest.get("state"))

    blocks = manifest.get("blocks")
    if not isinstance(blocks, dict):
        raise CapsuleError("manifest.blocks must be an object")
    if blocks.get("file") != BLOCK_FILE or blocks.get("encoding") != BLOCK_ENCODING:
        raise CapsuleError("unsupported block payload name or encoding")
    box = blocks.get("box")
    cells = cell_count(box)
    if blocks.get("cells") != cells or blocks.get("bytes") != cells * 2:
        raise CapsuleError("block payload dimensions do not match cells/bytes")
    raw = (capsule_dir / BLOCK_FILE).read_bytes()
    if len(raw) != cells * 2:
        raise CapsuleError(
            f"{BLOCK_FILE}: expected {cells * 2} bytes, got {len(raw)}"
        )
    digest = sha256(raw)
    if blocks.get("sha256") != digest:
        raise CapsuleError(
            f"{BLOCK_FILE}: sha256 mismatch "
            f"(manifest {blocks.get('sha256')}, actual {digest})"
        )

    nbt_payloads = manifest.get("nbt_payloads")
    if not isinstance(nbt_payloads, list):
        raise CapsuleError("manifest.nbt_payloads must be an array")
    expected_nbt_payloads = []
    for index, stack in enumerate(manifest["state"]["inventory"]):
        nbt_raw = _validate_item_stack_payload(
            stack.get("stack_payload"),
            f"state.inventory[{index}].stack_payload")
        if nbt_raw is None:
            continue
        expected_nbt_payloads.append((
            {"kind": "item_stack", "owner": "inventory",
             "index": index, "slot": stack["slot"]},
            f"item_stack_inventory_{index:04d}.nbt", nbt_raw,
        ))
    for index, stack in enumerate(
            manifest["state"].get("ender_inventory", [])):
        nbt_raw = _validate_item_stack_payload(
            stack.get("stack_payload"),
            f"state.ender_inventory[{index}].stack_payload")
        if nbt_raw is None:
            continue
        expected_nbt_payloads.append((
            {"kind": "item_stack", "owner": "ender_inventory",
             "index": index, "slot": stack["slot"]},
            f"item_stack_ender_inventory_{index:04d}.nbt", nbt_raw,
        ))
    for index, entity in enumerate(manifest["state"]["entities"]):
        nbt_raw = _validate_item_stack_payload(
            entity.get("stack_payload"),
            f"state.entities[{index}].stack_payload")
        if nbt_raw is None:
            continue
        expected_nbt_payloads.append((
            {"kind": "item_stack", "owner": "entity",
             "index": index, "eid": entity["eid"]},
            f"item_stack_entity_{index:04d}.nbt", nbt_raw,
        ))
    for index, frame in enumerate(manifest["state"]["item_frames"]):
        nbt_raw = _validate_item_stack_payload(
            frame.get("stack_payload"),
            f"state.item_frames[{index}].stack_payload")
        if nbt_raw is None:
            continue
        expected_nbt_payloads.append((
            {"kind": "item_stack", "owner": "item_frame",
             "index": index, "eid": frame["eid"]},
            f"item_stack_item_frame_{index:04d}.nbt", nbt_raw,
        ))
    for entity_index, entity in enumerate(manifest["state"]["entities"]):
        if entity.get("horse_exact") is not True:
            continue
        for item_index, item in enumerate(entity["horse_inventory"]):
            nbt_raw = _validate_item_stack_payload(
                item.get("stack_payload"),
                f"state.entities[{entity_index}].horse_inventory["
                f"{item_index}].stack_payload")
            if nbt_raw is None:
                continue
            expected_nbt_payloads.append((
                {"kind": "item_stack", "owner": "horse",
                 "entity_index": entity_index, "item_index": item_index,
                 "eid": entity["eid"], "slot": item["slot"]},
                f"item_stack_horse_{entity_index:04d}_"
                f"{item['slot']:04d}.nbt",
                nbt_raw,
            ))
    for entity_index, entity in enumerate(manifest["state"]["entities"]):
        if entity.get("armor_stand_exact") is not True:
            continue
        for item_index, item in enumerate(
                entity["armor_stand_equipment"]):
            nbt_raw = _validate_item_stack_payload(
                item.get("stack_payload"),
                f"state.entities[{entity_index}].armor_stand_equipment["
                f"{item_index}].stack_payload")
            if nbt_raw is None:
                continue
            expected_nbt_payloads.append((
                {"kind": "item_stack", "owner": "armor_stand",
                 "entity_index": entity_index, "item_index": item_index,
                 "eid": entity["eid"], "slot": item["slot"]},
                f"item_stack_armor_stand_{entity_index:04d}_"
                f"{item['slot']:04d}.nbt",
                nbt_raw,
            ))
    for entity_index, entity in enumerate(manifest["state"]["entities"]):
        if entity["type"] not in {
                "EntityMinecartChest", "EntityMinecartHopper"}:
            continue
        for item_index, item in enumerate(entity["items"]):
            nbt_raw = _validate_item_stack_payload(
                item.get("stack_payload"),
                f"state.entities[{entity_index}].items[{item_index}]"
                ".stack_payload")
            if nbt_raw is None:
                continue
            expected_nbt_payloads.append((
                {"kind": "item_stack", "owner": "minecart",
                 "entity_index": entity_index, "item_index": item_index,
                 "eid": entity["eid"], "slot": item["slot"]},
                f"item_stack_minecart_{entity_index:04d}_"
                f"{item['slot']:04d}.nbt",
                nbt_raw,
            ))
    for entity_index, entity in enumerate(manifest["state"]["entities"]):
        if entity["type"] != "EntityMinecartMobSpawner":
            continue
        expected_nbt_payloads.append((
            {"kind": "minecart_spawner_spawn_data",
             "entity_index": entity_index, "eid": entity["eid"]},
            f"minecart_spawner_spawn_data_{entity_index:04d}.nbt",
            _validate_root_nbt_payload(
                entity["spawner_spawn_data_nbt"],
                f"state.entities[{entity_index}]"
                ".spawner_spawn_data_nbt"),
        ))
        for potential_index, potential in enumerate(
                entity["spawner_potentials"]):
            expected_nbt_payloads.append((
                {"kind": "minecart_spawner_potential",
                 "entity_index": entity_index,
                 "potential_index": potential_index,
                 "eid": entity["eid"]},
                f"minecart_spawner_potential_{entity_index:04d}_"
                f"{potential_index:04d}.nbt",
                _validate_root_nbt_payload(
                    potential["entity_nbt"],
                    f"state.entities[{entity_index}].spawner_potentials["
                    f"{potential_index}].entity_nbt"),
            ))
    for container_index, container in enumerate(
            manifest["state"]["containers"]):
        for item_index, item in enumerate(container["items"]):
            nbt_raw = _validate_item_stack_payload(
                item.get("stack_payload"),
                f"state.containers[{container_index}].items[{item_index}]"
                ".stack_payload")
            if nbt_raw is None:
                continue
            expected_nbt_payloads.append((
                {"kind": "item_stack", "owner": "container",
                 "container_index": container_index,
                 "item_index": item_index, "slot": item["slot"]},
                f"item_stack_container_{container_index:04d}_"
                f"{item['slot']:04d}.nbt",
                nbt_raw,
            ))
    for index, skull in enumerate(manifest["state"]["skulls"]):
        if not skull["has_owner"]:
            continue
        expected_nbt_payloads.append((
            {"kind": "skull_owner", "index": index},
            f"skull_owner_{index:04d}.nbt",
            _validate_game_profile_nbt(
                skull["owner_nbt"],
                f"state.skulls[{index}].owner_nbt"),
        ))
    for index, tile in enumerate(manifest["state"]["decorative_tiles"]):
        expected_nbt_payloads.append((
            {"kind": "decorative_tile", "index": index},
            f"decorative_tile_{index:04d}.nbt",
            _validate_root_nbt_payload(
                tile["tile_nbt"],
                f"state.decorative_tiles[{index}].tile_nbt"),
        ))
        if "drop_nbt" in tile:
            expected_nbt_payloads.append((
                {"kind": "decorative_drop", "index": index},
                f"decorative_drop_{index:04d}.nbt",
                _validate_root_nbt_payload(
                    tile["drop_nbt"],
                    f"state.decorative_tiles[{index}].drop_nbt"),
            ))
    for spawner_index, spawner in enumerate(
            manifest["state"].get("spawners", [])):
        expected_nbt_payloads.append((
            {"kind": "spawner_spawn_data",
             "spawner_index": spawner_index},
            f"spawner_spawn_data_{spawner_index:04d}.nbt",
            _validate_root_nbt_payload(
                spawner["spawn_data_nbt"],
                f"state.spawners[{spawner_index}].spawn_data_nbt"),
        ))
        for potential_index, potential in enumerate(spawner["potentials"]):
            expected_nbt_payloads.append((
                {"kind": "spawner_potential",
                 "spawner_index": spawner_index,
                 "potential_index": potential_index},
                f"spawner_potential_{spawner_index:04d}_"
                f"{potential_index:04d}.nbt",
                _validate_root_nbt_payload(
                    potential["entity_nbt"],
                    f"state.spawners[{spawner_index}].potentials["
                    f"{potential_index}].entity_nbt"),
            ))
    for index, container in enumerate(manifest["state"]["containers"]):
        if container["type"] != "shulker_box":
            continue
        expected_nbt_payloads.append((
            {"kind": "shulker_item_tag", "index": index},
            f"shulker_item_tag_{index:04d}.nbt",
            _validate_shulker_item_tag_nbt(
                container["item_tag_nbt"],
                f"state.containers[{index}].item_tag_nbt", container),
        ))
    if len(nbt_payloads) != len(expected_nbt_payloads):
        raise CapsuleError(
            "manifest.nbt_payloads does not cover every NBT-backed state")
    total_nbt_bytes = 0
    for payload_index, (payload, expected) in enumerate(
            zip(nbt_payloads, expected_nbt_payloads)):
        label = f"manifest.nbt_payloads[{payload_index}]"
        identity, filename, canonical_raw = expected
        common = {"file", "encoding", "bytes", "sha256"}
        if not isinstance(payload, dict) \
                or set(payload) != set(identity) | common:
            raise CapsuleError(f"{label} has an incomplete payload schema")
        if any(payload.get(key) != value for key, value in identity.items()) \
                or payload.get("file") != filename \
                or payload.get("encoding") != NBT_ENCODING:
            raise CapsuleError(f"{label} has an invalid identity or encoding")
        nbt_raw = (capsule_dir / filename).read_bytes()
        total_nbt_bytes += len(nbt_raw)
        if total_nbt_bytes > MAX_NBT_PAYLOAD_TOTAL:
            raise CapsuleError("capsule NBT payloads exceed 16 MiB")
        if payload.get("bytes") != len(nbt_raw) \
                or payload.get("sha256") != sha256(nbt_raw):
            raise CapsuleError(f"{filename}: length or sha256 mismatch")
        if nbt_raw != canonical_raw:
            raise CapsuleError(
                f"{filename}: payload differs from canonical state")

    map_payloads = manifest.get("map_payloads")
    if not isinstance(map_payloads, list):
        raise CapsuleError("manifest.map_payloads must be an array")
    expected_map_payloads = []
    for index, frame in enumerate(manifest["state"]["item_frames"]):
        map_raw = _validate_map_colors_b64(
            frame["map_colors_b64"],
            f"state.item_frames[{index}].map_colors_b64")
        if not map_raw:
            continue
        expected_map_payloads.append((
            {"kind": "map_colors", "owner": "item_frame",
             "index": index, "eid": frame["eid"]},
            f"map_colors_item_frame_{index:04d}.u8", map_raw,
        ))
    if len(map_payloads) != len(expected_map_payloads):
        raise CapsuleError(
            "manifest.map_payloads does not cover every filled map")
    for payload_index, (payload, expected) in enumerate(
            zip(map_payloads, expected_map_payloads)):
        label = f"manifest.map_payloads[{payload_index}]"
        identity, filename, canonical_raw = expected
        common = {"file", "encoding", "bytes", "sha256"}
        if not isinstance(payload, dict) \
                or set(payload) != set(identity) | common:
            raise CapsuleError(f"{label} has an incomplete payload schema")
        if any(payload.get(key) != value for key, value in identity.items()) \
                or payload.get("file") != filename \
                or payload.get("encoding") != MAP_COLORS_ENCODING:
            raise CapsuleError(f"{label} has an invalid identity or encoding")
        map_raw = (capsule_dir / filename).read_bytes()
        if payload.get("bytes") != len(map_raw) \
                or payload.get("sha256") != sha256(map_raw):
            raise CapsuleError(f"{filename}: length or sha256 mismatch")
        if map_raw != canonical_raw:
            raise CapsuleError(
                f"{filename}: payload differs from canonical state")

    capabilities = manifest.get("capabilities")
    expected_capabilities = copy.deepcopy(CAPABILITIES_V2)
    sky_light = manifest.get("sky_light")
    block_light = manifest.get("block_light")
    if sky_light is not None:
        expected_capabilities["world.light.sky_nibbles"] = "exact"
    if block_light is not None:
        expected_capabilities["world.light.block_nibbles"] = "exact"
    if capabilities != expected_capabilities:
        raise CapsuleError("v1 capability ledger is missing or has been altered")
    if sky_light is not None:
        if not isinstance(sky_light, dict):
            raise CapsuleError("manifest.sky_light must be an object")
        if (
            sky_light.get("file") != SKY_LIGHT_FILE
            or sky_light.get("encoding") != SKY_LIGHT_ENCODING
            or sky_light.get("box") != box
        ):
            raise CapsuleError(
                "skylight payload must use the block box and supported encoding"
            )
        if (
            sky_light.get("cells") != cells
            or sky_light.get("bytes") != cells
        ):
            raise CapsuleError(
                "skylight payload dimensions do not match cells/bytes"
            )
        sky_raw = (capsule_dir / SKY_LIGHT_FILE).read_bytes()
        if len(sky_raw) != cells:
            raise CapsuleError(
                f"{SKY_LIGHT_FILE}: expected {cells} bytes, got {len(sky_raw)}"
            )
        digest = sha256(sky_raw)
        if sky_light.get("sha256") != digest:
            raise CapsuleError(
                f"{SKY_LIGHT_FILE}: sha256 mismatch "
                f"(manifest {sky_light.get('sha256')}, actual {digest})"
            )
        invalid = next(
            (index for index, value in enumerate(sky_raw) if value > 15), None
        )
        if invalid is not None:
            raise CapsuleError(
                f"{SKY_LIGHT_FILE}: invalid skylight value {sky_raw[invalid]} "
                f"at {coordinate(invalid, box)}"
            )
    if block_light is not None:
        if not isinstance(block_light, dict):
            raise CapsuleError("manifest.block_light must be an object")
        if (
            block_light.get("file") != BLOCK_LIGHT_FILE
            or block_light.get("encoding") != BLOCK_LIGHT_ENCODING
            or block_light.get("box") != box
        ):
            raise CapsuleError(
                "block-light payload must use the block box and supported "
                "encoding"
            )
        if (
            block_light.get("cells") != cells
            or block_light.get("bytes") != cells
        ):
            raise CapsuleError(
                "block-light payload dimensions do not match cells/bytes"
            )
        block_light_raw = (capsule_dir / BLOCK_LIGHT_FILE).read_bytes()
        if len(block_light_raw) != cells:
            raise CapsuleError(
                f"{BLOCK_LIGHT_FILE}: expected {cells} bytes, got "
                f"{len(block_light_raw)}"
            )
        digest = sha256(block_light_raw)
        if block_light.get("sha256") != digest:
            raise CapsuleError(
                f"{BLOCK_LIGHT_FILE}: sha256 mismatch "
                f"(manifest {block_light.get('sha256')}, actual {digest})"
            )
        invalid = next(
            (index for index, value in enumerate(block_light_raw)
             if value > 15), None
        )
        if invalid is not None:
            raise CapsuleError(
                f"{BLOCK_LIGHT_FILE}: invalid block-light value "
                f"{block_light_raw[invalid]} at {coordinate(invalid, box)}"
            )
    if require_complete:
        incomplete = sorted(
            key for key, status in capabilities.items() if status != "exact"
        )
        if incomplete:
            raise CapsuleError(
                "capsule is not a complete save state; non-exact capabilities: "
                + ", ".join(incomplete)
            )
    return manifest, raw


def _magma_potion_payload_event(
        entity: dict, *, cloud: bool, nbt_file: str | None = None) -> dict:
    event = {
        "tick": 0,
        "type": "set_potion_payload_fixture",
        "eid": entity["eid"],
        "cloud": int(cloud),
        "color": entity["potion_color"],
        "custom_color": int(bool(entity["potion_custom_color"])),
        "effect_count": len(entity["potion_effects"]),
    }
    for effect_index in range(16):
        effect = entity["potion_effects"][effect_index] \
            if effect_index < len(entity["potion_effects"]) \
            else {"id": 0, "amp": 0, "dur": 0, "flags": 0}
        event[f"e{effect_index}_id"] = effect["id"]
        event[f"e{effect_index}_amp"] = effect["amp"]
        event[f"e{effect_index}_dur"] = effect["dur"]
        event[f"e{effect_index}_flags"] = effect["flags"]
    if nbt_file is not None:
        event["nbt_file"] = nbt_file
    return event


def magma_events(capsule_dir: pathlib.Path) -> list[dict]:
    """Translate all v1-exact fields into strict tick-zero GmRuntime events."""
    manifest, raw = validate_capsule(capsule_dir)
    state = manifest["state"]
    player = state["player"]
    time = state["time"]
    dimension = int(player["dim"])
    box = manifest["blocks"]["box"]
    x0, _y0, z0, x1, _y1, z1 = box
    cx0, cx1 = math.floor(x0 / 16), math.floor(x1 / 16)
    cz0, cz1 = math.floor(z0 / 16), math.floor(z1 / 16)
    center_cx = (cx0 + cx1) // 2
    center_cz = (cz0 + cz1) // 2
    radius = max(center_cx - cx0, cx1 - center_cx,
                 center_cz - cz0, cz1 - center_cz)
    if radius > 32:
        raise CapsuleError(
            f"block cuboid spans radius {radius}; GmRuntime snapshot limit is 32"
        )
    fire_contexts = state.get("scheduled_tick_context", [])
    do_fire_tick = int(
        fire_contexts[0]["do_fire_tick"] if fire_contexts else True
    )
    do_entity_drops = int(state.get("do_entity_drops", True))
    do_mob_spawning = state.get("do_mob_spawning", True)
    do_mob_loot = state.get("do_mob_loot", True)
    random_tick_speed = int(state.get("random_tick_speed", 3))
    world_spawn = state.get("world_spawn", {"x": 0, "y": 64, "z": 0})
    inventory_stack_payloads = {
        payload["slot"]: payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "item_stack"
        and payload["owner"] == "inventory"
    }
    ender_inventory_stack_payloads = {
        payload["slot"]: payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "item_stack"
        and payload["owner"] == "ender_inventory"
    }
    entity_stack_payloads = {
        payload["eid"]: payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "item_stack"
        and payload["owner"] == "entity"
    }
    item_frame_stack_payloads = {
        payload["eid"]: payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "item_stack"
        and payload["owner"] == "item_frame"
    }
    item_frame_map_payloads = {
        payload["eid"]: payload["file"]
        for payload in manifest["map_payloads"]
        if payload["kind"] == "map_colors"
        and payload["owner"] == "item_frame"
    }
    minecart_stack_payloads = {
        (payload["eid"], payload["slot"]): payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "item_stack"
        and payload["owner"] == "minecart"
    }
    minecart_spawner_spawn_data_payloads = {
        payload["eid"]: payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "minecart_spawner_spawn_data"
    }
    minecart_spawner_potential_payloads = {
        (payload["eid"], payload["potential_index"]): payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "minecart_spawner_potential"
    }
    horse_stack_payloads = {
        (payload["eid"], payload["slot"]): payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "item_stack"
        and payload["owner"] == "horse"
    }
    armor_stand_stack_payloads = {
        (payload["eid"], payload["slot"]): payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "item_stack"
        and payload["owner"] == "armor_stand"
    }
    spawner_spawn_data_payloads = {
        payload["spawner_index"]: payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "spawner_spawn_data"
    }
    spawner_potential_payloads = {
        (payload["spawner_index"], payload["potential_index"]):
            payload["file"]
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "spawner_potential"
    }

    events = [
        {"tick": 0, "type": "set_dimension", "dimension": dimension},
        {
            "tick": 0,
            "type": "restore_default_game_mode",
            "value": int(state.get("default_game_mode", 0)),
        },
        {
            "tick": 0,
            "type": "restore_difficulty",
            "value": int(state.get("difficulty", 2)),
        },
        {
            "tick": 0,
            "type": "restore_world_border",
            "center_x": float(state.get("world_border", {}).get(
                "center_x", 0.0)),
            "center_z": float(state.get("world_border", {}).get(
                "center_z", 0.0)),
            "diameter": float(state.get("world_border", {}).get(
                "diameter", 60000000.0)),
            "target_diameter": float(state.get("world_border", {}).get(
                "target_diameter", 60000000.0)),
            "time_until_target": int(state.get("world_border", {}).get(
                "time_until_target", 0)),
            "damage_amount": float(state.get("world_border", {}).get(
                "damage_amount", 0.2)),
            "damage_buffer": float(state.get("world_border", {}).get(
                "damage_buffer", 5.0)),
            "warning_time": int(state.get("world_border", {}).get(
                "warning_time", 15)),
            "warning_distance": int(state.get("world_border", {}).get(
                "warning_distance", 5)),
        },
        {
            "tick": 0,
            "type": "set_player_entity_id",
            "value": int(player.get("eid", 1)),
        },
        {
            "tick": 0,
            "type": "restore_player_uuid",
            "most_hi": int(player.get(
                "uuid_most_hex", "a01e3843e5213998"), 16) >> 32,
            "most_lo": int(player.get(
                "uuid_most_hex", "a01e3843e5213998"), 16) & 0xFFFFFFFF,
            "least_hi": int(player.get(
                "uuid_least_hex", "958af459800e4d11"), 16) >> 32,
            "least_lo": int(player.get(
                "uuid_least_hex", "958af459800e4d11"), 16) & 0xFFFFFFFF,
        },
        {
            "tick": 0,
            "type": "restore_player_name",
            "name": player.get("name", "Player"),
        },
        {
            "tick": 0,
            "type": "restore_player_spawn",
            "present": int(bool(player.get("spawn_present", False))),
            "x": int(player.get("spawn_x", 0)),
            "y": int(player.get("spawn_y", 0)),
            "z": int(player.get("spawn_z", 0)),
            "forced": int(bool(player.get("spawn_forced", False))),
        },
        {
            "tick": 0,
            "type": "restore_trigger_qrl_score",
            "present": int(bool(player.get("trigger_qrl_present", False))),
            "score": int(player.get("trigger_qrl_score", 0)),
            "locked": int(bool(player.get("trigger_qrl_locked", False))),
        },
        {
            "tick": 0,
            "type": "restore_achievement_open_inventory",
            "value": int(bool(player.get("achievement_open_inventory", False))),
        },
        {
            "tick": 0,
            "type": "restore_player_game_mode",
            "value": int(player.get(
                "game_mode", 1 if player.get("creative", False) else 0)),
        },
        {
            "tick": 0,
            "type": "set_do_fire_tick",
            "enabled": do_fire_tick,
        },
        {
            "tick": 0,
            "type": "set_do_entity_drops",
            "enabled": do_entity_drops,
        },
        {
            "tick": 0,
            "type": "set_gamerules",
            "doMobSpawning": "true" if do_mob_spawning else "false",
            "doMobLoot": "true" if do_mob_loot else "false",
        },
        {
            "tick": 0,
            "type": "set_random_tick_speed",
            "value": random_tick_speed,
        },
        {
            "tick": 0,
            "type": "set_world_spawn",
            "x": int(world_spawn["x"]),
            "y": int(world_spawn["y"]),
            "z": int(world_spawn["z"]),
        },
        {"tick": 0, "type": "set_time", "value": int(time["world_time"])},
        {"tick": 0, "type": "set_total_time", "value": int(time["total_time"])},
        {
            "tick": 0,
            "type": "set_daylight_cycle",
            "enabled": int(time["do_daylight_cycle"]),
        },
        {
            "tick": 0,
            "type": "set_weather",
            "raining": int(time["raining"]),
            "thundering": int(time["thundering"]),
            "rain_time": int(time["rain_time"]),
            "thunder_time": int(time["thunder_time"]),
            "clean_weather_time": int(time["clean_weather_time"]),
            "weather_cycle": int(time["do_weather_cycle"]),
            "prev_rain_strength": time["prev_rain_strength"],
            "rain_strength": time["rain_strength"],
            "prev_thunder_strength": time["prev_thunder_strength"],
            "thunder_strength": time["thunder_strength"],
        },
        {
            "tick": 0,
            "type": "set_world_random_seed",
            "value": int(state["world_rng"]["java_seed48"]),
        },
        {
            "tick": 0,
            "type": "set_world_random_gaussian",
            "have_next": int(state["world_rng"].get(
                "java_have_gaussian", False)),
            "next": float(state["world_rng"].get("java_gaussian", 0.0)),
        },
        {
            "tick": 0,
            "type": "set_math_random_seed",
            "value": int(state["world_rng"]["math_seed48"]),
        },
        *([{
            "tick": 0,
            "type": "set_collections_random_seed",
            "value": int(state["world_rng"]["collections_seed48"]),
        }] if "collections_seed48" in state["world_rng"] else []),
        *([{
            "tick": 0,
            "type": "set_server_uuid_random_seed",
            "value": int(state["world_rng"]["server_uuid_seed48"]),
        }] if "server_uuid_seed48" in state["world_rng"] else []),
        *([{
            "tick": 0,
            "type": "set_entity_seed_generator_seed",
            "value": int(state["world_rng"][
                "entity_seed_generator_seed48"]),
        }] if "entity_seed_generator_seed48" in state["world_rng"] else []),
        {
            "tick": 0,
            "type": "set_block_random_seed",
            "value": int(state["world_rng"]["block_seed48"]),
        },
        *([{
            "tick": 0,
            "type": "set_inventory_helper_random",
            "value": int(state["world_rng"][
                "inventory_helper_seed48"]),
            "have_next": int(state["world_rng"][
                "inventory_helper_have_gaussian"]),
            "next": float(state["world_rng"][
                "inventory_helper_gaussian"]),
        }] if "inventory_helper_seed48" in state["world_rng"] else []),
        {
            "tick": 0,
            "type": "set_world_update_lcg",
            "value": int(state["world_rng"]["update_lcg"]),
        },
        {
            "tick": 0,
            "type": "snapshot_region",
            "dim": dimension,
            "cx": center_cx,
            "cz": center_cz,
            "radius": radius,
        },
    ]
    for effect in state.get("weather_effects", []):
        events.append({
            "tick": 0,
            "type": "restore_lightning",
            "dimension": dimension,
            **effect,
            "effect_only": int(effect["effect_only"]),
        })
    if state.get("ticking_chunks_complete") is True:
        ticking_chunks = state["ticking_chunks"]
        events.append({
            "tick": 0,
            "type": "ticking_chunks_begin",
            "count": len(ticking_chunks),
        })
        for chunk in ticking_chunks:
            events.append({
                "tick": 0,
                "type": "ticking_chunk_set",
                "order": chunk["order"],
                "x": chunk["x"],
                "z": chunk["z"],
                "random_tick_mask": chunk["random_tick_mask"],
            })
        events.append({"tick": 0, "type": "ticking_chunks_finalize"})
    events.append({
        "tick": 0,
        "type": "restore_village_collection",
        "collection_tick": state["village_collection_tick"],
        "count": len(state["villages"]),
    })
    for village_index, village in enumerate(state["villages"]):
        events.append({
            "tick": 0,
            "type": "restore_village_state",
            "index": village_index,
            **{
                field: village[field]
                for field in (
                    "population", "radius", "golems", "stable",
                    "state_tick", "mating_tick", "center_x", "center_y",
                    "center_z", "helper_x", "helper_y", "helper_z",
                )
            },
        })
        for door in village["doors"]:
            events.append({
                "tick": 0,
                "type": "restore_village_door",
                "index": village_index,
                **door,
            })
        for reputation in village["reputations"]:
            most = int(reputation["uuid_most_hex"], 16)
            least = int(reputation["uuid_least_hex"], 16)
            events.append({
                "tick": 0,
                "type": "restore_village_reputation",
                "index": village_index,
                "most_hi": most >> 32,
                "most_lo": most & 0xFFFFFFFF,
                "least_hi": least >> 32,
                "least_lo": least & 0xFFFFFFFF,
                "score": reputation["score"],
            })
    exact_fire_positions = {
        (entry["x"], entry["y"], entry["z"], entry["block"])
        for entry in state["scheduled_ticks"] if entry["block"] == 51
    }
    exact_rain_contexts = [
        context for context in fire_contexts
        if context.get("raining") is True
        and (context["x"], context["y"], context["z"], context["block"])
            in exact_fire_positions
    ]
    exact_humidity_contexts = [
        context for context in fire_contexts
        if context.get("high_humidity") is True
        and context.get("raining") is not True
        and (context["x"], context["y"], context["z"], context["block"])
            in exact_fire_positions
    ]
    for context in exact_humidity_contexts:
        events.append({
            "tick": 0,
            "type": "set_fire_humidity_context",
            "x": context["x"],
            "y": context["y"],
            "z": context["z"],
        })
    if time["raining"] and exact_rain_contexts:
        for context in exact_rain_contexts:
            events.append({
                "tick": 0,
                "type": "set_fire_rain_context",
                "x": context["x"],
                "y": context["y"],
                "z": context["z"],
                "can_die": int(any(context.get(field) is True for field in (
                    "raining_at", "raining_at_west", "raining_at_east",
                    "raining_at_north", "raining_at_south",
                ))),
                "raining_at_east": int(
                    context.get("raining_at_east") is True),
                "can_die_west_candidate": int(
                    context.get("rain_can_die_west_candidate") is True),
            })
    values = struct.unpack(f"<{manifest['blocks']['cells']}H", raw)
    for index, value in enumerate(values):
        x, y, z = coordinate(index, box)
        events.append({
            "tick": 0,
            "type": "snapshot_block",
            "dim": dimension,
            "x": x,
            "y": y,
            "z": z,
            "id": value >> 4,
            "meta": value & 15,
        })
    if "sky_light" in manifest or "block_light" in manifest:
        # The block-only batch dirties the generated column baseline. Resolve
        # that once before overlaying Java's authoritative saved light values.
        events.append({
            "tick": 0,
            "type": "snapshot_blocks_finalize",
            "dim": dimension,
            "cx": center_cx,
            "cz": center_cz,
            "radius": radius,
        })
    if "sky_light" in manifest:
        sky_raw = (capsule_dir / SKY_LIGHT_FILE).read_bytes()
        for index, value in enumerate(sky_raw):
            x, y, z = coordinate(index, box)
            events.append({
                "tick": 0,
                "type": "snapshot_sky_light",
                "dim": dimension,
                "x": x,
                "y": y,
                "z": z,
                "value": value,
            })
        events.append({
            "tick": 0,
            "type": "snapshot_sky_light_finalize",
            "dim": dimension,
        })
    if "block_light" in manifest:
        block_light_raw = (capsule_dir / BLOCK_LIGHT_FILE).read_bytes()
        for index, value in enumerate(block_light_raw):
            x, y, z = coordinate(index, box)
            events.append({
                "tick": 0,
                "type": "snapshot_block_light",
                "dim": dimension,
                "x": x,
                "y": y,
                "z": z,
                "value": value,
            })
        events.append({
            "tick": 0,
            "type": "snapshot_block_light_finalize",
            "dim": dimension,
        })
    events.append({"tick": 0, "type": "player_potions_clear"})
    for effect in player["potions"]:
        events.append({
            "tick": 0,
            "type": "player_potion_add",
            "id": effect["id"],
            "amplifier": effect["amp"],
            "duration": effect["dur"],
            "ambient": int(effect["ambient"]),
            "show_particles": int(effect["show_particles"]),
        })
    events.extend([
        {
            "tick": 0,
            "type": "set_pose_state",
            "x": player["x"],
            "y": player["y"],
            "z": player["z"],
            "yaw": player["yaw"],
            "pitch": player["pitch"],
            "vx": player["vx"],
            "vy": player["vy"],
            "vz": player["vz"],
            "on_ground": int(bool(player["on_ground"])),
            "fall": player["fall_distance"],
        },
        {
            "tick": 0,
            "type": "set_vitals",
            "health": player["health"],
            "food": int(player["food"]),
        },
        {
            "tick": 0,
            "type": "set_food_stats",
            "saturation": player["saturation"],
            "exhaustion": player["food_exhaustion"],
        },
        {
            "tick": 0,
            "type": "set_food_timer",
            "timer": player["food_timer"],
        },
        {
            "tick": 0,
            "type": "set_air",
            "air": player["air"],
        },
        {
            "tick": 0,
            "type": "set_fire",
            "fire": player["fire"],
        },
        {
            "tick": 0,
            "type": "set_position_update_ticks",
            "value": player["position_update_ticks"],
            "pending": int(bool(player["position_packet_pending"])),
        },
        {
            "tick": 0,
            "type": "set_player_xp",
            "level": player["xp_level"],
            "fraction": player["xp_frac"],
            "total": player["xp_total"],
        },
        {
            "tick": 0,
            "type": "set_player_combat",
            "attack_ticks": player["attack_ticks"],
            "hurt_time": player["hurt_time"],
            "hurt_resistant_time": player["hurt_resistant_time"],
            "death_time": player["death_time"],
            "dead": int(bool(player["dead"])),
            "deaths": player["deaths"],
        },
        {
            "tick": 0,
            "type": "set_player_absorption",
            "value": player["absorption"],
        },
    ])
    if player["dead"] or player["death_time"] > 0:
        events.append({"tick": 0, "type": "continue_after_death"})
    # Clear the complete 41-slot player inventory so loading is independent of
    # the runtime's defaults, then apply the sparse stacks from the capsule.
    for slot in range(41):
        events.append({
            "tick": 0,
            "type": "set_inventory",
            "slot": slot,
            "item": 0,
            "count": 0,
            "meta": 0,
        })
    for item in state["inventory"]:
        stack_event = {
            "tick": 0,
            "type": "set_inventory",
            "slot": item["slot"],
            "item": item["id"],
            "count": item["count"],
            "meta": item["meta"],
        }
        enchantments = item["enchants"]
        if enchantments:
            stack_event["n_ench"] = len(enchantments)
            for enchant_index, (enchantment_id, level) in enumerate(
                    enchantments):
                stack_event[f"e{enchant_index}"] = (
                    enchantment_id << 16) | level
        repair_cost = item.get("repair_cost", 0)
        custom_name = item.get("custom_name", "")
        if repair_cost:
            stack_event["repair_cost"] = repair_cost
        if custom_name:
            if len(custom_name.encode("utf-8")) < 32:
                stack_event["custom_name"] = custom_name
        if item["slot"] in inventory_stack_payloads:
            stack_event["nbt_file"] = inventory_stack_payloads[item["slot"]]
        events.append(stack_event)
    for slot in range(27):
        events.append({
            "tick": 0,
            "type": "set_ender_inventory",
            "slot": slot,
            "item": 0,
            "count": 0,
            "meta": 0,
        })
    for item in state.get("ender_inventory", []):
        stack_event = {
            "tick": 0,
            "type": "set_ender_inventory",
            "slot": item["slot"],
            "item": item["id"],
            "count": item["count"],
            "meta": item["meta"],
        }
        enchantments = item["enchants"]
        if enchantments:
            stack_event["n_ench"] = len(enchantments)
            for enchant_index, (enchantment_id, level) in enumerate(
                    enchantments):
                stack_event[f"e{enchant_index}"] = (
                    enchantment_id << 16) | level
        repair_cost = item.get("repair_cost", 0)
        custom_name = item.get("custom_name", "")
        if repair_cost:
            stack_event["repair_cost"] = repair_cost
        if custom_name and len(custom_name.encode("utf-8")) < 32:
            stack_event["custom_name"] = custom_name
        if item["slot"] in ender_inventory_stack_payloads:
            stack_event["nbt_file"] = ender_inventory_stack_payloads[
                item["slot"]]
        events.append(stack_event)
    events.append({
        "tick": 0,
        "type": "set_selected_slot",
        "slot": player["held_slot"],
    })

    def append_no_ai_base(entity: dict) -> None:
        if "uuid_most" in entity:
            events.append({
                "tick": 0,
                "type": "restore_mob_uuid",
                "eid": entity["eid"],
                "most": entity["uuid_most"],
                "least": entity["uuid_least"],
            })
        events.append({
            "tick": 0,
            "type": "restore_no_ai_mob_state",
            "eid": entity["eid"],
            "air": entity["air"],
            "fire": entity["fire"],
            "on_ground": int(bool(entity["on_ground"])),
            "fall_distance": entity["fall_distance"],
            "in_water": int(bool(entity["in_water"])),
            "ticks_existed": entity["ticks_existed"],
            "living_sound_time": entity["base_living_sound_time"],
            "last_damage": entity["base_last_damage"],
            "entity_seed48": entity["base_entity_seed48"],
            "entity_have_gaussian": int(bool(
                entity["base_entity_have_gaussian"])),
            "entity_gaussian": entity["base_entity_gaussian"],
        })
        events.append({
            "tick": 0,
            "type": "restore_no_ai_mob_box",
            "eid": entity["eid"],
            "min_x": entity["base_box_min_x"],
            "min_y": entity["base_box_min_y"],
            "min_z": entity["base_box_min_z"],
            "max_x": entity["base_box_max_x"],
            "max_y": entity["base_box_max_y"],
            "max_z": entity["base_box_max_z"],
        })
        for effect in entity.get("mob_effects", []):
            events.append({
                "tick": 0,
                "type": "restore_mob_effect",
                "eid": entity["eid"],
                "id": effect["id"],
                "amplifier": effect["amp"],
                "duration": effect["dur"],
                "ambient": int(effect["ambient"]),
                "show_particles": int(effect["show_particles"]),
            })
        if entity.get("mob_effects") or float(entity["absorption"]) != 0.0:
            events.append({
                "tick": 0,
                "type": "restore_mob_health_absorption",
                "eid": entity["eid"],
                "health": entity["health"],
                "absorption": entity["absorption"],
            })

    # Promote only payloads whose complete tick-relevant state is represented
    # by the canonical schema. New captures retain Java loadedEntityList order
    # independently of the distance-sorted entity payload and entity IDs. An
    # old v1 capsule can safely omit that rank only when at most one represented
    # living/XP entity will be restored; otherwise regeneration is required.
    exact_entities = [
        value for value in state["entities"]
        if entity_payload_is_restorable(value)
    ]
    if len(exact_entities) > 1 and not all(
            "loaded_order" in value for value in state["entities"]):
        raise CapsuleError(
            "capsule has multiple restorable entities but no "
            "loaded_order; regenerate it with the current Java oracle"
        )
    entity_order_key = (
        (lambda value: value["loaded_order"])
        if state["entities"] and all(
            "loaded_order" in value for value in state["entities"])
        else (lambda value: value["eid"])
    )
    restorable_entity_eids = {value["eid"] for value in exact_entities}
    restored_entity_order = 0
    cloud_identity_events = []
    cloud_deadline_events = []
    for entity in sorted(state["entities"], key=entity_order_key):
        if entity["eid"] in restorable_entity_eids:
            events.append({
                "tick": 0,
                "type": "restore_loaded_entity_order",
                "order": restored_entity_order,
                "eid": entity["eid"],
            })
            restored_entity_order += 1
        plain_no_ai_types = {
            "EntityZombie": 2,
            "EntitySkeleton": 3,
            "EntityWitherSkeleton": 32,
            "EntityCreeper": 4,
            "EntitySpider": 5,
            "EntityCaveSpider": 39,
            "EntityEnderman": 6,
            "EntityBlaze": 7,
            "EntityGhast": 26,
            "EntityWitch": 23,
            "EntityZombieVillager": 41,
            "EntityVindicator": 51,
            "EntityEvoker": 52,
            "EntityVex": 53,
            "EntityGuardian": 55,
            "EntityElderGuardian": 56,
            "EntitySheep": 10,
            "EntityChicken": 13,
            "EntitySquid": 14,
            "EntityMagmaCube": 27,
            "EntitySlime": 35,
            "EntityPigZombie": 15,
            "EntitySilverfish": 36,
            "EntityCow": 12,
            "EntityIronGolem": 57,
            "EntityStray": 58,
            "EntityHusk": 59,
            "EntityMooshroom": 60,
            "EntityRabbit": 61,
            "EntityPolarBear": 62,
            "EntityBat": 24,
            "EntityEndermite": 63,
            "EntitySnowman": 64,
            "EntityGiantZombie": 65,
        }
        if entity.get("no_ai_plain_exact") is True:
            health_boost = sum(
                4.0 * (effect["amp"] + 1)
                for effect in entity.get("mob_effects", [])
                if effect["id"] == 21)
            constructor_max_health = float(entity["max_health"]) \
                - health_boost
            spawn_event = {
                "tick": 0,
                "type": "spawn_mob_fixture",
                "entity": plain_no_ai_types[entity["type"]],
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                # EntityLiving's constructor maximum predates restored
                # Health Boost. The exact value is restored after effects.
                "health": min(
                    float(entity["health"]), constructor_max_health),
                "no_ai": 1,
                "hurt_time": entity["hurt_time"],
                "death_time": entity["death_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
            }
            if entity["type"] in ("EntitySlime", "EntityMagmaCube"):
                spawn_event["size"] = entity["slime_size"]
            events.append(spawn_event)
            append_no_ai_base(entity)
            if entity["type"] == "EntitySheep":
                events.append({
                    "tick": 0,
                    "type": "restore_sheep_state",
                    "eid": entity["eid"],
                    "fleece_color": entity["sheep_fleece_color"],
                    "sheared": int(entity["sheep_sheared"]),
                })
            elif entity["type"] == "EntityChicken":
                events.append({
                    "tick": 0,
                    "type": "restore_chicken_state",
                    "eid": entity["eid"],
                    "time_until_next_egg": entity["chicken_egg_time"],
                    "wing_rotation": entity["chicken_wing_rotation"],
                    "dest_pos": entity["chicken_dest_pos"],
                    "old_flap_speed": entity["chicken_old_flap_speed"],
                    "old_flap": entity["chicken_old_flap"],
                    "wing_rot_delta": entity["chicken_wing_rot_delta"],
                    "chicken_jockey": 0,
                })
            elif entity["type"] in ("EntitySlime", "EntityMagmaCube"):
                events.append({
                    "tick": 0,
                    "type": "restore_slime_state",
                    "eid": entity["eid"],
                    "squish_amount": entity["slime_squish_amount"],
                    "squish_factor": entity["slime_squish_factor"],
                    "prev_squish_factor":
                        entity["slime_prev_squish_factor"],
                    "was_on_ground": int(entity["slime_was_on_ground"]),
                })
            elif entity["type"] == "EntityIronGolem":
                events.append({
                    "tick": 0,
                    "type": "restore_iron_golem_state",
                    "eid": entity["eid"],
                    "player_created": int(
                        entity["golem_player_created"]),
                    "home_timer": entity["golem_home_timer"],
                    "attack_timer": entity["golem_attack_timer"],
                    "rose_timer": entity["golem_rose_timer"],
                })
            elif entity["type"] == "EntityBat":
                events.append({
                    "tick": 0,
                    "type": "restore_bat_ai_state",
                    "eid": entity["eid"],
                    "hanging": int(entity["bat_hanging"]),
                    "spawn_valid": int(entity["bat_spawn_valid"]),
                    "spawn_x": entity.get("bat_spawn_x") or 0,
                    "spawn_y": entity.get("bat_spawn_y") or 0,
                    "spawn_z": entity.get("bat_spawn_z") or 0,
                    "head_yaw": entity["bat_head_yaw"],
                    "render_yaw": entity["bat_render_yaw_offset"],
                    "body_tick": entity["bat_body_rotation_tick_counter"],
                    "body_prev_head_yaw":
                        entity["bat_body_prev_head_yaw"],
                    "entity_age": entity["bat_entity_age"],
                    "persistence_required": int(
                        entity["bat_persistence_required"]),
                })
            elif entity["type"] == "EntityEndermite":
                events.append({
                    "tick": 0,
                    "type": "restore_endermite_state",
                    "eid": entity["eid"],
                    "lifetime": entity["endermite_lifetime"],
                    "player_spawned": int(
                        entity["endermite_player_spawned"]),
                    "persistence_required": int(
                        entity["endermite_persistence_required"]),
                })
            elif entity["type"] == "EntitySquid":
                events.append({
                    "tick": 0,
                    "type": "restore_squid_state",
                    "eid": entity["eid"],
                    "squid_pitch": entity["squid_pitch"],
                    "prev_squid_pitch": entity["squid_prev_pitch"],
                    "squid_yaw": entity["squid_yaw"],
                    "prev_squid_yaw": entity["squid_prev_yaw"],
                    "squid_rotation": entity["squid_rotation"],
                    "prev_squid_rotation":
                        entity["squid_prev_rotation"],
                    "tentacle_angle": entity["squid_tentacle_angle"],
                    "last_tentacle_angle":
                        entity["squid_last_tentacle_angle"],
                    "random_motion_speed":
                        entity["squid_random_motion_speed"],
                    "rotation_velocity":
                        entity["squid_rotation_velocity"],
                    "rotate_speed": entity["squid_rotate_speed"],
                    "random_motion_x": entity["squid_random_motion_x"],
                    "random_motion_y": entity["squid_random_motion_y"],
                    "random_motion_z": entity["squid_random_motion_z"],
                    "render_yaw_offset":
                        entity["squid_render_yaw_offset"],
                    "head_yaw": entity["squid_head_yaw"],
                    "body_tick":
                        entity["squid_body_rotation_tick_counter"],
                    "body_prev_head_yaw":
                        entity["squid_body_prev_head_yaw"],
                })
            elif entity["type"] == "EntitySnowman":
                events.append({
                    "tick": 0,
                    "type": "restore_snowman_state",
                    "eid": entity["eid"],
                    "pumpkin": int(entity["snowman_pumpkin"]),
                })
        elif entity["type"] == "EntityBat" \
                and entity.get("bat_active_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_mob_fixture",
                "entity": plain_no_ai_types["EntityBat"],
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "health": entity["health"],
                "no_ai": 1,
                "hurt_time": entity["hurt_time"],
                "death_time": entity["death_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
            })
            append_no_ai_base(entity)
            events.append({
                "tick": 0,
                "type": "restore_bat_ai_state",
                "eid": entity["eid"],
                "hanging": int(entity["bat_hanging"]),
                "spawn_valid": int(entity["bat_spawn_valid"]),
                "spawn_x": entity.get("bat_spawn_x") or 0,
                "spawn_y": entity.get("bat_spawn_y") or 0,
                "spawn_z": entity.get("bat_spawn_z") or 0,
                "head_yaw": entity["bat_head_yaw"],
                "render_yaw": entity["bat_render_yaw_offset"],
                "body_tick": entity["bat_body_rotation_tick_counter"],
                "body_prev_head_yaw": entity["bat_body_prev_head_yaw"],
                "entity_age": entity["bat_entity_age"],
                "persistence_required": int(
                    entity["bat_persistence_required"]),
            })
            events.append({
                "tick": 0,
                "type": "set_mob_no_ai",
                "eid": entity["eid"],
                "no_ai": 0,
            })
        elif entity["type"] == "EntitySquid" \
                and entity.get("squid_active_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_mob_fixture",
                "entity": plain_no_ai_types["EntitySquid"],
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "health": entity["health"],
                "no_ai": 1,
                "hurt_time": entity["hurt_time"],
                "death_time": entity["death_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
            })
            append_no_ai_base(entity)
            events.append({
                "tick": 0,
                "type": "restore_squid_state",
                "eid": entity["eid"],
                "squid_pitch": entity["squid_pitch"],
                "prev_squid_pitch": entity["squid_prev_pitch"],
                "squid_yaw": entity["squid_yaw"],
                "prev_squid_yaw": entity["squid_prev_yaw"],
                "squid_rotation": entity["squid_rotation"],
                "prev_squid_rotation": entity["squid_prev_rotation"],
                "tentacle_angle": entity["squid_tentacle_angle"],
                "last_tentacle_angle":
                    entity["squid_last_tentacle_angle"],
                "random_motion_speed":
                    entity["squid_random_motion_speed"],
                "rotation_velocity": entity["squid_rotation_velocity"],
                "rotate_speed": entity["squid_rotate_speed"],
                "random_motion_x": entity["squid_random_motion_x"],
                "random_motion_y": entity["squid_random_motion_y"],
                "random_motion_z": entity["squid_random_motion_z"],
                "render_yaw_offset": entity["squid_render_yaw_offset"],
                "head_yaw": entity["squid_head_yaw"],
                "body_tick": entity["squid_body_rotation_tick_counter"],
                "body_prev_head_yaw":
                    entity["squid_body_prev_head_yaw"],
            })
            events.append({
                "tick": 0,
                "type": "restore_squid_ai_state",
                "eid": entity["eid"],
                "entity_age": entity["squid_entity_age"],
                "persistence_required": int(
                    entity["squid_persistence_required"]),
            })
            events.append({
                "tick": 0,
                "type": "set_mob_no_ai",
                "eid": entity["eid"],
                "no_ai": 0,
            })
        elif entity["type"] in {
                "EntityHorse", "EntityDonkey", "EntityMule",
                "EntitySkeletonHorse", "EntityZombieHorse", "EntityLlama"} \
                and entity.get("horse_exact") is True:
            horse_types = {
                "EntityHorse": 68,
                "EntityDonkey": 69,
                "EntityMule": 70,
                "EntitySkeletonHorse": 71,
                "EntityZombieHorse": 72,
            }
            horse_status = sum(
                bit for field, bit in (
                    ("horse_tame", 2), ("horse_saddled", 4),
                    ("horse_bred", 8), ("horse_eating", 16),
                    ("horse_rearing", 32), ("horse_mouth_open", 64),
                ) if entity[field]
            )
            spawn_event = {
                "tick": 0,
                "type": ("spawn_llama_fixture"
                         if entity["type"] == "EntityLlama"
                         else "spawn_horse_fixture"),
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "health": entity["health"],
                "no_ai": 1,
                "max_health": entity["horse_max_health_base"],
                "movement_speed": entity["horse_movement_speed_base"],
                "jump_strength": entity["horse_jump_strength"],
                "growing_age": entity["horse_growing_age"],
                "status": horse_status,
                "temper": entity["horse_temper"],
                "variant": entity["horse_variant"],
                "chested": int(entity["horse_chested"]),
                "hurt_time": entity["hurt_time"],
                "death_time": entity["death_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
            }
            if entity["type"] == "EntityLlama":
                spawn_event.update({
                    "strength": entity["llama_strength"],
                    "decor": entity["llama_decor"],
                    "did_spit": int(entity["llama_did_spit"]),
                    "leashed": int(entity["llama_leashed"]),
                })
            else:
                spawn_event.update({
                    "entity": horse_types[entity["type"]],
                    "armor": entity["horse_armor"],
                    "trap": int(entity["horse_trap"]),
                    "trap_time": entity["horse_trap_time"],
                })
            events.append(spawn_event)
            append_no_ai_base(entity)
            events.append({
                "tick": 0,
                "type": "restore_horse_lifecycle",
                "eid": entity["eid"],
                "in_love": entity["horse_in_love"],
                "forced_age": entity["horse_forced_age"],
                "forced_age_timer": entity["horse_forced_age_timer"],
                "eating_counter": entity["horse_eating_counter"],
                "open_mouth_counter": entity["horse_open_mouth_counter"],
                "jump_rearing_counter":
                    entity["horse_jump_rearing_counter"],
                "tail_counter": entity["horse_tail_counter"],
                "sprint_counter": entity["horse_sprint_counter"],
                "gallop_time": entity["horse_gallop_time"],
                "horse_jumping": int(entity["horse_jumping"]),
                "allow_stand_sliding": int(
                    entity["horse_allow_stand_sliding"]),
                "jump_power": entity["horse_jump_power"],
                "head_lean": entity["horse_head_lean"],
                "prev_head_lean": entity["horse_prev_head_lean"],
                "rearing_amount": entity["horse_rearing_amount"],
                "prev_rearing_amount":
                    entity["horse_prev_rearing_amount"],
                "mouth_openness": entity["horse_mouth_openness"],
                "prev_mouth_openness":
                    entity["horse_prev_mouth_openness"],
                "prev_limb_amount": entity["horse_prev_limb_amount"],
                "limb_amount": entity["horse_limb_amount"],
                "limb_swing": entity["horse_limb_swing"],
            })
            events.append({
                "tick": 0,
                "type": "restore_horse_owner",
                "eid": entity["eid"],
                "present": int(entity["horse_owner_present"]),
                "most": entity["horse_owner_uuid_most"],
                "least": entity["horse_owner_uuid_least"],
            })
            for stack in entity["horse_inventory"]:
                stack_event = {
                    "tick": 0,
                    "type": "restore_horse_inventory",
                    "eid": entity["eid"],
                    "slot": stack["slot"],
                    "item": stack["id"],
                    "count": stack["count"],
                    "meta": stack["meta"],
                }
                if stack.get("repair_cost", 0):
                    stack_event["repair_cost"] = stack["repair_cost"]
                if stack.get("custom_name", ""):
                    stack_event["custom_name"] = stack["custom_name"]
                enchantments = stack["enchants"]
                if enchantments:
                    stack_event["n_ench"] = len(enchantments)
                    for enchant_index, (enchantment_id, level) in enumerate(
                            enchantments):
                        stack_event[f"e{enchant_index}"] = (
                            enchantment_id << 16) | level
                payload = horse_stack_payloads.get(
                    (entity["eid"], stack["slot"]))
                if payload is not None:
                    stack_event["nbt_file"] = payload
                events.append(stack_event)
        elif entity["type"] == "EntityArmorStand" \
                and entity.get("armor_stand_exact") is True:
            status = sum(
                bit for field, bit in (
                    ("armor_stand_small", 1),
                    ("armor_stand_show_arms", 4),
                    ("armor_stand_no_base_plate", 8),
                    ("armor_stand_marker", 16),
                ) if entity[field]
            )
            events.append({
                "tick": 0,
                "type": "spawn_armor_stand_fixture",
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "health": entity["health"],
                "on_ground": int(entity["armor_stand_on_ground"]),
                "no_gravity": int(entity["armor_stand_no_gravity"]),
                "invisible": int(entity["armor_stand_invisible"]),
                "status": status,
                "disabled_slots": entity["armor_stand_disabled_slots"],
                "ticks_existed": entity["armor_stand_ticks_existed"],
                "fire": entity["armor_stand_fire"],
                "punch_cooldown": entity["armor_stand_punch_cooldown"],
            })
            events.append({
                "tick": 0,
                "type": "set_armor_stand_living_state",
                "eid": entity["eid"],
                "air": entity["armor_stand_air"],
                "in_water": int(entity["armor_stand_in_water"]),
                "fall_distance": entity["armor_stand_fall_distance"],
                "hurt_time": entity["hurt_time"],
                "death_time": entity["death_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
                "last_damage": entity["armor_stand_last_damage"],
            })
            events.append({
                "tick": 0,
                "type": "set_armor_stand_generic_state",
                "eid": entity["eid"],
                "absorption": entity["armor_stand_absorption"],
                "max_health": entity["armor_stand_max_health"],
                "max_health_base":
                    entity["armor_stand_max_health_base"],
                "revenge_timer": entity["armor_stand_revenge_timer"],
                "portal_cooldown": entity["armor_stand_portal_cooldown"],
                "name_visible": int(
                    entity["armor_stand_custom_name_visible"]),
                "silent": int(entity["armor_stand_silent"]),
                "glowing": int(entity["armor_stand_glowing"]),
                "invulnerable": int(entity["armor_stand_invulnerable"]),
                "update_blocked": int(
                    entity["armor_stand_update_blocked"]),
                "fall_flying": int(entity["armor_stand_fall_flying"]),
                "vehicle_eid": entity["armor_stand_vehicle_eid"],
            })
            if entity["armor_stand_custom_name"]:
                events.append({
                    "tick": 0,
                    "type": "set_armor_stand_custom_name",
                    "eid": entity["eid"],
                    "name": entity["armor_stand_custom_name"],
                })
            for tag in entity["armor_stand_tags"]:
                events.append({
                    "tick": 0,
                    "type": "add_armor_stand_tag",
                    "eid": entity["eid"],
                    "tag": tag,
                })
            for effect in entity["armor_stand_effects"]:
                events.append({
                    "tick": 0,
                    "type": "add_armor_stand_effect",
                    "eid": entity["eid"],
                    "id": effect["id"],
                    "amplifier": effect["amp"],
                    "duration": effect["dur"],
                    "ambient": int(effect["ambient"]),
                    "show_particles": int(effect["show_particles"]),
                })
            events.append({
                "tick": 0,
                "type": "set_armor_stand_uuid",
                "eid": entity["eid"],
                "most": entity["uuid_most"],
                "least": entity["uuid_least"],
            })
            events.append({
                "tick": 0,
                "type": "set_armor_stand_random_state",
                "eid": entity["eid"],
                "entity_seed48": entity["armor_stand_entity_seed48"],
                "entity_have_gaussian": int(
                    entity["armor_stand_entity_have_gaussian"]),
                "entity_gaussian": entity["armor_stand_entity_gaussian"],
            })
            for part, rotation in enumerate(entity["armor_stand_pose"]):
                events.append({
                    "tick": 0,
                    "type": "set_armor_stand_pose",
                    "eid": entity["eid"],
                    "part": part,
                    "x": rotation[0],
                    "y": rotation[1],
                    "z": rotation[2],
                })
            for stack in entity["armor_stand_equipment"]:
                stack_event = {
                    "tick": 0,
                    "type": "set_armor_stand_equipment",
                    "eid": entity["eid"],
                    "slot": stack["slot"],
                    "item": stack["id"],
                    "count": stack["count"],
                    "meta": stack["meta"],
                }
                if stack.get("repair_cost", 0):
                    stack_event["repair_cost"] = stack["repair_cost"]
                if stack.get("custom_name", ""):
                    stack_event["custom_name"] = stack["custom_name"]
                enchantments = stack["enchants"]
                if enchantments:
                    stack_event["n_ench"] = len(enchantments)
                    for enchant_index, (enchantment_id, level) in enumerate(
                            enchantments):
                        stack_event[f"e{enchant_index}"] = (
                            enchantment_id << 16) | level
                payload = armor_stand_stack_payloads.get(
                    (entity["eid"], stack["slot"]))
                if payload is not None:
                    stack_event["nbt_file"] = payload
                events.append(stack_event)
        elif entity["type"] == "EntityPig" \
                and entity.get("no_ai_pig_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_mob_fixture",
                "entity": 11,
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "health": entity["health"],
                "no_ai": 1,
                "hurt_time": entity["hurt_time"],
                "death_time": entity["death_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
            })
            append_no_ai_base(entity)
        elif entity["type"] == "EntityVillager" \
                and entity.get("villager_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_villager_fixture",
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "health": entity["health"],
                "hurt_time": entity["hurt_time"],
                "death_time": entity["death_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
                "profession": entity["profession"],
                "living_sound_time": entity["living_sound_time"],
                "entity_seed48": entity["entity_seed48"],
                "entity_have_gaussian": int(bool(
                    entity["entity_have_gaussian"])),
                "entity_gaussian": entity["entity_gaussian"],
            })
            append_no_ai_base(entity)
            if entity.get("offers_initialized") is True:
                offers = entity["offers"]
                events.append({
                    "tick": 0,
                    "type": "restore_villager_trade",
                    "eid": entity["eid"],
                    "career": entity["career"],
                    "career_level": entity["career_level"],
                    "wealth": entity["wealth"],
                    "willing": int(bool(entity["willing"])),
                    "offer_count": len(offers),
                })
                for offer_index, offer in enumerate(offers):
                    events.append({
                        "tick": 0,
                        "type": "restore_villager_offer",
                        "eid": entity["eid"],
                        "index": offer_index,
                        "uses": offer["uses"],
                        "max_uses": offer["max_uses"],
                        "rewards_exp": int(bool(offer["rewards_exp"])),
                    })
                    for part, name in enumerate(("buy_a", "buy_b", "sell")):
                        stack = offer[name]
                        stack_event = {
                            "tick": 0,
                            "type": "restore_villager_offer_stack",
                            "eid": entity["eid"],
                            "index": offer_index,
                            "part": part,
                            "item": stack["id"],
                            "count": stack["count"],
                            "meta": stack["meta"],
                        }
                        enchantments = stack["enchants"]
                        if enchantments:
                            stack_event["n_ench"] = len(enchantments)
                            for enchant_index, (enchantment_id, level) \
                                    in enumerate(enchantments):
                                stack_event[f"e{enchant_index}"] = (
                                    enchantment_id << 16
                                ) | level
                        events.append(stack_event)
            if entity.get("active_fresh_villager_exact") is True:
                events.append({
                    "tick": 0,
                    "type": "set_mob_no_ai",
                    "eid": entity["eid"],
                    "no_ai": 0,
                })
        elif entity["type"] in ("EntityWolf", "EntityOcelot") \
                and entity.get("tameable_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_mob_fixture",
                "entity": 16 if entity["type"] == "EntityWolf" else 17,
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "health": entity["health"],
                "no_ai": 1,
                "hurt_time": entity["hurt_time"],
                "death_time": entity["death_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
            })
            events.append({
                "tick": 0,
                "type": "restore_tameable_state",
                "eid": entity["eid"],
                "tamed": int(bool(entity["tamed"])),
                "sitting": int(bool(entity["sitting"])),
                "player_owner": int(bool(entity["player_owner"])),
                "variant": entity["variant"],
                "growing_age": entity["growing_age"],
                "living_sound_time": entity["living_sound_time"],
                "entity_seed48": entity["entity_seed48"],
                "entity_have_gaussian": int(bool(
                    entity["entity_have_gaussian"])),
                "entity_gaussian": entity["entity_gaussian"],
            })
            append_no_ai_base(entity)
        elif entity["type"] == "EntityWither" \
                and entity.get("wither_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_wither_fixture",
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "render_yaw_offset": entity["render_yaw_offset"],
                "health": entity["health"],
                "invul_time": entity["invul_time"],
                "ticks_existed": entity["ticks_existed"],
                "hurt_time": entity["hurt_time"],
                "death_time": entity["death_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
                "block_break_counter": entity["block_break_counter"],
                "entity_seed48": entity["base_entity_seed48"],
                "entity_have_gaussian": int(bool(
                    entity["base_entity_have_gaussian"])),
                "entity_gaussian": entity["base_entity_gaussian"],
            })
            events.append({
                "tick": 0,
                "type": "restore_wither_base_state",
                "eid": entity["eid"],
                "no_ai": int(bool(entity["no_ai"])),
                "no_gravity": int(bool(entity["no_gravity"])),
                "air": entity["air"],
                "fire": entity["fire"],
                "on_ground": int(bool(entity["on_ground"])),
                "fall_distance": entity["fall_distance"],
                "in_water": int(bool(entity["in_water"])),
                "living_sound_time": entity["base_living_sound_time"],
                "last_damage": entity["base_last_damage"],
                "recently_hit": entity["recently_hit"],
                "attacking_player": int(bool(entity["attacking_player"])),
            })
            events.append({
                "tick": 0,
                "type": "restore_wither_ai_state",
                "eid": entity["eid"],
                "target_eid": entity["attack_target_eid"],
                "target_is_player": int(bool(
                    entity["attack_target_is_player"])),
                "revenge_eid": entity["revenge_eid"],
                "revenge_is_player": int(bool(entity["revenge_is_player"])),
                "revenge_timer": entity["revenge_timer"],
                "hurt_target_task_active": int(bool(
                    entity["hurt_target_task_active"])),
                "hurt_target_eid": entity["hurt_target_eid"],
                "hurt_target_is_player": int(bool(
                    entity["hurt_target_is_player"])),
                "hurt_revenge_timer_old":
                    entity["hurt_revenge_timer_old"],
                "hurt_target_unseen_ticks":
                    entity["hurt_target_unseen_ticks"],
                "nearest_target_task_active": int(bool(
                    entity["nearest_target_task_active"])),
                "target_task_tick": entity["target_task_tick"],
                "goal_task_tick": entity["goal_task_tick"],
                "invul_task_active": int(bool(
                    entity["invul_task_active"])),
                "ranged_task_active": int(bool(
                    entity["ranged_task_active"])),
                "ranged_attack_time": entity["ranged_attack_time"],
                "ranged_see_time": entity["ranged_see_time"],
            })
            events.append({
                "tick": 0,
                "type": "restore_wither_rotation_state",
                "eid": entity["eid"],
                "rotation_yaw_head": entity["rotation_yaw_head"],
                "prev_rotation_yaw_head":
                    entity["prev_rotation_yaw_head"],
                "prev_render_yaw_offset":
                    entity["prev_render_yaw_offset"],
                "body_rotation_tick_counter":
                    entity["body_rotation_tick_counter"],
                "body_prev_render_yaw_head":
                    entity["body_prev_render_yaw_head"],
            })
            for head in range(3):
                side = max(0, head - 1)
                events.append({
                    "tick": 0,
                    "type": "set_wither_head_state",
                    "eid": entity["eid"],
                    "head": head,
                    "target_eid": entity[f"head_target_{head}"],
                    "target_is_player": int(bool(
                        entity[f"head_target_player_{head}"])),
                    "next_update": entity[f"head_next_{side}"]
                        if head > 0 else 0,
                    "idle_updates": entity[f"head_idle_{side}"]
                        if head > 0 else 0,
                    "yaw": entity[f"head_yaw_{side}"]
                        if head > 0 else 0.0,
                    "pitch": entity[f"head_pitch_{side}"]
                        if head > 0 else 0.0,
                    "prev_yaw": entity[f"head_prev_yaw_{side}"]
                        if head > 0 else 0.0,
                    "prev_pitch": entity[f"head_prev_pitch_{side}"]
                        if head > 0 else 0.0,
                })
            if "uuid_most" in entity:
                events.append({
                    "tick": 0,
                    "type": "set_wither_uuid",
                    "eid": entity["eid"],
                    "most": entity["uuid_most"],
                    "least": entity["uuid_least"],
                })
        elif entity["type"] == "EntityWitherSkull" \
                and entity.get("wither_skull_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_wither_skull_fixture",
                "eid": entity["eid"],
                "shooter_eid": entity["shooter_eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "ax": entity["ax"],
                "ay": entity["ay"],
                "az": entity["az"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "invulnerable": int(bool(entity["invulnerable"])),
                "ticks_in_air": entity["ticks_in_air"],
                "life": entity["life"],
            })
            if "uuid_most" in entity:
                events.append({
                    "tick": 0,
                    "type": "restore_transient_entity_uuid",
                    "eid": entity["eid"],
                    "most": entity["uuid_most"],
                    "least": entity["uuid_least"],
                })
        elif entity["type"] == "EntityLlamaSpit" \
                and entity.get("llama_spit_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_llama_spit_fixture",
                "eid": entity["eid"],
                "owner_eid": entity["llama_spit_owner_eid"],
                "owner_uuid_present": int(
                    entity["llama_spit_owner_uuid_present"]),
                "owner_uuid_most":
                    entity["llama_spit_owner_uuid_most"],
                "owner_uuid_least":
                    entity["llama_spit_owner_uuid_least"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "ticks_existed": entity["ticks_existed"],
                "no_gravity": int(entity["llama_spit_no_gravity"]),
            })
            if "uuid_most" in entity:
                events.append({
                    "tick": 0,
                    "type": "restore_transient_entity_uuid",
                    "eid": entity["eid"],
                    "most": entity["uuid_most"],
                    "least": entity["uuid_least"],
                })
        elif entity["type"] == "EntityShulker" \
                and entity.get("shulker_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_shulker_state_fixture",
                "eid": entity["eid"],
                "attach_x": entity["attach_x"],
                "attach_y": entity["attach_y"],
                "attach_z": entity["attach_z"],
                "face": entity["face"],
                "no_ai": int(bool(entity["no_ai"])),
                "peek_tick": entity["peek_tick"],
                "peek_time": entity["peek_time"],
                "attack_time": entity["attack_time"],
                "has_player_target": int(bool(
                    entity["has_player_target"])),
                "watch_time": entity["watch_time"],
                "idle_look_time": entity["idle_look_time"],
                "living_sound_time": entity["living_sound_time"],
                "ticks_existed": entity["ticks_existed"],
                "hurt_time": entity["hurt_time"],
                "hurt_resistant_time": entity["hurt_resistant_time"],
                "death_time": entity["death_time"],
                "health": entity["health"],
                "last_damage": entity["last_damage"],
                "prev_peek_amount": entity["prev_peek_amount"],
                "peek_amount": entity["peek_amount"],
                "head_yaw": entity["head_yaw"],
                "head_pitch": entity["head_pitch"],
                "entity_seed48": entity["entity_seed48"],
            })
        elif entity["type"] == "EntityShulkerBullet" \
                and entity.get("shulker_bullet_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_shulker_bullet_state_fixture",
                "eid": entity["eid"],
                "owner_eid": entity["owner_eid"],
                "direction": entity["direction"],
                "steps": entity["steps"],
                "ticks_existed": entity["ticks_existed"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "target_dx": entity["target_dx"],
                "target_dy": entity["target_dy"],
                "target_dz": entity["target_dz"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "entity_seed48": entity["entity_seed48"],
            })
        elif entity["type"] == "EntityXPOrb":
            events.append({
                "tick": 0,
                "type": "spawn_xp_fixture",
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "value": entity["value"],
                "eid": entity["eid"],
                "age": entity["age"],
                "pickup_delay": entity["pickup_delay"],
                "color": entity["color"],
                "target_color": entity["target_color"],
            })
            events.append({
                "tick": 0,
                "type": "restore_xp_orb_box",
                "eid": entity["eid"],
                "min_x": entity["xp_box_min_x"],
                "min_y": entity["xp_box_min_y"],
                "min_z": entity["xp_box_min_z"],
                "max_x": entity["xp_box_max_x"],
                "max_y": entity["xp_box_max_y"],
                "max_z": entity["xp_box_max_z"],
            })
        elif entity["type"] == "EntityItem" \
                and entity.get("item_exact") is True:
            item_event = {
                "tick": 0,
                "type": "spawn_item_state_fixture",
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "hover_start": entity["hover_start"],
                "item": entity["item"],
                "count": entity["count"],
                "meta": entity["meta"],
                "age": entity["age"],
                "ticks_existed": entity["ticks_existed"],
                "pickup_delay": entity["pickup_delay"],
                "health": entity["health"],
                "lifespan": entity["lifespan"],
                "on_ground": int(bool(entity["on_ground"])),
                "no_gravity": int(bool(entity["no_gravity"])),
                "fire": entity["fire"],
                "in_water": int(bool(entity["in_water"])),
                "first_update": int(bool(entity["first_update"])),
                "entity_seed48": entity["entity_seed48"],
            }
            if entity["eid"] in entity_stack_payloads:
                item_event["nbt_file"] = entity_stack_payloads[entity["eid"]]
            events.append(item_event)
            if "uuid_most" in entity:
                events.append({
                    "tick": 0,
                    "type": "restore_item_entity_uuid",
                    "eid": entity["eid"],
                    "most": entity["uuid_most"],
                    "least": entity["uuid_least"],
                })
        elif entity["type"] == "EntityFallingBlock" \
                and entity.get("falling_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_falling_fixture",
                "eid": entity["eid"],
                "block": entity["block"],
                "meta": entity["meta"],
                "fall_time": entity["fall_time"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "no_gravity": 0,
                "no_ground": 0,
            })
            events.append({
                "tick": 0,
                "type": "restore_falling_origin",
                "eid": entity["eid"],
                "x": entity["origin_x"],
                "y": entity["origin_y"],
                "z": entity["origin_z"],
            })
            events.append({
                "tick": 0,
                "type": "restore_transient_entity_uuid",
                "eid": entity["eid"],
                "most": entity["uuid_most"],
                "least": entity["uuid_least"],
            })
        elif entity["type"] == "EntityTNTPrimed" \
                and entity.get("primed_tnt_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_primed_tnt_fixture",
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "fuse": entity["fuse"],
            })
            events.append({
                "tick": 0,
                "type": "restore_transient_entity_uuid",
                "eid": entity["eid"],
                "most": entity["uuid_most"],
                "least": entity["uuid_least"],
            })
        elif entity["type"] == "EntityEnderCrystal" \
                and entity.get("end_crystal_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_end_crystal_fixture",
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "inner_rotation": entity["inner_rotation"],
                "show_bottom": entity["show_bottom"],
                "has_beam": entity["has_beam"],
                "beam_x": entity["beam_x"],
                "beam_y": entity["beam_y"],
                "beam_z": entity["beam_z"],
            })
            events.append({
                "tick": 0,
                "type": "restore_transient_entity_uuid",
                "eid": entity["eid"],
                "most": entity["uuid_most"],
                "least": entity["uuid_least"],
            })
        elif entity["type"] == "EntityFireworkRocket" \
                and entity.get("firework_exact") is True:
            firework_event = {
                "tick": 0,
                "type": "spawn_firework_state_fixture",
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "prev_yaw": entity["prev_yaw"],
                "prev_pitch": entity["prev_pitch"],
                "age": entity["firework_age"],
                "lifetime": entity["lifetime"],
                "ticks_existed": entity["ticks_existed"],
                "attached_player": int(bool(entity["attached_player"])),
                "flight": entity["flight"],
                "explosion_count": entity["explosion_count"],
                "large_blast": int(bool(entity["large_blast"])),
                "twinkle": int(bool(entity["twinkle"])),
                "firework_item_present": int(bool(
                    entity["firework_item_present"])),
                "firework_item": entity["firework_item"],
                "firework_count": entity["firework_count"],
                "firework_meta": entity["firework_meta"],
                "entity_seed48": entity["entity_seed48"],
                "entity_have_gaussian": int(bool(
                    entity["entity_have_gaussian"])),
                "entity_gaussian": entity["entity_gaussian"],
            }
            if entity["eid"] in entity_stack_payloads:
                firework_event["nbt_file"] = entity_stack_payloads[
                    entity["eid"]]
            events.append(firework_event)
            events.append({
                "tick": 0,
                "type": "restore_transient_entity_uuid",
                "eid": entity["eid"],
                "most": entity["uuid_most"],
                "least": entity["uuid_least"],
            })
        elif entity["type"] in {"EntityTippedArrow", "EntitySpectralArrow"} \
                and entity.get("arrow_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_player_arrow_state_fixture",
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "ticks_in_air": entity["ticks_in_air"],
                "fire_ticks": entity["fire_ticks"],
                "damage": entity["damage"],
                "knockback": entity["knockback"],
                "critical": int(bool(entity["critical"])),
                "pickup_status": entity["pickup_status"],
                "in_ground": int(bool(entity["in_ground"])),
                "shake": entity["shake"],
                "ticks_in_ground": entity["ticks_in_ground"],
                "tile_x": entity["tile_x"],
                "tile_y": entity["tile_y"],
                "tile_z": entity["tile_z"],
                "tile_block": entity["tile_block"],
                "tile_meta": entity["tile_meta"],
                "random_seed48": entity["entity_seed48"],
                "random_have_gaussian": int(bool(
                    entity["entity_have_gaussian"])),
                "random_next_gaussian": entity["entity_gaussian"],
            })
            payload_event = {
                "tick": 0,
                "type": "set_arrow_payload_fixture",
                "eid": entity["eid"],
                "arrow_kind": entity["arrow_kind"],
                "potion_type": entity["potion_type"],
                "spectral_duration": entity["spectral_duration"],
                "color": entity["arrow_color"],
                "custom_color": int(bool(
                    entity["arrow_custom_color"])),
                "pickup_item": entity["pickup_item"],
                "pickup_meta": entity["pickup_meta"],
                "effect_count": len(entity["arrow_effects"]),
                "time_in_ground": entity["time_in_ground"],
            }
            for effect_index in range(16):
                effect = entity["arrow_effects"][effect_index] \
                    if effect_index < len(entity["arrow_effects"]) \
                    else {"id": 0, "amp": 0, "dur": 0, "flags": 0}
                payload_event[f"e{effect_index}_id"] = effect["id"]
                payload_event[f"e{effect_index}_amp"] = effect["amp"]
                payload_event[f"e{effect_index}_dur"] = effect["dur"]
                payload_event[f"e{effect_index}_flags"] = effect["flags"]
            if entity["eid"] in entity_stack_payloads:
                payload_event["nbt_file"] = entity_stack_payloads[
                    entity["eid"]]
            events.append(payload_event)
            if "uuid_most" in entity:
                events.append({
                    "tick": 0,
                    "type": "restore_transient_entity_uuid",
                    "eid": entity["eid"],
                    "most": entity["uuid_most"],
                    "least": entity["uuid_least"],
                })
        elif entity["type"] in {
                "EntityEgg", "EntitySnowball", "EntityExpBottle",
                "EntityEnderPearl", "EntityPotion"} \
                and entity.get("throwable_exact") is True:
            projectile_types = {
                "EntityPotion": 6,
                "EntityEgg": 7,
                "EntitySnowball": 8,
                "EntityExpBottle": 9,
                "EntityEnderPearl": 12,
            }
            events.append({
                "tick": 0,
                "type": "spawn_throwable_state_fixture",
                "eid": entity["eid"],
                "projectile_type": projectile_types[entity["type"]],
                "potion_item": entity.get("potion_item", 0),
                "potion_type": entity.get("potion_type", 0),
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "prev_yaw": entity["prev_yaw"],
                "prev_pitch": entity["prev_pitch"],
                "age": entity["age"],
                "ticks_in_air": entity["ticks_in_air"],
                "player_thrower": int(bool(entity["player_thrower"])),
                "thrower_player_pending": int(bool(
                    entity["thrower_player_pending"])),
                "ignore_player": int(bool(entity["ignore_player"])),
                "ignore_player_time": entity["ignore_player_time"],
                "pearl_private_thrower": int(bool(
                    entity["pearl_private_thrower"])),
                "throwable_shake": entity["throwable_shake"],
                "in_ground": int(bool(entity["in_ground"])),
                "ticks_in_ground": entity["ticks_in_ground"],
                "tile_x": entity["tile_x"],
                "tile_y": entity["tile_y"],
                "tile_z": entity["tile_z"],
                "tile_block": entity["tile_block"],
                "portal_counter": entity["portal_counter"],
                "in_portal": int(bool(entity["in_portal"])),
                "portal_cooldown": entity["portal_cooldown"],
                "last_portal_pos_valid": int(bool(
                    entity["last_portal_pos_valid"])),
                "last_portal_x": entity["last_portal_x"],
                "last_portal_y": entity["last_portal_y"],
                "last_portal_z": entity["last_portal_z"],
                "last_portal_vec_x": entity["last_portal_vec_x"],
                "last_portal_vec_y": entity["last_portal_vec_y"],
                "teleport_direction": entity["teleport_direction"],
                "client_random_valid": int(bool(
                    entity["client_random_valid"])),
                "client_entity_seed48": entity["client_entity_seed48"],
                "random_seed48": entity["entity_seed48"],
                "random_have_gaussian": int(bool(
                    entity["entity_have_gaussian"])),
                "random_next_gaussian": entity["entity_gaussian"],
            })
            if entity["type"] == "EntityPotion":
                events.append(_magma_potion_payload_event(
                    entity, cloud=False,
                    nbt_file=entity_stack_payloads.get(entity["eid"])))
            events.append({
                "tick": 0,
                "type": "restore_transient_entity_uuid",
                "eid": entity["eid"],
                "most": entity["uuid_most"],
                "least": entity["uuid_least"],
            })
        elif entity["type"] == "EntityPotion" \
                and entity.get("potion_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_potion_fixture",
                "eid": entity["eid"],
                "potion_item": entity["potion_item"],
                "potion_type": entity["potion_type"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "age": entity["age"],
                "player_thrower": int(bool(entity["player_thrower"])),
                "ignore_player": int(bool(entity["ignore_player"])),
                "ignore_player_time": entity["ignore_player_time"],
            })
            events.append(_magma_potion_payload_event(
                entity, cloud=False,
                nbt_file=entity_stack_payloads.get(entity["eid"])))
        elif entity["type"] == "EntityAreaEffectCloud" \
                and entity.get("cloud_exact") is True:
            events.append({
                "tick": 0,
                "type": "spawn_area_effect_cloud_fixture",
                "eid": entity["eid"],
                "potion_type": entity["potion_type"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "prev_yaw": entity["prev_yaw"],
                "prev_pitch": entity["prev_pitch"],
                "age": entity["age"],
                "duration": entity["duration"],
                "duration_on_use": entity["duration_on_use"],
                "wait_time": entity["wait_time"],
                "reapplication_delay": entity["reapplication_delay"],
                "radius": entity["radius"],
                "radius_on_use": entity["radius_on_use"],
                "radius_per_tick": entity["radius_per_tick"],
                "next_application": entity["next_application"],
                "player_owner": int(bool(entity["player_owner"])),
                "ignore_radius": int(bool(entity["ignore_radius"])),
                "particle": entity["particle"],
                "particle_param1": entity["particle_param1"],
                "particle_param2": entity["particle_param2"],
            })
            events.append({
                "tick": 0,
                "type": "set_area_effect_cloud_common_state",
                "cloud_eid": entity["eid"],
                "dimension": entity["dimension"],
                "air": entity["air"],
                "fire": entity["fire"],
                "portal_cooldown": entity["portal_cooldown"],
                "on_ground": int(bool(entity["on_ground"])),
                "no_gravity": int(bool(entity["no_gravity"])),
                "invulnerable": int(bool(entity["invulnerable"])),
                "silent": int(bool(entity["silent"])),
                "glowing": int(bool(entity["glowing"])),
                "update_blocked": int(bool(entity["update_blocked"])),
                "in_water": int(bool(entity["in_water"])),
                "first_update": int(bool(entity["first_update"])),
                "fall_distance": entity["fall_distance"],
                "prev_x": entity["prev_x"],
                "prev_y": entity["prev_y"],
                "prev_z": entity["prev_z"],
                "last_tick_x": entity["last_tick_x"],
                "last_tick_y": entity["last_tick_y"],
                "last_tick_z": entity["last_tick_z"],
                "server_entity_seed48": entity["server_entity_seed48"],
                "server_entity_have_gaussian": int(bool(
                    entity["server_entity_have_gaussian"])),
                "server_entity_gaussian":
                    entity["server_entity_gaussian"],
            })
            events.append(_magma_potion_payload_event(
                entity, cloud=True))
            cloud_identity_events.append({
                "tick": 0,
                "type": "set_area_effect_cloud_identity",
                "cloud_eid": entity["eid"],
                "uuid_most": entity["uuid_most"],
                "uuid_least": entity["uuid_least"],
                "owner_present": int(bool(entity["owner_present"])),
                "owner_eid": entity["owner_eid"],
                "owner_uuid_most": entity["owner_uuid_most"],
                "owner_uuid_least": entity["owner_uuid_least"],
            })
            for deadline in entity["reapplication_deadlines"]:
                cloud_deadline_events.append({
                    "tick": 0,
                    "type": "set_area_effect_cloud_deadline",
                    "cloud_eid": entity["eid"],
                    "target_eid": deadline["eid"],
                    "deadline": deadline["deadline"],
                })
        elif entity["type"] in {
                "EntityMinecartEmpty", "EntityMinecartChest",
                "EntityMinecartFurnace", "EntityMinecartTNT",
                "EntityMinecartMobSpawner", "EntityMinecartHopper"}:
            events.append({
                "tick": 0,
                "type": "spawn_minecart_fixture",
                "kind": entity["minecart_kind"],
                "eid": entity["eid"],
                "x": entity["x"],
                "y": entity["y"],
                "z": entity["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "reverse": int(bool(entity["reverse"])),
                "rolling_amplitude": entity["rolling_amplitude"],
                "rolling_direction": entity["rolling_direction"],
                "damage": entity["damage"],
                "fuel": entity["fuel"],
                "push_x": entity["push_x"],
                "push_z": entity["push_z"],
                "tnt_fuse": entity["tnt_fuse"],
                "hopper_enabled": int(bool(entity["hopper_enabled"])),
                "transfer_cooldown": entity["transfer_cooldown"],
                "entity_seed48": entity["entity_seed48"],
                "entity_have_gaussian": int(bool(
                    entity["entity_have_gaussian"])),
                "entity_gaussian": entity["entity_gaussian"],
            })
            if "uuid_most" in entity:
                events.append({
                    "tick": 0,
                    "type": "restore_minecart_uuid",
                    "eid": entity["eid"],
                    "most": entity["uuid_most"],
                    "least": entity["uuid_least"],
                })
            if entity["type"] == "EntityMinecartMobSpawner":
                entity_id = entity["spawner_entity_id"]
                if ":" not in entity_id:
                    entity_id = "minecraft:" + entity_id.lower()
                events.append({
                    "tick": 0,
                    "type": "set_minecart_spawner_state",
                    "eid": entity["eid"],
                    "entity": SPAWNER_ENTITY_TYPES[entity_id],
                    "delay": entity["spawner_delay"],
                    "min_delay": entity["spawner_min_delay"],
                    "max_delay": entity["spawner_max_delay"],
                    "spawn_count": entity["spawner_spawn_count"],
                    "max_nearby": entity["spawner_max_nearby"],
                    "activate_range": entity["spawner_activate_range"],
                    "spawn_range": entity["spawner_spawn_range"],
                    "spawn_nbt_file":
                        minecart_spawner_spawn_data_payloads[entity["eid"]],
                    "default_entity_nbt":
                        entity["spawner_default_entity_nbt"],
                })
                for potential_index, potential in enumerate(
                        entity["spawner_potentials"]):
                    potential_id = potential["entity_id"]
                    if ":" not in potential_id:
                        potential_id = (
                            "minecraft:" + potential_id.lower())
                    events.append({
                        "tick": 0,
                        "type": "add_minecart_spawner_potential",
                        "eid": entity["eid"],
                        "entity": SPAWNER_ENTITY_TYPES[potential_id],
                        "weight": potential["weight"],
                        "entity_nbt_file":
                            minecart_spawner_potential_payloads[
                                (entity["eid"], potential_index)],
                        "default_entity_nbt":
                            potential["default_entity_nbt"],
                    })
            for item in entity["items"]:
                slot_event = {
                    "tick": 0,
                    "type": "set_minecart_slot",
                    "eid": entity["eid"],
                    "slot": item["slot"],
                    "item": item["id"],
                    "count": item["count"],
                    "meta": item["meta"],
                }
                payload = minecart_stack_payloads.get(
                    (entity["eid"], item["slot"]))
                if payload is not None:
                    slot_event["stack_nbt_file"] = payload
                events.append(slot_event)
    # Owners and deadline targets can follow their cloud in Java loaded order.
    # Apply both graphs only after every represented living entity exists.
    events.extend(cloud_identity_events)
    events.extend(cloud_deadline_events)
    for painting in state["paintings"]:
        events.append({
            "tick": 0,
            "type": "set_painting",
            "dim": dimension,
            "eid": painting["eid"],
            "hanging_x": painting["hanging_x"],
            "hanging_y": painting["hanging_y"],
            "hanging_z": painting["hanging_z"],
            "facing": painting["facing"],
            "art": painting["art"],
            "tick_counter": painting["tick_counter"],
            "most": painting["uuid_most"],
            "least": painting["uuid_least"],
        })
    for knot in state["leash_knots"]:
        events.append({
            "tick": 0,
            "type": "set_leash_knot",
            "dim": dimension,
            "eid": knot["eid"],
            "x": knot["hanging_x"],
            "y": knot["hanging_y"],
            "z": knot["hanging_z"],
            "tick_counter": knot["tick_counter"],
            "most": knot["uuid_most"],
            "least": knot["uuid_least"],
        })
    # Llama caravan targets may appear later in loadedEntityList than their
    # owners. Restore that graph first; the class-neutral leash graph below
    # owns every holder and pending-NBT relationship, including llamas.
    for entity in state["entities"]:
        if entity["type"] != "EntityLlama" \
                or entity.get("horse_exact") is not True:
            continue
        events.append({
            "tick": 0,
            "type": "restore_llama_links",
            "eid": entity["eid"],
            "leash_holder_kind": 0,
            "leash_holder_eid": -1,
            "caravan_head_eid": entity["llama_caravan_head_eid"],
            "caravan_tail_eid": entity["llama_caravan_tail_eid"],
            "caravan_speed": entity["llama_caravan_speed"],
            "caravan_dist_counter":
                entity["llama_caravan_dist_counter"],
        })
    for leash in state["living_leashes"]:
        entity = next(
            value for value in state["entities"]
            if value["eid"] == leash["eid"])
        if entity["type"] == "EntityWolf":
            events.append({
                "tick": 0,
                "type": "set_wolf_angry",
                "eid": leash["eid"],
                "angry": int(leash["wolf_angry"]),
            })
        if leash["holder_kind"] == 3:
            events.append({
                "tick": 0,
                "type": "restore_living_leash_knot",
                "eid": leash["eid"],
                "knot_eid": leash["holder_eid"],
            })
        elif leash["pending"]:
            events.append({
                "tick": 0,
                "type": "restore_living_leash_pending",
                "eid": leash["eid"],
                "x": leash["pending_x"],
                "y": leash["pending_y"],
                "z": leash["pending_z"],
            })
        else:
            events.append({
                "tick": 0,
                "type": "restore_living_leash",
                "eid": leash["eid"],
                "holder_kind": leash["holder_kind"],
                "holder_eid": leash["holder_eid"],
            })
    if player.get("riding_eid", -1) >= 0:
        events.append({
            "tick": 0,
            "type": "restore_player_riding",
            "eid": player["riding_eid"],
        })
    for entity in state["entities"]:
        if entity["type"] != "EntityFishHook":
            continue
        events.append({
            "tick": 0,
            "type": "spawn_fish_hook_fixture",
            "eid": entity["eid"],
            "x": entity["x"],
            "y": entity["y"],
            "z": entity["z"],
            "vx": entity["vx"],
            "vy": entity["vy"],
            "vz": entity["vz"],
            "yaw": entity["yaw"],
            "pitch": entity["pitch"],
            "fish_state": entity["fish_state"],
            "in_ground": int(bool(entity["in_ground"])),
            "ticks_in_ground": entity["ticks_in_ground"],
            "ticks_in_air": entity["ticks_in_air"],
            "ticks_catchable": entity["ticks_catchable"],
            "ticks_caught_delay": entity["ticks_caught_delay"],
            "ticks_catchable_delay": entity["ticks_catchable_delay"],
            "fish_approach_angle": entity["fish_approach_angle"],
            "lure": entity["lure"],
            "luck": entity["luck"],
            "caught_eid": entity["caught_eid"],
            "entity_seed48": entity["entity_seed48"],
            "entity_have_gaussian": int(bool(
                entity["entity_have_gaussian"])),
            "entity_gaussian": entity["entity_gaussian"],
        })
    for frame in state["item_frames"]:
        frame_event = {
            "tick": 0,
            "type": "set_item_frame",
            "dim": dimension,
            "eid": frame["eid"],
            "hanging_x": frame["hanging_x"],
            "hanging_y": frame["hanging_y"],
            "hanging_z": frame["hanging_z"],
            "facing": frame["facing"],
            "item": frame["item"],
            "count": frame["count"],
            "meta": frame["meta"],
            "rotation": frame["rotation"],
            "tick_counter": frame["tick_counter"],
            "item_drop_chance": frame["item_drop_chance"],
            "entity_seed48": frame["entity_seed48"],
            "entity_have_gaussian": int(bool(
                frame["entity_have_gaussian"])),
            "entity_gaussian": frame["entity_gaussian"],
            "most": frame["uuid_most"],
            "least": frame["uuid_least"],
            "repair_cost": frame["repair_cost"],
            "custom_name": frame["custom_name"],
            "n_ench": len(frame["enchants"]),
            "tracker_update_counter": frame["tracker_update_counter"],
            "map_data_present": int(bool(frame["map_data_present"])),
            "map_dimension": frame["map_dimension"],
            "map_x_center": frame["map_x_center"],
            "map_z_center": frame["map_z_center"],
            "map_scale": frame["map_scale"],
            "map_tracking_position": int(bool(
                frame["map_tracking_position"])),
            "map_unlimited_tracking": int(bool(
                frame["map_unlimited_tracking"])),
            "map_decoration_present": int(bool(
                frame["map_decoration_present"])),
            "map_decoration_type": frame["map_decoration_type"],
            "map_decoration_x": frame["map_decoration_x"],
            "map_decoration_z": frame["map_decoration_z"],
            "map_decoration_rotation": frame["map_decoration_rotation"],
        }
        for enchant_index, (enchant_id, level) in enumerate(
                frame["enchants"]):
            frame_event[f"e{enchant_index}"] = (
                ((enchant_id & 0xFFFF) << 16) | (level & 0xFFFF))
        if frame["eid"] in item_frame_stack_payloads:
            frame_event["nbt_file"] = item_frame_stack_payloads[frame["eid"]]
        if frame["eid"] in item_frame_map_payloads:
            frame_event["map_colors_file"] = item_frame_map_payloads[
                frame["eid"]]
        events.append(frame_event)
    hanging_order = [
        *state["item_frames"], *state["paintings"], *state["leash_knots"],
    ]
    for frame in sorted(
            hanging_order, key=lambda value: value["loaded_order"]):
        events.append({
            "tick": 0,
            "type": "restore_loaded_entity_order",
            "order": restored_entity_order,
            "eid": frame["eid"],
        })
        restored_entity_order += 1
    # Java constructors consume the static entity-id cursor before a restored
    # entity's saved runtime id is assigned. Reapply the captured cursor only
    # after every entity-bearing fixture has been reconstructed.
    events.append({
        "tick": 0,
        "type": "set_entity_id_cursor",
        "value": int(state["entity_id_cursor"]),
    })
    shulker_payloads = {
        payload["index"]: payload
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "shulker_item_tag"
    }
    container_stack_payloads = {
        (payload["container_index"], payload["slot"]): payload
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "item_stack"
        and payload["owner"] == "container"
    }
    if state.get("loaded_tiles"):
        for tile in sorted(
                state["loaded_tiles"],
                key=lambda value: value["loaded_order"]):
            events.append({
                "tick": 0,
                "type": "restore_loaded_tile_order",
                "order": tile["loaded_order"],
                "x": tile["x"],
                "y": tile["y"],
                "z": tile["z"],
            })
        for tile in sorted(
                (value for value in state["loaded_tiles"]
                 if value["tickable"]),
                key=lambda value: value["update_order"]):
            events.append({
                "tick": 0,
                "type": "restore_tickable_tile_order",
                "order": tile["update_order"],
                "x": tile["x"],
                "y": tile["y"],
                "z": tile["z"],
            })
    elif state["containers"] and all(
            "loaded_order" in container
            for container in state["containers"]):
        for order, container in enumerate(sorted(
                state["containers"],
                key=lambda value: value["loaded_order"])):
            events.append({
                "tick": 0,
                "type": "restore_loaded_tile_order",
                "order": order,
                "x": container["x"],
                "y": container["y"],
                "z": container["z"],
            })
    for spawner_index, spawner in enumerate(state.get("spawners", [])):
        entity_id = spawner["entity_id"]
        if ":" not in entity_id:
            entity_id = "minecraft:" + entity_id.lower()
        events.append({
            "tick": 0,
            "type": "set_spawner_state",
            "dim": dimension,
            "x": spawner["x"],
            "y": spawner["y"],
            "z": spawner["z"],
            "entity": SPAWNER_ENTITY_TYPES[entity_id],
            "delay": spawner["delay"],
            "min_delay": spawner["min_delay"],
            "max_delay": spawner["max_delay"],
            "spawn_count": spawner["spawn_count"],
            "max_nearby": spawner["max_nearby"],
            "activate_range": spawner["activate_range"],
            "spawn_range": spawner["spawn_range"],
            "spawn_nbt_file": spawner_spawn_data_payloads[spawner_index],
            "default_entity_nbt": spawner["default_entity_nbt"],
        })
        for potential_index, potential in enumerate(spawner["potentials"]):
            potential_id = potential["entity_id"]
            if ":" not in potential_id:
                potential_id = "minecraft:" + potential_id.lower()
            events.append({
                "tick": 0,
                "type": "add_spawner_potential",
                "dim": dimension,
                "x": spawner["x"],
                "y": spawner["y"],
                "z": spawner["z"],
                "entity": SPAWNER_ENTITY_TYPES[potential_id],
                "weight": potential["weight"],
                "entity_nbt_file": spawner_potential_payloads[
                    (spawner_index, potential_index)],
                "default_entity_nbt": potential["default_entity_nbt"],
            })
    for container_index, container in enumerate(state["containers"]):
        if container["type"] in (
                "command_block", "repeating_command_block",
                "chain_command_block"):
            events.append({
                "tick": 0,
                "type": "set_command_block_state",
                "dim": dimension,
                "x": container["x"],
                "y": container["y"],
                "z": container["z"],
                "success_count": container["success_count"],
                "command": container["command"],
                "last_output": container["last_output"],
                "powered": container["powered"],
                "automatic": container["automatic"],
                "condition_met": container["condition_met"],
            })
            continue
        by_slot = {
            item["slot"]: item for item in container["items"]
        }
        # Materialize an empty supported TE with one explicit slot-zero clear;
        # every other slot starts empty.
        first = by_slot.get(
            0, {"slot": 0, "id": 0, "count": 0, "meta": 0})
        event_type = (
            "set_chest_slot"
            if container["type"] in (
                "single_chest", "single_trapped_chest",
                "double_chest_half", "double_trapped_chest_half")
            else "set_furnace_slot"
            if container["type"] == "furnace"
            else "set_brewing_slot"
            if container["type"] == "brewing_stand"
            else "set_static_container_slot"
        )
        first_event = {
            "tick": 0,
            "type": event_type,
            "dim": dimension,
            "x": container["x"],
            "y": container["y"],
            "z": container["z"],
            "slot": first["slot"],
            "item": first["id"],
            "count": first["count"],
            "meta": first["meta"],
        }
        first_payload = container_stack_payloads.get(
            (container_index, first["slot"]))
        if first_payload is not None:
            first_event["stack_nbt_file"] = first_payload["file"]
        if container["type"] == "furnace":
            first_event.update({
                "burn_time": container["burn_time"],
                "current_burn_time": container["current_burn_time"],
                "cook_time": container["cook_time"],
                "total_cook_time": container["total_cook_time"],
                "custom_name": container["custom_name"],
            })
        if container["type"] == "brewing_stand":
            first_event.update({
                "brew_time": container["brew_time"],
                "fuel": container["fuel"],
            })
        if container["type"] == "shulker_box":
            first_event["nbt_file"] = \
                shulker_payloads[container_index]["file"]
        events.append(first_event)
        for item in container["items"]:
            if item["slot"] == 0:
                continue
            slot_event = {
                "tick": 0,
                "type": event_type,
                "dim": dimension,
                "x": container["x"],
                "y": container["y"],
                "z": container["z"],
                "slot": item["slot"],
                "item": item["id"],
                "count": item["count"],
                "meta": item["meta"],
            }
            slot_payload = container_stack_payloads.get(
                (container_index, item["slot"]))
            if slot_payload is not None:
                slot_event["stack_nbt_file"] = slot_payload["file"]
            if container["type"] == "furnace":
                slot_event.update({
                    "burn_time": container["burn_time"],
                    "current_burn_time": container["current_burn_time"],
                    "cook_time": container["cook_time"],
                    "total_cook_time": container["total_cook_time"],
                })
            if container["type"] == "brewing_stand":
                slot_event.update({
                    "brew_time": container["brew_time"],
                    "fuel": container["fuel"],
                })
            events.append(slot_event)
        if container["type"] == "shulker_box":
            events.append({
                "tick": 0,
                "type": "restore_shulker_transient",
                "dim": dimension,
                "x": container["x"],
                "y": container["y"],
                "z": container["z"],
                "open_count": container["open_count"],
                "animation_status": container["animation_status"],
                "progress_bits": container["progress_bits"],
                "progress_old_bits": container["progress_old_bits"],
            })
        if container["type"] in (
                "single_chest", "single_trapped_chest",
                "double_chest_half", "double_trapped_chest_half"):
            events.append({
                "tick": 0,
                "type": "restore_chest_transient",
                "dim": dimension,
                "x": container["x"],
                "y": container["y"],
                "z": container["z"],
                "num_players_using": container["num_players_using"],
                "lid_angle_bits": container["lid_angle_bits"],
                "prev_lid_angle_bits": container[
                    "prev_lid_angle_bits"],
                "ticks_since_sync": container["ticks_since_sync"],
            })
        if container["type"] == "hopper":
            events.append({
                "tick": 0,
                "type": "set_hopper_transfer_state",
                "dim": dimension,
                "x": container["x"],
                "y": container["y"],
                "z": container["z"],
                "transfer_cooldown": container["transfer_cooldown"],
                "ticked_game_time": container["ticked_game_time"],
            })
        if container["type"] == "brewing_stand":
            events.append({
                "tick": 0,
                "type": "restore_brewing_ingredient",
                "dim": dimension,
                "x": container["x"],
                "y": container["y"],
                "z": container["z"],
                "ingredient_id": container["ingredient_id"],
            })
    for flower_pot in state["flower_pots"]:
        events.append({
            "tick": 0,
            "type": "set_flower_pot",
            "dim": dimension,
            "x": flower_pot["x"],
            "y": flower_pot["y"],
            "z": flower_pot["z"],
            "item": flower_pot["item"],
            "meta": flower_pot["meta"],
        })
    for note_block in state["note_blocks"]:
        events.append({
            "tick": 0,
            "type": "set_note_block",
            "dim": dimension,
            "x": note_block["x"],
            "y": note_block["y"],
            "z": note_block["z"],
            "note": note_block["note"],
            "powered": int(note_block["powered"]),
        })
    skull_payloads = {
        payload["index"]: payload
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "skull_owner"
    }
    for skull_index, skull in enumerate(state["skulls"]):
        skull_event = {
            "tick": 0,
            "type": "set_skull",
            "dim": dimension,
            "x": skull["x"],
            "y": skull["y"],
            "z": skull["z"],
            "skull_type": skull["type"],
            "rotation": skull["rotation"],
        }
        if skull["has_owner"]:
            skull_event["nbt_file"] = skull_payloads[skull_index]["file"]
        events.append(skull_event)
    decorative_tile_payloads = {
        payload["index"]: payload
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "decorative_tile"
    }
    decorative_drop_payloads = {
        payload["index"]: payload
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "decorative_drop"
    }
    for tile_index, tile in enumerate(state["decorative_tiles"]):
        tile_event = {
            "tick": 0,
            "type": "set_decorative_tile",
            "dim": dimension,
            "x": tile["x"], "y": tile["y"], "z": tile["z"],
            "tile_nbt_file": decorative_tile_payloads[tile_index]["file"],
            "drop_item": tile["drop_item"],
            "drop_meta": tile["drop_meta"],
        }
        if tile_index in decorative_drop_payloads:
            tile_event["drop_nbt_file"] = decorative_drop_payloads[
                tile_index]["file"]
        events.append(tile_event)
    for comparator in state["comparators"]:
        events.append({
            "tick": 0,
            "type": "set_comparator_output",
            "dim": dimension,
            "x": comparator["x"],
            "y": comparator["y"],
            "z": comparator["z"],
            "output_signal": comparator["output_signal"],
        })
    for piston in state["moving_pistons"]:
        events.append({
            "tick": 0,
            "type": "load_moving_piston",
            "dim": dimension,
            **{
                field: int(value) if isinstance(value, bool) else value
                for field, value in piston.items()
            },
        })
    # Queue insertion itself is keyed by the persisted `order`, so the script
    # event order is not observable after restore. Load falling-block columns
    # bottom-up, however: strict admission of a stacked callback requires its
    # lower callback to have been admitted first. An upper-first Java tie is
    # still restored upper-first because its explicit order values are kept.
    pending_scheduled = list(state["scheduled_ticks"])
    scheduled_restore = []
    while pending_scheduled:
        ready_index = next(
            (
                index for index, entry in enumerate(pending_scheduled)
                if entry["block"] not in (12, 13) or not any(
                    candidate["block"] == entry["block"]
                    and candidate["x"] == entry["x"]
                    and candidate["y"] == entry["y"] - 1
                    and candidate["z"] == entry["z"]
                    and candidate["time"] == entry["time"]
                    and candidate["priority"] == entry["priority"]
                    for candidate in pending_scheduled
                )
            ),
            None,
        )
        if ready_index is None:
            raise CapsuleError("cyclic falling-block schedule dependency")
        scheduled_restore.append(pending_scheduled.pop(ready_index))
    for entry in scheduled_restore:
        events.append({
            "tick": 0,
            "type": "schedule_tick",
            "x": entry["x"],
            "y": entry["y"],
            "z": entry["z"],
            "block": entry["block"],
            "time": entry["time"],
            "priority": entry["priority"],
            "order": entry["order"],
        })
    for toggle in state["redstone_torch_toggles"]:
        events.append({
            "tick": 0,
            "type": "redstone_torch_toggle",
            "x": toggle["x"],
            "y": toggle["y"],
            "z": toggle["z"],
            "time": toggle["time"],
        })
    return events


def emit_magma(capsule_dir: pathlib.Path, output: pathlib.Path) -> int:
    events = magma_events(capsule_dir)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as stream:
        for row in events:
            stream.write(json.dumps(row, separators=(",", ":")) + "\n")
    return len(events)


def selftest() -> None:
    with tempfile.TemporaryDirectory(prefix="netherite_capsule_") as temp:
        root = pathlib.Path(temp)
        state = {
            "tick": -1,
            "do_entity_drops": True,
            "do_mob_spawning": False,
            "do_mob_loot": False,
            "random_tick_speed": 1,
            "ticking_chunks": [],
            "ticking_chunks_complete": True,
            "entity_id_cursor": 93,
            "player": {
                "eid": 0,
                "x": 1.5, "y": 65.0, "z": -2.5,
                "yaw": 90.0, "pitch": 10.0,
                "vx": 0.1, "vy": -0.0784000015258789, "vz": 0.0,
                "on_ground": 1, "health": 19.0, "max_health": 20.0,
                "absorption": 0.0, "food": 18,
                "saturation": 3.5, "food_exhaustion": 1.25,
                "food_timer": 7, "air": 300, "fire": -20,
                "position_update_ticks": 19,
                "position_packet_pending": 1,
                "xp_level": 2, "xp_frac": 0.25, "xp_total": 18,
                "fall_distance": 0.0,
                "sprinting": 0, "sneaking": 0, "jumping": 0,
                "held_slot": 2, "held_id": 1, "held_count": 3,
                "held_meta": 0, "attack_cooldown": 0.6,
                "attack_ticks": 3, "hurt_time": 4,
                "hurt_resistant_time": 7, "death_time": 0, "dead": 0,
                "deaths": 0, "dim": 0,
                "potions": [
                    {"id": 1, "amp": 0, "dur": 10,
                     "ambient": False, "show_particles": True},
                    {"id": 16, "amp": 1, "dur": 20,
                     "ambient": True, "show_particles": False},
                ],
            },
            "inventory": [
                {"slot": 2, "id": 1, "count": 3, "meta": 0,
                 "enchants": [[16, 5]], "repair_cost": 7,
                 "custom_name": "Oracle Stone", "nbt_subset_exact": True},
                {"slot": 38, "id": 311, "count": 1, "meta": 7,
                 "enchants": [[0, 4]]},
                {"slot": 40, "id": 442, "count": 1, "meta": 5,
                 "enchants": []},
            ],
            "ender_inventory": [
                {"slot": 7, "id": 264, "count": 5, "meta": 0,
                 "enchants": []},
            ],
            "entities": [
                {
                    "eid": 91, "type": "EntityPig",
                    "loaded_order": 1,
                    "x": -4.5, "y": 65.0, "z": -4.5,
                    "dx": -6.0, "dy": 0.0, "dz": -2.0,
                    "vx": 0.0, "vy": 0.0, "vz": 0.0,
                    "yaw": 0.0, "pitch": 0.0, "health": 5.0,
                    "hurt_time": 3, "death_time": 0,
                    "hurt_resistant_time": 13, "no_ai": True,
                    "no_ai_pig_exact": True,
                    "no_ai_base_exact": True,
                    "air": 300, "fire": -1, "on_ground": False,
                    "fall_distance": 0.0, "in_water": False,
                    "ticks_existed": 20,
                    "base_living_sound_time": 3,
                    "base_last_damage": 0.0,
                    "base_entity_seed48": 0x123456789ABC,
                    "base_entity_have_gaussian": False,
                    "base_entity_gaussian": 0.0,
                    "mob_potions_empty": True,
                    "mob_equipment_empty": True,
                    "max_health": 10.0, "absorption": 0.0,
                    "base_box_min_x": -5.0, "base_box_min_y": 65.0,
                    "base_box_min_z": -5.0, "base_box_max_x": -4.0,
                    "base_box_max_y": 66.0, "base_box_max_z": -4.0,
                },
                {
                    "eid": 92, "type": "EntityXPOrb",
                    "loaded_order": 0,
                    "x": 4.5, "y": 65.5, "z": -2.5,
                    "dx": 3.0, "dy": 0.5, "dz": 0.0,
                    "vx": 0.0, "vy": 0.1, "vz": 0.0,
                    "yaw": 0.0, "pitch": 0.0, "health": -1.0,
                    "value": 5, "age": 7, "pickup_delay": 2,
                    "color": 11, "target_color": -100,
                    "xp_box_exact": True,
                    "xp_box_min_x": 4.25, "xp_box_min_y": 65.5,
                    "xp_box_min_z": -2.75, "xp_box_max_x": 4.75,
                    "xp_box_max_y": 66.0, "xp_box_max_z": -2.25,
                },
            ],
            "scheduled_ticks": [
                {
                    "x": -5, "y": 64, "z": -5, "block": 1,
                    "time": 45, "priority": 2, "order": 17,
                },
                {
                    "x": 0, "y": 65, "z": 0, "block": 8,
                    "time": 46, "priority": 0, "order": 18,
                },
            ],
            "scheduled_ticks_complete": True,
            "comparators": [],
            "comparators_complete": True,
            "containers": [],
            "containers_complete": True,
            "flower_pots": [],
            "flower_pots_complete": True,
            "note_blocks": [],
            "note_blocks_complete": True,
            "skulls": [],
            "skulls_complete": True,
            "decorative_tiles": [],
            "decorative_tiles_complete": True,
            "moving_pistons": [],
            "moving_pistons_complete": True,
            "item_frames": [],
            "item_frames_complete": True,
            "paintings": [],
            "paintings_complete": True,
            "leash_knots": [],
            "leash_knots_complete": True,
            "living_leashes": [],
            "living_leashes_complete": True,
            "village_collection_tick": 41,
            "villages": [],
            "villages_complete": True,
            "redstone_torch_toggles": [
                {"x": 1, "y": 65, "z": -1, "time": 40},
            ],
            "redstone_torch_toggles_complete": True,
            "world_rng": {
                "java_seed48": 0x5DEECE664,
                "math_seed48": 0x123456789ABC,
                "block_seed48": 0x0ABCDEF12345,
                "inventory_helper_seed48": 0x0123456789AB,
                "inventory_helper_have_gaussian": True,
                "inventory_helper_gaussian": -0.125,
                "update_lcg": 1094913777,
            },
            "time": {
                "world_time": 6000, "total_time": 42, "moon_phase": 0,
                "raining": False, "thundering": False,
                "rain_time": 0, "thunder_time": 0,
                "clean_weather_time": 0,
                "do_weather_cycle": False,
                "do_daylight_cycle": False,
                "prev_rain_strength": 0.0, "rain_strength": 0.0,
                "prev_thunder_strength": 0.0,
                "thunder_strength": 0.0,
            },
        }
        state_path = root / "state.json"
        state_path.write_text(json.dumps(state), encoding="utf-8")
        blocks_path = root / "source.bin"
        block_states = [0] * 484
        box = [-5, 63, -5, 5, 66, 5]
        for z in range(-5, 6):
            for x in range(-5, 6):
                index = ((63 - box[1]) * 11 + (z - box[2])) * 11 + (x - box[0])
                block_states[index] = 1 << 4
        for x, y, z, packed in (
            (-5, 64, -5, 1 << 4),
            (0, 65, 0, 8 << 4),
            (4, 64, -4, (23 << 4) | 3),
            (2, 64, -4, 154 << 4),
            (-2, 64, -4, 52 << 4),
            (5, 64, 5, (117 << 4) | 1),
            (-4, 64, -4, (84 << 4) | 1),
            (-3, 64, -4, (137 << 4) | 2),
            (3, 64, 3, (61 << 4) | 2),
            (4, 64, 3, (146 << 4) | 2),
            (-4, 64, 3, (54 << 4) | 2),
            (-3, 64, 3, (54 << 4) | 2),
        ):
            index = ((y - box[1]) * 11 + (z - box[2])) * 11 + (x - box[0])
            block_states[index] = packed
        blocks_path.write_bytes(struct.pack("<484H", *block_states))
        sky_path = root / "source_sky.bin"
        sky_values = bytes(index & 15 for index in range(484))
        sky_path.write_bytes(sky_values)
        block_light_path = root / "source_block_light.bin"
        block_light_values = bytes((index * 3) & 15 for index in range(484))
        block_light_path.write_bytes(block_light_values)
        capsule = root / "capsule"
        create_capsule(
            state_path, blocks_path, box, capsule,
            sky_light_path=sky_path,
            block_light_path=block_light_path,
            seed=7, source_engine="selftest", source_version="1",
        )
        manifest, raw = validate_capsule(capsule)
        assert manifest["blocks"]["cells"] == 484 and raw == blocks_path.read_bytes()
        assert manifest["capabilities"]["world.light.sky_nibbles"] == "exact"
        assert manifest["capabilities"]["world.light.block_nibbles"] == "exact"
        events = magma_events(capsule)
        assert events[0]["type"] == "set_dimension"
        assert any(
            row["type"] == "set_world_random_seed"
            and row["value"] == 0x5DEECE664
            for row in events
        )
        assert any(
            row["type"] == "set_math_random_seed"
            and row["value"] == 0x123456789ABC
            for row in events
        )
        assert any(
            row["type"] == "set_block_random_seed"
            and row["value"] == 0x0ABCDEF12345
            for row in events
        )
        assert any(
            row["type"] == "set_inventory_helper_random"
            and row["value"] == 0x0123456789AB
            and row["have_next"] == 1 and row["next"] == -0.125
            for row in events
        )
        assert any(
            row["type"] == "set_world_update_lcg"
            and row["value"] == 1094913777
            for row in events
        )
        assert sum(
            row["type"] == "set_ender_inventory"
            and row["item"] == 0 for row in events
        ) == 27
        assert any(
            row["type"] == "set_ender_inventory"
            and row["slot"] == 7 and row["item"] == 264
            and row["count"] == 5
            for row in events
        )
        assert any(
            row["type"] == "set_random_tick_speed"
            and row["value"] == 1
            for row in events
        )
        assert sum(
            row["type"] == "snapshot_block_light" for row in events
        ) == 484
        assert any(
            row["type"] == "snapshot_block_light_finalize" for row in events
        )
        assert any(
            row["type"] == "restore_village_collection"
            and row["collection_tick"] == 41 and row["count"] == 0
            for row in events
        )
        assert [
            (row["order"], row["eid"])
            for row in events
            if row["type"] == "restore_loaded_entity_order"
        ] == [(0, 92), (1, 91)]
        map_state = copy.deepcopy(state)
        map_raw = bytes(
            0 if x < 16 and y < 16
            else (1 + ((x // 16) + (y // 16) * 8) % 35) * 4
                + (((x // 4) + (y // 4)) & 3)
            for y in range(128) for x in range(128)
        )
        map_state["item_frames"] = [{
            "eid": 90, "x": 4.5, "y": 64.5, "z": 4.96875,
            "hanging_x": 4, "hanging_y": 64, "hanging_z": 4,
            "facing": 2, "item": 358, "count": 1, "meta": 42,
            "rotation": 0, "tick_counter": 7,
            "item_drop_chance": 1.0,
            "entity_seed48": 0x23456789ABCD,
            "entity_have_gaussian": False, "entity_gaussian": 0.0,
            "uuid_most": 101, "uuid_least": 102,
            "loaded_order": 0, "repair_cost": 0,
            "custom_name": "", "enchants": [],
            "tracker_update_counter": 10,
            "map_data_present": True,
            "map_colors_b64": base64.b64encode(map_raw).decode("ascii"),
            "map_dimension": 0, "map_x_center": 4, "map_z_center": 4,
            "map_scale": 0, "map_tracking_position": True,
            "map_unlimited_tracking": False,
            "map_decoration_present": True, "map_decoration_type": 1,
            "map_decoration_x": 0, "map_decoration_z": 0,
            "map_decoration_rotation": 8,
        }]
        map_state_path = root / "map_state.json"
        map_state_path.write_text(json.dumps(map_state), encoding="utf-8")
        map_block_states = list(block_states)
        support_index = ((64 - box[1]) * 11
                         + (5 - box[2])) * 11 + (4 - box[0])
        map_block_states[support_index] = 1 << 4
        map_blocks_path = root / "map_source.bin"
        map_blocks_path.write_bytes(struct.pack("<484H", *map_block_states))
        map_capsule = root / "map_capsule"
        create_capsule(
            map_state_path, map_blocks_path, box, map_capsule,
            seed=7, source_engine="selftest", source_version="1",
        )
        map_manifest, _ = validate_capsule(map_capsule)
        assert len(map_manifest["map_payloads"]) == 1
        map_payload = map_manifest["map_payloads"][0]
        assert (map_capsule / map_payload["file"]).read_bytes() == map_raw
        map_events = magma_events(map_capsule)
        map_event = next(
            row for row in map_events if row["type"] == "set_item_frame")
        assert map_event["map_colors_file"] == map_payload["file"]
        map_sidecar = map_capsule / map_payload["file"]
        map_sidecar.write_bytes(map_raw[:-1] + bytes([map_raw[-1] ^ 1]))
        try:
            validate_capsule(map_capsule)
            raise AssertionError("corrupt map sidecar passed validation")
        except CapsuleError:
            pass
        map_sidecar.write_bytes(map_raw)
        llama_state = copy.deepcopy(state)
        llama_state["entities"].extend([
            {
                "eid": 90, "type": "EntityLlama", "loaded_order": 2,
                "uuid_most": -1070935975390360081,
                "uuid_least": -9141386507638288913,
                "x": 20.5, "y": 65.0, "z": 20.5,
                "dx": 19.0, "dy": 0.0, "dz": 23.0,
                "vx": 0.0, "vy": 0.0, "vz": 0.0,
                "yaw": 35.0, "pitch": 0.0, "health": 19.0,
                "hurt_time": 2, "death_time": 0,
                "hurt_resistant_time": 6, "no_ai": True,
                "horse_exact": True, "horse_subtype": "llama",
                "no_ai_base_exact": True,
                "air": 300, "fire": -1, "on_ground": False,
                "fall_distance": 0.0, "in_water": False,
                "ticks_existed": 37, "base_living_sound_time": 4,
                "base_last_damage": 0.0,
                "base_entity_seed48": 0x3456789ABCDE,
                "base_entity_have_gaussian": False,
                "base_entity_gaussian": 0.0,
                "mob_potions_empty": True, "mob_equipment_empty": True,
                "max_health": 20.0, "absorption": 0.0,
                "base_box_min_x": 20.05, "base_box_min_y": 65.0,
                "base_box_min_z": 20.05, "base_box_max_x": 20.95,
                "base_box_max_y": 66.87, "base_box_max_z": 20.95,
                "horse_growing_age": 0, "horse_forced_age": 0,
                "horse_forced_age_timer": 0, "horse_in_love": 0,
                "horse_tame": True, "horse_saddled": False,
                "horse_bred": False, "horse_eating": False,
                "horse_rearing": False, "horse_mouth_open": False,
                "horse_temper": 17, "horse_owner_present": True,
                "horse_owner_uuid_most": 11,
                "horse_owner_uuid_least": 12,
                "horse_variant": 3, "horse_armor": 0,
                "horse_chested": True, "horse_trap": False,
                "horse_trap_time": 0, "horse_jump_strength": 0.5,
                "horse_max_health_base": 20.0,
                "horse_movement_speed_base": 0.175,
                "horse_eating_counter": 0,
                "horse_open_mouth_counter": 0,
                "horse_jump_rearing_counter": 0,
                "horse_tail_counter": 0, "horse_sprint_counter": 0,
                "horse_gallop_time": 0, "horse_jumping": False,
                "horse_jump_power": 0.0,
                "horse_allow_stand_sliding": False,
                "horse_head_lean": 0.0, "horse_prev_head_lean": 0.0,
                "horse_rearing_amount": 0.0,
                "horse_prev_rearing_amount": 0.0,
                "horse_mouth_openness": 0.0,
                "horse_prev_mouth_openness": 0.0,
                "horse_prev_limb_amount": 0.0,
                "horse_limb_amount": 0.0, "horse_limb_swing": 0.0,
                "llama_strength": 2, "llama_decor": 12,
                "llama_did_spit": True, "llama_leashed": True,
                "llama_leash_holder_kind": 1,
                "llama_leash_holder_eid": 0,
                "llama_leash_holder_uuid_most":
                    int("a01e3843e5213998", 16) - (1 << 64),
                "llama_leash_holder_uuid_least":
                    int("958af459800e4d11", 16) - (1 << 64),
                "llama_leash_pending": False,
                "llama_leash_pending_x": 0,
                "llama_leash_pending_y": 0,
                "llama_leash_pending_z": 0,
                "llama_caravan_head_eid": -1,
                "llama_caravan_tail_eid": 88,
                "llama_caravan_speed": 2.0999999046325684,
                "llama_caravan_dist_counter": 0,
                "horse_inventory": [
                    {"slot": 1, "id": 171, "count": 1, "meta": 12,
                     "enchants": []},
                    {"slot": 2, "id": 264, "count": 3, "meta": 0,
                     "enchants": []},
                ],
            },
            {
                "eid": 89, "type": "EntityLlamaSpit",
                "loaded_order": 3,
                "uuid_most": 8152436061380546031,
                "uuid_least": 6999710690289384943,
                "x": 22.0, "y": 66.7, "z": 20.5,
                "dx": 20.5, "dy": 1.7, "dz": 23.0,
                "vx": 0.25, "vy": 0.125, "vz": -0.5,
                "yaw": 153.0, "pitch": 11.0, "health": -1.0,
                "ticks_existed": 17, "llama_spit_exact": True,
                "llama_spit_owner_eid": 90,
                "llama_spit_owner_uuid_present": True,
                "llama_spit_owner_uuid_most": -1070935975390360081,
                "llama_spit_owner_uuid_least": -9141386507638288913,
                "llama_spit_no_gravity": True,
            },
        ])
        follower = copy.deepcopy(llama_state["entities"][-2])
        follower.update({
            "eid": 88, "loaded_order": 4,
            "uuid_most": 1152921504606846977,
            "uuid_least": -8070450532247928831,
            "x": 24.5, "dx": 23.0,
            "base_box_min_x": 24.05, "base_box_max_x": 24.95,
            "llama_decor": -1,
            "llama_did_spit": False, "llama_leashed": False,
            "llama_leash_holder_kind": 0,
            "llama_leash_holder_eid": -1,
            "llama_leash_holder_uuid_most": 0,
            "llama_leash_holder_uuid_least": 0,
            "llama_caravan_head_eid": 90,
            "llama_caravan_tail_eid": -1,
            "horse_inventory": [],
        })
        llama_state["entities"].append(follower)
        llama_state["living_leashes"] = [{
            "eid": 90,
            "uuid_most": -1070935975390360081,
            "uuid_least": -9141386507638288913,
            "leashed": True,
            "holder_kind": 1,
            "holder_eid": 0,
            "holder_uuid_most":
                int("a01e3843e5213998", 16) - (1 << 64),
            "holder_uuid_least":
                int("958af459800e4d11", 16) - (1 << 64),
            "pending": False,
            "pending_x": 0, "pending_y": 0, "pending_z": 0,
            "wolf_angry": False,
        }]
        llama_state_path = root / "llama_state.json"
        llama_state_path.write_text(
            json.dumps(llama_state), encoding="utf-8")
        llama_capsule = root / "llama_capsule"
        create_capsule(
            llama_state_path, blocks_path, box, llama_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        llama_events = magma_events(llama_capsule)
        assert any(
            row["type"] == "spawn_llama_fixture"
            and row["eid"] == 90 and row["strength"] == 2
            and row["decor"] == 12 and row["chested"] == 1
            for row in llama_events
        )
        assert any(
            row["type"] == "spawn_llama_spit_fixture"
            and row["eid"] == 89 and row["owner_eid"] == 90
            and row["ticks_existed"] == 17 and row["no_gravity"] == 1
            for row in llama_events
        )
        assert [
            (row["eid"], row["leash_holder_kind"],
             row["caravan_head_eid"], row["caravan_tail_eid"])
            for row in llama_events
            if row["type"] == "restore_llama_links"
        ] == [(90, 0, -1, 88), (88, 0, 90, -1)]
        assert any(
            row["type"] == "restore_living_leash"
            and row["eid"] == 90 and row["holder_kind"] == 1
            and row["holder_eid"] == 0
            for row in llama_events
        )
        golem_state = copy.deepcopy(state)
        golem_state["entities"].append({
            "eid": 90, "type": "EntityIronGolem", "loaded_order": 2,
            "x": 2.5, "y": 65.0, "z": 2.5,
            "dx": 1.0, "dy": 0.0, "dz": 1.0,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "yaw": 15.0, "pitch": 0.0, "health": 87.0,
            "hurt_time": 2, "death_time": 0,
            "hurt_resistant_time": 6, "no_ai": True,
            "no_ai_plain_exact": True, "no_ai_base_exact": True,
            "air": 300, "fire": -1, "on_ground": False,
            "fall_distance": 0.0, "in_water": False,
            "ticks_existed": 22, "base_living_sound_time": 4,
            "base_last_damage": 0.0,
            "base_entity_seed48": 0x23456789ABCD,
            "base_entity_have_gaussian": False,
            "base_entity_gaussian": 0.0,
            "mob_potions_empty": True, "mob_equipment_empty": True,
            "max_health": 100.0, "absorption": 0.0,
            "base_box_min_x": 1.8, "base_box_min_y": 65.0,
            "base_box_min_z": 1.8, "base_box_max_x": 3.2,
            "base_box_max_y": 67.7, "base_box_max_z": 3.2,
            "golem_player_created": True, "golem_home_timer": 83,
            "golem_attack_timer": 7, "golem_rose_timer": 321,
        })
        golem_state_path = root / "golem_state.json"
        golem_state_path.write_text(
            json.dumps(golem_state), encoding="utf-8")
        golem_capsule = root / "golem_capsule"
        create_capsule(
            golem_state_path, blocks_path, box, golem_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        golem_events = magma_events(golem_capsule)
        assert any(
            row == {
                "tick": 0, "type": "restore_iron_golem_state",
                "eid": 90, "player_created": 1,
                "home_timer": 83, "attack_timer": 7,
                "rose_timer": 321,
            }
            for row in golem_events
        )
        armor_stand_state = copy.deepcopy(state)
        armor_stand_state["entities"].append({
            "eid": 90, "type": "EntityArmorStand", "loaded_order": 2,
            "uuid_most": -17, "uuid_least": 23,
            "x": 2.5, "y": 65.0, "z": 2.5,
            "dx": 1.0, "dy": 0.0, "dz": 5.0,
            "vx": 0.125, "vy": -0.25, "vz": 0.375,
            "yaw": 67.5, "pitch": -11.25, "health": 19.5,
            "hurt_time": 0, "death_time": 0,
            "hurt_resistant_time": 0,
            "armor_stand_exact": True,
            "armor_stand_small": True,
            "armor_stand_show_arms": True,
            "armor_stand_no_base_plate": True,
            "armor_stand_marker": False,
            "armor_stand_no_gravity": True,
            "armor_stand_invisible": False,
            "armor_stand_disabled_slots": 0x10204,
            "armor_stand_air": 300,
            "armor_stand_in_water": False,
            "armor_stand_on_ground": True,
            "armor_stand_fall_distance": 0.5,
            "armor_stand_fire": -1,
            "armor_stand_ticks_existed": 37,
            "armor_stand_punch_cooldown": 29,
            "armor_stand_last_damage": 0.0,
            "armor_stand_entity_seed48": 0x123456789ABC,
            "armor_stand_entity_have_gaussian": True,
            "armor_stand_entity_gaussian": -0.375,
            "armor_stand_absorption": 2.5,
            "armor_stand_max_health": 24.0,
            "armor_stand_max_health_base": 24.0,
            "armor_stand_revenge_timer": 31,
            "armor_stand_portal_cooldown": 7,
            "armor_stand_custom_name": "Sentinel",
            "armor_stand_custom_name_visible": True,
            "armor_stand_silent": True,
            "armor_stand_glowing": True,
            "armor_stand_invulnerable": True,
            "armor_stand_update_blocked": False,
            "armor_stand_fall_flying": False,
            "armor_stand_vehicle_eid": -1,
            "armor_stand_tags": ["guard", "west"],
            "armor_stand_effects": [
                {"id": 10, "amp": 1, "dur": 80,
                 "ambient": False, "show_particles": True},
            ],
            "armor_stand_equipment": [
                {"slot": 0, "id": 276, "count": 1, "meta": 7,
                 "enchants": [[16, 5]]},
                {"slot": 4, "id": 311, "count": 1, "meta": 3,
                 "enchants": []},
            ],
            "armor_stand_pose": [
                [1.0, 2.0, 3.0], [4.0, 5.0, 6.0],
                [7.0, 8.0, 9.0], [10.0, 11.0, 12.0],
                [13.0, 14.0, 15.0], [16.0, 17.0, 18.0],
            ],
        })
        armor_stand_path = root / "armor_stand_state.json"
        armor_stand_path.write_text(
            json.dumps(armor_stand_state), encoding="utf-8")
        armor_stand_capsule = root / "armor_stand_capsule"
        create_capsule(
            armor_stand_path, blocks_path, box, armor_stand_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        armor_stand_events = magma_events(armor_stand_capsule)
        armor_stand_spawn = next(
            row for row in armor_stand_events
            if row["type"] == "spawn_armor_stand_fixture")
        assert armor_stand_spawn["eid"] == 90 \
            and armor_stand_spawn["status"] == 13 \
            and armor_stand_spawn["no_gravity"] == 1
        assert any(
            row["type"] == "set_armor_stand_generic_state"
            and row["absorption"] == 2.5
            and row["max_health_base"] == 24.0
            and row["portal_cooldown"] == 7
            and row["invulnerable"] == 1
            for row in armor_stand_events
        )
        assert any(
            row["type"] == "set_armor_stand_custom_name"
            and row["name"] == "Sentinel"
            for row in armor_stand_events
        )
        assert [
            row["tag"] for row in armor_stand_events
            if row["type"] == "add_armor_stand_tag"
        ] == ["guard", "west"]
        assert any(
            row["type"] == "add_armor_stand_effect"
            and row["id"] == 10 and row["duration"] == 80
            for row in armor_stand_events
        )
        assert [
            row["part"] for row in armor_stand_events
            if row["type"] == "set_armor_stand_pose"
        ] == [0, 1, 2, 3, 4, 5]
        assert [
            (row["slot"], row["item"])
            for row in armor_stand_events
            if row["type"] == "set_armor_stand_equipment"
        ] == [(0, 276), (4, 311)]
        village_state = copy.deepcopy(state)
        village_state["villages"] = [{
            "population": 1, "radius": 32, "golems": 0,
            "stable": 40, "state_tick": 41, "mating_tick": 0,
            "center_x": 5, "center_y": 65, "center_z": -5,
            "helper_x": 5, "helper_y": 65, "helper_z": -5,
            "doors": [{
                "x": 5, "y": 65, "z": -5,
                "inside_dx": 2, "inside_dz": 0, "timestamp": 40,
            }],
            "reputations": [{
                "uuid_most_hex": "0123456789abcdef",
                "uuid_least_hex": "fedcba9876543210", "score": -7,
            }],
        }]
        village_state_path = root / "village_state.json"
        village_state_path.write_text(
            json.dumps(village_state), encoding="utf-8")
        village_block_states = list(block_states)
        village_door_index = (
            ((65 - box[1]) * 11 + (-5 - box[2])) * 11
            + (5 - box[0]))
        village_block_states[village_door_index] = 64 << 4
        village_blocks_path = root / "village_blocks.bin"
        village_blocks_path.write_bytes(
            struct.pack("<484H", *village_block_states))
        village_capsule = root / "village_capsule"
        create_capsule(
            village_state_path, village_blocks_path, box, village_capsule,
            seed=7, source_engine="selftest", source_version="1",
        )
        village_events = magma_events(village_capsule)
        assert any(
            row["type"] == "restore_village_collection"
            and row["collection_tick"] == 41 and row["count"] == 1
            for row in village_events
        )
        assert any(
            row["type"] == "restore_village_reputation"
            and row["most_hi"] == 0x01234567
            and row["least_lo"] == 0x76543210
            and row["score"] == -7
            for row in village_events
        )
        village_with_golem = copy.deepcopy(village_state)
        village_with_golem["villages"][0]["population"] = 20
        village_with_golem["villages"][0]["golems"] = 1
        _validate_state(village_with_golem)
        try:
            create_capsule(
                village_state_path, blocks_path, box,
                root / "village_without_door_capsule",
                seed=7, source_engine="selftest", source_version="1",
            )
        except CapsuleError as exc:
            assert "not a represented wooden door" in str(exc)
        else:
            raise AssertionError("unrepresented saved door passed admission")
        assert any(
            row == {
                "tick": 0, "type": "set_inventory", "slot": 2,
                "item": 1, "count": 3, "meta": 0,
                "n_ench": 1, "e0": (16 << 16) | 5,
                "repair_cost": 7, "custom_name": "Oracle Stone",
            }
            for row in events
        )
        assert any(
            row == {
                "tick": 0, "type": "set_player_xp",
                "level": 2, "fraction": 0.25, "total": 18,
            }
            for row in events
        )
        transient_state = copy.deepcopy(state)
        transient_state["entities"].extend(({
            "eid": 93, "type": "EntityTNTPrimed", "loaded_order": 2,
            "uuid_most": -7, "uuid_least": 5601,
            "x": -2.5, "y": 65.5, "z": 1.5,
            "dx": -4.0, "dy": 0.5, "dz": 4.0,
            "vx": 0.0625, "vy": -0.125, "vz": -0.03125,
            "yaw": 0.0, "pitch": 0.0, "health": -1.0,
            "fuse": 40, "primed_tnt_exact": True,
        }, {
            "eid": 94, "type": "EntityFallingBlock", "loaded_order": 3,
            "uuid_most": -7, "uuid_least": 5602,
            "x": 5.5, "y": 65.5, "z": 1.5,
            "dx": 4.0, "dy": 0.5, "dz": 4.0,
            "vx": -0.0625, "vy": -0.125, "vz": 0.03125,
            "yaw": 0.0, "pitch": 0.0, "health": -1.0,
            "block": 12, "meta": 0, "fall_time": 5,
            "origin_x": 0, "origin_y": 0, "origin_z": 0,
            "falling_exact": True,
        }))
        transient_path = root / "transient_state.json"
        transient_path.write_text(
            json.dumps(transient_state), encoding="utf-8")
        transient_capsule = root / "transient_capsule"
        create_capsule(
            transient_path, blocks_path, box, transient_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        transient_events = magma_events(transient_capsule)
        assert next(
            row for row in transient_events
            if row["type"] == "spawn_primed_tnt_fixture"
        )["fuse"] == 40
        falling_spawn = next(
            row for row in transient_events
            if row["type"] == "spawn_falling_fixture")
        assert falling_spawn["block"] == 12 \
            and falling_spawn["fall_time"] == 5
        assert next(
            row for row in transient_events
            if row["type"] == "restore_falling_origin"
        )["x"] == 0
        assert [
            row["eid"] for row in transient_events
            if row["type"] == "restore_transient_entity_uuid"
        ] == [93, 94]
        invalid_transient = copy.deepcopy(transient_state)
        invalid_transient["entities"][-2]["fuse"] = 0
        try:
            _validate_state(invalid_transient)
        except CapsuleError as exc:
            assert "invalid exact primed-TNT state" in str(exc)
        else:
            raise AssertionError("invalid exact primed TNT was accepted")
        arrow_state = copy.deepcopy(state)
        arrow_state["entities"].append({
            "eid": 90, "type": "EntityTippedArrow",
            "loaded_order": 2,
            "x": 2.5, "y": 66.25, "z": -1.5,
            "dx": 1.0, "dy": 1.25, "dz": 1.0,
            "vx": 0.75, "vy": -0.125, "vz": 0.25,
            "yaw": 71.5, "pitch": -8.25, "health": -1.0,
            "arrow_exact": True,
            "ticks_in_air": 7, "fire_ticks": 1993,
            "damage": 4.5, "knockback": 2,
            "critical": True, "pickup_status": 1,
            "in_ground": False, "shake": 0,
            "ticks_in_ground": 0, "time_in_ground": 0,
            "tile_x": -1, "tile_y": -1, "tile_z": -1,
            "tile_block": 0, "tile_meta": 0,
            "entity_seed48": 0x23456789ABCD,
            "entity_have_gaussian": True,
            "entity_gaussian": -0.375,
            "arrow_kind": 0, "potion_type": 0,
            "spectral_duration": 200, "arrow_color": -1,
            "arrow_custom_color": False, "arrow_effects": [],
            "pickup_item": 262, "pickup_meta": 0,
        })
        arrow_state_path = root / "arrow_state.json"
        arrow_state_path.write_text(
            json.dumps(arrow_state), encoding="utf-8")
        arrow_capsule = root / "arrow_capsule"
        create_capsule(
            arrow_state_path, blocks_path, box, arrow_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        arrow_spawn = next(
            row for row in magma_events(arrow_capsule)
            if row["type"] == "spawn_player_arrow_state_fixture"
        )
        assert arrow_spawn == {
            "tick": 0,
            "type": "spawn_player_arrow_state_fixture",
            "eid": 90,
            "x": 2.5, "y": 66.25, "z": -1.5,
            "vx": 0.75, "vy": -0.125, "vz": 0.25,
            "yaw": 71.5, "pitch": -8.25,
            "ticks_in_air": 7, "fire_ticks": 1993,
            "damage": 4.5, "knockback": 2,
            "critical": 1, "pickup_status": 1,
            "in_ground": 0, "shake": 0,
            "ticks_in_ground": 0,
            "tile_x": -1, "tile_y": -1, "tile_z": -1,
            "tile_block": 0, "tile_meta": 0,
            "random_seed48": 0x23456789ABCD,
            "random_have_gaussian": 1,
            "random_next_gaussian": -0.375,
        }
        arrow_payload = next(
            row for row in magma_events(arrow_capsule)
            if row["type"] == "set_arrow_payload_fixture"
        )
        assert arrow_payload["eid"] == 90 \
            and arrow_payload["arrow_kind"] == 0 \
            and arrow_payload["effect_count"] == 0 \
            and arrow_payload["time_in_ground"] == 0 \
            and arrow_payload["pickup_item"] == 262
        invalid_arrow = copy.deepcopy(arrow_state)
        invalid_arrow["entities"][-1]["pickup_status"] = 3
        try:
            _validate_state(invalid_arrow)
        except CapsuleError as exc:
            assert "invalid exact arrow state" in str(exc)
        else:
            raise AssertionError("invalid exact arrow state was accepted")
        item_state = copy.deepcopy(state)
        item_tag_hex = nbt_codec.encode_hex({
            "name": "",
            "tag": {"type": "compound", "value": {
                "netherite_test": {"type": "compound", "value": {
                    "answer": {"type": "int", "value": 42},
                    "label": {"type": "string", "value": "opaque"},
                }},
            }},
        })
        item_state["entities"].append({
            "eid": 0, "type": "EntityItem", "loaded_order": 2,
            "uuid_most": -1, "uuid_least": 20737,
            "x": -3.5, "y": 65.0, "z": 1.5,
            "dx": -5.0, "dy": 0.0, "dz": 4.0,
            "vx": 0.125, "vy": 0.25, "vz": -0.125,
            "yaw": 123.5, "pitch": 0.0, "health": 4,
            "item_exact": True, "item": 264, "count": 3,
            "meta": 0, "age": 37, "ticks_existed": 24,
            "pickup_delay": 9,
            "lifespan": 6000, "hover_start": 1.25,
            "on_ground": False, "no_gravity": False,
            "fire": -1, "in_water": False, "first_update": False,
            "entity_seed48": 0x123456789ABC,
            "stack_payload": {"kind": "item_tag", "nbt": item_tag_hex},
        })
        item_state_path = root / "item_state.json"
        item_state_path.write_text(
            json.dumps(item_state), encoding="utf-8")
        item_capsule = root / "item_capsule"
        create_capsule(
            item_state_path, blocks_path, box, item_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        item_spawn = next(
            row for row in magma_events(item_capsule)
            if row["type"] == "spawn_item_state_fixture"
        )
        assert item_spawn == {
            "tick": 0, "type": "spawn_item_state_fixture",
            "eid": 0, "x": -3.5, "y": 65.0, "z": 1.5,
            "vx": 0.125, "vy": 0.25, "vz": -0.125,
            "yaw": 123.5, "hover_start": 1.25,
            "item": 264, "count": 3, "meta": 0,
            "age": 37, "ticks_existed": 24,
            "pickup_delay": 9, "health": 4,
            "lifespan": 6000, "on_ground": 0, "no_gravity": 0,
            "fire": -1, "in_water": 0, "first_update": 0,
            "entity_seed48": 0x123456789ABC,
            "nbt_file": "item_stack_entity_0002.nbt",
        }
        assert any(
            row == {
                "tick": 0, "type": "restore_item_entity_uuid",
                "eid": 0, "most": -1, "least": 20737,
            }
            for row in magma_events(item_capsule)
        )
        incomplete_uuid = copy.deepcopy(item_state)
        del incomplete_uuid["entities"][-1]["uuid_least"]
        try:
            _validate_state(incomplete_uuid)
        except CapsuleError as exc:
            assert "incomplete UUID" in str(exc)
        else:
            raise AssertionError("incomplete entity UUID was accepted")
        invalid_item = copy.deepcopy(item_state)
        invalid_item["entities"][-1]["stack_payload"] = {
            "kind": "item_tag", "nbt": "00",
        }
        try:
            _validate_state(invalid_item)
        except CapsuleError as exc:
            assert "invalid NBT" in str(exc)
        else:
            raise AssertionError("malformed tagged exact item was accepted")
        unsupported_stack_state = copy.deepcopy(state)
        unsupported_stack_state["inventory"][0]["nbt_subset_exact"] = False
        try:
            _validate_state(unsupported_stack_state)
        except CapsuleError as exc:
            assert "unsupported ItemStack NBT" in str(exc)
        else:
            raise AssertionError("unsupported player ItemStack NBT was accepted")
        tagged_stack_state = copy.deepcopy(unsupported_stack_state)
        tagged_stack_state["inventory"][0]["stack_payload"] = {
            "kind": "item_tag", "nbt": item_tag_hex,
        }
        tagged_stack_path = root / "tagged_stack_state.json"
        tagged_stack_path.write_text(
            json.dumps(tagged_stack_state), encoding="utf-8")
        tagged_stack_capsule = root / "tagged_stack_capsule"
        create_capsule(
            tagged_stack_path, blocks_path, box, tagged_stack_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        tagged_inventory_event = next(
            row for row in magma_events(tagged_stack_capsule)
            if row["type"] == "set_inventory" and row["slot"] == 2
            and row["count"] == 3)
        assert tagged_inventory_event["nbt_file"] \
            == "item_stack_inventory_0000.nbt"
        tagged_ender_state = copy.deepcopy(state)
        tagged_ender_state["ender_inventory"][0]["stack_payload"] = {
            "kind": "item_tag", "nbt": item_tag_hex,
        }
        tagged_ender_path = root / "tagged_ender_state.json"
        tagged_ender_path.write_text(
            json.dumps(tagged_ender_state), encoding="utf-8")
        tagged_ender_capsule = root / "tagged_ender_capsule"
        create_capsule(
            tagged_ender_path, blocks_path, box, tagged_ender_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        tagged_ender_event = next(
            row for row in magma_events(tagged_ender_capsule)
            if row["type"] == "set_ender_inventory"
            and row["slot"] == 7 and row["count"] == 5)
        assert tagged_ender_event["nbt_file"] \
            == "item_stack_ender_inventory_0000.nbt"
        assert any(
            row == {
                "tick": 0, "type": "set_player_combat",
                "attack_ticks": 3, "hurt_time": 4,
                "hurt_resistant_time": 7, "death_time": 0,
                "dead": 0, "deaths": 0,
            }
            for row in events
        )
        assert [
            row for row in events if row["type"] == "player_potion_add"
        ] == [
            {"tick": 0, "type": "player_potion_add",
             "id": 1, "amplifier": 0, "duration": 10,
             "ambient": 0, "show_particles": 1},
            {"tick": 0, "type": "player_potion_add",
             "id": 16, "amplifier": 1, "duration": 20,
             "ambient": 1, "show_particles": 0},
        ]
        assert any(
            row == {
                "tick": 0, "type": "set_inventory", "slot": 38,
                "item": 311, "count": 1, "meta": 7,
                "n_ench": 1, "e0": 4,
            }
            for row in events
        )
        assert any(
            row == {
                "tick": 0, "type": "set_inventory", "slot": 40,
                "item": 442, "count": 1, "meta": 5,
            }
            for row in events
        )
        assert any(
            row == {
                "tick": 0,
                "type": "redstone_torch_toggle",
                "x": 1,
                "y": 65,
                "z": -1,
                "time": 40,
            }
            for row in events
        )
        potion_cloud_state = copy.deepcopy(state)
        potion_cloud_state["entities"].extend([
            {
                "eid": 90, "type": "EntityPotion",
                "loaded_order": 2,
                "x": 12.5, "y": 100.0, "z": 8.5,
                "dx": 11.0, "dy": 35.0, "dz": 11.0,
                "vx": 0.25, "vy": 0.5, "vz": -0.125,
                "yaw": 0.0, "pitch": 0.0, "health": -1.0,
                "potion_exact": True,
                "potion_item": 438, "potion_type": 24, "age": 7,
                "potion_color": 4393481,
                "potion_custom_color": False,
                "potion_effects": [],
                "player_thrower": True, "ignore_player": True,
                "ignore_player_time": 2,
            },
            {
                "eid": 89, "type": "EntityAreaEffectCloud",
                "loaded_order": 3,
                "x": 8.5, "y": 100.0, "z": 8.5,
                "dx": 7.0, "dy": 35.0, "dz": 11.0,
                "vx": 0.375, "vy": -0.0625, "vz": -0.21875,
                "yaw": 31.25, "pitch": -12.5,
                "prev_yaw": 29.75, "prev_pitch": -11.0,
                "health": -1.0,
                "uuid_most": 4660, "uuid_least": 22136,
                "cloud_exact": True, "cloud_common_exact": True,
                "potion_type": 16, "age": 9, "duration": 600,
                "duration_on_use": -25,
                "potion_color": 8171462,
                "potion_custom_color": False,
                "potion_effects": [],
                "wait_time": 10, "reapplication_delay": 20,
                "radius": 3.0, "radius_on_use": -0.5,
                "radius_per_tick": -0.005,
                "next_application": 12, "player_owner": True,
                "owner_present": True, "owner_eid": 0,
                "owner_uuid_most": -6909022914081637992,
                "owner_uuid_least": -7671050350034531055,
                "reapplication_deadlines": [
                    {"eid": 0, "deadline": 12},
                    {"eid": 91, "deadline": 33},
                ],
                "ignore_radius": True, "particle": 37,
                "particle_param1": 1, "particle_param2": 73,
                "dimension": 0, "air": 123, "fire": 17,
                "portal_cooldown": 7,
                "on_ground": True, "no_gravity": True,
                "invulnerable": True, "silent": True,
                "glowing": True, "update_blocked": False,
                "in_water": False, "first_update": True,
                "fall_distance": 3.25,
                "prev_x": 6.5, "prev_y": 97.0, "prev_z": 4.5,
                "last_tick_x": 3.5, "last_tick_y": 94.0,
                "last_tick_z": 1.5,
                "server_entity_seed48": 0x1234ABCD5678,
                "server_entity_have_gaussian": True,
                "server_entity_gaussian": -0.78125,
            },
        ])
        potion_cloud_state_path = root / "potion_cloud_state.json"
        potion_cloud_state_path.write_text(
            json.dumps(potion_cloud_state), encoding="utf-8")
        potion_cloud_capsule = root / "potion_cloud_capsule"
        create_capsule(
            potion_cloud_state_path, blocks_path, box,
            potion_cloud_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        potion_cloud_events = [
            row for row in magma_events(potion_cloud_capsule)
            if row["type"] in {
                "spawn_xp_fixture", "spawn_mob_fixture",
                "spawn_potion_fixture",
                "spawn_area_effect_cloud_fixture",
            }
        ]
        assert [row["type"] for row in potion_cloud_events] == [
            "spawn_xp_fixture", "spawn_mob_fixture",
            "spawn_potion_fixture", "spawn_area_effect_cloud_fixture",
        ]
        assert potion_cloud_events[2] == {
            "tick": 0, "type": "spawn_potion_fixture",
            "eid": 90, "potion_item": 438, "potion_type": 24,
            "x": 12.5, "y": 100.0, "z": 8.5,
            "vx": 0.25, "vy": 0.5, "vz": -0.125, "age": 7,
            "player_thrower": 1, "ignore_player": 1,
            "ignore_player_time": 2,
        }
        assert potion_cloud_events[3] == {
            "tick": 0, "type": "spawn_area_effect_cloud_fixture",
            "eid": 89, "potion_type": 16,
            "x": 8.5, "y": 100.0, "z": 8.5,
            "vx": 0.375, "vy": -0.0625, "vz": -0.21875,
            "yaw": 31.25, "pitch": -12.5,
            "prev_yaw": 29.75, "prev_pitch": -11.0,
            "age": 9, "duration": 600, "duration_on_use": -25,
            "wait_time": 10,
            "reapplication_delay": 20,
            "radius": 3.0, "radius_on_use": -0.5,
            "radius_per_tick": -0.005, "next_application": 12,
            "player_owner": 1, "ignore_radius": 1,
            "particle": 37, "particle_param1": 1,
            "particle_param2": 73,
        }
        assert [
            row for row in magma_events(potion_cloud_capsule)
            if row["type"] == "set_area_effect_cloud_common_state"
        ] == [{
            "tick": 0,
            "type": "set_area_effect_cloud_common_state",
            "cloud_eid": 89, "dimension": 0, "air": 123,
            "fire": 17, "portal_cooldown": 7,
            "on_ground": 1, "no_gravity": 1, "invulnerable": 1,
            "silent": 1, "glowing": 1, "update_blocked": 0,
            "in_water": 0, "first_update": 1,
            "fall_distance": 3.25,
            "prev_x": 6.5, "prev_y": 97.0, "prev_z": 4.5,
            "last_tick_x": 3.5, "last_tick_y": 94.0,
            "last_tick_z": 1.5,
            "server_entity_seed48": 0x1234ABCD5678,
            "server_entity_have_gaussian": 1,
            "server_entity_gaussian": -0.78125,
        }]
        assert [
            row for row in magma_events(potion_cloud_capsule)
            if row["type"] == "set_area_effect_cloud_identity"
        ] == [{
            "tick": 0, "type": "set_area_effect_cloud_identity",
            "cloud_eid": 89, "uuid_most": 4660, "uuid_least": 22136,
            "owner_present": 1, "owner_eid": 0,
            "owner_uuid_most": -6909022914081637992,
            "owner_uuid_least": -7671050350034531055,
        }]
        assert [
            row for row in magma_events(potion_cloud_capsule)
            if row["type"] == "set_area_effect_cloud_deadline"
        ] == [
            {
                "tick": 0, "type": "set_area_effect_cloud_deadline",
                "cloud_eid": 89, "target_eid": 0, "deadline": 12,
            },
            {
                "tick": 0, "type": "set_area_effect_cloud_deadline",
                "cloud_eid": 89, "target_eid": 91, "deadline": 33,
            },
        ]
        invalid_potion_state = copy.deepcopy(potion_cloud_state)
        invalid_potion_state["entities"][2]["player_thrower"] = False
        try:
            _validate_state(invalid_potion_state)
        except CapsuleError as exc:
            assert "invalid exact thrown-potion state" in str(exc)
        else:
            raise AssertionError(
                "ignored non-player potion passed capsule validation")
        invalid_cloud_state = copy.deepcopy(potion_cloud_state)
        invalid_cloud_state["entities"][3]["radius"] = 0.49
        try:
            _validate_state(invalid_cloud_state)
        except CapsuleError as exc:
            assert "invalid exact area-effect-cloud state" in str(exc)
        else:
            raise AssertionError(
                "undersized area-effect cloud passed capsule validation")
        invalid_cloud_target = copy.deepcopy(potion_cloud_state)
        invalid_cloud_target["entities"][3][
            "reapplication_deadlines"][1]["eid"] = 92
        try:
            _validate_state(invalid_cloud_target)
        except CapsuleError as exc:
            assert "not an exact restored living entity" in str(exc)
        else:
            raise AssertionError(
                "nonliving cloud deadline target passed capsule validation")
        invalid_cloud_owner = copy.deepcopy(potion_cloud_state)
        invalid_cloud_owner["entities"][3]["owner_uuid_least"] += 1
        try:
            _validate_state(invalid_cloud_owner)
        except CapsuleError as exc:
            assert "cloud owner is not an exact" in str(exc)
        else:
            raise AssertionError(
                "mismatched cloud owner UUID passed capsule validation")
        invalid_marker_state = copy.deepcopy(potion_cloud_state)
        invalid_marker_state["entities"][0]["potion_exact"] = True
        try:
            _validate_state(invalid_marker_state)
        except CapsuleError as exc:
            assert "potion_exact requires EntityPotion" in str(exc)
        else:
            raise AssertionError(
                "potion exact marker passed on a non-potion entity")
        throwable_state = copy.deepcopy(state)
        throwable_state["entities"].append({
            "eid": 94, "type": "EntityEnderPearl", "loaded_order": 2,
            "uuid_most": -17, "uuid_least": 29,
            "x": 12.5, "y": 200.0, "z": -18.5,
            "dx": 11.0, "dy": 135.0, "dz": -16.0,
            "vx": 0.25, "vy": -0.125, "vz": 0.5,
            "yaw": 33.5, "pitch": -14.25, "health": -1.0,
            "throwable_exact": True, "age": 7, "ticks_in_air": 7,
            "prev_yaw": 31.25, "prev_pitch": -12.5,
            "player_thrower": False, "thrower_player_pending": True,
            "ignore_player": False,
            "ignore_player_time": 0, "pearl_private_thrower": False,
            "throwable_shake": 2, "in_ground": True,
            "ticks_in_ground": 119, "tile_x": 12, "tile_y": 199,
            "tile_z": -19, "tile_block": 1,
            "portal_counter": 1, "in_portal": False,
            "portal_cooldown": 13,
            "last_portal_pos_valid": True,
            "last_portal_x": 12, "last_portal_y": 199,
            "last_portal_z": -19,
            "last_portal_vec_x": 0.25,
            "last_portal_vec_y": 0.75,
            "teleport_direction": 3,
            "client_random_valid": True,
            "client_entity_seed48": 0x23456789ABCD,
            "entity_seed48": 0x123456789ABC,
            "entity_have_gaussian": True,
            "entity_gaussian": -0.125,
        })
        throwable_state_path = root / "throwable_state.json"
        throwable_state_path.write_text(
            json.dumps(throwable_state), encoding="utf-8")
        throwable_capsule = root / "throwable_capsule"
        create_capsule(
            throwable_state_path, blocks_path, box, throwable_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        throwable_events = magma_events(throwable_capsule)
        throwable_spawn = next(
            row for row in throwable_events
            if row["type"] == "spawn_throwable_state_fixture")
        assert throwable_spawn == {
            "tick": 0, "type": "spawn_throwable_state_fixture",
            "eid": 94, "projectile_type": 12,
            "potion_item": 0, "potion_type": 0,
            "x": 12.5, "y": 200.0, "z": -18.5,
            "vx": 0.25, "vy": -0.125, "vz": 0.5,
            "yaw": 33.5, "pitch": -14.25,
            "prev_yaw": 31.25, "prev_pitch": -12.5,
            "age": 7, "ticks_in_air": 7,
            "player_thrower": 0, "thrower_player_pending": 1,
            "ignore_player": 0,
            "ignore_player_time": 0, "pearl_private_thrower": 0,
            "throwable_shake": 2, "in_ground": 1,
            "ticks_in_ground": 119, "tile_x": 12, "tile_y": 199,
            "tile_z": -19, "tile_block": 1,
            "portal_counter": 1, "in_portal": 0,
            "portal_cooldown": 13,
            "last_portal_pos_valid": 1,
            "last_portal_x": 12, "last_portal_y": 199,
            "last_portal_z": -19,
            "last_portal_vec_x": 0.25,
            "last_portal_vec_y": 0.75,
            "teleport_direction": 3,
            "client_random_valid": 1,
            "client_entity_seed48": 0x23456789ABCD,
            "random_seed48": 0x123456789ABC,
            "random_have_gaussian": 1,
            "random_next_gaussian": -0.125,
        }
        assert any(
            row == {
                "tick": 0, "type": "restore_transient_entity_uuid",
                "eid": 94, "most": -17, "least": 29,
            }
            for row in throwable_events
        )
        invalid_throwable = copy.deepcopy(throwable_state)
        invalid_throwable["entities"][-1][
            "pearl_private_thrower"] = False
        invalid_throwable["entities"][-1]["ignore_player"] = True
        invalid_throwable["entities"][-1]["player_thrower"] = False
        try:
            _validate_state(invalid_throwable)
        except CapsuleError as exc:
            assert "invalid exact throwable state" in str(exc)
        else:
            raise AssertionError(
                "invalid exact throwable owner state was accepted")
        firework_item_tag_hex = nbt_codec.encode_hex({
            "name": "",
            "tag": {
                "type": "compound",
                "value": {
                    "Fireworks": {
                        "type": "compound",
                        "value": {
                            "Explosions": {
                                "type": "list",
                                "element_type": "compound",
                                "value": [{
                                    "type": "compound",
                                    "value": {
                                        "Colors": {
                                            "type": "int_array",
                                            "value": [0x112233, 0xABCDEF],
                                        },
                                        "FadeColors": {
                                            "type": "int_array",
                                            "value": [0x654321],
                                        },
                                        "Flicker": {
                                            "type": "byte", "value": 1,
                                        },
                                        "Trail": {
                                            "type": "byte", "value": 1,
                                        },
                                        "Type": {
                                            "type": "byte", "value": 1,
                                        },
                                    },
                                }],
                            },
                            "Flight": {"type": "byte", "value": 1},
                        },
                    },
                },
            },
        })
        firework_state = copy.deepcopy(state)
        firework_state["entities"].append({
            "eid": 95, "type": "EntityFireworkRocket",
            "loaded_order": 2,
            "uuid_most": -31, "uuid_least": 47,
            "x": 5.25, "y": 104.5, "z": -7.75,
            "dx": 3.0, "dy": 39.5, "dz": -5.0,
            "vx": 0.125, "vy": 0.25, "vz": -0.375,
            "yaw": 153.5, "pitch": -27.25, "health": -1.0,
            "firework_exact": True,
            "firework_age": 4, "lifetime": 30,
            "ticks_existed": 7,
            "prev_yaw": 150.25, "prev_pitch": -25.5,
            "attached_player": False,
            "flight": 1, "explosion_count": 1,
            "large_blast": True, "twinkle": True,
            "firework_item_present": True,
            "firework_item": 401, "firework_count": 1,
            "firework_meta": 0,
            "entity_seed48": 0x3456789ABCDE,
            "entity_have_gaussian": True,
            "entity_gaussian": 0.375,
            "stack_payload": {
                "kind": "item_tag", "nbt": firework_item_tag_hex,
            },
        })
        firework_state_path = root / "firework_state.json"
        firework_state_path.write_text(
            json.dumps(firework_state), encoding="utf-8")
        firework_capsule = root / "firework_capsule"
        create_capsule(
            firework_state_path, blocks_path, box, firework_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        firework_events = magma_events(firework_capsule)
        assert next(
            row for row in firework_events
            if row["type"] == "spawn_firework_state_fixture"
        ) == {
            "tick": 0, "type": "spawn_firework_state_fixture",
            "eid": 95,
            "x": 5.25, "y": 104.5, "z": -7.75,
            "vx": 0.125, "vy": 0.25, "vz": -0.375,
            "yaw": 153.5, "pitch": -27.25,
            "prev_yaw": 150.25, "prev_pitch": -25.5,
            "age": 4, "lifetime": 30, "ticks_existed": 7,
            "attached_player": 0,
            "flight": 1, "explosion_count": 1,
            "large_blast": 1, "twinkle": 1,
            "firework_item_present": 1,
            "firework_item": 401, "firework_count": 1,
            "firework_meta": 0,
            "entity_seed48": 0x3456789ABCDE,
            "entity_have_gaussian": 1,
            "entity_gaussian": 0.375,
            "nbt_file": "item_stack_entity_0002.nbt",
        }
        assert any(
            row == {
                "tick": 0, "type": "restore_transient_entity_uuid",
                "eid": 95, "most": -31, "least": 47,
            }
            for row in firework_events
        )
        invalid_firework = copy.deepcopy(firework_state)
        invalid_firework["entities"][-1]["firework_age"] = 31
        try:
            _validate_state(invalid_firework)
        except CapsuleError as exc:
            assert "invalid exact firework state" in str(exc)
        else:
            raise AssertionError(
                "expired exact firework passed capsule validation")
        moving_state = copy.deepcopy(state)
        moving_state["moving_pistons"] = [{
            "x": 2, "y": 64, "z": 2,
            "moved_block": 1, "moved_meta": 0, "facing": 5,
            "extending": True, "source": False,
            "progress_bits": 0x3F000000,
            "last_progress_bits": 0,
        }]
        moving_state_path = root / "moving_state.json"
        moving_state_path.write_text(
            json.dumps(moving_state), encoding="utf-8")
        moving_blocks = list(block_states)
        moving_index = (
            ((64 - box[1]) * 11 + (2 - box[2])) * 11
            + (2 - box[0])
        )
        moving_blocks[moving_index] = (36 << 4) | 5
        moving_blocks_path = root / "moving_source.bin"
        moving_blocks_path.write_bytes(
            struct.pack("<484H", *moving_blocks))
        moving_capsule = root / "moving_capsule"
        create_capsule(
            moving_state_path, moving_blocks_path, box, moving_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        moving_events = [
            row for row in magma_events(moving_capsule)
            if row["type"] == "load_moving_piston"
        ]
        assert moving_events == [{
            "tick": 0, "type": "load_moving_piston", "dim": 0,
            "x": 2, "y": 64, "z": 2,
            "moved_block": 1, "moved_meta": 0, "facing": 5,
            "extending": 1, "source": 0,
            "progress_bits": 0x3F000000,
            "last_progress_bits": 0,
        }]
        mismatched_moving = copy.deepcopy(moving_state)
        mismatched_moving["moving_pistons"][0]["facing"] = 4
        mismatched_moving_path = root / "mismatched_moving_state.json"
        mismatched_moving_path.write_text(
            json.dumps(mismatched_moving), encoding="utf-8")
        try:
            create_capsule(
                mismatched_moving_path, moving_blocks_path, box,
                root / "mismatched_moving_capsule", seed=7,
                source_engine="selftest", source_version="1",
            )
        except CapsuleError as exc:
            assert "facing-compatible moving block" in str(exc)
        else:
            raise AssertionError(
                "moving-piston facing mismatch passed capsule validation")
        flower_pot_state = copy.deepcopy(state)
        flower_pot_state["flower_pots"] = [
            {"x": 2, "y": 64, "z": -4, "item": 38, "meta": 2},
        ]
        flower_pot_state_path = root / "flower_pot_state.json"
        flower_pot_state_path.write_text(
            json.dumps(flower_pot_state), encoding="utf-8")
        flower_pot_blocks = list(block_states)
        flower_pot_offset = (
            ((64 - box[1]) * 11 + (-4 - box[2])) * 11
            + (2 - box[0])
        )
        flower_pot_blocks[flower_pot_offset] = 140 << 4
        flower_pot_blocks_path = root / "flower_pot_source.bin"
        flower_pot_blocks_path.write_bytes(
            struct.pack("<484H", *flower_pot_blocks))
        flower_pot_capsule = root / "flower_pot_capsule"
        create_capsule(
            flower_pot_state_path, flower_pot_blocks_path, box,
            flower_pot_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_flower_pot",
                "dim": 0,
                "x": 2,
                "y": 64,
                "z": -4,
                "item": 38,
                "meta": 2,
            }
            for row in magma_events(flower_pot_capsule)
        )
        note_state = copy.deepcopy(state)
        note_state["note_blocks"] = [{
            "x": 2, "y": 64, "z": -4,
            "note": 17, "powered": True,
        }]
        note_state_path = root / "note_state.json"
        note_state_path.write_text(
            json.dumps(note_state), encoding="utf-8")
        note_blocks = list(block_states)
        note_blocks[flower_pot_offset] = 25 << 4
        note_blocks_path = root / "note_source.bin"
        note_blocks_path.write_bytes(
            struct.pack("<484H", *note_blocks))
        note_capsule = root / "note_capsule"
        create_capsule(
            note_state_path, note_blocks_path, box, note_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_note_block",
                "dim": 0,
                "x": 2,
                "y": 64,
                "z": -4,
                "note": 17,
                "powered": 1,
            }
            for row in magma_events(note_capsule)
        )
        skull_state = copy.deepcopy(state)
        skull_state["skulls"] = [{
            "x": 2, "y": 64, "z": -4,
            "type": 5, "rotation": 11, "has_owner": False,
        }]
        skull_state_path = root / "skull_state.json"
        skull_state_path.write_text(
            json.dumps(skull_state), encoding="utf-8")
        skull_blocks = list(block_states)
        skull_blocks[flower_pot_offset] = (144 << 4) | 1
        skull_blocks_path = root / "skull_source.bin"
        skull_blocks_path.write_bytes(
            struct.pack("<484H", *skull_blocks))
        skull_capsule = root / "skull_capsule"
        create_capsule(
            skull_state_path, skull_blocks_path, box, skull_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_skull",
                "dim": 0,
                "x": 2,
                "y": 64,
                "z": -4,
                "skull_type": 5,
                "rotation": 11,
            }
            for row in magma_events(skull_capsule)
        )
        owned_skull_state = copy.deepcopy(skull_state)
        owned_skull_state["skulls"][0] = {
            "x": 2, "y": 64, "z": -4,
            "type": 3, "rotation": 7, "has_owner": True,
            "owner_nbt": nbt_codec.encode_hex({
                "name": "",
                "tag": {
                    "type": "compound",
                    "value": {
                        "Id": {
                            "type": "string",
                            "value": "12345678-1234-5678-9abc-def012345678",
                        },
                        "Name": {"type": "string", "value": "ParityHead"},
                        "Properties": {
                            "type": "compound",
                            "value": {
                                "textures": {
                                    "type": "list",
                                    "element_type": "compound",
                                    "value": [{
                                        "type": "compound",
                                        "value": {
                                            "Value": {
                                                "type": "string",
                                                "value": "dGVzdA==",
                                            },
                                            "Signature": {
                                                "type": "string",
                                                "value": "sig",
                                            },
                                        },
                                    }],
                                },
                            },
                        },
                    },
                },
            }),
        }
        owned_skull_state_path = root / "owned_skull_state.json"
        owned_skull_state_path.write_text(
            json.dumps(owned_skull_state), encoding="utf-8")
        owned_skull_capsule = root / "owned_skull_capsule"
        create_capsule(
            owned_skull_state_path, skull_blocks_path, box,
            owned_skull_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        owned_manifest, _ = validate_capsule(owned_skull_capsule)
        assert owned_manifest["nbt_payloads"][0]["kind"] == "skull_owner"
        assert any(
            row.get("type") == "set_skull"
            and row.get("skull_type") == 3
            and row.get("rotation") == 7
            and row.get("nbt_file") == "skull_owner_0000.nbt"
            for row in magma_events(owned_skull_capsule)
        )
        invalid_owned_skull = copy.deepcopy(owned_skull_state)
        invalid_owned_skull["skulls"][0]["type"] = 5
        try:
            _validate_state(invalid_owned_skull)
        except CapsuleError as exc:
            assert "type 3" in str(exc)
        else:
            raise AssertionError("non-player skull accepted a profile")
        invalid_owned_skull = copy.deepcopy(owned_skull_state)
        invalid_owned_skull["skulls"][0]["owner_nbt"] = "0a000008"
        try:
            _validate_state(invalid_owned_skull)
        except CapsuleError as exc:
            assert "invalid NBT" in str(exc)
        else:
            raise AssertionError("truncated player profile NBT passed")
        owner_payload = owned_skull_capsule / "skull_owner_0000.nbt"
        owner_raw = owner_payload.read_bytes()
        owner_payload.write_bytes(owner_raw[:-1] + bytes([owner_raw[-1] ^ 1]))
        try:
            validate_capsule(owned_skull_capsule)
        except CapsuleError as exc:
            assert "length or sha256 mismatch" in str(exc)
        else:
            raise AssertionError("corrupt player-profile payload passed")
        owner_payload.write_bytes(owner_raw)
        shulker_item_tag_hex = nbt_codec.encode_hex({
            "name": "",
            "tag": {
                "type": "compound",
                "value": {
                    "BlockEntityTag": {
                        "type": "compound",
                        "value": {
                            "Items": {
                                "type": "list",
                                "element_type": "compound",
                                "value": [{
                                    "type": "compound",
                                    "value": {
                                        "Count": {
                                            "type": "byte", "value": 64,
                                        },
                                        "Damage": {
                                            "type": "short", "value": 0,
                                        },
                                        "Slot": {
                                            "type": "byte", "value": 0,
                                        },
                                        "id": {
                                            "type": "string",
                                            "value": "minecraft:stone",
                                        },
                                    },
                                }],
                            },
                        },
                    },
                },
            },
        })
        shulker_state = copy.deepcopy(state)
        shulker_state["containers"] = [{
            "type": "shulker_box",
            "x": 2, "y": 64, "z": -4, "size": 27,
            "block": 229, "facing": 5,
            "open_count": 1,
            "animation_status": 1,
            "progress_bits": 0x3ECCCCCD,
            "progress_old_bits": 0x3E99999A,
            "item_tag_nbt": shulker_item_tag_hex,
            "items": [
                {"slot": 0, "id": 1, "count": 64, "meta": 0},
            ],
        }]
        shulker_state_path = root / "shulker_state.json"
        shulker_state_path.write_text(
            json.dumps(shulker_state), encoding="utf-8")
        shulker_blocks = list(block_states)
        shulker_blocks[flower_pot_offset] = (229 << 4) | 5
        shulker_blocks_path = root / "shulker_source.bin"
        shulker_blocks_path.write_bytes(
            struct.pack("<484H", *shulker_blocks))
        shulker_capsule = root / "shulker_capsule"
        create_capsule(
            shulker_state_path, shulker_blocks_path, box, shulker_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_static_container_slot",
                "dim": 0,
                "x": 2,
                "y": 64,
                "z": -4,
                "slot": 0,
                "item": 1,
                "count": 64,
                "meta": 0,
                "nbt_file": "shulker_item_tag_0000.nbt",
            }
            for row in magma_events(shulker_capsule)
        )
        assert any(
            row == {
                "tick": 0,
                "type": "restore_shulker_transient",
                "dim": 0,
                "x": 2,
                "y": 64,
                "z": -4,
                "open_count": 1,
                "animation_status": 1,
                "progress_bits": 0x3ECCCCCD,
                "progress_old_bits": 0x3E99999A,
            }
            for row in magma_events(shulker_capsule)
        )
        shulker_manifest, _ = validate_capsule(shulker_capsule)
        assert shulker_manifest["nbt_payloads"][0]["kind"] \
            == "shulker_item_tag"
        shulker_payload = shulker_capsule / "shulker_item_tag_0000.nbt"
        shulker_raw = shulker_payload.read_bytes()
        shulker_payload.write_bytes(shulker_raw[:-1])
        try:
            validate_capsule(shulker_capsule)
        except CapsuleError as exc:
            assert "length or sha256 mismatch" in str(exc)
        else:
            raise AssertionError("truncated shulker NBT payload passed")
        shulker_payload.write_bytes(shulker_raw)
        overstacked_shulker_state = copy.deepcopy(shulker_state)
        overstacked_shulker_state["containers"][0]["items"][0] = {
            "slot": 0, "id": 219, "count": 2, "meta": 0,
        }
        overstacked_tag = nbt_codec.decode_hex(shulker_item_tag_hex)
        overstacked_tag["tag"]["value"]["BlockEntityTag"]["value"] \
            ["Items"]["value"][0]["value"]["Count"]["value"] = 2
        overstacked_shulker_state["containers"][0]["item_tag_nbt"] = \
            nbt_codec.encode_hex(overstacked_tag)
        try:
            _validate_state(overstacked_shulker_state)
        except CapsuleError as exc:
            assert "stack limit" in str(exc)
        else:
            raise AssertionError(
                "overstacked shulker item passed capsule validation")
        furnace_state = copy.deepcopy(state)
        furnace_state["containers"] = [{
            "type": "furnace",
            "x": 3, "y": 64, "z": 3, "size": 3,
            "burn_time": 0, "current_burn_time": 0,
            "cook_time": 0, "total_cook_time": 200,
            "custom_name": "Oracle Furnace",
            "items": [
                {"slot": 0, "id": 1, "count": 64, "meta": 0,
                 "stack_payload": {
                     "kind": "item_tag", "nbt": item_tag_hex}},
            ],
        }]
        furnace_state_path = root / "furnace_state.json"
        furnace_state_path.write_text(
            json.dumps(furnace_state), encoding="utf-8")
        furnace_capsule = root / "furnace_capsule"
        create_capsule(
            furnace_state_path, blocks_path, box, furnace_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_furnace_slot",
                "dim": 0,
                "x": 3,
                "y": 64,
                "z": 3,
                "slot": 0,
                "item": 1,
                "count": 64,
                "meta": 0,
                "burn_time": 0,
                "current_burn_time": 0,
                "cook_time": 0,
                "total_cook_time": 200,
                "custom_name": "Oracle Furnace",
                "stack_nbt_file": "item_stack_container_0000_0000.nbt",
            }
            for row in magma_events(furnace_capsule)
        )
        malformed_furnace = copy.deepcopy(furnace_state)
        del malformed_furnace["containers"][0]["burn_time"]
        try:
            _validate_state(malformed_furnace)
        except CapsuleError as exc:
            assert "unsupported or incomplete" in str(exc)
        else:
            raise AssertionError(
                "incomplete furnace tile state passed validation")
        brewing_state = copy.deepcopy(state)
        brewing_state["containers"] = [{
            "type": "brewing_stand",
            "x": 5, "y": 64, "z": 5, "size": 5,
            "brew_time": 200, "fuel": 19, "ingredient_id": 372,
            "items": [
                {"slot": 0, "id": 373, "count": 1, "meta": 1},
                {"slot": 3, "id": 372, "count": 2, "meta": 0},
                {"slot": 4, "id": 377, "count": 1, "meta": 0},
            ],
        }]
        brewing_state_path = root / "brewing_state.json"
        brewing_state_path.write_text(
            json.dumps(brewing_state), encoding="utf-8")
        brewing_capsule = root / "brewing_capsule"
        create_capsule(
            brewing_state_path, blocks_path, box, brewing_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_brewing_slot",
                "dim": 0,
                "x": 5,
                "y": 64,
                "z": 5,
                "slot": 0,
                "item": 373,
                "count": 1,
                "meta": 1,
                "brew_time": 200,
                "fuel": 19,
            }
            for row in magma_events(brewing_capsule)
        )
        assert any(
            row == {
                "tick": 0,
                "type": "restore_brewing_ingredient",
                "dim": 0,
                "x": 5,
                "y": 64,
                "z": 5,
                "ingredient_id": 372,
            }
            for row in magma_events(brewing_capsule)
        )
        malformed_brewing = copy.deepcopy(brewing_state)
        malformed_brewing["containers"][0]["items"][0]["count"] = 2
        try:
            _validate_state(malformed_brewing)
        except CapsuleError as exc:
            assert "stack limit" in str(exc)
        else:
            raise AssertionError(
                "overstacked brewing potion passed validation")
        dispenser_state = copy.deepcopy(state)
        dispenser_state["containers"] = [{
            "type": "dispenser",
            "x": 4, "y": 64, "z": -4, "size": 9,
            "items": [
                {"slot": 0, "id": 1, "count": 64, "meta": 0},
            ],
        }]
        dispenser_state_path = root / "dispenser_state.json"
        dispenser_state_path.write_text(
            json.dumps(dispenser_state), encoding="utf-8")
        dispenser_capsule = root / "dispenser_capsule"
        create_capsule(
            dispenser_state_path, blocks_path, box,
            dispenser_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_static_container_slot",
                "dim": 0,
                "x": 4,
                "y": 64,
                "z": -4,
                "slot": 0,
                "item": 1,
                "count": 64,
                "meta": 0,
            }
            for row in magma_events(dispenser_capsule)
        )
        malformed_dispenser = copy.deepcopy(dispenser_state)
        malformed_dispenser["containers"][0]["size"] = 10
        try:
            _validate_state(malformed_dispenser)
        except CapsuleError as exc:
            assert "nine-slot" in str(exc)
        else:
            raise AssertionError(
                "malformed dispenser tile state passed validation")
        hopper_state = copy.deepcopy(state)
        hopper_state["containers"] = [{
            "type": "hopper",
            "x": 2, "y": 64, "z": -4, "size": 5,
            "transfer_cooldown": 6,
            "ticked_game_time": 123,
            "items": [],
        }]
        hopper_state_path = root / "hopper_state.json"
        hopper_state_path.write_text(
            json.dumps(hopper_state), encoding="utf-8")
        hopper_capsule = root / "hopper_capsule"
        create_capsule(
            hopper_state_path, blocks_path, box,
            hopper_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_static_container_slot",
                "dim": 0,
                "x": 2,
                "y": 64,
                "z": -4,
                "slot": 0,
                "item": 0,
                "count": 0,
                "meta": 0,
            }
            for row in magma_events(hopper_capsule)
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_hopper_transfer_state",
                "dim": 0,
                "x": 2,
                "y": 64,
                "z": -4,
                "transfer_cooldown": 6,
                "ticked_game_time": 123,
            }
            for row in magma_events(hopper_capsule)
        )
        spawner_state = copy.deepcopy(state)
        spawner_state["loaded_tiles"] = [{
            "x": -2, "y": 64, "z": -4, "loaded_order": 0,
            "update_order": 0, "class": "TileEntityMobSpawner",
            "tickable": True, "block": 52, "meta": 0,
        }]
        spawner_state["loaded_tiles_complete"] = True
        default_zombie_nbt = nbt_codec.encode_hex({
            "name": "",
            "tag": {"type": "compound", "value": {
                "id": {"type": "string", "value": "minecraft:zombie"},
            }},
        })
        spawner_state["spawners"] = [{
            "x": -2, "y": 64, "z": -4,
            "delay": 37, "min_delay": 200, "max_delay": 800,
            "spawn_count": 4, "max_nearby": 6,
            "activate_range": 16, "spawn_range": 4,
            "entity_id": "minecraft:zombie",
            "spawn_data_nbt": default_zombie_nbt,
            "default_entity_nbt": True,
            "potentials": [{
                "weight": 1, "entity_id": "minecraft:zombie",
                "entity_nbt": default_zombie_nbt,
                "default_entity_nbt": True,
            }],
        }]
        spawner_state["spawners_complete"] = True
        spawner_state_path = root / "spawner_state.json"
        spawner_state_path.write_text(
            json.dumps(spawner_state), encoding="utf-8")
        spawner_capsule = root / "spawner_capsule"
        create_capsule(
            spawner_state_path, blocks_path, box,
            spawner_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        spawner_events = magma_events(spawner_capsule)
        assert any(
            row["type"] == "set_spawner_state"
            and row["x"] == -2 and row["entity"] == 2
            and row["delay"] == 37
            for row in spawner_events)
        assert any(
            row["type"] == "add_spawner_potential"
            and row["entity"] == 2 and row["weight"] == 1
            for row in spawner_events)
        custom_spawner = copy.deepcopy(spawner_state)
        custom_spawner["spawners"][0]["default_entity_nbt"] = False
        try:
            _validate_state(custom_spawner)
        except CapsuleError as exc:
            assert "inconsistent SpawnData" in str(exc)
        else:
            raise AssertionError("custom spawner entity NBT passed validation")

        ordered_tiles_state = copy.deepcopy(state)
        ordered_tiles_state["containers"] = [
            {
                "type": "dispenser", "loaded_order": 1,
                "x": 4, "y": 64, "z": -4, "size": 9,
                "items": [],
            },
            {
                "type": "hopper", "loaded_order": 0,
                "x": 2, "y": 64, "z": -4, "size": 5,
                "transfer_cooldown": 6, "ticked_game_time": 123,
                "items": [],
            },
        ]
        ordered_tiles_state["loaded_tiles"] = [
            {
                "x": 2, "y": 64, "z": -4, "loaded_order": 0,
                "update_order": 0,
                "class": "TileEntityHopper", "tickable": True,
                "block": 154, "meta": 0,
            },
            {
                "x": 4, "y": 64, "z": -4, "loaded_order": 1,
                "update_order": -1,
                "class": "TileEntityDispenser", "tickable": False,
                "block": 23, "meta": 3,
            },
        ]
        ordered_tiles_state["loaded_tiles_complete"] = True
        ordered_tiles_path = root / "ordered_tiles_state.json"
        ordered_tiles_path.write_text(
            json.dumps(ordered_tiles_state), encoding="utf-8")
        ordered_tiles_capsule = root / "ordered_tiles_capsule"
        create_capsule(
            ordered_tiles_path, blocks_path, box,
            ordered_tiles_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        ordered_tile_events = magma_events(ordered_tiles_capsule)
        assert [
            (row["order"], row["x"], row["y"], row["z"])
            for row in ordered_tile_events
            if row["type"] == "restore_loaded_tile_order"
        ] == [(0, 2, 64, -4), (1, 4, 64, -4)]
        assert [
            (row["order"], row["x"], row["y"], row["z"])
            for row in ordered_tile_events
            if row["type"] == "restore_tickable_tile_order"
        ] == [(0, 2, 64, -4)]
        duplicate_tile_order = copy.deepcopy(ordered_tiles_state)
        duplicate_tile_order["containers"][0]["loaded_order"] = 0
        try:
            _validate_state(duplicate_tile_order)
        except CapsuleError as exc:
            assert "loaded_order must be a unique" in str(exc)
        else:
            raise AssertionError("duplicate loaded tile order passed validation")
        partial_tile_order = copy.deepcopy(ordered_tiles_state)
        del partial_tile_order["containers"][0]["loaded_order"]
        try:
            _validate_state(partial_tile_order)
        except CapsuleError as exc:
            assert "all include loaded_order" in str(exc)
        else:
            raise AssertionError("partial loaded tile order passed validation")
        mismatched_tile_order = copy.deepcopy(ordered_tiles_state)
        mismatched_tile_order["containers"][0]["loaded_order"] = 0
        mismatched_tile_order["containers"][1]["loaded_order"] = 1
        try:
            _validate_state(mismatched_tile_order)
        except CapsuleError as exc:
            assert "does not match state.loaded_tiles" in str(exc)
        else:
            raise AssertionError(
                "container/loaded-tile order mismatch passed validation")
        invalid_non_tickable_order = copy.deepcopy(ordered_tiles_state)
        invalid_non_tickable_order["loaded_tiles"][1]["update_order"] = 0
        try:
            _validate_state(invalid_non_tickable_order)
        except CapsuleError as exc:
            assert "must be -1 for a non-tickable tile" in str(exc)
        else:
            raise AssertionError(
                "non-tickable tile update order passed validation")
        gapped_tickable_order = copy.deepcopy(ordered_tiles_state)
        gapped_tickable_order["loaded_tiles"][0]["update_order"] = 1
        try:
            _validate_state(gapped_tickable_order)
        except CapsuleError as exc:
            assert "update_order must be contiguous" in str(exc)
        else:
            raise AssertionError("gapped tile update order passed validation")
        malformed_hopper = copy.deepcopy(hopper_state)
        malformed_hopper["containers"][0]["size"] = 6
        try:
            _validate_state(malformed_hopper)
        except CapsuleError as exc:
            assert "five-slot" in str(exc)
        else:
            raise AssertionError(
                "malformed hopper tile state passed validation")
        jukebox_state = copy.deepcopy(state)
        jukebox_state["containers"] = [{
            "type": "jukebox",
            "x": -4, "y": 64, "z": -4, "size": 1,
            "items": [
                {"slot": 0, "id": 2256, "count": 1, "meta": 0},
            ],
        }]
        jukebox_state_path = root / "jukebox_state.json"
        jukebox_state_path.write_text(
            json.dumps(jukebox_state), encoding="utf-8")
        jukebox_capsule = root / "jukebox_capsule"
        create_capsule(
            jukebox_state_path, blocks_path, box,
            jukebox_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_static_container_slot",
                "dim": 0,
                "x": -4,
                "y": 64,
                "z": -4,
                "slot": 0,
                "item": 2256,
                "count": 1,
                "meta": 0,
            }
            for row in magma_events(jukebox_capsule)
        )
        malformed_jukebox = copy.deepcopy(jukebox_state)
        malformed_jukebox["containers"][0]["items"][0]["id"] = 2255
        try:
            _validate_state(malformed_jukebox)
        except CapsuleError as exc:
            assert "music record" in str(exc)
        else:
            raise AssertionError(
                "non-record jukebox tile state passed validation")
        command_state = copy.deepcopy(state)
        command_state["containers"] = [{
            "type": "command_block",
            "x": -3, "y": 64, "z": -4, "size": 0,
            "success_count": 7,
            "command": "", "last_output": "",
            "powered": False, "automatic": False,
            "condition_met": False,
            "items": [],
        }]
        command_state_path = root / "command_state.json"
        command_state_path.write_text(
            json.dumps(command_state), encoding="utf-8")
        command_capsule = root / "command_capsule"
        create_capsule(
            command_state_path, blocks_path, box,
            command_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "set_command_block_state",
                "dim": 0,
                "x": -3,
                "y": 64,
                "z": -4,
                "success_count": 7,
                "command": "",
                "last_output": "",
                "powered": False,
                "automatic": False,
                "condition_met": False,
            }
            for row in magma_events(command_capsule)
        )
        time_command = copy.deepcopy(command_state)
        time_command["containers"][0].update({
            "success_count": 1,
            "command": "time add 250",
            "last_output": (
                '{"extra":[{"translate":"commands.time.added",'
                '"with":["250"]}],"text":"[12:34:56] "}'),
        })
        time_command_path = root / "time_command_state.json"
        time_command_path.write_text(
            json.dumps(time_command), encoding="utf-8")
        time_command_capsule = root / "time_command_capsule"
        create_capsule(
            time_command_path, blocks_path, box,
            time_command_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row.get("type") == "set_command_block_state"
            and row.get("command") == "time add 250"
            and row.get("last_output") ==
                time_command["containers"][0]["last_output"]
            for row in magma_events(time_command_capsule)
        )
        query_command = copy.deepcopy(command_state)
        query_command["containers"][0].update({
            "success_count": 1,
            "command": "time query gametime",
            "last_output": (
                '{"extra":[{"translate":"commands.time.query",'
                '"with":["123456"]}],"text":"[12:34:56] "}'),
        })
        _validate_state(query_command)
        weather_command = copy.deepcopy(command_state)
        weather_command["containers"][0].update({
            "success_count": 1,
            "command": "weather thunder 7",
            "last_output": (
                '{"extra":[{"translate":"commands.weather.thunder"}],'
                '"text":"[12:34:56] "}'),
        })
        _validate_state(weather_command)
        gamerule_command = copy.deepcopy(command_state)
        gamerule_command["containers"][0].update({
            "success_count": 1,
            "command": "gamerule randomTickSpeed 17",
            "last_output": (
                '{"extra":[{"translate":"commands.gamerule.success",'
                '"with":["randomTickSpeed","17"]}],'
                '"text":"[12:34:56] "}'),
        })
        _validate_state(gamerule_command)
        downfall_command = copy.deepcopy(command_state)
        downfall_command["containers"][0].update({
            "success_count": 1,
            "command": "toggledownfall",
            "last_output": (
                '{"extra":[{"translate":"commands.downfall.success"}],'
                '"text":"[12:34:56] "}'),
        })
        _validate_state(downfall_command)
        invalid_gamerule = copy.deepcopy(command_state)
        invalid_gamerule["containers"][0]["command"] = (
            "gamerule doFireTick TRUE")
        try:
            _validate_state(invalid_gamerule)
        except CapsuleError as exc:
            assert "bounded command set" in str(exc)
        else:
            raise AssertionError("invalid gamerule boolean passed validation")
        malformed_command = copy.deepcopy(command_state)
        malformed_command["containers"][0]["success_count"] = 16
        try:
            _validate_state(malformed_command)
        except CapsuleError as exc:
            assert "success_count" in str(exc)
        else:
            raise AssertionError(
                "out-of-range command success count passed validation")
        overflow_command = copy.deepcopy(command_state)
        overflow_command["containers"][0]["command"] = (
            "time set 2147483648")
        try:
            _validate_state(overflow_command)
        except CapsuleError as exc:
            assert "bounded command set" in str(exc)
        else:
            raise AssertionError(
                "overflowing time command passed validation")
        pending_command = copy.deepcopy(command_state)
        pending_command["scheduled_ticks"].append({
            "x": -3, "y": 64, "z": -4, "block": 137,
            "time": 47, "priority": 0, "order": 19,
        })
        pending_command_path = root / "pending_command_state.json"
        pending_command_path.write_text(
            json.dumps(pending_command), encoding="utf-8")
        pending_command_capsule = root / "pending_command_capsule"
        create_capsule(
            pending_command_path, blocks_path, box,
            pending_command_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row.get("type") == "schedule_tick"
            and row.get("block") == 137
            and row.get("time") == 47
            and row.get("order") == 19
            for row in magma_events(pending_command_capsule)
        )
        trapped_chest_state = copy.deepcopy(state)
        trapped_chest_state["containers"] = [{
            "type": "single_trapped_chest",
            "x": 4, "y": 64, "z": 3, "size": 27,
            "num_players_using": 0,
            "lid_angle_bits": 0,
            "prev_lid_angle_bits": 0,
            "ticks_since_sync": 0,
            "items": [
                {"slot": 0, "id": 1, "count": 64, "meta": 0},
            ],
        }]
        trapped_chest_state_path = root / "trapped_chest_state.json"
        trapped_chest_state_path.write_text(
            json.dumps(trapped_chest_state), encoding="utf-8")
        trapped_chest_capsule = root / "trapped_chest_capsule"
        create_capsule(
            trapped_chest_state_path, blocks_path, box,
            trapped_chest_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        trapped_chest_events = [
            row for row in magma_events(trapped_chest_capsule)
            if row["type"] == "set_chest_slot"
        ]
        assert trapped_chest_events == [{
            "tick": 0,
            "type": "set_chest_slot",
            "dim": 0,
            "x": 4,
            "y": 64,
            "z": 3,
            "slot": 0,
            "item": 1,
            "count": 64,
            "meta": 0,
        }]
        malformed_trapped = copy.deepcopy(trapped_chest_state)
        malformed_trapped["containers"][0]["type"] = "single_chest"
        malformed_trapped_path = root / "malformed_trapped_state.json"
        malformed_trapped_path.write_text(
            json.dumps(malformed_trapped), encoding="utf-8")
        try:
            create_capsule(
                malformed_trapped_path, blocks_path, box,
                root / "malformed_trapped_capsule", seed=7,
                source_engine="selftest", source_version="1",
            )
        except CapsuleError as exc:
            assert "does not match its block type" in str(exc)
        else:
            raise AssertionError(
                "trapped chest accepted an ordinary chest schema")
        open_trapped = copy.deepcopy(trapped_chest_state)
        open_trapped["containers"][0]["num_players_using"] = 1
        open_trapped["containers"][0]["prev_lid_angle_bits"] = 0x3E99999A
        open_trapped["containers"][0]["lid_angle_bits"] = 0x3ECCCCCD
        open_trapped["containers"][0]["ticks_since_sync"] = 17
        open_trapped_path = root / "open_trapped_state.json"
        open_trapped_path.write_text(
            json.dumps(open_trapped), encoding="utf-8")
        open_trapped_capsule = root / "open_trapped_capsule"
        create_capsule(
            open_trapped_path, blocks_path, box,
            open_trapped_capsule, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert any(
            row == {
                "tick": 0,
                "type": "restore_chest_transient",
                "dim": 0,
                "x": 4,
                "y": 64,
                "z": 3,
                "num_players_using": 1,
                "lid_angle_bits": 0x3ECCCCCD,
                "prev_lid_angle_bits": 0x3E99999A,
                "ticks_since_sync": 17,
            }
            for row in magma_events(open_trapped_capsule)
        )
        malformed_open = copy.deepcopy(open_trapped)
        malformed_open["containers"][0]["lid_angle_bits"] = 0x3E4CCCCD
        try:
            _validate_state(malformed_open)
        except CapsuleError as exc:
            assert "invalid chest animation state" in str(exc)
        else:
            raise AssertionError(
                "backward-moving open chest passed validation")
        double_chest_state = copy.deepcopy(state)
        double_chest_state["containers"] = [
            {
                "type": "double_chest_half",
                "x": -4, "y": 64, "z": 3, "size": 27,
                "pair_x": -3, "pair_y": 64, "pair_z": 3,
                "num_players_using": 0,
                "lid_angle_bits": 0,
                "prev_lid_angle_bits": 0,
                "ticks_since_sync": 0,
                "items": [],
            },
            {
                "type": "double_chest_half",
                "x": -3, "y": 64, "z": 3, "size": 27,
                "pair_x": -4, "pair_y": 64, "pair_z": 3,
                "num_players_using": 0,
                "lid_angle_bits": 0,
                "prev_lid_angle_bits": 0,
                "ticks_since_sync": 0,
                "items": [
                    {
                        "slot": slot, "id": 1,
                        "count": 64, "meta": 0,
                    }
                    for slot in range(4)
                ],
            },
        ]
        double_chest_state_path = root / "double_chest_state.json"
        double_chest_state_path.write_text(
            json.dumps(double_chest_state), encoding="utf-8")
        double_chest_capsule = root / "double_chest_capsule"
        create_capsule(
            double_chest_state_path, blocks_path, box,
            double_chest_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        double_chest_events = [
            row for row in magma_events(double_chest_capsule)
            if row["type"] == "set_chest_slot"
        ]
        assert len(double_chest_events) == 5
        assert sum(
            row["item"] == 1 and row["count"] == 64
            for row in double_chest_events
        ) == 4
        malformed_double = copy.deepcopy(double_chest_state)
        malformed_double["containers"][1]["pair_x"] = -5
        malformed_double_path = root / "malformed_double_state.json"
        malformed_double_path.write_text(
            json.dumps(malformed_double), encoding="utf-8")
        try:
            create_capsule(
                malformed_double_path, blocks_path, box,
                root / "malformed_double_capsule", seed=7,
                source_engine="selftest", source_version="1",
            )
        except CapsuleError as exc:
            assert "reciprocal horizontal pair" in str(exc)
        else:
            raise AssertionError(
                "non-reciprocal double chest passed validation")
        double_trapped_state = copy.deepcopy(double_chest_state)
        for container in double_trapped_state["containers"]:
            container["type"] = "double_trapped_chest_half"
        double_trapped_state_path = root / "double_trapped_state.json"
        double_trapped_state_path.write_text(
            json.dumps(double_trapped_state), encoding="utf-8")
        double_trapped_blocks = list(block_states)
        for x in (-4, -3):
            index = (
                ((64 - box[1]) * 11 + (3 - box[2])) * 11
                + (x - box[0])
            )
            double_trapped_blocks[index] = (146 << 4) | 2
        double_trapped_blocks_path = root / "double_trapped_source.bin"
        double_trapped_blocks_path.write_bytes(
            struct.pack("<484H", *double_trapped_blocks))
        double_trapped_capsule = root / "double_trapped_capsule"
        create_capsule(
            double_trapped_state_path, double_trapped_blocks_path, box,
            double_trapped_capsule, sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        double_trapped_events = [
            row for row in magma_events(double_trapped_capsule)
            if row["type"] == "set_chest_slot"
        ]
        assert len(double_trapped_events) == 5
        assert sum(
            row["item"] == 1 and row["count"] == 64
            for row in double_trapped_events
        ) == 4
        malformed_double_trapped = copy.deepcopy(double_trapped_state)
        malformed_double_trapped["containers"][1]["type"] = (
            "double_chest_half")
        malformed_double_trapped_path = (
            root / "malformed_double_trapped_state.json")
        malformed_double_trapped_path.write_text(
            json.dumps(malformed_double_trapped), encoding="utf-8")
        try:
            create_capsule(
                malformed_double_trapped_path,
                double_trapped_blocks_path, box,
                root / "malformed_double_trapped_capsule", seed=7,
                source_engine="selftest", source_version="1",
            )
        except CapsuleError as exc:
            assert (
                "does not match its block type" in str(exc)
                or "reciprocal horizontal pair" in str(exc)
            )
        else:
            raise AssertionError(
                "double trapped chest accepted a mixed ordinary schema")
        assert any(
            row["type"] == "set_entity_id_cursor"
            and row["value"] == 93
            for row in events
        )
        assert any(
            row["type"] == "set_do_entity_drops"
            and row["enabled"] == 1
            for row in events
        )
        assert any(
            row["type"] == "set_gamerules"
            and row.get("doMobSpawning") == "false"
            and row.get("doMobLoot") == "false"
            for row in events
        )
        assert sum(row["type"] == "snapshot_block" for row in events) == 484
        assert sum(
            row["type"] == "snapshot_sky_light" for row in events
        ) == 484
        block_finalize = next(
            index for index, row in enumerate(events)
            if row["type"] == "snapshot_blocks_finalize"
        )
        first_sky = next(
            index for index, row in enumerate(events)
            if row["type"] == "snapshot_sky_light"
        )
        sky_finalize = next(
            index for index, row in enumerate(events)
            if row["type"] == "snapshot_sky_light_finalize"
        )
        assert block_finalize < first_sky < sky_finalize
        pig_events = [
            row for row in events if row["type"] == "spawn_mob_fixture"
        ]
        orb_events = [
            row for row in events if row["type"] == "spawn_xp_fixture"
        ]
        assert len(pig_events) == 1 and pig_events[0]["eid"] == 91
        assert pig_events[0]["hurt_time"] == 3
        pig_base = next(
            row for row in events
            if row["type"] == "restore_no_ai_mob_state"
            and row["eid"] == 91
        )
        assert pig_base["ticks_existed"] == 20
        assert pig_base["entity_seed48"] == 0x123456789ABC
        assert len(orb_events) == 1 and orb_events[0]["eid"] == 92
        assert orb_events[0]["target_color"] == -100
        entity_events = [
            row for row in events
            if row["type"] in ("spawn_mob_fixture", "spawn_xp_fixture")
        ]
        assert [row["eid"] for row in entity_events] == [92, 91]
        plain_state = copy.deepcopy(state)
        plain_state["entities"].append({
            "eid": 90, "type": "EntityGhast", "loaded_order": 2,
            "x": 4.5, "y": 65.0, "z": 4.5,
            "dx": 3.0, "dy": 0.0, "dz": 7.0,
            "vx": 0.125, "vy": -0.25, "vz": -0.0625,
            "yaw": 37.0, "pitch": 0.0, "health": 10.0,
            "hurt_time": 0, "death_time": 0,
            "hurt_resistant_time": 0, "no_ai": True,
            "no_ai_base_exact": True,
            "no_ai_plain_exact": True,
            "air": 300, "fire": -1, "on_ground": False,
            "fall_distance": 1.25, "in_water": False,
            "ticks_existed": 19,
            "base_living_sound_time": 1000,
            "base_last_damage": 0.0,
            "base_entity_seed48": 0x23456789ABCD,
            "base_entity_have_gaussian": False,
            "base_entity_gaussian": 0.0,
            "mob_potions_empty": True,
            "mob_equipment_empty": True,
            "max_health": 10.0, "absorption": 0.0,
            "base_box_min_x": 4.0, "base_box_min_y": 65.0,
            "base_box_min_z": 4.0, "base_box_max_x": 5.0,
            "base_box_max_y": 66.0, "base_box_max_z": 5.0,
        })
        plain_state_path = root / "plain_state.json"
        plain_state_path.write_text(
            json.dumps(plain_state), encoding="utf-8")
        plain_capsule = root / "plain_capsule"
        create_capsule(
            plain_state_path, blocks_path, box, plain_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        plain_events = magma_events(plain_capsule)
        ghast_spawn = next(
            row for row in plain_events
            if row["type"] == "spawn_mob_fixture" and row["eid"] == 90
        )
        ghast_base = next(
            row for row in plain_events
            if row["type"] == "restore_no_ai_mob_state"
            and row["eid"] == 90
        )
        assert ghast_spawn["entity"] == 26 and ghast_spawn["no_ai"] == 1
        assert ghast_base["fall_distance"] == 1.25
        assert ghast_base["entity_seed48"] == 0x23456789ABCD
        villager_state = copy.deepcopy(state)
        villager_state["entities"].append({
            "eid": 90, "type": "EntityVillager", "loaded_order": 2,
            "x": 4.5, "y": 65.0, "z": 4.5,
            "dx": 3.0, "dy": 0.0, "dz": 7.0,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "yaw": 0.0, "pitch": 0.0, "health": 20.0,
            "hurt_time": 0, "death_time": 0,
            "hurt_resistant_time": 0, "no_ai": True,
            "villager_exact": True,
            "profession": 1, "growing_age": 0,
            "career": 0, "career_level": 0,
            "living_sound_time": 0,
            "offers_initialized": False,
            "entity_seed48": 0x3456789ABCDE,
            "entity_have_gaussian": True,
            "entity_gaussian": -0.25,
            "no_ai_base_exact": True,
            "air": 300, "fire": -1, "on_ground": False,
            "fall_distance": 0.0, "in_water": False,
            "ticks_existed": 20,
            "base_living_sound_time": 0,
            "base_last_damage": 0.0,
            "base_entity_seed48": 0x3456789ABCDE,
            "base_entity_have_gaussian": True,
            "base_entity_gaussian": -0.25,
            "mob_potions_empty": True,
            "mob_equipment_empty": True,
            "max_health": 20.0, "absorption": 0.0,
            "base_box_min_x": 4.0, "base_box_min_y": 65.0,
            "base_box_min_z": 4.0, "base_box_max_x": 5.0,
            "base_box_max_y": 66.0, "base_box_max_z": 5.0,
        })
        villager_state_path = root / "villager_state.json"
        villager_state_path.write_text(
            json.dumps(villager_state), encoding="utf-8")
        villager_capsule = root / "villager_capsule"
        create_capsule(
            villager_state_path, blocks_path, box, villager_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        villager_events = magma_events(villager_capsule)
        villager_spawn = next(
            row for row in villager_events
            if row["type"] == "spawn_villager_fixture"
        )
        assert villager_spawn["eid"] == 90
        assert villager_spawn["profession"] == 1
        assert villager_spawn["living_sound_time"] == 0
        assert villager_spawn["entity_seed48"] == 0x3456789ABCDE
        assert [
            row["eid"] for row in villager_events
            if row["type"] in (
                "spawn_mob_fixture", "spawn_xp_fixture",
                "spawn_villager_fixture",
            )
        ] == [92, 91, 90]
        active_villager = copy.deepcopy(villager_state)
        active_entity = active_villager["entities"][-1]
        active_entity.update({
            "no_ai": False,
            "active_fresh_villager_exact": True,
            "living_base_exact": True,
            "villager_inventory_empty": True,
            "ticks_existed": 0,
            "base_box_min_x": 4.2,
            "base_box_min_y": 65.0,
            "base_box_min_z": 4.2,
            "base_box_max_x": 4.8,
            "base_box_max_y": 66.95,
            "base_box_max_z": 4.8,
        })
        active_entity.pop("no_ai_base_exact")
        active_path = root / "active_villager_state.json"
        active_path.write_text(
            json.dumps(active_villager), encoding="utf-8")
        active_capsule = root / "active_villager_capsule"
        create_capsule(
            active_path, blocks_path, box, active_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        active_events = magma_events(active_capsule)
        active_base_index = next(
            index for index, row in enumerate(active_events)
            if row["type"] == "restore_no_ai_mob_state"
            and row["eid"] == 90
        )
        active_toggle_index = next(
            index for index, row in enumerate(active_events)
            if row == {
                "tick": 0, "type": "set_mob_no_ai",
                "eid": 90, "no_ai": 0,
            }
        )
        assert active_events[active_base_index]["ticks_existed"] == 0
        assert active_base_index < active_toggle_index
        malformed_active = copy.deepcopy(active_villager)
        malformed_active["entities"][-1][
            "villager_inventory_empty"] = False
        try:
            _validate_state(malformed_active)
        except CapsuleError as exc:
            assert "inventory must be empty" in str(exc)
        else:
            raise AssertionError(
                "active fresh villager accepted a nonempty inventory")
        initialized_villager = copy.deepcopy(villager_state)
        initialized_entity = initialized_villager["entities"][-1]
        initialized_entity.update({
            "career": 1,
            "career_level": 4,
            "offers_initialized": True,
            "wealth": 15,
            "willing": True,
            "villager_inventory_empty": True,
            "offers": [{
                "uses": 3,
                "max_uses": 9,
                "rewards_exp": True,
                "buy_a": {
                    "id": 340, "count": 1, "meta": 0,
                    "repair_cost": 0, "custom_name": "",
                    "nbt_subset_exact": True, "enchants": [],
                },
                "buy_b": {
                    "id": 388, "count": 17, "meta": 0,
                    "repair_cost": 0, "custom_name": "",
                    "nbt_subset_exact": True, "enchants": [],
                },
                "sell": {
                    "id": 403, "count": 1, "meta": 0,
                    "repair_cost": 0, "custom_name": "",
                    "nbt_subset_exact": True, "enchants": [[70, 1]],
                },
            }],
        })
        initialized_path = root / "initialized_villager_state.json"
        initialized_path.write_text(
            json.dumps(initialized_villager), encoding="utf-8")
        initialized_capsule = root / "initialized_villager_capsule"
        create_capsule(
            initialized_path, blocks_path, box, initialized_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        initialized_events = magma_events(initialized_capsule)
        trade_header = next(
            row for row in initialized_events
            if row["type"] == "restore_villager_trade"
        )
        offer_header = next(
            row for row in initialized_events
            if row["type"] == "restore_villager_offer"
        )
        offer_stacks = [
            row for row in initialized_events
            if row["type"] == "restore_villager_offer_stack"
        ]
        assert trade_header["career_level"] == 4 \
            and trade_header["wealth"] == 15 \
            and trade_header["offer_count"] == 1
        assert offer_header["uses"] == 3 \
            and offer_header["max_uses"] == 9
        assert len(offer_stacks) == 3 \
            and offer_stacks[2]["item"] == 403 \
            and offer_stacks[2]["n_ench"] == 1 \
            and offer_stacks[2]["e0"] == (70 << 16) | 1
        invalid_villager = copy.deepcopy(villager_state)
        invalid_villager["entities"][-1]["career"] = 1
        try:
            _validate_state(invalid_villager)
        except CapsuleError as exc:
            assert "invalid unopened NoAI-villager state" in str(exc)
        else:
            raise AssertionError(
                "initialized villager career passed unopened validation")
        tameable_state = copy.deepcopy(state)
        tameable_state["entities"].append({
            "eid": 90, "type": "EntityWolf", "loaded_order": 2,
            "uuid_most": -500, "uuid_least": 700,
            "x": 4.5, "y": 65.0, "z": 4.5,
            "dx": 3.0, "dy": 0.0, "dz": 7.0,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "yaw": 45.0, "pitch": 0.0, "health": 17.0,
            "hurt_time": 2, "death_time": 0,
            "hurt_resistant_time": 6, "no_ai": True,
            "tameable_exact": True, "tamed": True,
            "sitting": True, "player_owner": True, "variant": 5,
            "growing_age": -1200, "living_sound_time": 23,
            "entity_seed48": 0x23456789ABCD,
            "entity_have_gaussian": True,
            "entity_gaussian": 0.375,
            "no_ai_base_exact": True,
            "air": 300, "fire": -1, "on_ground": False,
            "fall_distance": 0.0, "in_water": False,
            "ticks_existed": 20,
            "base_living_sound_time": 23,
            "base_last_damage": 0.0,
            "base_entity_seed48": 0x23456789ABCD,
            "base_entity_have_gaussian": True,
            "base_entity_gaussian": 0.375,
            "mob_potions_empty": True,
            "mob_equipment_empty": True,
            "max_health": 20.0, "absorption": 0.0,
            "base_box_min_x": 4.0, "base_box_min_y": 65.0,
            "base_box_min_z": 4.0, "base_box_max_x": 5.0,
            "base_box_max_y": 66.0, "base_box_max_z": 5.0,
        })
        tameable_state["living_leashes"] = [{
            "eid": 90, "uuid_most": -500, "uuid_least": 700,
            "leashed": True, "holder_kind": 1, "holder_eid": 0,
            "holder_uuid_most":
                int("a01e3843e5213998", 16) - (1 << 64),
            "holder_uuid_least":
                int("958af459800e4d11", 16) - (1 << 64),
            "pending": False,
            "pending_x": 0, "pending_y": 0, "pending_z": 0,
            "wolf_angry": True,
        }]
        tameable_state_path = root / "tameable_state.json"
        tameable_state_path.write_text(
            json.dumps(tameable_state), encoding="utf-8")
        tameable_capsule = root / "tameable_capsule"
        create_capsule(
            tameable_state_path, blocks_path, box, tameable_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        tameable_events = magma_events(tameable_capsule)
        tameable_spawn = next(
            row for row in tameable_events
            if row["type"] == "spawn_mob_fixture" and row["eid"] == 90
        )
        tameable_restore = next(
            row for row in tameable_events
            if row["type"] == "restore_tameable_state"
        )
        assert tameable_spawn["entity"] == 16 \
            and tameable_spawn["health"] == 17.0
        assert tameable_restore == {
            "tick": 0, "type": "restore_tameable_state", "eid": 90,
            "tamed": 1, "sitting": 1, "player_owner": 1,
            "variant": 5, "growing_age": -1200,
            "living_sound_time": 23,
            "entity_seed48": 0x23456789ABCD,
            "entity_have_gaussian": 1, "entity_gaussian": 0.375,
        }
        assert any(
            row["type"] == "set_wolf_angry" and row["eid"] == 90
            and row["angry"] == 1 for row in tameable_events
        )
        assert any(
            row["type"] == "restore_living_leash" and row["eid"] == 90
            and row["holder_kind"] == 1 and row["holder_eid"] == 0
            for row in tameable_events
        )
        shulker_state = copy.deepcopy(state)
        shulker_state["entities"].extend(({
            "eid": 90, "type": "EntityShulker", "loaded_order": 2,
            "x": 3.5, "y": 64.0, "z": -2.5,
            "dx": 2.0, "dy": -1.0, "dz": 0.0,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "yaw": 180.0, "pitch": -12.0, "health": 27.5,
            "hurt_time": 4, "death_time": 0,
            "hurt_resistant_time": 8, "no_ai": True,
            "shulker_exact": True,
            "attach_x": 3, "attach_y": 64, "attach_z": -3,
            "face": 0, "peek_tick": 30, "peek_time": 0,
            "attack_time": 0, "has_player_target": False,
            "watch_time": 0, "idle_look_time": 0,
            "living_sound_time": 17, "ticks_existed": 42,
            "last_damage": 3.0, "prev_peek_amount": 0.2,
            "peek_amount": 0.25, "head_yaw": 35.0,
            "head_pitch": -12.0, "entity_seed48": 0x123456789ABC,
        }, {
            "eid": 93, "type": "EntityShulkerBullet", "loaded_order": 3,
            "x": 3.5, "y": 64.5, "z": -2.5,
            "dx": 2.0, "dy": -0.5, "dz": 0.0,
            "vx": 0.01, "vy": 0.02, "vz": -0.03,
            "yaw": 170.0, "pitch": 12.0, "health": -1.0,
            "shulker_bullet_exact": True, "owner_eid": 90,
            "target_player": True, "direction": 2, "steps": 18,
            "ticks_existed": 7, "target_dx": 0.04,
            "target_dy": -0.05, "target_dz": 0.06,
            "entity_seed48": 0x23456789ABCD,
        }))
        shulker_state_path = root / "shulker_state.json"
        shulker_state_path.write_text(
            json.dumps(shulker_state), encoding="utf-8")
        shulker_capsule = root / "shulker_capsule"
        create_capsule(
            shulker_state_path, blocks_path, box, shulker_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        shulker_events = magma_events(shulker_capsule)
        shulker_spawn = next(
            row for row in shulker_events
            if row["type"] == "spawn_shulker_state_fixture")
        bullet_spawn = next(
            row for row in shulker_events
            if row["type"] == "spawn_shulker_bullet_state_fixture")
        assert shulker_spawn["eid"] == 90 \
            and shulker_spawn["peek_tick"] == 30 \
            and shulker_spawn["entity_seed48"] == 0x123456789ABC
        assert bullet_spawn["eid"] == 93 \
            and bullet_spawn["owner_eid"] == 90 \
            and bullet_spawn["target_dz"] == 0.06
        orphan_bullet = copy.deepcopy(shulker_state)
        next(entity for entity in orphan_bullet["entities"]
             if entity["type"] == "EntityShulkerBullet")["owner_eid"] = 12345
        try:
            _validate_state(orphan_bullet)
        except CapsuleError as exc:
            assert "requires its exact restored shulker owner" in str(exc)
        else:
            raise AssertionError(
                "exact shulker bullet accepted a missing owner")
        minecart_state = copy.deepcopy(state)
        minecart_state["entities"].append({
            "eid": 90, "type": "EntityMinecartChest",
            "loaded_order": 2,
            "uuid_most": -7, "uuid_least": 11,
            "x": 2.5, "y": 64.0625, "z": 1.5,
            "dx": 1.0, "dy": -0.9375, "dz": 4.0,
            "vx": 0.125, "vy": 0.0, "vz": -0.25,
            "yaw": 180.0, "pitch": 0.0, "health": -1.0,
            "minecart_kind": 1, "reverse": True,
            "rolling_amplitude": 4, "rolling_direction": -1,
            "damage": 2.5, "fuel": 0,
            "push_x": 0.0, "push_z": 0.0, "tnt_fuse": -1,
            "hopper_enabled": True, "transfer_cooldown": -1,
            "entity_seed48": 0x123456789ABC,
            "entity_have_gaussian": True,
            "entity_gaussian": -0.125,
            "items": [
                {"slot": 0, "id": 1, "count": 64, "meta": 3},
                {"slot": 26, "id": 264, "count": 2, "meta": 0},
            ],
        })
        minecart_state_path = root / "minecart_state.json"
        minecart_state_path.write_text(
            json.dumps(minecart_state), encoding="utf-8")
        minecart_capsule = root / "minecart_capsule"
        create_capsule(
            minecart_state_path, blocks_path, box, minecart_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        minecart_events = magma_events(minecart_capsule)
        minecart_spawn = next(
            row for row in minecart_events
            if row["type"] == "spawn_minecart_fixture"
        )
        assert minecart_spawn["eid"] == 90
        assert minecart_spawn["kind"] == 1
        assert minecart_spawn["reverse"] == 1
        assert minecart_spawn["entity_seed48"] == 0x123456789ABC
        assert next(
            row for row in minecart_events
            if row["type"] == "restore_minecart_uuid"
        )["most"] == -7
        minecart_slots = [
            row for row in minecart_events
            if row["type"] == "set_minecart_slot"
        ]
        assert [(row["slot"], row["item"], row["count"])
                for row in minecart_slots] == [(0, 1, 64), (26, 264, 2)]
        invalid_minecart = copy.deepcopy(minecart_state)
        invalid_minecart["entities"][-1]["minecart_kind"] = 5
        try:
            _validate_state(invalid_minecart)
        except CapsuleError as exc:
            assert "invalid minecart state" in str(exc)
        else:
            raise AssertionError("mismatched minecart subtype passed validation")
        custom_pig_nbt = nbt_codec.encode_hex({
            "name": "",
            "tag": {"type": "compound", "value": {
                "id": {"type": "string", "value": "minecraft:pig"},
                "Health": {"type": "short", "value": 7},
                "Saddle": {"type": "byte", "value": 1},
                "NoAI": {"type": "byte", "value": 1},
            }},
        })
        spawner_cart_state = copy.deepcopy(minecart_state)
        spawner_cart = spawner_cart_state["entities"][-1]
        spawner_cart.update({
            "type": "EntityMinecartMobSpawner",
            "minecart_kind": 4,
            "items": [],
            "spawner_entity_id": "minecraft:pig",
            "spawner_delay": 0,
            "spawner_min_delay": 7,
            "spawner_max_delay": 11,
            "spawner_spawn_count": 1,
            "spawner_max_nearby": 6,
            "spawner_activate_range": 16,
            "spawner_spawn_range": 4,
            "spawner_spawn_data_nbt": custom_pig_nbt,
            "spawner_default_entity_nbt": False,
            "spawner_potentials": [
                {
                    "weight": 2,
                    "entity_id": "minecraft:zombie",
                    "entity_nbt": default_zombie_nbt,
                    "default_entity_nbt": True,
                },
                {
                    "weight": 3,
                    "entity_id": "minecraft:pig",
                    "entity_nbt": custom_pig_nbt,
                    "default_entity_nbt": False,
                },
            ],
        })
        spawner_cart_path = root / "spawner_cart_state.json"
        spawner_cart_path.write_text(
            json.dumps(spawner_cart_state), encoding="utf-8")
        spawner_cart_capsule = root / "spawner_cart_capsule"
        create_capsule(
            spawner_cart_path, blocks_path, box, spawner_cart_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        spawner_cart_events = magma_events(spawner_cart_capsule)
        assert next(
            row for row in spawner_cart_events
            if row["type"] == "set_minecart_spawner_state"
        )["spawn_nbt_file"] == "minecart_spawner_spawn_data_0002.nbt"
        assert [
            (row["entity"], row["weight"], row["default_entity_nbt"])
            for row in spawner_cart_events
            if row["type"] == "add_minecart_spawner_potential"
        ] == [(2, 2, True), (11, 3, False)]
        invalid_spawner_cart = copy.deepcopy(spawner_cart_state)
        invalid_spawner_cart["entities"][-1][
            "spawner_default_entity_nbt"] = True
        try:
            _validate_state(invalid_spawner_cart)
        except CapsuleError as exc:
            assert "inconsistent minecart SpawnData" in str(exc)
        else:
            raise AssertionError(
                "inconsistent minecart SpawnData passed validation")
        ridden_state = copy.deepcopy(minecart_state)
        ridden_state["player"]["riding_eid"] = 90
        ridden_state["entities"][-1]["type"] = "EntityMinecartEmpty"
        ridden_state["entities"][-1]["minecart_kind"] = 0
        ridden_state["entities"][-1]["items"] = []
        ridden_state_path = root / "ridden_minecart_state.json"
        ridden_state_path.write_text(
            json.dumps(ridden_state), encoding="utf-8")
        ridden_capsule = root / "ridden_minecart_capsule"
        create_capsule(
            ridden_state_path, blocks_path, box, ridden_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert next(
            row for row in magma_events(ridden_capsule)
            if row["type"] == "restore_player_riding"
        )["eid"] == 90
        invalid_vehicle = copy.deepcopy(ridden_state)
        invalid_vehicle["player"]["riding_eid"] = 91
        try:
            _validate_state(invalid_vehicle)
        except CapsuleError as exc:
            assert "requires an exact rideable minecart" in str(exc)
        else:
            raise AssertionError("missing player vehicle passed validation")
        fish_state = copy.deepcopy(state)
        fish_state["player"]["held_id"] = 346
        fish_state["player"]["held_count"] = 1
        fish_state["inventory"] = [
            {"slot": 2, "id": 346, "count": 1, "meta": 7,
             "enchants": []},
        ]
        fish_state["entities"].append({
            "eid": 90, "type": "EntityFishHook", "loaded_order": 2,
            "x": 1.5, "y": 65.8, "z": -4.5,
            "dx": 0.0, "dy": 0.8, "dz": -2.0,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "yaw": 15.0, "pitch": -5.0, "health": -1.0,
            "fish_state": 1, "in_ground": False,
            "ticks_in_ground": 0, "ticks_in_air": 8,
            "ticks_catchable": 0, "ticks_caught_delay": 123,
            "ticks_catchable_delay": 0, "fish_approach_angle": 37.5,
            "lure": 2, "luck": 3, "caught_eid": 91,
            "entity_seed48": 0x23456789ABCD,
            "entity_have_gaussian": True,
            "entity_gaussian": 0.75,
        })
        fish_state_path = root / "fish_hook_state.json"
        fish_state_path.write_text(json.dumps(fish_state), encoding="utf-8")
        fish_capsule = root / "fish_hook_capsule"
        create_capsule(
            fish_state_path, blocks_path, box, fish_capsule,
            sky_light_path=sky_path, seed=7,
            source_engine="selftest", source_version="1",
        )
        fish_events = magma_events(fish_capsule)
        fish_spawn = next(
            row for row in fish_events
            if row["type"] == "spawn_fish_hook_fixture"
        )
        assert fish_spawn["eid"] == 90 and fish_spawn["caught_eid"] == 91
        assert fish_spawn["ticks_caught_delay"] == 123
        assert next(
            index for index, row in enumerate(fish_events)
            if row["type"] == "spawn_mob_fixture" and row["eid"] == 91
        ) < next(
            index for index, row in enumerate(fish_events)
            if row["type"] == "spawn_fish_hook_fixture"
        )
        invalid_fish = copy.deepcopy(fish_state)
        invalid_fish["entities"][-1]["caught_eid"] = 92
        try:
            _validate_state(invalid_fish)
        except CapsuleError as exc:
            assert "not an exact restorable" in str(exc)
        else:
            raise AssertionError("fishing hook accepted an XP-orb target")
        legacy_state = json.loads(json.dumps(state))
        for entity in legacy_state["entities"]:
            del entity["loaded_order"]
        legacy_state_path = root / "legacy_multi_entity_state.json"
        legacy_state_path.write_text(
            json.dumps(legacy_state), encoding="utf-8")
        legacy_capsule = root / "legacy_multi_entity_capsule"
        create_capsule(
            legacy_state_path, blocks_path, box, legacy_capsule,
            sky_light_path=sky_path,
            seed=7, source_engine="selftest", source_version="1",
        )
        try:
            magma_events(legacy_capsule)
        except CapsuleError as exc:
            assert "regenerate" in str(exc)
        else:
            raise AssertionError(
                "ambiguous legacy multi-entity order was restored"
            )
        scheduled_events = [
            row for row in events if row["type"] == "schedule_tick"
        ]
        assert len(scheduled_events) == 2, scheduled_events
        assert scheduled_events[0]["time"] == 45
        assert scheduled_events[1]["block"] == 8
        fire_box = [-2, 62, -2, 2, 69, 2]
        fire_state = copy.deepcopy(state)
        fire_state["scheduled_ticks"] = [{
            "x": 0, "y": 64, "z": 0, "block": 51,
            "time": 46, "priority": 0, "order": 18,
        }]
        fire_state["scheduled_tick_context"] = [{
            "x": 0, "y": 64, "z": 0, "block": 51,
            "high_humidity": False, "difficulty": 2,
            "do_fire_tick": True, "raining": False,
        }]
        fire_state_path = root / "fire_state.json"
        fire_state_path.write_text(
            json.dumps(fire_state), encoding="utf-8")
        fire_states = [0] * 200
        for z in range(-2, 3):
            for x in range(-2, 3):
                offset = ((63 - fire_box[1]) * 5
                          + (z - fire_box[2])) * 5 + (x - fire_box[0])
                fire_states[offset] = 1 << 4
        for x, y, z, packed in (
            (0, 64, 0, 51 << 4),
            (1, 64, 0, 5 << 4),
        ):
            offset = ((y - fire_box[1]) * 5
                      + (z - fire_box[2])) * 5 + (x - fire_box[0])
            fire_states[offset] = packed
        fire_blocks_path = root / "fire_source.bin"
        fire_blocks_path.write_bytes(
            struct.pack("<200H", *fire_states))
        fire_capsule = root / "fire_capsule"
        create_capsule(
            fire_state_path, fire_blocks_path, fire_box, fire_capsule,
            seed=7, source_engine="selftest", source_version="1",
        )
        fire_events = [
            row for row in magma_events(fire_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(fire_events) == 1 and fire_events[0]["block"] == 51
        netherrack_fire_states = list(fire_states)
        netherrack_support_offset = (
            ((63 - fire_box[1]) * 5 + (0 - fire_box[2])) * 5
            + (0 - fire_box[0])
        )
        netherrack_fire_states[netherrack_support_offset] = 87 << 4
        netherrack_fire_path = root / "netherrack_fire_source.bin"
        netherrack_fire_path.write_bytes(
            struct.pack("<200H", *netherrack_fire_states))
        netherrack_fire_capsule = root / "netherrack_fire_capsule"
        create_capsule(
            fire_state_path, netherrack_fire_path, fire_box,
            netherrack_fire_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        netherrack_fire_events = [
            row for row in magma_events(netherrack_fire_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(netherrack_fire_events) == 1 \
            and netherrack_fire_events[0]["block"] == 51
        nether_fire_state = copy.deepcopy(fire_state)
        nether_fire_state["player"]["dim"] = -1
        nether_fire_state_path = root / "nether_fire_state.json"
        nether_fire_state_path.write_text(
            json.dumps(nether_fire_state), encoding="utf-8")
        nether_fire_capsule = root / "nether_fire_capsule"
        create_capsule(
            nether_fire_state_path, netherrack_fire_path, fire_box,
            nether_fire_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        nether_fire_events = magma_events(nether_fire_capsule)
        assert len([
            row for row in nether_fire_events
            if row["type"] == "schedule_tick" and row["block"] == 51
        ]) == 1
        end_fire_state = copy.deepcopy(fire_state)
        end_fire_state["player"]["dim"] = 1
        end_fire_state_path = root / "end_fire_state.json"
        end_fire_state_path.write_text(
            json.dumps(end_fire_state), encoding="utf-8")
        end_fire_states = list(fire_states)
        end_fire_states[netherrack_support_offset] = 7 << 4
        end_fire_path = root / "end_bedrock_fire_source.bin"
        end_fire_path.write_bytes(struct.pack("<200H", *end_fire_states))
        end_fire_capsule = root / "end_bedrock_fire_capsule"
        create_capsule(
            end_fire_state_path, end_fire_path, fire_box,
            end_fire_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        end_fire_events = [
            row for row in magma_events(end_fire_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(end_fire_events) == 1 \
            and end_fire_events[0]["block"] == 51
        rain_fire_state = copy.deepcopy(fire_state)
        rain_fire_state["time"].update({
            "raining": True,
            "thundering": False,
            "rain_time": 19999950,
            "thunder_time": 19999950,
        })
        rain_fire_state["scheduled_tick_context"][0].update({
            "raining": True,
            "rain_time": 19999950,
            "thunder_time": 19999950,
            "raining_at": True,
            "raining_at_west": True,
            "raining_at_east": True,
            "raining_at_north": True,
            "raining_at_south": True,
            "rain_can_die_west_candidate": True,
        })
        rain_fire_state_path = root / "rain_fire_state.json"
        rain_fire_state_path.write_text(
            json.dumps(rain_fire_state), encoding="utf-8")
        rain_fire_states = list(fire_states)
        source_offset = (
            ((64 - fire_box[1]) * 5 + (0 - fire_box[2])) * 5
            + (0 - fire_box[0])
        )
        east_offset = (
            ((64 - fire_box[1]) * 5 + (0 - fire_box[2])) * 5
            + (1 - fire_box[0])
        )
        rain_fire_states[source_offset] = (51 << 4) | 15
        rain_fire_states[east_offset] = 0
        rain_fire_path = root / "rain_fire_source.bin"
        rain_fire_path.write_bytes(struct.pack("<200H", *rain_fire_states))
        rain_fire_capsule = root / "rain_fire_capsule"
        create_capsule(
            rain_fire_state_path, rain_fire_path, fire_box,
            rain_fire_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        rain_fire_events = magma_events(rain_fire_capsule)
        assert sum(row["type"] == "schedule_tick"
                   for row in rain_fire_events) == 1
        assert any(
            row["type"] == "set_weather"
            and row["raining"] == 1
            and row["rain_time"] == 19999950
            for row in rain_fire_events
        )
        assert any(
            row["type"] == "set_fire_rain_context"
            and row["can_die"] == 1
            and row["raining_at_east"] == 1
            and row["can_die_west_candidate"] == 1
            for row in rain_fire_events
        )
        thunder_fire_state = copy.deepcopy(rain_fire_state)
        thunder_fire_state["time"]["thundering"] = True
        thunder_fire_state_path = root / "thunder_fire_state.json"
        thunder_fire_state_path.write_text(
            json.dumps(thunder_fire_state), encoding="utf-8")
        thunder_fire_capsule = root / "thunder_fire_capsule"
        create_capsule(
            thunder_fire_state_path, rain_fire_path, fire_box,
            thunder_fire_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        thunder_fire_events = magma_events(thunder_fire_capsule)
        assert sum(row["type"] == "schedule_tick"
                   for row in thunder_fire_events) == 1
        assert any(
            row["type"] == "set_weather"
            and row["raining"] == 1
            and row["thundering"] == 1
            for row in thunder_fire_events
        )
        covered_rain_state = copy.deepcopy(rain_fire_state)
        for field in (
                "raining_at", "raining_at_west", "raining_at_east",
                "raining_at_north", "raining_at_south"):
            covered_rain_state["scheduled_tick_context"][0][field] = False
        covered_rain_state_path = root / "covered_rain_fire_state.json"
        covered_rain_state_path.write_text(
            json.dumps(covered_rain_state), encoding="utf-8")
        covered_rain_capsule = root / "covered_rain_fire_capsule"
        create_capsule(
            covered_rain_state_path, rain_fire_path, fire_box,
            covered_rain_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        covered_rain_events = magma_events(covered_rain_capsule)
        assert not any(
            row["type"] in (
                "schedule_tick", "set_fire_rain_context")
            for row in covered_rain_events
        )
        assert sum(row["type"] == "set_weather"
                   for row in covered_rain_events) == 1
        disabled_fire_state = copy.deepcopy(fire_state)
        disabled_fire_state["scheduled_tick_context"][0][
            "do_fire_tick"] = False
        disabled_fire_state_path = root / "disabled_fire_state.json"
        disabled_fire_state_path.write_text(
            json.dumps(disabled_fire_state), encoding="utf-8")
        disabled_fire_capsule = root / "disabled_fire_capsule"
        create_capsule(
            disabled_fire_state_path, fire_blocks_path, fire_box,
            disabled_fire_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        disabled_fire_events = magma_events(disabled_fire_capsule)
        assert any(
            row["type"] == "set_do_fire_tick" and row["enabled"] == 0
            for row in disabled_fire_events
        )
        assert sum(
            row["type"] == "schedule_tick" and row["block"] == 51
            for row in disabled_fire_events
        ) == 1
        humid_fire_state = copy.deepcopy(fire_state)
        humid_fire_state["scheduled_tick_context"][0][
            "high_humidity"] = True
        humid_fire_state_path = root / "humid_fire_state.json"
        humid_fire_state_path.write_text(
            json.dumps(humid_fire_state), encoding="utf-8")
        humid_fire_capsule = root / "humid_fire_capsule"
        create_capsule(
            humid_fire_state_path, fire_blocks_path, fire_box,
            humid_fire_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        humid_fire_events = magma_events(humid_fire_capsule)
        assert sum(
            row["type"] == "schedule_tick" and row["block"] == 51
            for row in humid_fire_events
        ) == 1
        assert any(
            row["type"] == "set_fire_humidity_context"
            and row["x"] == 0 and row["y"] == 64 and row["z"] == 0
            for row in humid_fire_events
        )
        lamp_state = copy.deepcopy(state)
        lamp_state["scheduled_ticks"] = [{
            "x": 0, "y": 64, "z": 0, "block": 124,
            "time": 46, "priority": 0, "order": 18,
        }]
        lamp_state["scheduled_tick_context"] = []
        lamp_state_path = root / "lamp_state.json"
        lamp_state_path.write_text(
            json.dumps(lamp_state), encoding="utf-8")
        lamp_states = [0] * 200
        for z in range(-2, 3):
            for x in range(-2, 3):
                offset = ((63 - fire_box[1]) * 5
                          + (z - fire_box[2])) * 5 + (x - fire_box[0])
                lamp_states[offset] = 1 << 4
        lamp_offset = ((64 - fire_box[1]) * 5
                       + (0 - fire_box[2])) * 5 + (0 - fire_box[0])
        lamp_states[lamp_offset] = 124 << 4
        lamp_blocks_path = root / "lamp_source.bin"
        lamp_blocks_path.write_bytes(
            struct.pack("<200H", *lamp_states))
        lamp_capsule = root / "lamp_capsule"
        create_capsule(
            lamp_state_path, lamp_blocks_path, fire_box, lamp_capsule,
            seed=7, source_engine="selftest", source_version="1",
        )
        lamp_events = [
            row for row in magma_events(lamp_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(lamp_events) == 1 and lamp_events[0]["block"] == 124
        powered_lamp_states = list(lamp_states)
        powered_offset = ((64 - fire_box[1]) * 5
                          + (0 - fire_box[2])) * 5 + (1 - fire_box[0])
        powered_lamp_states[powered_offset] = 152 << 4
        powered_lamp_path = root / "powered_lamp_source.bin"
        powered_lamp_path.write_bytes(
            struct.pack("<200H", *powered_lamp_states))
        powered_lamp_capsule = root / "powered_lamp_capsule"
        create_capsule(
            lamp_state_path, powered_lamp_path, fire_box,
            powered_lamp_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        powered_lamp_events = [
            row for row in magma_events(powered_lamp_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(powered_lamp_events) == 1
        plank_lamp_states = list(lamp_states)
        plank_lamp_states[powered_offset] = 5 << 4
        lever_offset = ((65 - fire_box[1]) * 5
                        + (0 - fire_box[2])) * 5 + (1 - fire_box[0])
        plank_lamp_states[lever_offset] = (69 << 4) | 13
        plank_lamp_path = root / "plank_lamp_source.bin"
        plank_lamp_path.write_bytes(
            struct.pack("<200H", *plank_lamp_states))
        plank_lamp_capsule = root / "plank_lamp_capsule"
        create_capsule(
            lamp_state_path, plank_lamp_path, fire_box,
            plank_lamp_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        plank_lamp_events = [
            row for row in magma_events(plank_lamp_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(plank_lamp_events) == 1
        unsupported_lamp_states = list(lamp_states)
        unsupported_lamp_states[powered_offset] = 151 << 4
        unsupported_lamp_path = root / "unsupported_lamp_source.bin"
        unsupported_lamp_path.write_bytes(
            struct.pack("<200H", *unsupported_lamp_states))
        unsupported_lamp_capsule = root / "unsupported_lamp_capsule"
        create_capsule(
            lamp_state_path, unsupported_lamp_path, fire_box,
            unsupported_lamp_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        unsupported_lamp_events = [
            row for row in magma_events(unsupported_lamp_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert not unsupported_lamp_events, unsupported_lamp_events
        observer_state = copy.deepcopy(state)
        observer_state["scheduled_ticks"] = [{
            "x": 0, "y": 64, "z": 0, "block": 218,
            "time": 44, "priority": 0, "order": 18,
        }]
        observer_state["scheduled_tick_context"] = []
        observer_state_path = root / "observer_state.json"
        observer_state_path.write_text(
            json.dumps(observer_state), encoding="utf-8")
        observer_states = [0] * 200
        for z in range(-2, 3):
            for x in range(-2, 3):
                offset = ((63 - fire_box[1]) * 5
                          + (z - fire_box[2])) * 5 + (x - fire_box[0])
                observer_states[offset] = 1 << 4
        observer_states[lamp_offset] = (218 << 4) | 4
        observer_states[powered_offset] = 123 << 4
        observer_blocks_path = root / "observer_source.bin"
        observer_blocks_path.write_bytes(
            struct.pack("<200H", *observer_states))
        observer_capsule = root / "observer_capsule"
        create_capsule(
            observer_state_path, observer_blocks_path, fire_box,
            observer_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        observer_events = [
            row for row in magma_events(observer_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(observer_events) == 1 \
            and observer_events[0]["block"] == 218
        invalid_observer_states = list(observer_states)
        invalid_observer_states[lamp_offset] = (218 << 4) | 6
        invalid_observer_path = root / "invalid_observer_source.bin"
        invalid_observer_path.write_bytes(
            struct.pack("<200H", *invalid_observer_states))
        invalid_observer_capsule = root / "invalid_observer_capsule"
        create_capsule(
            observer_state_path, invalid_observer_path, fire_box,
            invalid_observer_capsule, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert not any(
            row["type"] == "schedule_tick"
            for row in magma_events(invalid_observer_capsule)
        )
        unsafe_observer_states = list(observer_states)
        unsafe_neighbor_offset = (
            ((64 - fire_box[1]) * 5 + (0 - fire_box[2])) * 5
            + (2 - fire_box[0])
        )
        unsafe_observer_states[unsafe_neighbor_offset] = 151 << 4
        unsafe_observer_path = root / "unsafe_observer_source.bin"
        unsafe_observer_path.write_bytes(
            struct.pack("<200H", *unsafe_observer_states))
        unsafe_observer_capsule = root / "unsafe_observer_capsule"
        create_capsule(
            observer_state_path, unsafe_observer_path, fire_box,
            unsafe_observer_capsule, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert not any(
            row["type"] == "schedule_tick"
            for row in magma_events(unsafe_observer_capsule)
        )
        repeater_state = copy.deepcopy(state)
        repeater_state["scheduled_ticks"] = [{
            "x": 0, "y": 64, "z": 0, "block": 93,
            "time": 46, "priority": -1, "order": 18,
        }]
        repeater_state["scheduled_tick_context"] = []
        repeater_state_path = root / "repeater_state.json"
        repeater_state_path.write_text(
            json.dumps(repeater_state), encoding="utf-8")
        repeater_states = [0] * 200
        for z in range(-2, 3):
            for x in range(-2, 3):
                offset = ((63 - fire_box[1]) * 5
                          + (z - fire_box[2])) * 5 + (x - fire_box[0])
                repeater_states[offset] = 1 << 4
        repeater_states[lamp_offset] = (93 << 4) | 1
        repeater_states[powered_offset] = 123 << 4
        repeater_input_offset = ((64 - fire_box[1]) * 5
                                 + (0 - fire_box[2])) * 5 \
            + (-1 - fire_box[0])
        repeater_states[repeater_input_offset] = 152 << 4
        repeater_blocks_path = root / "repeater_source.bin"
        repeater_blocks_path.write_bytes(
            struct.pack("<200H", *repeater_states))
        repeater_capsule = root / "repeater_capsule"
        create_capsule(
            repeater_state_path, repeater_blocks_path, fire_box,
            repeater_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        repeater_events = [
            row for row in magma_events(repeater_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(repeater_events) == 1 \
            and repeater_events[0]["block"] == 93
        powered_repeater_state = copy.deepcopy(repeater_state)
        powered_repeater_state["scheduled_ticks"][0]["block"] = 94
        powered_repeater_state["scheduled_ticks"][0]["priority"] = -2
        powered_repeater_state_path = root / "powered_repeater_state.json"
        powered_repeater_state_path.write_text(
            json.dumps(powered_repeater_state), encoding="utf-8")
        powered_repeater_states = list(repeater_states)
        powered_repeater_states[lamp_offset] = (94 << 4) | 1
        powered_repeater_states[powered_offset] = 124 << 4
        powered_repeater_states[repeater_input_offset] = 0
        powered_repeater_blocks_path = (
            root / "powered_repeater_source.bin"
        )
        powered_repeater_blocks_path.write_bytes(
            struct.pack("<200H", *powered_repeater_states))
        powered_repeater_capsule = root / "powered_repeater_capsule"
        create_capsule(
            powered_repeater_state_path, powered_repeater_blocks_path,
            fire_box, powered_repeater_capsule, seed=7,
            source_engine="selftest", source_version="1",
        )
        powered_repeater_events = [
            row for row in magma_events(powered_repeater_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(powered_repeater_events) == 1 \
            and powered_repeater_events[0]["block"] == 94
        unsafe_repeater_states = list(repeater_states)
        unsafe_repeater_side_offset = ((64 - fire_box[1]) * 5
                                       + (1 - fire_box[2])) * 5 \
            + (0 - fire_box[0])
        unsafe_repeater_states[unsafe_repeater_side_offset] = 149 << 4
        unsafe_repeater_path = root / "unsafe_repeater_source.bin"
        unsafe_repeater_path.write_bytes(
            struct.pack("<200H", *unsafe_repeater_states))
        unsafe_repeater_capsule = root / "unsafe_repeater_capsule"
        create_capsule(
            repeater_state_path, unsafe_repeater_path, fire_box,
            unsafe_repeater_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        assert not any(
            row["type"] == "schedule_tick"
            for row in magma_events(unsafe_repeater_capsule)
        )
        weighted_plate_state = copy.deepcopy(state)
        weighted_plate_state["entities"] = []
        weighted_plate_state["scheduled_ticks"] = [{
            "x": 0, "y": 64, "z": 0, "block": 147,
            "time": 45, "priority": 0, "order": 18,
        }]
        weighted_plate_state["scheduled_tick_context"] = []
        weighted_plate_state_path = root / "weighted_plate_state.json"
        weighted_plate_state_path.write_text(
            json.dumps(weighted_plate_state), encoding="utf-8")
        weighted_plate_states = [0] * 200
        for z in range(-2, 3):
            for x in range(-2, 3):
                offset = ((63 - fire_box[1]) * 5
                          + (z - fire_box[2])) * 5 + (x - fire_box[0])
                weighted_plate_states[offset] = 1 << 4
        weighted_plate_states[lamp_offset] = (147 << 4) | 2
        weighted_plate_states[powered_offset] = (55 << 4) | 2
        weighted_lamp_offset = ((64 - fire_box[1]) * 5
                                + (0 - fire_box[2])) * 5 \
            + (2 - fire_box[0])
        weighted_plate_states[weighted_lamp_offset] = 124 << 4
        weighted_plate_blocks_path = root / "weighted_plate_source.bin"
        weighted_plate_blocks_path.write_bytes(
            struct.pack("<200H", *weighted_plate_states))
        weighted_plate_capsule = root / "weighted_plate_capsule"
        create_capsule(
            weighted_plate_state_path, weighted_plate_blocks_path, fire_box,
            weighted_plate_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        weighted_plate_events = [
            row for row in magma_events(weighted_plate_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(weighted_plate_events) == 1 \
            and weighted_plate_events[0]["block"] == 147
        occupied_weighted_state = copy.deepcopy(weighted_plate_state)
        occupied_weighted_state["entities"] = [
            copy.deepcopy(state["entities"][1])
        ]
        occupied_weighted_state_path = root / "occupied_weighted_state.json"
        occupied_weighted_state_path.write_text(
            json.dumps(occupied_weighted_state), encoding="utf-8")
        occupied_weighted_capsule = root / "occupied_weighted_capsule"
        create_capsule(
            occupied_weighted_state_path, weighted_plate_blocks_path,
            fire_box, occupied_weighted_capsule, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert not any(
            row["type"] == "schedule_tick"
            for row in magma_events(occupied_weighted_capsule)
        )
        button_state = copy.deepcopy(state)
        button_state["scheduled_ticks"] = [{
            "x": 0, "y": 64, "z": 0, "block": 77,
            "time": 46, "priority": 0, "order": 18,
        }]
        button_state["scheduled_tick_context"] = []
        button_state_path = root / "button_state.json"
        button_state_path.write_text(
            json.dumps(button_state), encoding="utf-8")
        button_states = list(lamp_states)
        button_states[lamp_offset] = (77 << 4) | 13
        button_states[powered_offset] = 124 << 4
        button_blocks_path = root / "button_source.bin"
        button_blocks_path.write_bytes(
            struct.pack("<200H", *button_states))
        button_capsule = root / "button_capsule"
        create_capsule(
            button_state_path, button_blocks_path, fire_box, button_capsule,
            seed=7, source_engine="selftest", source_version="1",
        )
        button_events = [
            row for row in magma_events(button_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(button_events) == 1 and button_events[0]["block"] == 77
        released_button_states = list(button_states)
        released_button_states[lamp_offset] = (77 << 4) | 5
        released_button_path = root / "released_button_source.bin"
        released_button_path.write_bytes(
            struct.pack("<200H", *released_button_states))
        released_button_capsule = root / "released_button_capsule"
        create_capsule(
            button_state_path, released_button_path, fire_box,
            released_button_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        assert not any(
            row["type"] == "schedule_tick"
            for row in magma_events(released_button_capsule)
        )
        wood_button_state = copy.deepcopy(button_state)
        wood_button_state["scheduled_ticks"][0]["block"] = 143
        wood_button_state["scheduled_ticks"][0]["time"] = 45
        wood_button_state_path = root / "wood_button_state.json"
        wood_button_state_path.write_text(
            json.dumps(wood_button_state), encoding="utf-8")
        wood_button_states = list(button_states)
        wood_button_states[lamp_offset] = (143 << 4) | 13
        wood_button_blocks_path = root / "wood_button_source.bin"
        wood_button_blocks_path.write_bytes(
            struct.pack("<200H", *wood_button_states))
        wood_button_capsule = root / "wood_button_capsule"
        create_capsule(
            wood_button_state_path, wood_button_blocks_path, fire_box,
            wood_button_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        wood_button_events = [
            row for row in magma_events(wood_button_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(wood_button_events) == 1 \
            and wood_button_events[0]["block"] == 143
        occupied_wood_button_state = copy.deepcopy(wood_button_state)
        occupied_arrow = copy.deepcopy(state["entities"][1])
        occupied_arrow["type"] = "EntityTippedArrow"
        occupied_wood_button_state["entities"] = [occupied_arrow]
        occupied_wood_button_state_path = (
            root / "occupied_wood_button_state.json"
        )
        occupied_wood_button_state_path.write_text(
            json.dumps(occupied_wood_button_state), encoding="utf-8")
        occupied_wood_button_capsule = (
            root / "occupied_wood_button_capsule"
        )
        create_capsule(
            occupied_wood_button_state_path, wood_button_blocks_path,
            fire_box, occupied_wood_button_capsule, seed=7,
            source_engine="selftest", source_version="1",
        )
        assert not any(
            row["type"] == "schedule_tick"
            for row in magma_events(occupied_wood_button_capsule)
        )
        torch_state = copy.deepcopy(state)
        torch_state["scheduled_ticks"] = [{
            "x": 0, "y": 64, "z": 0, "block": 76,
            "time": 46, "priority": 0, "order": 18,
        }]
        torch_state["scheduled_tick_context"] = []
        torch_state_path = root / "torch_state.json"
        torch_state_path.write_text(
            json.dumps(torch_state), encoding="utf-8")
        torch_states = [0] * 200
        torch_states[lamp_offset] = (76 << 4) | 5
        torch_support_offset = ((63 - fire_box[1]) * 5
                                + (0 - fire_box[2])) * 5 \
            + (0 - fire_box[0])
        torch_states[torch_support_offset] = 152 << 4
        torch_blocks_path = root / "torch_source.bin"
        torch_blocks_path.write_bytes(
            struct.pack("<200H", *torch_states))
        torch_capsule = root / "torch_capsule"
        create_capsule(
            torch_state_path, torch_blocks_path, fire_box, torch_capsule,
            seed=7, source_engine="selftest", source_version="1",
        )
        torch_events = [
            row for row in magma_events(torch_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(torch_events) == 1 and torch_events[0]["block"] == 76
        stale_torch_states = list(torch_states)
        stale_torch_states[torch_support_offset] = 1 << 4
        stale_torch_path = root / "stale_torch_source.bin"
        stale_torch_path.write_bytes(
            struct.pack("<200H", *stale_torch_states))
        stale_torch_capsule = root / "stale_torch_capsule"
        create_capsule(
            torch_state_path, stale_torch_path, fire_box,
            stale_torch_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        stale_torch_events = [
            row for row in magma_events(stale_torch_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(stale_torch_events) == 1 \
            and stale_torch_events[0]["block"] == 76
        invalid_torch_states = list(torch_states)
        invalid_torch_states[torch_support_offset] = 0
        invalid_torch_path = root / "invalid_torch_source.bin"
        invalid_torch_path.write_bytes(
            struct.pack("<200H", *invalid_torch_states))
        invalid_torch_capsule = root / "invalid_torch_capsule"
        create_capsule(
            torch_state_path, invalid_torch_path, fire_box,
            invalid_torch_capsule, seed=7, source_engine="selftest",
            source_version="1",
        )
        assert not any(
            row["type"] == "schedule_tick"
            for row in magma_events(invalid_torch_capsule)
        )
        for wall_meta, (support_x, support_z) in {
                1: (-1, 0), 2: (1, 0), 3: (0, -1), 4: (0, 1),
        }.items():
            wall_torch_states = [0] * 200
            wall_torch_states[lamp_offset] = (76 << 4) | wall_meta
            wall_support_offset = ((64 - fire_box[1]) * 5
                                   + (support_z - fire_box[2])) * 5 \
                + (support_x - fire_box[0])
            wall_torch_states[wall_support_offset] = 152 << 4
            wall_torch_path = root / f"wall_torch_{wall_meta}_source.bin"
            wall_torch_path.write_bytes(
                struct.pack("<200H", *wall_torch_states))
            wall_torch_capsule = root / f"wall_torch_{wall_meta}_capsule"
            create_capsule(
                torch_state_path, wall_torch_path, fire_box,
                wall_torch_capsule, seed=7, source_engine="selftest",
                source_version="1",
            )
            wall_torch_events = [
                row for row in magma_events(wall_torch_capsule)
                if row["type"] == "schedule_tick"
            ]
            assert len(wall_torch_events) == 1 \
                and wall_torch_events[0]["block"] == 76
            wall_torch_states[wall_support_offset] = 0
            invalid_wall_torch_path = (
                root / f"invalid_wall_torch_{wall_meta}_source.bin"
            )
            invalid_wall_torch_path.write_bytes(
                struct.pack("<200H", *wall_torch_states))
            invalid_wall_torch_capsule = (
                root / f"invalid_wall_torch_{wall_meta}_capsule"
            )
            create_capsule(
                torch_state_path, invalid_wall_torch_path, fire_box,
                invalid_wall_torch_capsule, seed=7,
                source_engine="selftest", source_version="1",
            )
            assert not any(
                row["type"] == "schedule_tick"
                for row in magma_events(invalid_wall_torch_capsule)
            )
        directional_torch_supports = (
            ("top_slab", 5, 0, -1, 0, 44, 8, True),
            ("top_stair", 5, 0, -1, 0, 53, 4, True),
            ("full_snow", 5, 0, -1, 0, 78, 7, True),
            ("hopper_top", 5, 0, -1, 0, 154, 0, True),
            ("farmland_side", 1, -1, 0, 0, 60, 0, True),
            ("stair_side", 1, -1, 0, 0, 53, 0, True),
            ("oak_fence_top", 5, 0, -1, 0, 85, 0, True),
            ("nether_fence_top", 5, 0, -1, 0, 113, 0, True),
            ("spruce_fence_top", 5, 0, -1, 0, 188, 0, True),
            ("birch_fence_top", 5, 0, -1, 0, 189, 0, True),
            ("jungle_fence_top", 5, 0, -1, 0, 190, 0, True),
            ("dark_oak_fence_top", 5, 0, -1, 0, 191, 0, True),
            ("acacia_fence_top", 5, 0, -1, 0, 192, 0, True),
            ("glass_top", 5, 0, -1, 0, 20, 0, True),
            ("stained_glass_top", 5, 0, -1, 0, 95, 0, True),
            ("cobblestone_wall_top", 5, 0, -1, 0, 139, 0, True),
            ("bottom_slab", 5, 0, -1, 0, 44, 0, False),
            ("bottom_stair", 5, 0, -1, 0, 53, 0, False),
            ("partial_snow", 5, 0, -1, 0, 78, 6, False),
            ("hopper_side", 1, -1, 0, 0, 154, 0, False),
            ("wrong_stair_side", 1, -1, 0, 0, 53, 1, False),
            ("fence_side", 1, -1, 0, 0, 85, 0, False),
        )
        for (label, torch_meta, support_dx, support_dy, support_dz,
             support_id, support_meta, admitted) in directional_torch_supports:
            directional_states = [0] * 200
            directional_states[lamp_offset] = (76 << 4) | torch_meta
            support_offset = (
                ((64 + support_dy - fire_box[1]) * 5
                 + (support_dz - fire_box[2])) * 5
                + (support_dx - fire_box[0])
            )
            directional_states[support_offset] = (
                (support_id << 4) | support_meta
            )
            directional_path = root / f"torch_{label}_source.bin"
            directional_path.write_bytes(
                struct.pack("<200H", *directional_states))
            directional_capsule = root / f"torch_{label}_capsule"
            create_capsule(
                torch_state_path, directional_path, fire_box,
                directional_capsule, seed=7, source_engine="selftest",
                source_version="1",
            )
            directional_events = [
                row for row in magma_events(directional_capsule)
                if row["type"] == "schedule_tick"
            ]
            assert bool(directional_events) == admitted
            if admitted:
                assert len(directional_events) == 1 \
                    and directional_events[0]["block"] == 76
        lava_state = copy.deepcopy(state)
        lava_state["scheduled_ticks"] = [
            dict(state["scheduled_ticks"][0]),
            {
                "x": 0, "y": 64, "z": 0, "block": 10,
                "time": 46, "priority": 0, "order": 18,
            },
        ]
        lava_state_path = root / "lava_state.json"
        lava_state_path.write_text(json.dumps(lava_state), encoding="utf-8")
        lava_states = [0] * 484
        for z in range(-5, 6):
            for x in range(-5, 6):
                index = ((63 - box[1]) * 11 + (z - box[2])) * 11 \
                    + (x - box[0])
                lava_states[index] = 1 << 4
        for x, y, z, packed in (
            (-5, 64, -5, 1 << 4),
            (0, 64, 0, 10 << 4),
        ):
            index = ((y - box[1]) * 11 + (z - box[2])) * 11 \
                + (x - box[0])
            lava_states[index] = packed
        lava_blocks_path = root / "lava_source.bin"
        lava_blocks_path.write_bytes(struct.pack("<484H", *lava_states))
        lava_capsule = root / "lava_capsule"
        create_capsule(
            lava_state_path, lava_blocks_path, box, lava_capsule,
            seed=7, source_engine="selftest", source_version="1",
        )
        lava_events = [
            row for row in magma_events(lava_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert len(lava_events) == 2 and lava_events[1]["block"] == 10
        reaction_state = copy.deepcopy(state)
        reaction_state["scheduled_ticks"] = [
            dict(state["scheduled_ticks"][0]),
            {
                "x": 0, "y": 64, "z": 0, "block": 8,
                "time": 46, "priority": 0, "order": 18,
            },
            {
                "x": 0, "y": 65, "z": 0, "block": 10,
                "time": 47, "priority": 0, "order": 19,
            },
        ]
        reaction_state_path = root / "reaction_state.json"
        reaction_state_path.write_text(
            json.dumps(reaction_state), encoding="utf-8")
        reaction_states = [0] * 484
        for z in range(-5, 6):
            for x in range(-5, 6):
                index = ((63 - box[1]) * 11 + (z - box[2])) * 11 \
                    + (x - box[0])
                reaction_states[index] = 1 << 4
        for x, y, z, packed in (
            (-5, 64, -5, 1 << 4),
            (0, 64, -1, 1 << 4),
            (0, 64, 1, 1 << 4),
            (-1, 64, 0, 1 << 4),
            (1, 64, 0, 1 << 4),
            (0, 64, 0, 8 << 4),
            (0, 65, 0, 10 << 4),
        ):
            index = ((y - box[1]) * 11 + (z - box[2])) * 11 \
                + (x - box[0])
            reaction_states[index] = packed
        reaction_blocks_path = root / "reaction_source.bin"
        reaction_blocks_path.write_bytes(
            struct.pack("<484H", *reaction_states))
        reaction_capsule = root / "reaction_capsule"
        create_capsule(
            reaction_state_path, reaction_blocks_path, box, reaction_capsule,
            seed=7, source_engine="selftest", source_version="1",
        )
        reaction_events = [
            row for row in magma_events(reaction_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert [row["block"] for row in reaction_events] == [1, 8, 10]
        sand_state = copy.deepcopy(state)
        sand_state["scheduled_ticks"] = [
            dict(state["scheduled_ticks"][0]),
            {
                "x": 0, "y": 66, "z": 0, "block": 12,
                "time": 47, "priority": 0, "order": 18,
            },
            {
                "x": 2, "y": 65, "z": 0, "block": 132,
                "time": 47, "priority": 0, "order": 19,
            },
        ]
        sand_state_path = root / "sand_state.json"
        sand_state_path.write_text(
            json.dumps(sand_state), encoding="utf-8")
        sand_states = [0] * 484
        for z in range(-5, 6):
            for x in range(-5, 6):
                index = ((63 - box[1]) * 11 + (z - box[2])) * 11 \
                    + (x - box[0])
                sand_states[index] = 1 << 4
        for x, y, z, packed in (
            (-5, 64, -5, 1 << 4),
            (0, 64, 0, 132 << 4),
            (0, 66, 0, 12 << 4),
            (2, 65, 0, (132 << 4) | 1),
        ):
            index = ((y - box[1]) * 11 + (z - box[2])) * 11 \
                + (x - box[0])
            sand_states[index] = packed
        sand_blocks_path = root / "sand_source.bin"
        sand_blocks_path.write_bytes(struct.pack("<484H", *sand_states))
        sand_capsule = root / "sand_capsule"
        create_capsule(
            sand_state_path, sand_blocks_path, box, sand_capsule,
            seed=7, source_engine="selftest", source_version="1",
        )
        sand_events = [
            row for row in magma_events(sand_capsule)
            if row["type"] == "schedule_tick"
        ]
        assert [row["block"] for row in sand_events] == [1, 12, 132]
        for supported_egg in (False, True):
            egg_state = copy.deepcopy(state)
            egg_state["scheduled_ticks"] = [
                dict(state["scheduled_ticks"][0]),
                {
                    "x": 0, "y": 66, "z": 0, "block": 122,
                    "time": 47, "priority": 0, "order": 18,
                },
            ]
            egg_state_path = root / (
                "supported_egg_state.json" if supported_egg
                else "falling_egg_state.json")
            egg_state_path.write_text(
                json.dumps(egg_state), encoding="utf-8")
            egg_states = [0] * 484
            for z in range(-5, 6):
                for x in range(-5, 6):
                    index = ((63 - box[1]) * 11 + (z - box[2])) * 11 \
                        + (x - box[0])
                    egg_states[index] = 1 << 4
            for x, y, z, packed in (
                (-5, 64, -5, 1 << 4),
                (0, 66, 0, 122 << 4),
                (0, 65, 0, (1 << 4) if supported_egg else 0),
            ):
                index = ((y - box[1]) * 11 + (z - box[2])) * 11 \
                    + (x - box[0])
                egg_states[index] = packed
            egg_blocks_path = root / (
                "supported_egg_source.bin" if supported_egg
                else "falling_egg_source.bin")
            egg_blocks_path.write_bytes(struct.pack("<484H", *egg_states))
            egg_capsule = root / (
                "supported_egg_capsule" if supported_egg
                else "falling_egg_capsule")
            create_capsule(
                egg_state_path, egg_blocks_path, box, egg_capsule,
                seed=7, source_engine="selftest", source_version="1",
            )
            egg_events = [
                row for row in magma_events(egg_capsule)
                if row["type"] == "schedule_tick"
            ]
            assert [row["block"] for row in egg_events] == [1, 122]
        for supported_anvil in (False, True):
            anvil_state = copy.deepcopy(state)
            anvil_state["scheduled_ticks"] = [
                dict(state["scheduled_ticks"][0]),
                {
                    "x": 0, "y": 66, "z": 0, "block": 145,
                    "time": 47, "priority": 0, "order": 18,
                },
            ]
            anvil_state_path = root / (
                "supported_anvil_state.json" if supported_anvil
                else "falling_anvil_state.json")
            anvil_state_path.write_text(
                json.dumps(anvil_state), encoding="utf-8")
            anvil_states = [0] * 484
            for z in range(-5, 6):
                for x in range(-5, 6):
                    index = ((63 - box[1]) * 11 + (z - box[2])) * 11 \
                        + (x - box[0])
                    anvil_states[index] = 1 << 4
            for x, y, z, packed in (
                (-5, 64, -5, 1 << 4),
                (0, 66, 0, (145 << 4) | 8),
                (0, 65, 0, (1 << 4) if supported_anvil else 0),
            ):
                index = ((y - box[1]) * 11 + (z - box[2])) * 11 \
                    + (x - box[0])
                anvil_states[index] = packed
            anvil_blocks_path = root / (
                "supported_anvil_source.bin" if supported_anvil
                else "falling_anvil_source.bin")
            anvil_blocks_path.write_bytes(
                struct.pack("<484H", *anvil_states))
            anvil_capsule = root / (
                "supported_anvil_capsule" if supported_anvil
                else "falling_anvil_capsule")
            create_capsule(
                anvil_state_path, anvil_blocks_path, box, anvil_capsule,
                seed=7, source_engine="selftest", source_version="1",
            )
            anvil_events = [
                row for row in magma_events(anvil_capsule)
                if row["type"] == "schedule_tick"
            ]
            expected_blocks = [1, 145] if supported_anvil else [1]
            assert [row["block"] for row in anvil_events] == expected_blocks
        duplicate = json.loads(json.dumps(state))
        duplicate["entities"][1]["eid"] = 91
        try:
            _validate_state(duplicate)
        except CapsuleError as exc:
            assert "must be unique" in str(exc)
        else:
            raise AssertionError("duplicate entity id passed validation")
        duplicate_order = json.loads(json.dumps(state))
        duplicate_order["entities"][1]["loaded_order"] = 1
        try:
            _validate_state(duplicate_order)
        except CapsuleError as exc:
            assert "loaded_order must be unique" in str(exc)
        else:
            raise AssertionError("duplicate loaded entity order passed validation")
        partial_order = json.loads(json.dumps(state))
        del partial_order["entities"][1]["loaded_order"]
        try:
            _validate_state(partial_order)
        except CapsuleError as exc:
            assert "all include loaded_order" in str(exc)
        else:
            raise AssertionError("partial loaded entity order passed validation")
        incomplete_orb = json.loads(json.dumps(state))
        del incomplete_orb["entities"][1]["target_color"]
        try:
            _validate_state(incomplete_orb)
        except CapsuleError as exc:
            assert "target_color" in str(exc)
        else:
            raise AssertionError("incomplete exact XP orb passed validation")
        duplicate_tick = json.loads(json.dumps(state))
        duplicate_tick["scheduled_ticks"].append(
            dict(duplicate_tick["scheduled_ticks"][0]))
        try:
            _validate_state(duplicate_tick)
        except CapsuleError as exc:
            assert "duplicates" in str(exc)
        else:
            raise AssertionError("duplicate scheduled tick passed validation")
        try:
            validate_capsule(capsule, require_complete=True)
        except CapsuleError as exc:
            assert "world.rng_cursors" in str(exc)
        else:
            raise AssertionError("partial v1 capsule passed --require-complete")
        sky_payload = capsule / SKY_LIGHT_FILE
        original_sky = sky_payload.read_bytes()
        sky_payload.write_bytes(original_sky[:-1] + bytes([16]))
        try:
            validate_capsule(capsule)
        except CapsuleError as exc:
            assert "sha256 mismatch" in str(exc)
        else:
            raise AssertionError("corrupt skylight payload passed validation")
        sky_payload.write_bytes(original_sky)
        block_light_payload = capsule / BLOCK_LIGHT_FILE
        original_block_light = block_light_payload.read_bytes()
        block_light_payload.write_bytes(
            original_block_light[:-1] + bytes([16]))
        try:
            validate_capsule(capsule)
        except CapsuleError as exc:
            assert "sha256 mismatch" in str(exc)
        else:
            raise AssertionError("corrupt block-light payload passed validation")
        block_light_payload.write_bytes(original_block_light)
        payload = capsule / BLOCK_FILE
        original = payload.read_bytes()
        payload.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
        try:
            validate_capsule(capsule)
        except CapsuleError as exc:
            assert "sha256 mismatch" in str(exc)
        else:
            raise AssertionError("corrupt block payload passed validation")
    print("state_capsule selftest: PASS "
          "(round-trip, entity/scheduled payload/order/negative, "
          "incomplete-state, checksum-negative)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    create = sub.add_parser("create")
    create.add_argument("--state", required=True, type=pathlib.Path)
    create.add_argument("--blocks", required=True, type=pathlib.Path)
    create.add_argument("--sky-light", type=pathlib.Path)
    create.add_argument("--block-light", type=pathlib.Path)
    create.add_argument("--box", required=True, nargs=6, type=int)
    create.add_argument("--out", required=True, type=pathlib.Path)
    create.add_argument("--seed", required=True, type=int)
    create.add_argument("--source-engine", default="minecraft-java")
    create.add_argument("--source-version", default="1.11.2")

    validate = sub.add_parser("validate")
    validate.add_argument("--capsule", required=True, type=pathlib.Path)
    validate.add_argument("--require-complete", action="store_true")

    emit = sub.add_parser("emit-magma")
    emit.add_argument("--capsule", required=True, type=pathlib.Path)
    emit.add_argument("--out", required=True, type=pathlib.Path)

    sub.add_parser("selftest")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "create":
            path = create_capsule(
                args.state, args.blocks, args.box, args.out,
                sky_light_path=args.sky_light,
                block_light_path=args.block_light,
                seed=args.seed,
                source_engine=args.source_engine,
                source_version=args.source_version,
            )
            print(f"wrote validated state capsule -> {path}")
        elif args.command == "validate":
            manifest, _raw = validate_capsule(
                args.capsule, require_complete=args.require_complete
            )
            exact = sum(
                status == "exact"
                for status in manifest["capabilities"].values()
            )
            print(
                f"state capsule valid: schema={SCHEMA} version={VERSION} "
                f"exact_capabilities={exact}/{len(CAPABILITIES_V2)}"
            )
        elif args.command == "emit-magma":
            count = emit_magma(args.capsule, args.out)
            print(f"wrote {count} tick-zero magma events -> {args.out}")
        else:
            selftest()
    except (OSError, CapsuleError) as exc:
        print(f"state_capsule: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
