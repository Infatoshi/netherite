#!/usr/bin/env python3
"""Drive Netherite's full shared GmRuntime with a qrl action tape.

The small trace_game binary is intentionally a narrow player-physics oracle.
This adapter exercises the shipped interactive/headless runtime instead, using
its strict event script, then normalizes the richer raw state log into the same
canonical JSONL schema as trace_java.py.

The spawn sidecar must contain the complete settled pre-tick travel state:
    x y z yaw pitch vx vy vz on_ground fall_distance
trace_game writes that format; Java deliberately consumes only the first five
fields because its own teleport settle establishes the remaining values.
"""

import argparse
import json
import math
import os
import pathlib
import subprocess
import sys

import block_edit_sequence
import nbt_codec
import state_capsule


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
GAME = MAGMA / "magma_game"
AIM_QUANTUM = 15.0

MOB_CLASS = {
    2: "EntityZombie",
    3: "EntitySkeleton",
    4: "EntityCreeper",
    5: "EntitySpider",
    6: "EntityEnderman",
    7: "EntityBlaze",
    10: "EntitySheep",
    11: "EntityPig",
    12: "EntityCow",
    13: "EntityChicken",
    15: "EntityPigZombie",
    26: "EntityGhast",
    27: "EntityMagmaCube",
    32: "EntityWitherSkeleton",
    35: "EntitySlime",
    36: "EntitySilverfish",
    37: "EntityBoat",
    40: "EntityVillager",
}


def canonical_container(entry):
    value = {
        **entry,
        "items": sorted(
            entry.get("items", []), key=lambda item: item.get("slot")),
    }
    if value.get("type") == "shulker_box" \
            and "item_tag_nbt" in value:
        value["item_tag_nbt"] = nbt_codec.canonical_hex(
            value["item_tag_nbt"])
    return value


