#!/usr/bin/env python3
"""Bit-compare one isolated real birth-tick entity boundary with magma."""

import argparse
import json
import pathlib
import re
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
BASE = {
    "first_color": 14,
    "second_color": 0,
    "distance": 0.0,
    "z_offset": 0.0,
    "world_seed48": 0x123456789ABC,
    "first_entity_seed48": 0x23456789ABCD,
    "second_entity_seed48": 0x3456789ABCDE,
    "child_entity_seed48": 0x56789ABCDEF0,
    "math_seed48": 0x456789ABCDEF,
    "next_entity_id": 690000,
    "do_mob_loot": True,
    "grounded": False,
    "pair_count": 1,
    "pair_gap": 12.0,
    "first_color_2": 0,
    "second_color_2": 1,
    "distance_2": 0.25,
    "z_offset_2": 0.0,
    "first_entity_seed48_2": 0x6789ABCDEF01,
    "second_entity_seed48_2": 0x789ABCDEF012,
    "child_entity_seed48_2": 0x89ABCDEF0123,
    "preexisting_xp": False,
    "preexisting_xp_expires": False,
    "preexisting_living_expires": False,
}
CASES = (
    ("recipe_with_xp", {}),
    ("fallback_with_xp", {
        "first_color": 0,
        "second_color": 1,
        "world_seed48": 0,
    }),
    ("recipe_without_xp", {"do_mob_loot": False}),
    ("airborne_overlapping_quarter", {"distance": 0.25}),
    ("airborne_separated_one", {"distance": 1.0}),
    ("airborne_separated_two", {"distance": 2.0}),
    ("airborne_separated_near_limit", {"distance": 2.75}),
    ("grounded_separated_two", {"distance": 2.0, "grounded": True}),
    ("grounded_diagonal", {
        "distance": 2.0, "z_offset": 0.75, "grounded": True,
    }),
    ("grounded_turn_clamped", {
        "distance": 0.0, "z_offset": -2.0, "grounded": True,
    }),
    ("simultaneous_two_pairs_with_xp", {
        "pair_count": 2,
        "distance": 0.25,
    }),
    ("simultaneous_two_pairs_without_xp", {
        "pair_count": 2,
        "distance": 0.25,
        "do_mob_loot": False,
    }),
    ("preexisting_xp_before_parents", {
        "distance": 0.25,
        "preexisting_xp": True,
    }),
    ("preexisting_xp_slot_reuse", {
        "distance": 0.25,
        "preexisting_xp": True,
        "preexisting_xp_expires": True,
    }),
    ("preexisting_living_slot_reuse", {
        "distance": 0.25,
        "preexisting_living_expires": True,
    }),
)
ANIMAL_CASES = (
    ("cow_live_airborne_pair", {"species": "cow", "distance": 0.25}),
    ("cow_live_grounded_axial", {
        "species": "cow", "distance": 2.0, "grounded": True,
    }),
    ("pig_live_airborne_pair", {"species": "pig", "distance": 0.25}),
    ("pig_live_grounded_diagonal", {
        "species": "pig", "distance": 2.0, "z_offset": 0.75,
        "grounded": True,
    }),
    ("chicken_live_airborne_pair", {
        "species": "chicken",
        "distance": 0.25,
        "first_chicken_egg_timer": 7000,
        "second_chicken_egg_timer": 8000,
        "first_chicken_egg_timer_2": 9000,
        "second_chicken_egg_timer_2": 10000,
    }),
    ("chicken_live_grounded_pair", {
        "species": "chicken",
        "distance": 0.0,
        "grounded": True,
        "first_chicken_egg_timer": 7100,
        "second_chicken_egg_timer": 8100,
        "first_chicken_egg_timer_2": 9100,
        "second_chicken_egg_timer_2": 10100,
    }),
    ("chicken_live_grounded_turn_clamped", {
        "species": "chicken",
        "distance": 0.0,
        "z_offset": -2.0,
        "grounded": True,
        "first_chicken_egg_timer": 7200,
        "second_chicken_egg_timer": 8200,
        "first_chicken_egg_timer_2": 9200,
        "second_chicken_egg_timer_2": 10200,
    }),
    ("chicken_live_airborne_egg_threshold", {
        "species": "chicken",
        "distance": 0.25,
        "do_mob_loot": False,
        "first_chicken_egg_timer": 1,
        "second_chicken_egg_timer": 8000,
        "first_chicken_egg_timer_2": 9000,
        "second_chicken_egg_timer_2": 10000,
    }),
)
HEX8 = re.compile(r"^[0-9a-f]{8}$")
HEX16 = re.compile(r"^[0-9a-f]{16}$")
TOP_KEYS = {
    "ok", "delay_hook_count", "birth_pin_count", "child_pin_count",
    "entity_order", "update_order",
    "sheep", "xp_orbs", "particles", "world_seed48", "math_seed48",
    "next_entity_id",
}
SHEEP_KEYS = {
    "eid", "growing_age", "in_love", "fleece", "sheared",
    "ticks_existed", "entity_age", "living_sound_time",
    "task_tick_count", "on_ground", "fall_distance_bits", "yaw_bits",
    "pitch_bits", "position_bits", "motion_bits",
    "last_tick_position_bits", "previous_position_bits", "entity_seed48",
    "entity_have_next_gaussian", "entity_next_gaussian_bits",
}
ANIMAL_TOP_KEYS = ((TOP_KEYS - {"sheep"})
                   | {"species", "animals", "items", "events"})
