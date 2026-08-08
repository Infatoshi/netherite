#!/usr/bin/env python3
"""Bit-compare the real EntityAIMate animal birth boundary with magma."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
EID = 680000
BASE = {
    "species": "sheep",
    "first_color": 14,
    "second_color": 0,
    "first_age": 0,
    "second_age": 0,
    "first_love": 600,
    "second_love": 600,
    "updates": 60,
    "distance": 2.0,
    "world_seed48": 0x123456789ABC,
    "first_entity_seed48": 0x23456789ABCD,
    "second_entity_seed48": 0x3456789ABCDE,
    "child_entity_seed48": 0x13579BDF2468,
    "child_entity_have_next_gaussian": True,
    "child_entity_next_gaussian": 0.125,
    "math_seed48": 0x456789ABCDEF,
    "next_entity_id": EID,
    "do_mob_loot": True,
}
CASES = [
    ("no_love", {"first_love": 0}),
    ("zero_updates", {"updates": 0}),
    ("waiting_59", {"updates": 59}),
    ("distance_exact_three", {"distance": 3.0}),
    ("birth_recipe", {}),
    ("birth_fallback_false", {
        "first_color": 0, "second_color": 1, "world_seed48": 0}),
    ("birth_fallback_true", {
        "first_color": 0, "second_color": 1,
        "world_seed48": 1 << 47}),
    ("birth_without_xp", {"do_mob_loot": False}),
]
for species in ("cow", "pig", "chicken"):
    CASES.extend((
        (f"{species}_no_love", {
            "species": species, "first_love": 0}),
        (f"{species}_waiting_59", {
            "species": species, "updates": 59}),
        (f"{species}_distance_exact_three", {
            "species": species, "distance": 3.0}),
        (f"{species}_birth", {"species": species}),
        (f"{species}_birth_without_xp", {
            "species": species, "do_mob_loot": False}),
    ))


def normalized(overrides, index, position):
    case = dict(BASE)
    case.update(zip(("x", "y", "z"), position))
    case.update(overrides)
    case["next_entity_id"] += index * 10
    return case


def native(case, java):
    child_egg = (java["children"][0]["time_until_next_egg"]
                 if java["children"] else -1)
    args = [
        MAGMA / "game" / "test_sheep_mating_oracle",
        case["species"],
        case["first_color"], case["second_color"],
        case["first_age"], case["second_age"],
        case["first_love"], case["second_love"], case["updates"],
        case["distance"], case["x"], case["y"], case["z"],
        case["world_seed48"], case["first_entity_seed48"],
        case["second_entity_seed48"], case["math_seed48"],
        case["next_entity_id"], int(case["do_mob_loot"]),
        case["child_entity_seed48"],
        int(case["child_entity_have_next_gaussian"]),
        case["child_entity_next_gaussian"], child_egg,
    ]
    raw = subprocess.check_output([str(value) for value in args], text=True)
    return json.loads(raw)


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
    if not cases:
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
        time.sleep(2.0)
        observation = request(args.port, "obs")
        position = (
            observation["x"], observation["y"], observation["z"])
        subprocess.run(
            ["make", "-C", str(MAGMA), "game/test_sheep_mating_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for index, name, overrides in cases:
            case = normalized(overrides, index, position)
            java = request(args.port, "mate_animal_locked", case)
            if case["species"] == "chicken" and java["children"]:
                egg_timer = java["children"][0]["time_until_next_egg"]
                if not 6000 <= egg_timer <= 11999:
                    raise AssertionError(
                        f"{name}: invalid Java child egg timer {egg_timer}")
            magma = native(case, java)
            if java != magma:
                differing = {
                    key: (java.get(key), magma.get(key))
                    for key in sorted(set(java) | set(magma))
                    if java.get(key) != magma.get(key)
                }
                raise AssertionError(f"{name}: differing={differing!r}")
        print(
            f"PASS java==magma: {len(cases)} animal-mating lifecycles, "
            "exact immediate birth, newborn continuation state, sheep "
            "genetics, particles, XP, parent/shared RNG and IDs")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