def read_tape(path):
    rows = []
    with open(path, encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            text = line.strip()
            if not text or text.startswith("#"):
                continue
            values = [int(value) for value in text.split()]
            if len(values) not in (11, 12):
                raise ValueError(
                    f"{path}:{line_no}: expected 11 or 12 tape fields, "
                    f"got {len(values)}"
                )
            if len(values) == 11:
                values.append(0)
            if any(value not in (0, 1) for value in values[:9]):
                raise ValueError(f"{path}:{line_no}: action flags must be 0 or 1")
            if any(value not in (-1, 0, 1) for value in values[9:11]):
                raise ValueError(f"{path}:{line_no}: aim steps must be -1, 0, or 1")
            if values[11] not in (0, 1):
                raise ValueError(
                    f"{path}:{line_no}: close flag must be 0 or 1")
            rows.append(values)
    return rows


def read_spawn(path):
    values = pathlib.Path(path).read_text(encoding="utf-8").split()
    if len(values) < 10:
        raise ValueError(
            f"{path}: full-runtime tracing needs 10 spawn-state fields; "
            "rebuild trace_game and regenerate c_spawn.txt"
        )
    state = [float(value) for value in values[:10]]
    if state[8] not in (0.0, 1.0) or state[9] < 0.0:
        raise ValueError(f"{path}: invalid on_ground/fall_distance")
    return state


def event(tick, kind, **fields):
    return {"tick": tick, "type": kind, **fields}


def write_script(
        path, tape, spawn, platform, fixtures, world_time,
        capsule=None, xp_fixture=None, item_fixture=None, arrow_fixture=None,
        primed_tnt_fixture=None, second_primed_tnt_fixture=None,
        end_crystal_fixture=None,
        boat_fixture=None,
        small_fireball_fixture=None, mob_fixture=None,
        potion_fixture=None, armor_fixture=None, random_tick_fixture=None,
        random_selection_fixture=None,
        tick0_set_block_offset=None, block_edit_rows=None,
        scheduled_random_reset=None, tick0_harvest_offset=None):
    sx, sy, sz, yaw, pitch, vx, vy, vz, on_ground, fall = spawn
    if capsule is not None:
        events = state_capsule.magma_events(pathlib.Path(capsule))
    else:
        events = [
            event(0, "set_time", value=world_time),
            event(0, "set_total_time", value=0),
            event(
                0,
                "set_pose_state",
                x=sx,
                y=sy,
                z=sz,
                yaw=yaw,
                pitch=pitch,
                vx=vx,
                vy=vy,
                vz=vz,
                on_ground=int(on_ground),
                fall=fall,
            )
        ]
        if platform:
            half = platform // 2
            cx, cz = round(sx), round(sz)
            floor_y = round(sy) - 1
            events.append(
                event(
                    0,
                    "snapshot_region",
                    cx=math.floor(cx / 16),
                    cz=math.floor(cz / 16),
                    radius=half // 16 + 1,
                )
            )
            for y in range(floor_y, floor_y + 4):
                block_id = 1 if y == floor_y else 0
                for z in range(cz - half, cz + half + 1):
                    for x in range(cx - half, cx + half + 1):
                        events.append(
                            event(
                                0, "set_block", x=x, y=y, z=z,
                                id=block_id, meta=0,
                            )
                        )
        for x, y, z, block_id, meta in fixtures:
            events.append(
                event(
                    0,
                    "snapshot_region",
                    cx=math.floor(x / 16),
                    cz=math.floor(z / 16),
                    radius=0,
                )
            )
            events.append(
                event(0, "set_block", x=x, y=y, z=z, id=block_id, meta=meta)
            )

    event_types = {row.get("type") for row in events}
    if xp_fixture is not None and "spawn_xp_fixture" not in event_types:
        events.append(event(0, "spawn_xp_fixture", **xp_fixture))
    if item_fixture is not None and "spawn_item_fixture" not in event_types:
        events.append(event(0, "spawn_item_fixture", **item_fixture))
    if arrow_fixture is not None and "spawn_arrow_fixture" not in event_types:
        events.append(event(0, "spawn_arrow_fixture", **arrow_fixture))
    if mob_fixture is not None and "spawn_mob_fixture" not in event_types:
        events.append(event(0, "spawn_mob_fixture", **mob_fixture))
    if (primed_tnt_fixture is not None
            and "spawn_primed_tnt_fixture" not in event_types):
        events.append(event(
            0, "spawn_primed_tnt_fixture", **primed_tnt_fixture))
    if (second_primed_tnt_fixture is not None
            and "spawn_primed_tnt_fixture" not in event_types):
        events.append(event(
            0, "spawn_primed_tnt_fixture", **second_primed_tnt_fixture))
    if (end_crystal_fixture is not None
            and "spawn_end_crystal_fixture" not in event_types):
        events.append(event(
            0, "spawn_end_crystal_fixture", **end_crystal_fixture))
    if boat_fixture is not None and "spawn_boat_fixture" not in event_types:
        events.append(event(0, "spawn_boat_fixture", **boat_fixture))
    if (small_fireball_fixture is not None
            and "spawn_small_fireball_fixture" not in event_types):
        events.append(event(
            0, "spawn_small_fireball_fixture", **small_fireball_fixture))
    if potion_fixture is not None and "player_potion_add" not in event_types:
        potion_id, amplifier, duration = potion_fixture
        events.append(event(
            0, "potion_fixture", id=potion_id, amplifier=amplifier,
            duration=duration, clear=True))
    if armor_fixture is not None:
        capsule_has_equipment = any(
            row.get("type") == "set_inventory"
            and int(row.get("slot", -1)) >= 36
            for row in events
        )
        if not capsule_has_equipment:
            slot, item, meta, enchantment, level = armor_fixture
            fields = {
                "slot": slot, "item": item, "count": 1, "meta": meta,
            }
            if enchantment >= 0:
                fields.update(
                    n_ench=1, e0=(enchantment << 16) | level)
            events.append(event(0, "set_inventory", **fields))
    if random_tick_fixture is not None:
        dx, dy, dz, block, _public_seed = random_tick_fixture
        events.append(event(0, "begin_controlled_input"))
        events.append(event(
            0,
            "random_tick_block",
            x=math.floor(sx) + dx,
            y=math.floor(sy) + dy,
            z=math.floor(sz) + dz,
            block=block,
        ))
        events.append(event(0, "capture_controlled_input"))
    if random_selection_fixture is not None:
        events.append(event(
            0,
            "random_tick_selection",
            x=random_selection_fixture["x"],
            y=random_selection_fixture["y"],
            z=random_selection_fixture["z"],
            block=random_selection_fixture["block"],
            lcg_advances_before=(
                random_selection_fixture["lcg_advances_before"]),
        ))
    if tick0_set_block_offset is not None:
        dx, dy, dz, block, meta = tick0_set_block_offset
        events.append(event(0, "begin_controlled_input"))
        events.append(event(
            0,
            "set_block",
            x=math.floor(sx) + dx,
            y=math.floor(sy) + dy,
            z=math.floor(sz) + dz,
            id=block,
            meta=meta,
        ))
        events.append(event(0, "capture_controlled_input"))
    if tick0_harvest_offset is not None:
        dx, dy, dz = tick0_harvest_offset
        events.append(event(0, "begin_controlled_input"))
        events.append(event(
            0,
            "harvest_block",
            x=math.floor(sx) + dx,
            y=math.floor(sy) + dy,
            z=math.floor(sz) + dz,
        ))
        events.append(event(0, "capture_controlled_input"))
    for edit in block_edit_rows or []:
        events.append(event(edit.tick, "begin_controlled_input"))
        events.append(event(
            edit.tick,
            "set_block",
            x=math.floor(sx) + edit.dx,
            y=math.floor(sy) + edit.dy,
            z=math.floor(sz) + edit.dz,
            id=edit.block,
            meta=edit.meta,
        ))
        events.append(event(edit.tick, "capture_controlled_input"))

    previous_attack = 0
    previous_use = 0
    for tick, values in enumerate(tape):
        if scheduled_random_reset is not None \
                and tick == scheduled_random_reset[0]:
            public_seed = scheduled_random_reset[1]
            internal_seed = (
                public_seed ^ 0x5DEECE66D
            ) & ((1 << 48) - 1)
            events.append(event(
                tick, "set_world_random_seed", value=internal_seed))
        (
            forward,
            back,
            left,
            right,
            jump,
            sneak,
            sprint,
            attack,
            use,
            aim_yaw,
            aim_pitch,
            close_container,
        ) = values
        events.append(
            event(
                tick,
                "action",
                forward=forward - back,
                strafe=right - left,
                jump=jump,
                sneak=sneak,
                sprint=sprint,
                attack=attack,
                use=use,
                do_break=int(attack and not previous_attack),
                do_place=int(use and not previous_use),
                dyaw=aim_yaw * AIM_QUANTUM,
                dpitch=aim_pitch * AIM_QUANTUM,
                hotbar=-1,
                close_container=close_container,
            )
        )
        previous_attack = attack
        previous_use = use

    # Sequence edits are collected before tape actions so a stable sort keeps
    # each same-tick mutation ahead of the action while satisfying the strict
    # runtime script reader's global tick ordering.
    events.sort(key=lambda row: row["tick"])
    with open(path, "w", encoding="utf-8") as stream:
        for row in events:
            stream.write(json.dumps(row, separators=(",", ":")) + "\n")


def entity_class(entity):
    kind = entity.get("kind")
    if kind == "item":
        return "EntityItem"
    if kind == "xp_orb":
        return "EntityXPOrb"
    if kind == "falling_block":
        return "EntityFallingBlock"
    if kind == "primed_tnt":
        return "EntityTNTPrimed"
    if kind == "end_crystal":
        return "EntityEnderCrystal"
    if kind == "area_effect_cloud":
        return "EntityAreaEffectCloud"
    if kind == "minecart":
        return {
            0: "EntityMinecartEmpty",
            1: "EntityMinecartChest",
            2: "EntityMinecartFurnace",
            3: "EntityMinecartTNT",
            4: "EntityMinecartMobSpawner",
            5: "EntityMinecartHopper",
            6: "EntityMinecartCommandBlock",
        }.get(entity.get("minecart_kind"), "EntityMinecartEmpty")
    if kind == "fish_hook":
        return "EntityFishHook"
    if kind == "mob":
        return MOB_CLASS.get(entity.get("type"), f"MagmaMob{entity.get('type')}")
    if kind == "projectile":
        return {
            1: "EntityTippedArrow",
            2: "EntityTippedArrow",
            3: "EntitySmallFireball",
            4: "EntityEnderEye",
            5: "EntityLargeFireball",
            6: "EntityPotion",
        }.get(entity.get("type"), f"MagmaProjectile{entity.get('type')}")
    return f"MagmaEntity{entity.get('type')}"


def canonicalize(tick, raw):
    player = {
        "x": raw["x"],
        "y": raw["y"],
        "z": raw["z"],
        "yaw": raw["yaw"],
        "pitch": raw["pitch"],
        "vx": raw["vx"],
        "vy": raw["vy"],
        "vz": raw["vz"],
        "on_ground": raw["on_ground"],
        "health": raw["health"],
        "max_health": raw.get("max_health"),
        "absorption": raw.get("absorption"),
        "food": raw["food"],
        "saturation": raw["saturation"],
        "food_exhaustion": raw["food_exhaustion"],
        "food_timer": raw["food_timer"],
        "air": raw["air"],
        "fire": raw["fire"],
        "xp_level": raw["xp_level"],
        "xp_frac": raw["xp_frac"],
        "xp_total": raw.get("xp_total"),
        "fall_distance": raw["fall_distance"],
        "sprinting": raw["sprinting"],
        "sneaking": raw["sneaking"],
        "jumping": raw["jumping"],
        "held_slot": raw["held_slot"],
        "held_id": raw["held_id"],
        "held_count": raw["held_count"],
        "held_meta": raw["held_meta"],
        "attack_cooldown": raw["attack_cooldown"],
        "attack_ticks": raw.get("attack_ticks"),
        "hurt_time": raw["hurt_time"],
        "hurt_resistant_time": raw.get("hurt_resistant_time"),
        "death_time": raw["death_time"],
        "dead": raw["dead"],
        "deaths": raw["deaths"],
        "dim": raw["dim"],
        "potions": raw["potions"],
    }
    inventory = [
        {
            "slot": item["slot"],
            "id": item["item"],
            "count": item["count"],
            "meta": item["meta"],
            "enchants": item.get("enchants", []),
        }
        for item in raw.get("inventory", [])
        if item["slot"] <= 40 and item["item"] > 0 and item["count"] > 0
    ]
    entities = []
    for entity in raw.get("entities", []):
        x, y, z = entity["x"], entity["y"], entity["z"]
        identity = None
        if entity.get("kind") == "falling_block":
            identity = (
                f"falling:{entity['origin_x']}:{entity['origin_y']}:"
                f"{entity['origin_z']}:{entity['block']}:{entity['meta']}"
            )
        entities.append(
            {
                "eid": entity["eid"],
                "identity": identity,
                "type": entity_class(entity),
                "x": x,
                "y": y,
                "z": z,
                "dx": x - raw["x"],
                "dy": y - raw["y"],
                "dz": z - raw["z"],
                "vx": entity["vx"],
                "vy": entity["vy"],
                "vz": entity["vz"],
                "ax": entity.get("ax"),
                "ay": entity.get("ay"),
                "az": entity.get("az"),
                "yaw": entity["yaw"],
                "pitch": entity["pitch"],
                "health": entity["health"],
                "item": entity.get("item"),
                "count": entity.get("count"),
                "value": entity.get("value"),
                "age": entity.get("age"),
                "pickup_delay": entity.get("pickup_delay"),
                "stack_payload": (
                    {
                        "kind": "item_tag",
                        "nbt": nbt_codec.canonical_hex(
                            entity["stack_payload"].get("nbt")),
                    }
                    if isinstance(entity.get("stack_payload"), dict)
                    and entity["stack_payload"].get("kind") == "item_tag"
                    else entity.get("stack_payload")
                ),
                "color": entity.get("color"),
                "target_color": entity.get("target_color"),
                "hurt_time": entity.get("hurt_time"),
                "death_time": entity.get("death_time"),
                "hurt_resistant_time": entity.get("hurt_resistant_time"),
                "block": entity.get("block"),
                "meta": entity.get("meta"),
                "fall_time": entity.get("fall_time"),
                "origin_x": entity.get("origin_x"),
                "origin_y": entity.get("origin_y"),
                "origin_z": entity.get("origin_z"),
                "fuse": entity.get("fuse"),
                "inner_rotation": entity.get("inner_rotation"),
                "show_bottom": entity.get("show_bottom"),
                "has_beam": entity.get("has_beam"),
                "beam_x": entity.get("beam_x"),
                "beam_y": entity.get("beam_y"),
                "beam_z": entity.get("beam_z"),
                "minecart_kind": entity.get("minecart_kind"),
                "reverse": entity.get("reverse"),
                "rolling_amplitude": entity.get("rolling_amplitude"),
                "rolling_direction": entity.get("rolling_direction"),
                "damage": entity.get("damage"),
                "fuel": entity.get("fuel"),
                "push_x": entity.get("push_x"),
                "push_z": entity.get("push_z"),
                "tnt_fuse": entity.get("tnt_fuse"),
                "hopper_enabled": entity.get("hopper_enabled"),
                "transfer_cooldown": entity.get("transfer_cooldown"),
                "items": entity.get("items"),
                "entity_seed48": entity.get("entity_seed48"),
                "entity_have_gaussian": entity.get(
                    "entity_have_gaussian"),
                "entity_gaussian": entity.get("entity_gaussian"),
                "profession": entity.get("profession"),
                "growing_age": entity.get("growing_age"),
                "career": entity.get("career"),
                "career_level": entity.get("career_level"),
                "living_sound_time": entity.get("living_sound_time"),
                "offers_initialized": entity.get("offers_initialized"),
                "fish_state": entity.get("fish_state"),
                "in_ground": entity.get("in_ground"),
                "ticks_in_ground": entity.get("ticks_in_ground"),
                "ticks_in_air": entity.get("ticks_in_air"),
                "ticks_catchable": entity.get("ticks_catchable"),
                "ticks_caught_delay": entity.get("ticks_caught_delay"),
                "ticks_catchable_delay": entity.get(
                    "ticks_catchable_delay"),
                "fish_approach_angle": entity.get(
                    "fish_approach_angle"),
                "lure": entity.get("lure"),
                "luck": entity.get("luck"),
                "caught_eid": entity.get("caught_eid"),
            }
        )
    world_time = raw["world_time"]
    time = {
        "world_time": world_time,
        "total_time": raw["total_time"],
        "moon_phase": (world_time // 24000) % 8,
        "raining": bool(raw["weather"]["raining"]),
        "thundering": bool(raw["weather"]["thundering"]),
        "rain_time": int(raw["weather"]["rain_time"]),
        "thunder_time": int(raw["weather"]["thunder_time"]),
        "clean_weather_time": int(raw["weather"]["clean_weather_time"]),
        "do_weather_cycle": bool(raw["weather"]["weather_cycle"]),
        "do_daylight_cycle": bool(raw["weather"]["daylight_cycle"]),
        "prev_rain_strength": raw["weather"]["prev_rain_strength"],
        "rain_strength": raw["weather"]["rain_strength"],
        "prev_thunder_strength": raw["weather"]["prev_thunder_strength"],
        "thunder_strength": raw["weather"]["thunder_strength"],
    }
    # Expose the represented slice's current relative tie-break rank, matching
    # trace_java's normalization of Java's otherwise unstable global TreeSet
    # iteration rank.
    scheduled_ticks = []
    for entry in raw.get("scheduled_ticks", []):
        normalized = dict(entry)
        normalized["order"] = len(scheduled_ticks)
        scheduled_ticks.append(normalized)
    comparators = sorted(
        raw.get("comparators", []),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z")
        ),
    )
    moving_pistons = sorted(
        raw.get("moving_pistons", []),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z")
        ),
    )
    containers = sorted(
        (canonical_container(entry)
         for entry in raw.get("containers", [])),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z"),
            entry.get("type"),
        ),
    )
    flower_pots = sorted(
        raw.get("flower_pots", []),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z")
        ),
    )
    skulls = sorted(
        raw.get("skulls", []),
        key=lambda entry: (
            entry.get("x"), entry.get("y"), entry.get("z")
        ),
    )
    item_frames = sorted(
        raw.get("item_frames", []),
        key=lambda entry: (
            entry.get("hanging_x"), entry.get("hanging_y"),
            entry.get("hanging_z"), entry.get("eid"),
        ),
    )
    return {
        "tick": tick,
        "do_entity_drops": raw.get("do_entity_drops", True),
        "entity_id_cursor": raw.get("entity_id_cursor"),
        "world_rng": {
            "java_seed48": raw.get("world_rand_seed48"),
            "java_have_gaussian": raw.get(
                "world_rand_have_gaussian", False),
            "java_gaussian": raw.get("world_rand_gaussian", 0.0),
            "math_seed48": raw.get("math_rand_seed48"),
            "block_seed48": raw.get("block_rand_seed48"),
            "update_lcg": raw.get("world_update_lcg"),
        },
        "controlled_input": (
            {
                "before": {
                    "entity_id_cursor": (
                        raw["controlled_input"].get("before")
                        or {}).get("next_entity_id"),
                    "world_rng": {
                        "java_seed48": (
                            raw["controlled_input"].get("before")
                            or {}).get("world_rand_seed48"),
                        "math_seed48": (
                            raw["controlled_input"].get("before")
                            or {}).get("math_rand_seed48"),
                        "block_seed48": (
                            raw["controlled_input"].get("before")
                            or {}).get("block_rand_seed48"),
                        "update_lcg": (
                            raw["controlled_input"].get("before")
                            or {}).get("world_update_lcg"),
                    },
                },
                "entity_id_cursor": raw["controlled_input"].get(
                    "next_entity_id"),
                "world_rng": {
                    "java_seed48": raw["controlled_input"].get(
                        "world_rand_seed48"),
                    "math_seed48": raw["controlled_input"].get(
                        "math_rand_seed48"),
                    "block_seed48": raw["controlled_input"].get(
                        "block_rand_seed48"),
                    "update_lcg": raw["controlled_input"].get(
                        "world_update_lcg"),
                },
            }
            if isinstance(raw.get("controlled_input"), dict)
            else None
        ),
        "player": player,
        "inventory": inventory,
        "entities": entities,
        "scheduled_ticks": scheduled_ticks,
        "scheduled_ticks_complete": True,
        "comparators": comparators,
        "comparators_complete": True,
        "moving_pistons": moving_pistons,
        "moving_pistons_complete": True,
        "containers": containers,
        "containers_complete": True,
        "flower_pots": flower_pots,
        "flower_pots_complete": True,
        "skulls": skulls,
        "skulls_complete": True,
        "item_frames": item_frames,
        "item_frames_complete": True,
        "redstone_torch_toggles": raw.get(
            "redstone_torch_toggles", []),
        "redstone_torch_toggles_complete": True,
        "time": time,
        "diagnostics": {
            "client_look": raw.get("look"),
            "server_x": raw.get("server_x"),
            "server_y": raw.get("server_y"),
            "server_z": raw.get("server_z"),
            "server_vx": raw.get("server_vx"),
            "server_vy": raw.get("server_vy"),
            "server_vz": raw.get("server_vz"),
            "server_armor": [
                {
                    "slot": item["slot"],
                    "id": item["item"],
                    "count": item["count"],
                    "meta": item["meta"],
                    "enchants": item.get("enchants", []),
                }
                for item in raw.get("inventory", [])
                if 36 <= item["slot"] <= 39
                and item["item"] > 0 and item["count"] > 0
            ],
        },
    }


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tape", required=True)
    parser.add_argument("--spawn-file", required=True)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--platform", type=int, default=21)
    parser.add_argument("--world-time", type=int, default=6000)
    parser.add_argument("--set-block", type=int, nargs=5, action="append", default=[])
    parser.add_argument(
        "--capsule",
        help="validated Java pre-tick state capsule; replaces synthetic "
             "pose/platform/fixture initialization",
    )
    parser.add_argument(
        "--xp-fixture",
        help="exact locked Java XP fixture JSON, including its authoritative eid",
    )
    parser.add_argument(
        "--item-fixture",
        help="exact locked Java EntityItem fixture JSON, including its "
             "authoritative eid",
    )
    parser.add_argument(
        "--arrow-fixture",
        help="exact locked Java stationary EntityTippedArrow fixture JSON, "
             "including its authoritative eid",
    )
    parser.add_argument(
        "--primed-tnt-fixture",
        help="exact locked Java EntityTNTPrimed fixture JSON, including its "
             "authoritative eid and fuse",
    )
    parser.add_argument(
        "--second-primed-tnt-fixture",
        help="second exact locked Java EntityTNTPrimed fixture JSON",
    )
    parser.add_argument(
        "--end-crystal-fixture",
        help="exact locked Java EntityEnderCrystal fixture JSON, including "
             "its authoritative eid and render state",
    )
    parser.add_argument(
        "--boat-fixture",
        help="exact locked Java stationary EntityBoat fixture JSON, "
             "including its authoritative eid",
    )
    parser.add_argument(
        "--small-fireball-fixture",
        help="exact locked Java EntitySmallFireball trajectory fixture JSON, "
             "including its authoritative eid",
    )
    parser.add_argument(
        "--mob-fixture",
        help="exact locked Java NoAI living fixture JSON, including authoritative eid",
    )
    parser.add_argument(
        "--potion-fixture",
        type=int,
        nargs=3,
        metavar=("ID", "AMPLIFIER", "DURATION"),
        help="exact active potion fixture applied after capsule initialization",
    )
    parser.add_argument(
        "--armor-fixture",
        type=int,
        nargs=5,
        metavar=("SLOT", "ITEM", "META", "ENCHANTMENT", "LEVEL"),
        help="one exact armor stack; ENCHANTMENT=-1 and LEVEL=0 means plain",
    )
    parser.add_argument(
        "--random-tick-offset",
        type=int,
        nargs=5,
        metavar=("DX", "DY", "DZ", "BLOCK", "SEED"),
        help="invoke one represented random-tick block callback before tape "
             "tick 0; SEED is already restored from the Java capsule",
    )
    parser.add_argument(
        "--scheduled-random-reset",
        type=int,
        nargs=2,
        metavar=("TICK", "PUBLIC_SEED"),
        help="reseed java.util.Random immediately before one represented "
             "scheduled callback",
    )
    parser.add_argument(
        "--random-selection-fixture",
        help="exact isolated WorldServer selector sidecar written by "
             "trace_java.py",
    )
    parser.add_argument(
        "--tick0-set-block-offset",
        type=int,
        nargs=5,
        metavar=("DX", "DY", "DZ", "BLOCK", "META"),
        help="apply one player-relative block mutation immediately before "
             "full-runtime tape tick 0",
    )
    parser.add_argument(
        "--tick0-harvest-offset",
        type=int,
        nargs=3,
        metavar=("DX", "DY", "DZ"),
        help="harvest one player-relative block with the capsule's held item "
             "immediately before full-runtime tape tick 0",
    )
    parser.add_argument(
        "--block-edit-sequence",
        help="apply player-relative block edits before selected tape ticks; "
             "rows are TICK DX DY DZ BLOCK META",
    )
    parser.add_argument("--script-out", required=True)
    parser.add_argument("--raw-state", required=True)
    parser.add_argument("--state", required=True)
    parser.add_argument("--blocks-out")
    parser.add_argument("--block-light-out")
    parser.add_argument("--sky-light-out")
    parser.add_argument("--blocks-box", type=int, nargs=6)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    if bool(args.blocks_out) != bool(args.blocks_box):
        parser.error("--blocks-out and --blocks-box must be supplied together")
    if args.block_light_out and not args.blocks_box:
        parser.error("--block-light-out requires --blocks-box")
    if args.sky_light_out and not args.blocks_box:
        parser.error("--sky-light-out requires --blocks-box")
    if args.platform < 0 or (args.platform and args.platform % 2 == 0):
        parser.error("--platform must be 0 or a positive odd integer")
    if args.potion_fixture:
        potion_id, amplifier, duration = args.potion_fixture
        if not (1 <= potion_id <= 255 and 0 <= amplifier <= 255
                and duration > 0):
            parser.error("--potion-fixture requires ID 1..255, amplifier "
                         "0..255, and positive duration")
    if args.armor_fixture:
        slot, item, meta, enchantment, level = args.armor_fixture
        if (not 36 <= slot <= 39 or not 1 <= item <= 4095
                or not 0 <= meta <= 32767
                or not ((enchantment == -1 and level == 0)
                        or (0 <= enchantment <= 32767
                            and 1 <= level <= 32767))):
            parser.error("--armor-fixture requires SLOT 36..39, ITEM 1..4095, "
                         "META 0..32767, and either ENCHANTMENT=-1 LEVEL=0 "
                         "or a non-negative enchantment with positive level")
    if args.random_tick_offset:
        _dx, _dy, _dz, block, _seed = args.random_tick_offset
        if not 1 <= block <= 4095:
            parser.error("--random-tick-offset requires BLOCK 1..4095")
    if args.random_tick_offset and args.random_selection_fixture:
        parser.error("--random-tick-offset and --random-selection-fixture "
                     "are mutually exclusive")
    if args.scheduled_random_reset \
            and not 0 <= args.scheduled_random_reset[0]:
        parser.error("--scheduled-random-reset TICK must be non-negative")
    if args.tick0_set_block_offset:
        _dx, _dy, _dz, block, meta = args.tick0_set_block_offset
        if not (0 <= block <= 4095 and 0 <= meta <= 15):
            parser.error("--tick0-set-block-offset requires BLOCK 0..4095 "
                         "and META 0..15")
    if args.tick0_set_block_offset and args.block_edit_sequence:
        parser.error("--tick0-set-block-offset and --block-edit-sequence are "
                     "mutually exclusive")
    if args.tick0_harvest_offset and (
            args.tick0_set_block_offset or args.block_edit_sequence):
        parser.error("--tick0-harvest-offset is mutually exclusive with "
                     "block edits")
    return args