ANIMAL_KEYS = SHEEP_KEYS - {"fleece", "sheared"}
ANIMAL_KEYS.add("species")
SHEEP_ANIMAL_KEYS = ANIMAL_KEYS | {"fleece", "sheared"}
CHICKEN_KEYS = ANIMAL_KEYS | {
    "time_until_next_egg", "wing_rotation_bits", "dest_pos_bits",
    "o_flap_speed_bits", "o_flap_bits", "wing_rot_delta_bits",
    "chicken_jockey",
}
XP_KEYS = {
    "eid", "value", "ticks_existed", "xp_color", "xp_orb_age",
    "pickup_delay", "health", "on_ground", "yaw_bits", "position_bits",
    "motion_bits", "last_tick_position_bits", "previous_position_bits",
}
PARTICLE_KEYS = {
    "seq", "id", "ignore_range", "parameters", "payload_bits",
}
ITEM_KEYS = {
    "eid", "x", "y", "z", "vx", "vy", "vz", "yaw_bits", "item",
    "count", "meta", "age", "pickup_delay", "health", "lifespan",
    "hover_start_bits", "on_ground", "is_dead",
}
EVENT_KEYS = {
    "kind", "eid", "sound", "category", "x", "y", "z", "volume_bits",
    "pitch_bits",
}


def native(case):
    args = [
        MAGMA / "game" / "test_sheep_mating_tick_oracle",
        case["first_color"], case["second_color"], case["distance"],
        case["z_offset"],
        case["x"], case["y"], case["z"], case["world_seed48"],
        case["first_entity_seed48"], case["second_entity_seed48"],
        case["child_entity_seed48"], case["math_seed48"],
        case["next_entity_id"], int(case["do_mob_loot"]),
        int(case["grounded"]),
        case["pair_count"], case["pair_gap"],
        case["first_color_2"], case["second_color_2"],
        case["distance_2"], case["z_offset_2"],
        case["first_entity_seed48_2"], case["second_entity_seed48_2"],
        case["child_entity_seed48_2"],
        int(case["preexisting_xp"]),
        int(case["preexisting_xp_expires"]),
        int(case["preexisting_living_expires"]),
    ]
    return json.loads(subprocess.check_output(
        [str(value) for value in args], text=True))


