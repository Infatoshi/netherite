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
  ItemStack compounds without forcing large or nested values through the
  script parser.
* every payload is length- and SHA-256-checked before it can be emitted as
  magma script events.

Version 2 is intentionally incomplete.  It restores the fields named ``exact``
in CAPABILITIES_V2 and refuses ``--require-complete`` while general entities,
scheduled ticks, tile entities, and RNG cursors remain unsupported.  This
prevents a partial checkpoint from being mistaken for a full save/reload proof.
"""

from __future__ import annotations

import argparse
import copy
import functools
import hashlib
import json
import math
import pathlib
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
MANIFEST_FILE = "manifest.json"
BLOCK_ENCODING = "u16le:id<<4|meta:y-z-x"
SKY_LIGHT_ENCODING = "u8:nibble:y-z-x"
NBT_ENCODING = "minecraft:nbt-uncompressed-root-compound"
MAX_NBT_PAYLOAD_TOTAL = 16 << 20
BLOCKSTATE_PROPS_FILE = pathlib.Path(__file__).with_name(
    "blockstate_props_1_11_2.json"
)
BLOCKSTATE_PROPS_SHA256 = (
    "4d7a77822b3cb2591c94c2c81608520ff87dca172e80d2e5c8059c0309a4a2f0"
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
    "player.selected_slot": "exact",
    "world.block_cuboid": "exact",
    "world.light.sky_nibbles": "captured_only",
    "world.dimension": "exact",
    "world.time": "exact",
    "world.weather_clock_isolated": "exact",
    "world.weather_chunk_side_effects": "captured_only",
    "world.weather_steady_rain_fire_slice": "exact",
    "world.weather_steady_thunder_fire_slice": "exact",
    "player.air": "exact",
    "player.fire": "exact",
    "client.move_packet_cursor": "exact",
    "player.xp": "exact",
    "player.combat_timers": "exact",
    "player.potions": "exact",
    "player.armor_offhand.enchantment_subset": "exact",
    "player.inventory_arbitrary_nbt": "unavailable",
    "entities": "captured_only",
    # Integrated singleplayer shares this process-global counter with client
    # entity construction. Capturing at the parked pre-tick boundary and
    # restoring it in magma makes bounded emergent-entity fixtures exact.
    "entities.next_id": "exact",
    "entities.no_ai_pig": "exact",
    "entities.no_ai_unopened_villager": "exact",
    "entities.xp_orb": "exact",
    "entities.item_frame_comparator_source": "exact",
    "entities.hidden_state": "unavailable",
    "tile_entities": "unavailable",
    "tile_entities.comparator_output": "exact",
    "tile_entities.flower_pot": "exact",
    "tile_entities.skull_ownerless": "exact",
    "tile_entities.skull_player_profile": "exact",
    "tile_entities.moving_piston": "exact",
    "tile_entities.single_chest_inventory": "exact",
    "tile_entities.single_trapped_chest_inventory": "exact",
    "tile_entities.double_trapped_chest_inventory": "exact",
    "tile_entities.double_chest_inventory": "exact",
    "tile_entities.furnace_inventory": "exact",
    "tile_entities.brewing_stand_inventory": "exact",
    "tile_entities.dispenser_dropper_inventory": "exact",
    "tile_entities.shulker_box_plain_inventory": "exact",
    "entities.shulker_box_plain_item_payload": "exact",
    "tile_entities.shulker_box_persistent_nbt": "exact",
    "entities.shulker_box_item_nbt": "exact",
    "tile_entities.jukebox_record": "exact",
    "tile_entities.command_block_success_count": "exact",
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


class CapsuleError(ValueError):
    """A capsule violates the on-disk contract."""


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
        payload.get("schema") != "qrl.blockstate_props.v2"
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
    for key in ("player", "inventory", "entities", "time", "world_rng"):
        if key not in state:
            raise CapsuleError(f"state is missing {key!r}")
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
    for field in ("x", "y", "z", "yaw", "pitch", "vx", "vy", "vz",
                  "health", "max_health", "absorption", "food",
                  "saturation", "xp_frac", "attack_cooldown",
                  "fall_distance"):
        _finite_number(player.get(field), f"state.player.{field}")
    on_ground = player.get("on_ground")
    if on_ground not in (0, 1, False, True):
        raise CapsuleError("state.player.on_ground must be 0 or 1")
    dimension = player.get("dim")
    if isinstance(dimension, bool) or not isinstance(dimension, int) \
            or dimension not in (-1, 0, 1):
        raise CapsuleError("state.player.dim must be -1, 0, or 1")
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
    if not dead and player["death_time"] != 0:
        raise CapsuleError("a living player must have death_time 0")
    potions = player.get("potions")
    if not isinstance(potions, list) or len(potions) > 32:
        raise CapsuleError("state.player.potions must contain at most 32 effects")
    seen_potions = set()
    health_boost = 0
    for index, effect in enumerate(potions):
        label = f"state.player.potions[{index}]"
        if not isinstance(effect, dict) or set(effect) != {"id", "amp", "dur"}:
            raise CapsuleError(f"{label} must contain id/amp/dur")
        potion_id = effect["id"]
        amplifier = effect["amp"]
        duration = effect["dur"]
        if any(isinstance(value, bool) or not isinstance(value, int)
               for value in (potion_id, amplifier, duration)) \
                or not 1 <= potion_id <= 255 \
                or not 0 <= amplifier <= 255 \
                or not 1 <= duration <= 2147483647 \
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
        seen_slots.add(slot)
    entities = state["entities"]
    if not isinstance(entities, list):
        raise CapsuleError("state.entities must be an array")
    seen_eids = set()
    seen_loaded_orders = set()
    loaded_order_count = 0
    fish_hooks = []
    for index, entity in enumerate(entities):
        label = f"state.entities[{index}]"
        if not isinstance(entity, dict):
            raise CapsuleError(f"{label} must be an object")
        eid = entity.get("eid")
        entity_type = entity.get("type")
        if isinstance(eid, bool) or not isinstance(eid, int) or eid <= 0:
            raise CapsuleError(f"{label}.eid must be a positive integer")
        if eid in seen_eids:
            raise CapsuleError(f"{label}.eid must be unique")
        seen_eids.add(eid)
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
        if entity_type == "EntityPig" and entity.get("no_ai") is True:
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
        if entity_type == "EntityVillager" \
                and entity.get("no_ai") is True \
                and entity.get("offers_initialized") is False:
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
            if not 0 < float(entity["health"]) <= 20 \
                    or not 0 <= entity["hurt_time"] <= 10 \
                    or not 0 <= entity["death_time"] < 20 \
                    or not 0 <= entity["hurt_resistant_time"] <= 20 \
                    or not 0 <= entity["profession"] <= 5 \
                    or entity["growing_age"] != 0 \
                    or entity["career"] != 0 \
                    or entity["career_level"] != 0 \
                    or not -80 <= entity["living_sound_time"] <= 1000 \
                    or not 0 <= entity["entity_seed48"] < (1 << 48) \
                    or abs(float(entity["pitch"])) > 1e-12:
                raise CapsuleError(
                    f"{label} has invalid unopened NoAI-villager state"
                )
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
                if not isinstance(item, dict) or set(item) != {
                        "slot", "id", "count", "meta"}:
                    raise CapsuleError(
                        f"{item_label} must contain slot/id/count/meta")
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
                cart_slots.add(item["slot"])
    if loaded_order_count not in (0, len(entities)):
        raise CapsuleError(
            "state.entities must either all include loaded_order or all omit it"
        )
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
                and caught.get("no_ai") is True):
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
    frame_fields = {
        "eid", "x", "y", "z",
        "hanging_x", "hanging_y", "hanging_z",
        "facing", "item", "count", "meta", "rotation",
    }
    for index, frame in enumerate(item_frames):
        label = f"state.item_frames[{index}]"
        if not isinstance(frame, dict) or set(frame) != frame_fields:
            raise CapsuleError(
                f"{label} must contain exactly "
                + ", ".join(sorted(frame_fields))
            )
        eid = frame["eid"]
        if isinstance(eid, bool) or not isinstance(eid, int) or eid <= 0:
            raise CapsuleError(f"{label}.eid must be a positive integer")
        if eid in seen_eids:
            raise CapsuleError(f"{label}.eid must be globally unique")
        seen_eids.add(eid)
        for field in ("x", "y", "z"):
            _finite_number(frame[field], f"{label}.{field}")
        for field in (
            "hanging_x", "hanging_y", "hanging_z",
            "facing", "item", "count", "meta", "rotation",
        ):
            value = frame[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        hanging = (
            frame["hanging_x"], frame["hanging_y"], frame["hanging_z"])
        if hanging in seen_hanging_positions:
            raise CapsuleError(f"{label} duplicates a hanging position")
        seen_hanging_positions.add(hanging)
        if not 0 <= frame["hanging_y"] <= 255 \
                or not 2 <= frame["facing"] <= 5 \
                or not 0 <= frame["rotation"] <= 7:
            raise CapsuleError(f"{label} has invalid hanging state")
        if not (
            (
                frame["item"] == 0 and frame["count"] == 0
                and frame["meta"] == 0 and frame["rotation"] == 0
            )
            or (
                frame["item"] == 1 and frame["count"] == 1
                and frame["meta"] == 0
            )
        ):
            raise CapsuleError(
                f"{label} is outside the exact empty/plain-stone subset"
            )
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
        chest_fields = {
            "num_players_using", "lid_angle_bits",
            "prev_lid_angle_bits",
        }
        furnace_fields = {
            "burn_time", "current_burn_time",
            "cook_time", "total_cook_time",
        }
        brewing_fields = {"brew_time", "fuel"}
        hopper_fields = {"transfer_cooldown", "ticked_game_time"}
        command_fields = {"success_count"}
        shulker_fields = {"block", "facing", "item_tag_nbt"}
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
            for field in ("lid_angle_bits", "prev_lid_angle_bits"):
                if not 0 <= container[field] <= 0xFFFFFFFF:
                    raise CapsuleError(
                        f"{label}.{field} is outside unsigned 32-bit range")
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
        for field in ("x", "y", "z", "size"):
            value = container[field]
            if isinstance(value, bool) or not isinstance(value, int):
                raise CapsuleError(f"{label}.{field} must be an integer")
        if not 0 <= container["y"] <= 255:
            raise CapsuleError(f"{label}.y must be in 0..255")
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
                if isinstance(value, bool) or not isinstance(value, int) \
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
            if not isinstance(item, dict) or set(item) != {
                    "slot", "id", "count", "meta"}:
                raise CapsuleError(
                    f"{item_label} must contain exactly "
                    "slot, id, count, meta"
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
    block_seed48 = world_rng.get("block_seed48")
    if isinstance(block_seed48, bool) or not isinstance(block_seed48, int) \
            or not 0 <= block_seed48 < (1 << 48):
        raise CapsuleError(
            "state.world_rng.block_seed48 must be an integer in 0..2^48-1"
        )
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


def create_capsule(
    state_path: pathlib.Path,
    blocks_path: pathlib.Path,
    box: list[int],
    output_dir: pathlib.Path,
    *,
    sky_light_path: pathlib.Path | None = None,
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
    for skull in state["skulls"]:
        position = (skull["x"], skull["y"], skull["z"])
        packed = packed_at(*position)
        if packed is None or packed >> 4 != 144:
            raise CapsuleError(
                "captured skull tile state is outside the block cuboid or "
                f"does not match a skull block at {position}"
            )
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
                "command_block", "repeating_command_block",
                "chain_command_block") and any(
                    entry["x"] == position[0]
                    and entry["y"] == position[1]
                    and entry["z"] == position[2]
                    and entry["block"] == block_id
                    for entry in state["scheduled_ticks"]):
            raise CapsuleError(
                f"captured inert command block at {position} has a pending "
                "execution callback"
            )
        if container["type"] in (
                "furnace", "brewing_stand", "dispenser", "dropper",
                "hopper", "jukebox",
                "shulker_box",
                "command_block", "repeating_command_block",
                "chain_command_block"):
            continue
        if (
            container["num_players_using"] != 0
            or container["lid_angle_bits"] != 0
            or container["prev_lid_angle_bits"] != 0
        ):
            raise CapsuleError(
                f"captured chest at {position} is open or lid-transient")
        x, y, z = position
        if packed_at(x, y + 1, z) != 0:
            raise CapsuleError(
                f"captured chest at {position} is not provably unblocked")
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

    for entry in state["scheduled_ticks"]:
        if entry["block"] not in (
            1, 8, 10, 12, 13, 23, 51, 70, 75, 76, 77, 93, 94, 122, 124, 143,
            145,
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
            if block_state != (falling_block << 4) \
                    or below_id not in (0, 8, 9, 10, 11, 51):
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
                    and entity.get("no_ai") is True
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
    nbt_payloads = []
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
    for index, skull in enumerate(manifest["state"]["skulls"]):
        if not skull["has_owner"]:
            continue
        expected_nbt_payloads.append((
            "skull_owner", index, f"skull_owner_{index:04d}.nbt",
            _validate_game_profile_nbt(
                skull["owner_nbt"],
                f"state.skulls[{index}].owner_nbt"),
        ))
    for index, container in enumerate(manifest["state"]["containers"]):
        if container["type"] != "shulker_box":
            continue
        expected_nbt_payloads.append((
            "shulker_item_tag", index,
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
        kind, index, filename, canonical_raw = expected
        if not isinstance(payload, dict) or set(payload) != {
                "kind", "index", "file", "encoding", "bytes", "sha256"}:
            raise CapsuleError(f"{label} has an incomplete payload schema")
        if payload.get("kind") != kind \
                or payload.get("index") != index \
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

    capabilities = manifest.get("capabilities")
    expected_capabilities = copy.deepcopy(CAPABILITIES_V2)
    sky_light = manifest.get("sky_light")
    if sky_light is not None:
        expected_capabilities["world.light.sky_nibbles"] = "exact"
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

    events = [
        {"tick": 0, "type": "set_dimension", "dimension": dimension},
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
            "type": "set_entity_id_cursor",
            "value": int(state["entity_id_cursor"]),
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
        {
            "tick": 0,
            "type": "set_block_random_seed",
            "value": int(state["world_rng"]["block_seed48"]),
        },
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
    if "sky_light" in manifest:
        # The block-only batch dirties the generated column baseline. Resolve
        # that once before overlaying Java's authoritative saved nibble values,
        # then freeze those values as the exact pre-tick boundary.
        events.append({
            "tick": 0,
            "type": "snapshot_blocks_finalize",
            "dim": dimension,
            "cx": center_cx,
            "cz": center_cz,
            "radius": radius,
        })
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
    events.append({"tick": 0, "type": "player_potions_clear"})
    for effect in player["potions"]:
        events.append({
            "tick": 0,
            "type": "player_potion_add",
            "id": effect["id"],
            "amplifier": effect["amp"],
            "duration": effect["dur"],
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
    if player["dead"]:
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
        events.append(stack_event)
    events.append({
        "tick": 0,
        "type": "set_selected_slot",
        "slot": player["held_slot"],
    })
    # Promote only payloads whose complete tick-relevant state is represented
    # by the canonical schema. New captures retain Java loadedEntityList order
    # independently of the distance-sorted entity payload and entity IDs. An
    # old v1 capsule can safely omit that rank only when at most one represented
    # living/XP entity will be restored; otherwise regeneration is required.
    exact_entities = [
        value for value in state["entities"]
        if (value["type"] == "EntityPig" and value.get("no_ai") is True)
        or (
            value["type"] == "EntityVillager"
            and value.get("no_ai") is True
            and value.get("offers_initialized") is False
        )
        or value["type"] == "EntityXPOrb"
        or value["type"] in {
            "EntityMinecartEmpty", "EntityMinecartChest",
            "EntityMinecartFurnace", "EntityMinecartTNT",
            "EntityMinecartHopper", "EntityFishHook",
        }
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
    for entity in sorted(state["entities"], key=entity_order_key):
        if entity["type"] == "EntityPig" and entity.get("no_ai") is True:
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
        elif entity["type"] == "EntityVillager" \
                and entity.get("no_ai") is True \
                and entity.get("offers_initialized") is False:
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
        elif entity["type"] in {
                "EntityMinecartEmpty", "EntityMinecartChest",
                "EntityMinecartFurnace", "EntityMinecartTNT",
                "EntityMinecartHopper"}:
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
            for item in entity["items"]:
                events.append({
                    "tick": 0,
                    "type": "set_minecart_slot",
                    "eid": entity["eid"],
                    "slot": item["slot"],
                    "item": item["id"],
                    "count": item["count"],
                    "meta": item["meta"],
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
        events.append({
            "tick": 0,
            "type": "set_item_frame_source",
            "dim": dimension,
            "eid": frame["eid"],
            "x": frame["x"],
            "y": frame["y"],
            "z": frame["z"],
            "hanging_x": frame["hanging_x"],
            "hanging_y": frame["hanging_y"],
            "hanging_z": frame["hanging_z"],
            "facing": frame["facing"],
            "item": frame["item"],
            "count": frame["count"],
            "meta": frame["meta"],
            "rotation": frame["rotation"],
        })
    shulker_payloads = {
        payload["index"]: payload
        for payload in manifest["nbt_payloads"]
        if payload["kind"] == "shulker_item_tag"
    }
    for container_index, container in enumerate(state["containers"]):
        if container["type"] in (
                "command_block", "repeating_command_block",
                "chain_command_block"):
            events.append({
                "tick": 0,
                "type": "set_command_block_success",
                "dim": dimension,
                "x": container["x"],
                "y": container["y"],
                "z": container["z"],
                "success_count": container["success_count"],
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
        if container["type"] == "furnace":
            first_event.update({
                "burn_time": container["burn_time"],
                "current_burn_time": container["current_burn_time"],
                "cook_time": container["cook_time"],
                "total_cook_time": container["total_cook_time"],
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
    for entry in state["scheduled_ticks"]:
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
            "entity_id_cursor": 93,
            "player": {
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
                    {"id": 1, "amp": 0, "dur": 10},
                    {"id": 16, "amp": 1, "dur": 20},
                ],
            },
            "inventory": [
                {"slot": 2, "id": 1, "count": 3, "meta": 0,
                 "enchants": [[16, 5]]},
                {"slot": 38, "id": 311, "count": 1, "meta": 7,
                 "enchants": [[0, 4]]},
                {"slot": 40, "id": 442, "count": 1, "meta": 5,
                 "enchants": []},
            ],
            "entities": [
                {
                    "eid": 91, "type": "EntityPig",
                    "loaded_order": 1,
                    "x": 1.5, "y": 65.0, "z": -4.5,
                    "dx": 0.0, "dy": 0.0, "dz": -2.0,
                    "vx": 0.0, "vy": 0.0, "vz": 0.0,
                    "yaw": 0.0, "pitch": 0.0, "health": 5.0,
                    "hurt_time": 3, "death_time": 0,
                    "hurt_resistant_time": 13, "no_ai": True,
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
            "skulls": [],
            "skulls_complete": True,
            "moving_pistons": [],
            "moving_pistons_complete": True,
            "item_frames": [],
            "item_frames_complete": True,
            "redstone_torch_toggles": [
                {"x": 1, "y": 65, "z": -1, "time": 40},
            ],
            "redstone_torch_toggles_complete": True,
            "world_rng": {
                "java_seed48": 0x5DEECE664,
                "math_seed48": 0x123456789ABC,
                "block_seed48": 0x0ABCDEF12345,
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
        capsule = root / "capsule"
        create_capsule(
            state_path, blocks_path, box, capsule,
            sky_light_path=sky_path,
            seed=7, source_engine="selftest", source_version="1",
        )
        manifest, raw = validate_capsule(capsule)
        assert manifest["blocks"]["cells"] == 484 and raw == blocks_path.read_bytes()
        assert manifest["capabilities"]["world.light.sky_nibbles"] == "exact"
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
            row["type"] == "set_world_update_lcg"
            and row["value"] == 1094913777
            for row in events
        )
        assert any(
            row == {
                "tick": 0, "type": "set_player_xp",
                "level": 2, "fraction": 0.25, "total": 18,
            }
            for row in events
        )
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
             "id": 1, "amplifier": 0, "duration": 10},
            {"tick": 0, "type": "player_potion_add",
             "id": 16, "amplifier": 1, "duration": 20},
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
            "items": [
                {"slot": 0, "id": 1, "count": 64, "meta": 0},
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
            "brew_time": 200, "fuel": 19,
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
                "type": "set_command_block_success",
                "dim": 0,
                "x": -3,
                "y": 64,
                "z": -4,
                "success_count": 7,
            }
            for row in magma_events(command_capsule)
        )
        malformed_command = copy.deepcopy(command_state)
        malformed_command["containers"][0]["success_count"] = 16
        try:
            _validate_state(malformed_command)
        except CapsuleError as exc:
            assert "success_count" in str(exc)
        else:
            raise AssertionError(
                "out-of-range command success count passed validation")
        pending_command = copy.deepcopy(command_state)
        pending_command["scheduled_ticks"].append({
            "x": -3, "y": 64, "z": -4, "block": 137,
            "time": 47, "priority": 0, "order": 19,
        })
        pending_command_path = root / "pending_command_state.json"
        pending_command_path.write_text(
            json.dumps(pending_command), encoding="utf-8")
        try:
            create_capsule(
                pending_command_path, blocks_path, box,
                root / "pending_command_capsule",
                sky_light_path=sky_path, seed=7,
                source_engine="selftest", source_version="1",
            )
        except CapsuleError as exc:
            assert "pending execution callback" in str(exc)
        else:
            raise AssertionError(
                "scheduled command execution passed exact capsule proof")
        trapped_chest_state = copy.deepcopy(state)
        trapped_chest_state["containers"] = [{
            "type": "single_trapped_chest",
            "x": 4, "y": 64, "z": 3, "size": 27,
            "num_players_using": 0,
            "lid_angle_bits": 0,
            "prev_lid_angle_bits": 0,
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
        open_trapped_path = root / "open_trapped_state.json"
        open_trapped_path.write_text(
            json.dumps(open_trapped), encoding="utf-8")
        try:
            create_capsule(
                open_trapped_path, blocks_path, box,
                root / "open_trapped_capsule", seed=7,
                source_engine="selftest", source_version="1",
            )
        except CapsuleError as exc:
            assert "open or lid-transient" in str(exc)
        else:
            raise AssertionError(
                "open trapped chest passed the saved-state boundary")
        double_chest_state = copy.deepcopy(state)
        double_chest_state["containers"] = [
            {
                "type": "double_chest_half",
                "x": -4, "y": 64, "z": 3, "size": 27,
                "pair_x": -3, "pair_y": 64, "pair_z": 3,
                "num_players_using": 0,
                "lid_angle_bits": 0,
                "prev_lid_angle_bits": 0,
                "items": [],
            },
            {
                "type": "double_chest_half",
                "x": -3, "y": 64, "z": 3, "size": 27,
                "pair_x": -4, "pair_y": 64, "pair_z": 3,
                "num_players_using": 0,
                "lid_angle_bits": 0,
                "prev_lid_angle_bits": 0,
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
        assert len(orb_events) == 1 and orb_events[0]["eid"] == 92
        assert orb_events[0]["target_color"] == -100
        entity_events = [
            row for row in events
            if row["type"] in ("spawn_mob_fixture", "spawn_xp_fixture")
        ]
        assert [row["eid"] for row in entity_events] == [92, 91]
        villager_state = copy.deepcopy(state)
        villager_state["entities"].append({
            "eid": 90, "type": "EntityVillager", "loaded_order": 2,
            "x": 3.5, "y": 65.0, "z": -3.5,
            "dx": 2.0, "dy": 0.0, "dz": -1.0,
            "vx": 0.0, "vy": 0.0, "vz": 0.0,
            "yaw": 0.0, "pitch": 0.0, "health": 20.0,
            "hurt_time": 0, "death_time": 0,
            "hurt_resistant_time": 0, "no_ai": True,
            "profession": 1, "growing_age": 0,
            "career": 0, "career_level": 0,
            "living_sound_time": 0,
            "offers_initialized": False,
            "entity_seed48": 0x3456789ABCDE,
            "entity_have_gaussian": True,
            "entity_gaussian": -0.25,
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
        invalid_villager = copy.deepcopy(villager_state)
        invalid_villager["entities"][-1]["career"] = 1
        try:
            _validate_state(invalid_villager)
        except CapsuleError as exc:
            assert "invalid unopened NoAI-villager state" in str(exc)
        else:
            raise AssertionError(
                "initialized villager career passed unopened validation")
        minecart_state = copy.deepcopy(state)
        minecart_state["entities"].append({
            "eid": 90, "type": "EntityMinecartChest",
            "loaded_order": 2,
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