def main():
    args = parse_args()
    tape = read_tape(args.tape)
    if not tape:
        raise SystemExit("tape contains no ticks")
    try:
        block_edit_rows = (
            block_edit_sequence.load(args.block_edit_sequence, len(tape))
            if args.block_edit_sequence else []
        )
    except (OSError, ValueError) as exc:
        raise SystemExit(exc) from exc
    spawn = read_spawn(args.spawn_file)
    if args.capsule:
        manifest, _raw = state_capsule.validate_capsule(pathlib.Path(args.capsule))
        capsule_seed = int(manifest["source"]["seed"])
        if capsule_seed != args.seed:
            raise SystemExit(
                f"capsule seed {capsule_seed} does not match --seed {args.seed}"
            )
    xp_fixture = None
    if args.xp_fixture:
        xp_fixture = json.loads(
            pathlib.Path(args.xp_fixture).read_text(encoding="utf-8"))
        required = {
            "x", "y", "z", "vx", "vy", "vz", "value", "eid",
            "age", "pickup_delay", "color", "target_color",
        }
        if set(xp_fixture) != required:
            raise SystemExit(
                f"XP fixture keys differ: expected {sorted(required)}, "
                f"got {sorted(xp_fixture)}"
            )
    item_fixture = None
    if args.item_fixture:
        item_fixture = json.loads(
            pathlib.Path(args.item_fixture).read_text(encoding="utf-8"))
        required = {
            "eid", "x", "y", "z", "vx", "vy", "vz",
            "item", "count", "meta", "age", "pickup_delay",
            "controlled_stationary",
        }
        if set(item_fixture) != required:
            raise SystemExit(
                f"item fixture keys differ: expected {sorted(required)}, "
                f"got {sorted(item_fixture)}"
            )
        if (item_fixture["eid"] <= 0
                or not 1 <= item_fixture["item"] <= 4095
                or not 1 <= item_fixture["count"] <= 64
                or not 0 <= item_fixture["meta"] <= 32767
                or item_fixture["age"] < 0
                or not 0 <= item_fixture["pickup_delay"] <= 32767
                or item_fixture["controlled_stationary"] != 1):
            raise SystemExit("invalid stationary locked item fixture")
    arrow_fixture = None
    if args.arrow_fixture:
        arrow_fixture = json.loads(
            pathlib.Path(args.arrow_fixture).read_text(encoding="utf-8"))
        required = {
            "eid", "x", "y", "z", "vx", "vy", "vz", "yaw", "pitch",
            "controlled_stationary",
        }
        allowed = required | {"fire_ticks"}
        if not required <= set(arrow_fixture) <= allowed:
            raise SystemExit(
                f"arrow fixture keys differ: expected {sorted(required)} "
                "with optional fire_ticks, "
                f"got {sorted(arrow_fixture)}"
            )
        if (arrow_fixture["eid"] <= 0
                or arrow_fixture["controlled_stationary"] != 1
                or arrow_fixture["vx"] != 0.0
                or arrow_fixture["vy"] != 0.0
                or arrow_fixture["vz"] != 0.0
                or arrow_fixture["yaw"] != 0.0
                or arrow_fixture["pitch"] != 0.0
                or not 0 <= arrow_fixture.get("fire_ticks", 0) <= 32767):
            raise SystemExit("invalid stationary locked arrow fixture")
    def load_primed_tnt_fixture(path, label):
        if not path:
            return None
        fixture = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
        required = {
            "eid", "x", "y", "z", "vx", "vy", "vz", "fuse",
        }
        if set(fixture) != required:
            raise SystemExit(
                f"{label} primed-TNT fixture keys differ: "
                f"expected {sorted(required)}, "
                f"got {sorted(fixture)}"
            )
        if (fixture["eid"] <= 0
                or not 1 <= fixture["fuse"] <= 32767
                or any(not math.isfinite(float(fixture[key]))
                       for key in required - {"eid", "fuse"})):
            raise SystemExit(f"invalid locked {label} primed-TNT fixture")
        return fixture

    primed_tnt_fixture = load_primed_tnt_fixture(
        args.primed_tnt_fixture, "first")
    second_primed_tnt_fixture = load_primed_tnt_fixture(
        args.second_primed_tnt_fixture, "second")
    if second_primed_tnt_fixture is not None and primed_tnt_fixture is None:
        raise SystemExit(
            "--second-primed-tnt-fixture requires --primed-tnt-fixture")
    end_crystal_fixture = None
    if args.end_crystal_fixture:
        end_crystal_fixture = json.loads(pathlib.Path(
            args.end_crystal_fixture).read_text(encoding="utf-8"))
        required = {
            "eid", "x", "y", "z", "inner_rotation", "show_bottom",
            "has_beam", "beam_x", "beam_y", "beam_z",
        }
        if set(end_crystal_fixture) != required:
            raise SystemExit(
                "End-crystal fixture keys differ: "
                f"expected {sorted(required)}, "
                f"got {sorted(end_crystal_fixture)}"
            )
        if (end_crystal_fixture["eid"] <= 0
                or end_crystal_fixture["inner_rotation"] < 0
                or end_crystal_fixture["show_bottom"] not in (0, 1)
                or end_crystal_fixture["has_beam"] not in (0, 1)
                or any(not math.isfinite(float(end_crystal_fixture[key]))
                       for key in ("x", "y", "z"))):
            raise SystemExit("invalid locked End-crystal fixture")
    boat_fixture = None
    if args.boat_fixture:
        boat_fixture = json.loads(
            pathlib.Path(args.boat_fixture).read_text(encoding="utf-8"))
        required = {
            "eid", "x", "y", "z", "vx", "vy", "vz", "yaw", "pitch",
            "controlled_stationary",
        }
        if set(boat_fixture) != required:
            raise SystemExit(
                f"boat fixture keys differ: expected {sorted(required)}, "
                f"got {sorted(boat_fixture)}"
            )
        if (boat_fixture["eid"] <= 0
                or boat_fixture["controlled_stationary"] != 1
                or boat_fixture["vx"] != 0.0
                or boat_fixture["vy"] != 0.0
                or boat_fixture["vz"] != 0.0
                or boat_fixture["pitch"] != 0.0):
            raise SystemExit("invalid stationary locked boat fixture")
    small_fireball_fixture = None
    if args.small_fireball_fixture:
        small_fireball_fixture = json.loads(pathlib.Path(
            args.small_fireball_fixture).read_text(encoding="utf-8"))
        required = {
            "eid", "x", "y", "z", "vx", "vy", "vz", "ax", "ay", "az",
        }
        if set(small_fireball_fixture) != required:
            raise SystemExit(
                "small-fireball fixture keys differ: "
                f"expected {sorted(required)}, "
                f"got {sorted(small_fireball_fixture)}"
            )
        if small_fireball_fixture["eid"] <= 0 or any(
                not math.isfinite(float(small_fireball_fixture[key]))
                for key in required - {"eid"}):
            raise SystemExit("invalid locked small-fireball fixture")
    mob_fixture = None
    if args.mob_fixture:
        raw_fixture = json.loads(
            pathlib.Path(args.mob_fixture).read_text(encoding="utf-8"))
        required = {
            "type", "eid", "x", "y", "z", "vx", "vy", "vz", "yaw",
            "pitch", "health", "no_ai", "knockback_resistance", "mob_loot",
            "hurt_time", "death_time", "hurt_resistant_time",
        }
        if set(raw_fixture) != required:
            raise SystemExit(
                f"mob fixture keys differ: expected {sorted(required)}, "
                f"got {sorted(raw_fixture)}"
            )
        if raw_fixture["type"] != "pig" or raw_fixture["pitch"] != 0.0:
            raise SystemExit("only the locked pitch-zero pig fixture is supported")
        if (raw_fixture["knockback_resistance"] != 1.0
                or raw_fixture["mob_loot"] != 0):
            raise SystemExit("locked pig requires full knockback resistance and no loot")
        mob_fixture = {
            "entity": 11,
            "eid": raw_fixture["eid"],
            "x": raw_fixture["x"],
            "y": raw_fixture["y"],
            "z": raw_fixture["z"],
            "vx": raw_fixture["vx"],
            "vy": raw_fixture["vy"],
            "vz": raw_fixture["vz"],
            "yaw": raw_fixture["yaw"],
            "health": raw_fixture["health"],
            "no_ai": raw_fixture["no_ai"],
            "hurt_time": raw_fixture["hurt_time"],
            "death_time": raw_fixture["death_time"],
            "hurt_resistant_time": raw_fixture["hurt_resistant_time"],
        }
    random_selection_fixture = None
    if args.random_selection_fixture:
        random_selection_fixture = json.loads(pathlib.Path(
            args.random_selection_fixture).read_text(encoding="utf-8"))
        required = {
            "x", "y", "z", "block", "seed", "loaded_chunks",
            "iterator_chunks", "target_chunk_rank", "eligible_sections",
            "random_blocks", "sanitized_blocks", "lcg_advances_before",
            "selection_lcg_value", "update_lcg", "world_rand_seed48",
            "target_promoted",
        }
        if set(random_selection_fixture) != required:
            raise SystemExit(
                "random-selection fixture keys differ: "
                f"expected {sorted(required)}, "
                f"got {sorted(random_selection_fixture)}"
            )
        if (random_selection_fixture["eligible_sections"] != 1
                or random_selection_fixture["random_blocks"] != 1
                or random_selection_fixture["target_chunk_rank"] != 0
                or random_selection_fixture["target_promoted"] != 1
                or random_selection_fixture["lcg_advances_before"] < 0):
            raise SystemExit("random-selection fixture is not isolated")
    for output in (args.script_out, args.raw_state, args.state):
        pathlib.Path(output).resolve().parent.mkdir(parents=True, exist_ok=True)
    write_script(
        args.script_out,
        tape,
        spawn,
        args.platform,
        args.set_block,
        args.world_time,
        args.capsule,
        xp_fixture,
        item_fixture,
        arrow_fixture,
        primed_tnt_fixture,
        second_primed_tnt_fixture,
        end_crystal_fixture,
        boat_fixture,
        small_fireball_fixture,
        mob_fixture,
        args.potion_fixture,
        args.armor_fixture,
        args.random_tick_offset,
        random_selection_fixture,
        args.tick0_set_block_offset,
        block_edit_rows,
        args.scheduled_random_reset,
        args.tick0_harvest_offset,
    )
    if not args.skip_build:
        subprocess.run(["make", "game"], cwd=MAGMA, check=True)
    if not GAME.exists():
        raise SystemExit(f"{GAME} is missing; run make game")
    runtime_env = os.environ.copy()
    if args.capsule:
        runtime_env["MAGMA_CAPSULE_DIR"] = str(
            pathlib.Path(args.capsule).resolve())
    if args.blocks_out:
        runtime_env["MAGMA_BLOCKS_OUT"] = str(
            pathlib.Path(args.blocks_out).resolve())
        runtime_env["MAGMA_BLOCKS_BOX"] = ",".join(
            str(value) for value in args.blocks_box)
    if args.block_light_out:
        runtime_env["MAGMA_BLOCK_LIGHT_OUT"] = str(
            pathlib.Path(args.block_light_out).resolve())
        runtime_env["MAGMA_BLOCKS_BOX"] = ",".join(
            str(value) for value in args.blocks_box)
    if args.sky_light_out:
        runtime_env["MAGMA_SKY_LIGHT_OUT"] = str(
            pathlib.Path(args.sky_light_out).resolve())
        runtime_env["MAGMA_BLOCKS_BOX"] = ",".join(
            str(value) for value in args.blocks_box)
    subprocess.run(
        [
            str(GAME),
            "--seed",
            str(args.seed),
            "--world",
            "default",
            "--view-distance",
            "1",
            "--headless",
            "--ticks",
            str(len(tape)),
            "--script",
            str(pathlib.Path(args.script_out).resolve()),
            "--state-out",
            str(pathlib.Path(args.raw_state).resolve()),
            "--render",
            "off",
            "--pace",
            "unlimited",
            "--weather",
            "off",
            "--daylight",
            "off",
            "--mobs",
            "off",
        ],
        cwd=MAGMA,
        env=runtime_env,
        check=True,
    )
    raw_rows = [
        json.loads(line)
        for line in pathlib.Path(args.raw_state).read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    if len(raw_rows) != len(tape):
        raise SystemExit(
            f"runtime wrote {len(raw_rows)} rows for a {len(tape)}-tick tape"
        )
    with open(args.state, "w", encoding="utf-8") as stream:
        for tick, row in enumerate(raw_rows):
            stream.write(
                json.dumps(canonicalize(tick, row), separators=(",", ":")) + "\n"
            )
    print(
        f"wrote {len(raw_rows)} full-runtime rows -> {args.state} "
        f"(raw {args.raw_state}, "
        f"init={'capsule ' + args.capsule if args.capsule else 'synthetic fixture'})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