def animal_native(case, java=None):
    child_egg_timers = [0, 0]
    if case["species"] == "chicken":
        if java is None:
            raise AssertionError("chicken native fixture needs Java child state")
        children = java["animals"][case["pair_count"] * 2:]
        if len(children) != case["pair_count"]:
            raise AssertionError("invalid Java chicken child count")
        for index, child in enumerate(children):
            child_egg_timers[index] = child["time_until_next_egg"]
    args = [
        MAGMA / "game" / "test_sheep_mating_tick_oracle",
        case["species"],
        case["first_color"], case["second_color"], case["distance"],
        case["z_offset"],
        case["x"], case["y"], case["z"], case["world_seed48"],
        case["first_entity_seed48"], case["second_entity_seed48"],
        case["child_entity_seed48"], case["math_seed48"],
        case["next_entity_id"], int(case["do_mob_loot"]),
        int(case["grounded"]),
        case["pair_count"], case["pair_gap"],
        case["first_color_2"], case["second_color_2"],
        case["distance_2"], case["z_offset_2"],
        case["first_entity_seed48_2"], case["second_entity_seed48_2"],
        case["child_entity_seed48_2"],
        int(case["preexisting_xp"]),
        int(case["preexisting_xp_expires"]),
        int(case["preexisting_living_expires"]),
        case.get("first_chicken_egg_timer", 7000),
        case.get("second_chicken_egg_timer", 8000),
        case.get("first_chicken_egg_timer_2", 9000),
        case.get("second_chicken_egg_timer_2", 10000),
        child_egg_timers[0], child_egg_timers[1],
    ]
    return json.loads(subprocess.check_output(
        [str(value) for value in args], text=True))


def check_bits(values, count, label):
    if not isinstance(values, list) or len(values) != count:
        raise AssertionError(f"{label}: expected {count} bit strings")
    if not all(isinstance(value, str) and HEX16.fullmatch(value)
               for value in values):
        raise AssertionError(f"{label}: malformed double bits")


def validate(value, case):
    if set(value) != TOP_KEYS or value["ok"] is not True:
        raise AssertionError(f"{case}: invalid top-level schema")
    pair_count = case["pair_count"]
    preexisting_xp = int(case["preexisting_xp"])
    preexisting_xp_expires = int(case["preexisting_xp_expires"])
    preexisting_living_expires = int(case["preexisting_living_expires"])
    retained_preexisting_xp = preexisting_xp - preexisting_xp_expires
    expected_xp = retained_preexisting_xp + (
        pair_count if case["do_mob_loot"] else 0)
    expected_entities = retained_preexisting_xp + pair_count * 3 + (
        pair_count if case["do_mob_loot"] else 0)
    parent_base = (case["next_entity_id"] + preexisting_xp
                   + preexisting_living_expires)
    parent_ids = list(range(parent_base, parent_base + pair_count * 2))
    cursor = parent_base + pair_count * 2
    child_ids = []
    xp_ids = [case["next_entity_id"]] if retained_preexisting_xp else []
    tail_ids = []
    for _ in range(pair_count):
        child_ids.append(cursor)
        tail_ids.append(cursor)
        cursor += 1
        if case["do_mob_loot"]:
            xp_ids.append(cursor)
            tail_ids.append(cursor)
            cursor += 1
    expected_order = (
        ([case["next_entity_id"]] if retained_preexisting_xp else [])
        + parent_ids + tail_ids)
    expected_update_order = (
        ([case["next_entity_id"]] if preexisting_xp else [])
        + ([case["next_entity_id"] + preexisting_xp]
           if preexisting_living_expires else [])
        + parent_ids + tail_ids)
    if len(value["entity_order"]) != expected_entities:
        raise AssertionError(f"{case}: invalid entity order length")
    if len(value["update_order"]) != len(expected_update_order):
        raise AssertionError(f"{case}: invalid update order length")
    if value["entity_order"] != expected_order:
        raise AssertionError(
            f"{case}: invalid loaded entity order "
            f"{value['entity_order']!r} != {expected_order!r}")
    if value["update_order"] != expected_update_order:
        raise AssertionError(
            f"{case}: invalid dispatch order "
            f"{value['update_order']!r} != {expected_update_order!r}")
    if len(value["sheep"]) != pair_count * 3:
        raise AssertionError(f"{case}: invalid sheep count")
    if len(value["xp_orbs"]) != expected_xp:
        raise AssertionError(f"{case}: invalid XP count")
    if [row["eid"] for row in value["sheep"]] != parent_ids + child_ids:
        raise AssertionError(f"{case}: invalid sheep loaded order")
    if [row["eid"] for row in value["xp_orbs"]] != xp_ids:
        raise AssertionError(f"{case}: invalid XP loaded order")
    for key in ("delay_hook_count", "birth_pin_count", "child_pin_count"):
        if value[key] != pair_count:
            raise AssertionError(f"{case}: invalid {key}")
    if value["next_entity_id"] != cursor:
        raise AssertionError(f"{case}: invalid next entity ID")
    if len(value["particles"]) != pair_count * 7:
        raise AssertionError(f"{case}: invalid birth particle count")
    for index, row in enumerate(value["sheep"]):
        if set(row) != SHEEP_KEYS:
            raise AssertionError(f"{case}: sheep {index} schema changed")
        for key in ("position_bits", "motion_bits",
                    "last_tick_position_bits", "previous_position_bits"):
            check_bits(row[key], 3, f"{case}: sheep {index} {key}")
        for key in ("fall_distance_bits", "yaw_bits", "pitch_bits"):
            if not isinstance(row[key], str) or not HEX8.fullmatch(row[key]):
                raise AssertionError(f"{case}: sheep {index} malformed {key}")
        if not HEX16.fullmatch(row["entity_next_gaussian_bits"]):
            raise AssertionError(f"{case}: sheep {index} malformed gaussian")
    for index, row in enumerate(value["xp_orbs"]):
        if set(row) != XP_KEYS:
            raise AssertionError(f"{case}: XP {index} schema changed")
        for key in ("position_bits", "motion_bits",
                    "last_tick_position_bits", "previous_position_bits"):
            check_bits(row[key], 3, f"{case}: XP {index} {key}")
        if not HEX8.fullmatch(row["yaw_bits"]):
            raise AssertionError(f"{case}: XP {index} malformed yaw")
    for index, row in enumerate(value["particles"]):
        if set(row) != PARTICLE_KEYS:
            raise AssertionError(f"{case}: particle {index} schema changed")
        check_bits(row["payload_bits"], 6, f"{case}: particle {index}")


def validate_animal(value, case):
    if set(value) != ANIMAL_TOP_KEYS or value["ok"] is not True:
        raise AssertionError(f"{case}: invalid top-level animal schema")
    species = case["species"]
    if value["species"] != species:
        raise AssertionError(f"{case}: wrong species {value['species']!r}")
    pair_count = case["pair_count"]
    preexisting_xp = int(case["preexisting_xp"])
    preexisting_xp_expires = int(case["preexisting_xp_expires"])
    preexisting_living_expires = int(case["preexisting_living_expires"])
    retained_preexisting_xp = preexisting_xp - preexisting_xp_expires
    egg_parent_indices = []
    if species == "chicken":
        egg_timers = [
            case.get("first_chicken_egg_timer", 7000),
            case.get("second_chicken_egg_timer", 8000),
        ]
        if pair_count == 2:
            egg_timers.extend([
                case.get("first_chicken_egg_timer_2", 9000),
                case.get("second_chicken_egg_timer_2", 10000),
            ])
        egg_parent_indices = [
            index for index, timer in enumerate(egg_timers) if timer == 1
        ]
    egg_count = len(egg_parent_indices)
    expected_xp = retained_preexisting_xp + (
        pair_count if case["do_mob_loot"] else 0)
    expected_entities = (retained_preexisting_xp + pair_count * 3
                         + (pair_count if case["do_mob_loot"] else 0)
                         + egg_count)
    parent_base = (case["next_entity_id"] + preexisting_xp
                   + preexisting_living_expires)
    parent_ids = list(range(parent_base, parent_base + pair_count * 2))
    cursor = parent_base + pair_count * 2
    child_ids = []
    xp_ids = [case["next_entity_id"]] if retained_preexisting_xp else []
    tail_ids = []
    for _ in range(pair_count):
        child_ids.append(cursor)
        tail_ids.append(cursor)
        cursor += 1
        if case["do_mob_loot"]:
            xp_ids.append(cursor)
            tail_ids.append(cursor)
            cursor += 1
    egg_ids = list(range(cursor, cursor + egg_count))
    cursor += egg_count
    expected_order = (
        ([case["next_entity_id"]] if retained_preexisting_xp else [])
        + parent_ids + tail_ids + egg_ids)
    expected_update_order = (
        ([case["next_entity_id"]] if preexisting_xp else [])
        + ([case["next_entity_id"] + preexisting_xp]
           if preexisting_living_expires else [])
        + parent_ids + tail_ids + egg_ids)
    if len(value["entity_order"]) != expected_entities:
        raise AssertionError(f"{case}: invalid entity order length")
    if len(value["update_order"]) != len(expected_update_order):
        raise AssertionError(f"{case}: invalid update order length")
    if value["entity_order"] != expected_order:
        raise AssertionError(
            f"{case}: invalid loaded entity order "
            f"{value['entity_order']!r} != {expected_order!r}")
    if value["update_order"] != expected_update_order:
        raise AssertionError(
            f"{case}: invalid dispatch order "
            f"{value['update_order']!r} != {expected_update_order!r}")
    if len(value["animals"]) != pair_count * 3:
        raise AssertionError(f"{case}: invalid animal count")
    if len(value["xp_orbs"]) != expected_xp:
        raise AssertionError(f"{case}: invalid XP count")
    if len(value["items"]) != egg_count:
        raise AssertionError(f"{case}: invalid egg item count")
    if len(value["events"]) != egg_count:
        raise AssertionError(f"{case}: invalid egg event count")
    if [row["eid"] for row in value["animals"]] != parent_ids + child_ids:
        raise AssertionError(f"{case}: invalid animal loaded order")
    if [row["eid"] for row in value["xp_orbs"]] != xp_ids:
        raise AssertionError(f"{case}: invalid XP loaded order")
    if [row["eid"] for row in value["items"]] != egg_ids:
        raise AssertionError(f"{case}: invalid egg item order")
    for key in ("delay_hook_count", "birth_pin_count", "child_pin_count"):
        if value[key] != pair_count:
            raise AssertionError(f"{case}: invalid {key}")
    if value["next_entity_id"] != cursor:
        raise AssertionError(f"{case}: invalid next entity ID")
    if len(value["particles"]) != pair_count * 7:
        raise AssertionError(f"{case}: invalid birth particle count")
    expected_keys = (CHICKEN_KEYS if species == "chicken"
                     else SHEEP_ANIMAL_KEYS if species == "sheep"
                     else ANIMAL_KEYS)
    for index, row in enumerate(value["animals"]):
        if set(row) != expected_keys:
            raise AssertionError(f"{case}: animal {index} schema changed")
        if row["species"] != species:
            raise AssertionError(f"{case}: animal {index} wrong species")
        for key in ("position_bits", "motion_bits",
                    "last_tick_position_bits", "previous_position_bits"):
            check_bits(row[key], 3, f"{case}: animal {index} {key}")
        float_keys = ["fall_distance_bits", "yaw_bits", "pitch_bits"]
        if species == "chicken":
            float_keys.extend([
                "wing_rotation_bits", "dest_pos_bits",
                "o_flap_speed_bits", "o_flap_bits", "wing_rot_delta_bits",
            ])
            if not isinstance(row["time_until_next_egg"], int):
                raise AssertionError(
                    f"{case}: animal {index} malformed egg timer")
            if not isinstance(row["chicken_jockey"], bool):
                raise AssertionError(
                    f"{case}: animal {index} malformed jockey state")
        for key in float_keys:
            if not isinstance(row[key], str) or not HEX8.fullmatch(row[key]):
                raise AssertionError(
                    f"{case}: animal {index} malformed {key}")
        if not HEX16.fullmatch(row["entity_next_gaussian_bits"]):
            raise AssertionError(
                f"{case}: animal {index} malformed gaussian")
    for index, row in enumerate(value["xp_orbs"]):
        if set(row) != XP_KEYS:
            raise AssertionError(f"{case}: XP {index} schema changed")
        for key in ("position_bits", "motion_bits",
                    "last_tick_position_bits", "previous_position_bits"):
            check_bits(row[key], 3, f"{case}: XP {index} {key}")
        if not HEX8.fullmatch(row["yaw_bits"]):
            raise AssertionError(f"{case}: XP {index} malformed yaw")
    for index, row in enumerate(value["items"]):
        if set(row) != ITEM_KEYS:
            raise AssertionError(f"{case}: item {index} schema changed")
        if (row["item"], row["count"], row["meta"]) != (344, 1, 0):
            raise AssertionError(f"{case}: item {index} is not one egg")
        for key in ("yaw_bits", "hover_start_bits"):
            if not isinstance(row[key], str) or not HEX8.fullmatch(row[key]):
                raise AssertionError(
                    f"{case}: item {index} malformed {key}")
        if (row["age"], row["pickup_delay"], row["health"],
                row["lifespan"], row["is_dead"]) != (1, 9, 5, 6000, False):
            raise AssertionError(
                f"{case}: item {index} missed its same-boundary tick")
    for index, row in enumerate(value["events"]):
        if set(row) != EVENT_KEYS:
            raise AssertionError(f"{case}: event {index} schema changed")
        expected_eid = parent_ids[egg_parent_indices[index]]
        if (row["kind"], row["eid"], row["sound"], row["category"],
                row["volume_bits"]) != (
                    "sound", expected_eid,
                    "minecraft:entity.chicken.egg", "neutral", "3f800000"):
            raise AssertionError(f"{case}: invalid egg event {index}")
        if not isinstance(row["pitch_bits"], str) \
                or not HEX8.fullmatch(row["pitch_bits"]):
            raise AssertionError(f"{case}: malformed egg pitch {index}")
    for index, row in enumerate(value["particles"]):
        if set(row) != PARTICLE_KEYS:
            raise AssertionError(f"{case}: particle {index} schema changed")
        check_bits(row["payload_bits"], 6, f"{case}: particle {index}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--case")
    args = parser.parse_args()
    cases = [
        (index, name, overrides)
        for index, (name, overrides) in enumerate(CASES)
        if not args.case or name == args.case
    ]
    animal_cases = [
        (index, name, overrides)
        for index, (name, overrides) in enumerate(
            ANIMAL_CASES, start=len(CASES))
        if not args.case or name == args.case
    ]
    if not cases and not animal_cases:
        parser.error(f"unknown case: {args.case}")

    locked = False
    try:
        deadline = time.monotonic() + 120.0
        while True:
            try:
                request(args.port, "obs")
                break
            except (OSError, RuntimeError, ValueError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.5)
        request(args.port, "server_step_lock")
        locked = True
        observation = request(args.port, "obs")
        subprocess.run([
            "make", "-C", str(MAGMA), "game/test_sheep_mating_tick_oracle",
        ], check=True, stdout=subprocess.DEVNULL)
        for index, name, overrides in cases:
            case = dict(BASE)
            case.update(overrides)
            case.update(x=observation["x"], y=220.0, z=observation["z"])
            case["next_entity_id"] += index * 10
            java = request(args.port, "mate_sheep_tick_locked", case)
            magma = native(case)
            validate(java, case)
            validate(magma, case)
            if java != magma:
                differing = {
                    key: (java.get(key), magma.get(key))
                    for key in sorted(set(java) | set(magma))
                    if java.get(key) != magma.get(key)
                }
                raise AssertionError(f"{name}: differing={differing!r}")
        for index, name, overrides in animal_cases:
            case = dict(BASE)
            case.update(overrides)
            case.update(x=observation["x"], y=220.0, z=observation["z"])
            case["next_entity_id"] += index * 10
            java = request(args.port, "mate_animal_tick_locked", case)
            validate_animal(java, case)
            magma = animal_native(case, java)
            validate_animal(magma, case)
            if java != magma:
                differing = {
                    key: (java.get(key), magma.get(key))
                    for key in sorted(set(java) | set(magma))
                    if java.get(key) != magma.get(key)
                }
                raise AssertionError(f"{name}: differing={differing!r}")
        print(
            f"PASS java==magma: {len(cases) + len(animal_cases)} full birth "
            "ticks, exact persistent "
            "and dynamic entity order, first update, RNG, physics and IDs")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
