#!/usr/bin/env python3
"""Compare live 1.11.2 dispenser bonemeal plant behavior to magma."""

import argparse
import json
import subprocess
import time

from test_dragon_crystal_notification import request


HIGH_SEED48 = 0x23456789ABCD
MATH_SEED48 = 0x3456789ABCDE
NEXT_ENTITY_ID = 760000


def state_hash(states):
    value = 0xCBF29CE484222325
    for state in states:
        value ^= state
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def native_result(binary, case):
    raw = subprocess.check_output(
        [binary, str(case["crop"]), str(case["age"]),
         str(case["count"]), str(case["seed48"]),
         str(case["math_seed48"]), str(case["next_entity_id"]),
         str(int(case["upper"])), str(int(case["blocked"])),
         str(int(case["mega"])), str(case["biome"]),
         str(int(case["player"])), str(int(case["natural"])),
         str(int(case["offhand"])), str(int(case["adjacent"])),
         str(case["column"]), str(int(case["dense"])),
         str(int(case["hydrated"])), str(int(case["raining"])),
         str(int(case["lit"])), str(case["dimension"]),
         str(int(case["occupant"]))],
        text=True)
    return json.loads(raw)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", required=True)
    parser.add_argument("--print-grass-goldens", action="store_true")
    parser.add_argument("--print-player-sapling-goldens", action="store_true")
    parser.add_argument("--print-natural-sapling-goldens", action="store_true")
    args = parser.parse_args()
    crops = (
        (59, 7), (141, 7), (142, 7), (207, 3),
        (104, 7), (105, 7),
    )
    cases = []
    for crop, max_age in crops:
        cases.extend((
            {"crop": crop, "age": 0, "count": 2, "seed48": 0},
            {"crop": crop, "age": 0, "count": 2,
             "seed48": HIGH_SEED48},
            {"crop": crop, "age": max_age - 1, "count": 2,
             "seed48": HIGH_SEED48},
            {"crop": crop, "age": max_age, "count": 2,
             "seed48": HIGH_SEED48},
        ))
    for facing in (0, 2, 3):
        for age in range(3):
            cases.append({
                "crop": 127, "age": (age << 2) | facing,
                "count": 2, "seed48": HIGH_SEED48,
            })
    for plant_type in range(3):
        cases.append({
            "crop": 31, "age": plant_type,
            "count": 2, "seed48": HIGH_SEED48,
        })
    for plant_type in (0, 1, 4, 5):
        for upper in (False, True):
            cases.append({
                "crop": 175, "age": plant_type, "upper": upper,
                "count": 2, "seed48": HIGH_SEED48,
            })
    for tree_type in range(6):
        for seed48 in (0, HIGH_SEED48):
            cases.append({
                "crop": 6, "age": tree_type,
                "count": 2, "seed48": seed48,
            })
    for tree_type in range(6):
        cases.append({
            "crop": 6, "age": tree_type | 8,
            "count": 2, "seed48": 0,
        })
    cases.extend((
        {"crop": 6, "age": 8, "count": 2, "seed48": 2},
        {"crop": 6, "age": 9, "count": 2, "seed48": 0,
         "mega": True},
        {"crop": 6, "age": 11, "count": 2, "seed48": 0,
         "mega": True},
        {"crop": 6, "age": 13, "count": 2, "seed48": 0,
         "mega": True},
        {"crop": 6, "age": 8, "count": 2, "seed48": 0,
         "blocked": True},
    ))
    for tree_type in range(6):
        for seed48 in (0, HIGH_SEED48):
            cases.append({
                "crop": 6, "age": tree_type,
                "count": 2, "seed48": seed48, "player": True,
            })
    cases.extend((
        {"crop": 6, "age": 8, "count": 2, "seed48": 0,
         "player": True},
        {"crop": 6, "age": 9, "count": 2, "seed48": 0,
         "mega": True, "player": True},
        {"crop": 6, "age": 10, "count": 2, "seed48": 0,
         "player": True},
        {"crop": 6, "age": 11, "count": 2, "seed48": 0,
         "mega": True, "player": True},
        {"crop": 6, "age": 12, "count": 2, "seed48": 0,
         "player": True},
        {"crop": 6, "age": 13, "count": 2, "seed48": 0,
         "mega": True, "player": True},
        {"crop": 6, "age": 8, "count": 2, "seed48": 2,
         "player": True},
        {"crop": 6, "age": 8, "count": 2, "seed48": 0,
         "blocked": True, "player": True},
        {"crop": 6, "age": 13, "count": 2, "seed48": 0,
         "player": True},
        {"crop": 6, "age": 9, "count": 2, "seed48": 0,
         "player": True},
        {"crop": 6, "age": 11, "count": 2, "seed48": 0,
         "player": True},
    ))
    offhand_cases = []
    for case in cases:
        if case.get("player"):
            offhand_cases.append(dict(case, offhand=True))
    cases.extend(offhand_cases)
    for tree_type in range(6):
        for seed48 in (0, HIGH_SEED48):
            cases.append({
                "crop": 6, "age": tree_type,
                "count": 2, "seed48": seed48, "natural": True,
            })
    cases.extend((
        {"crop": 6, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "blocked": True, "natural": True},
        {"crop": 6, "age": 8, "count": 2, "seed48": 0,
         "natural": True},
        {"crop": 6, "age": 9, "count": 2, "seed48": 0,
         "mega": True, "natural": True},
        {"crop": 6, "age": 10, "count": 2, "seed48": 0,
         "natural": True},
        {"crop": 6, "age": 11, "count": 2, "seed48": 0,
         "mega": True, "natural": True},
        {"crop": 6, "age": 12, "count": 2, "seed48": 0,
         "natural": True},
        {"crop": 6, "age": 13, "count": 2, "seed48": 0,
         "mega": True, "natural": True},
        {"crop": 6, "age": 8, "count": 2, "seed48": 2,
         "natural": True},
        {"crop": 6, "age": 8, "count": 2, "seed48": 0,
         "blocked": True, "natural": True},
        {"crop": 6, "age": 13, "count": 2, "seed48": 0,
         "natural": True},
        {"crop": 6, "age": 9, "count": 2, "seed48": 0,
         "natural": True},
        {"crop": 6, "age": 11, "count": 2, "seed48": 0,
         "natural": True},
    ))
    for crop, max_age in (
            (59, 7), (141, 7), (142, 7), (207, 3),
            (104, 7), (105, 7)):
        for age in (0, max_age):
            for seed48 in (0, 1, HIGH_SEED48):
                cases.append({
                    "crop": crop, "age": age, "count": 2,
                    "seed48": seed48, "natural": True,
                })
    cases.append({
        "crop": 207, "age": 0, "count": 2,
        "seed48": 8, "natural": True,
    })
    for facing in (0, 2, 3):
        for age in (0, 2):
            for seed48 in (0, 1, HIGH_SEED48):
                cases.append({
                    "crop": 127, "age": (age << 2) | facing,
                    "count": 2, "seed48": seed48, "natural": True,
                })
    for crop in (104, 105):
        for seed48 in (2, 64):
            cases.append({
                "crop": crop, "age": 7, "count": 2,
                "seed48": seed48, "natural": True,
            })
        cases.extend((
            {"crop": crop, "age": 7, "count": 2, "seed48": 0,
             "natural": True, "blocked": True},
            {"crop": crop, "age": 7, "count": 2, "seed48": 0,
             "natural": True, "adjacent": True},
        ))
    for crop in (81, 83):
        cases.extend((
            {"crop": crop, "age": 0, "count": 2,
             "seed48": HIGH_SEED48, "natural": True},
            {"crop": crop, "age": 14, "count": 2,
             "seed48": HIGH_SEED48, "natural": True},
            {"crop": crop, "age": 15, "count": 2,
             "seed48": HIGH_SEED48, "natural": True},
            {"crop": crop, "age": 15, "count": 2,
             "seed48": HIGH_SEED48, "natural": True, "column": 2},
            {"crop": crop, "age": 14, "count": 2,
             "seed48": HIGH_SEED48, "natural": True, "column": 3},
            {"crop": crop, "age": 14, "count": 2,
             "seed48": HIGH_SEED48, "natural": True, "blocked": True},
        ))
    for age, seed48 in (
            (0, 0), (0, 1), (0, HIGH_SEED48),
            (3, 0), (3, HIGH_SEED48)):
        cases.append({
            "crop": 115, "age": age, "count": 2,
            "seed48": seed48, "natural": True,
        })
    for crop in (39, 40):
        for seed48 in (0, 1, HIGH_SEED48):
            cases.append({
                "crop": crop, "age": 0, "count": 2,
                "seed48": seed48, "natural": True,
            })
        cases.append({
            "crop": crop, "age": 0, "count": 2,
            "seed48": 0, "natural": True, "dense": True,
        })
    for seed48 in (0, 1, HIGH_SEED48):
        cases.append({
            "crop": 2, "age": 0, "count": 2,
            "seed48": seed48, "natural": True,
        })
    cases.append({
        "crop": 2, "age": 0, "count": 2,
        "seed48": 0, "natural": True, "blocked": True,
    })
    for seed48 in (0, 1, HIGH_SEED48):
        cases.append({
            "crop": 110, "age": 0, "count": 2,
            "seed48": seed48, "natural": True,
        })
    cases.append({
        "crop": 110, "age": 0, "count": 2,
        "seed48": 0, "natural": True, "blocked": True,
    })
    for age in (7, 1):
        cases.append({
            "crop": 60, "age": age, "count": 2,
            "seed48": HIGH_SEED48, "natural": True,
        })
    cases.extend((
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 1},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 2},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 3},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 4},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 5},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 6},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 7},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 8},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 9},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 10},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 11},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 12},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 13},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 14, "dimension": 1},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True,
         "occupant": 15, "dimension": 1},
        {"crop": 60, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True, "blocked": True},
        {"crop": 60, "age": 3, "count": 2,
         "seed48": HIGH_SEED48, "natural": True, "hydrated": True},
        {"crop": 60, "age": 7, "count": 2,
         "seed48": HIGH_SEED48, "natural": True, "hydrated": True},
        {"crop": 60, "age": 2, "count": 2,
         "seed48": HIGH_SEED48, "natural": True, "raining": True},
    ))
    for age in range(8):
        for lit in (False, True):
            cases.append({
                "crop": 78, "age": age, "count": 2,
                "seed48": HIGH_SEED48, "natural": True, "lit": lit,
            })
    for lit in (False, True):
        cases.append({
            "crop": 79, "age": 0, "count": 2,
            "seed48": HIGH_SEED48, "natural": True, "lit": lit,
        })
    for lit in (False, True):
        cases.append({
            "crop": 80, "age": 0, "count": 2,
            "seed48": HIGH_SEED48, "natural": True, "lit": lit,
        })
    for age in range(4):
        for lit in (False, True):
            cases.append({
                "crop": 212, "age": age, "count": 2,
                "seed48": HIGH_SEED48, "natural": True, "lit": lit,
            })
    cases.append({
        "crop": 212, "age": 3, "count": 2,
        "seed48": 0, "natural": True, "lit": True,
        "adjacent": True,
    })
    cases.append({
        "crop": 212, "age": 3, "count": 2,
        "seed48": 0, "natural": True, "lit": True,
        "dense": True,
    })
    for age in range(6):
        for seed48 in (0, HIGH_SEED48):
            cases.append({
                "crop": 200, "age": age, "count": 2,
                "seed48": seed48, "natural": True,
            })
    for age in range(5):
        for seed48 in (0, 1, HIGH_SEED48):
            cases.append({
                "crop": 200, "age": age, "count": 2,
                "seed48": seed48, "natural": True, "blocked": True,
            })
    for age in (0, 3, 4):
        for seed48 in (0, 1, 2, HIGH_SEED48):
            cases.append({
                "crop": 200, "age": age, "count": 2,
                "seed48": seed48, "natural": True, "column": 3,
            })
    cases.append({
        "crop": 200, "age": 0, "count": 2,
        "seed48": 0, "natural": True, "column": 2,
    })
    cases.extend((
        {"crop": 106, "age": 4, "count": 2,
         "seed48": HIGH_SEED48, "natural": True},
        {"crop": 106, "age": 15, "count": 2,
         "seed48": 6, "natural": True, "column": 2},
        {"crop": 106, "age": 15, "count": 2,
         "seed48": 2, "natural": True},
        {"crop": 106, "age": 4, "count": 2,
         "seed48": 0, "natural": True},
        {"crop": 106, "age": 4, "count": 2,
         "seed48": 3, "natural": True},
        {"crop": 106, "age": 4, "count": 2,
         "seed48": 5, "natural": True},
        {"crop": 106, "age": 4, "count": 2,
         "seed48": 54, "natural": True},
        {"crop": 106, "age": 4, "count": 2,
         "seed48": 3, "natural": True, "column": 3},
        {"crop": 106, "age": 4, "count": 2,
         "seed48": 5, "natural": True, "column": 3},
        {"crop": 106, "age": 4, "count": 2,
         "seed48": 54, "natural": True, "column": 3},
        {"crop": 106, "age": 15, "count": 2,
         "seed48": 2, "natural": True, "blocked": True},
        {"crop": 106, "age": 15, "count": 2,
         "seed48": 6, "natural": True, "column": 2, "dense": True},
        {"crop": 106, "age": 4, "count": 2,
         "seed48": 3, "natural": True, "dense": True},
        {"crop": 106, "age": 15, "count": 2,
         "seed48": 2, "natural": True, "dense": True},
        {"crop": 106, "age": 1, "count": 2,
         "seed48": 3, "natural": True, "adjacent": True},
        {"crop": 106, "age": 4, "count": 2,
         "seed48": 3, "natural": True, "adjacent": True},
        {"crop": 106, "age": 0, "count": 2,
         "seed48": 3, "natural": True, "adjacent": True},
    ))
    for column, seeds in (
            (1, (1, 2)),
            (2, (0, 4)),
            (3, (1, 2))):
        for seed48 in seeds:
            cases.append({
                "crop": 11, "age": 0, "count": 2,
                "seed48": seed48, "natural": True,
                "column": column,
            })
    for seed48 in (0, 1):
        cases.append({
            "crop": 11, "age": 0, "count": 2,
            "seed48": seed48, "natural": True,
            "column": 1, "blocked": True,
        })
    for fuel in range(1, 7):
        for column in (1, 3):
            cases.append({
                "crop": 11, "age": fuel, "count": 2,
                "seed48": 1, "natural": True,
                "column": column,
            })
    for seed48 in (0, 1, HIGH_SEED48):
        cases.append({
            "crop": 2, "age": 0, "count": 2, "seed48": seed48,
        })
    for biome in (4, 6, 132):
        for seed48 in (0, 1, HIGH_SEED48):
            cases.append({
                "crop": 2, "age": 0, "count": 2,
                "seed48": seed48, "biome": biome,
            })
    cases.extend((
        {"crop": 2, "age": 0, "count": 2, "seed48": 0,
         "biome": 129},
        {"crop": 2, "age": 0, "count": 2, "seed48": 0,
         "biome": 134},
    ))
    for mushroom in (39, 40):
        cases.extend((
            {"crop": mushroom, "age": 0, "count": 2,
             "seed48": HIGH_SEED48},
            {"crop": mushroom, "age": 0, "count": 2, "seed48": 0},
            {"crop": mushroom, "age": 0, "count": 2, "seed48": 1},
            {"crop": mushroom, "age": 0, "count": 2, "seed48": 0,
             "blocked": True},
        ))
    # Keep the cross-dimension fixtures grouped after the Overworld matrix.
    cases.extend((
        {"crop": 79, "age": 0, "count": 2,
         "seed48": HIGH_SEED48, "natural": True, "lit": True,
         "dimension": -1},
        {"crop": 212, "age": 3, "count": 2,
         "seed48": HIGH_SEED48, "natural": True, "lit": True,
         "dimension": -1},
        {"crop": 212, "age": 3, "count": 2,
         "seed48": 0, "natural": True, "lit": True,
         "adjacent": True, "dimension": -1},
        {"crop": 212, "age": 3, "count": 2,
         "seed48": 0, "natural": True, "lit": True,
         "dense": True, "dimension": -1},
    ))
    for case in cases:
        case.setdefault("upper", False)
        case.setdefault("blocked", False)
        case.setdefault("mega", False)
        case.setdefault("biome", 1)
        case.setdefault("player", False)
        case.setdefault("natural", False)
        case.setdefault("offhand", False)
        case.setdefault("adjacent", False)
        case.setdefault("column", 1)
        case.setdefault("dense", False)
        case.setdefault("hydrated", False)
        case.setdefault("raining", False)
        case.setdefault("lit", False)
        case.setdefault("dimension", 0)
        case.setdefault("occupant", 0)
        case["math_seed48"] = MATH_SEED48
        case["next_entity_id"] = NEXT_ENTITY_ID

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
        for index, case in enumerate(cases):
            try:
                java = request(args.port, "dispenser_bonemeal_locked", case)
            except RuntimeError as exc:
                raise RuntimeError(
                    f"case {index} {case}: {exc}") from exc
            native = native_result(args.native, case)
            if java != native:
                java_summary = dict(java)
                native_summary = dict(native)
                java_blocks = java_summary.pop("blocks", None)
                native_blocks = native_summary.pop("blocks", None)
                block_diffs = []
                if java_blocks is not None or native_blocks is not None:
                    java_blocks = java_blocks or []
                    native_blocks = native_blocks or []
                    block_diffs = [
                        (i, a, b) for i, (a, b) in enumerate(
                            zip(java_blocks, native_blocks)) if a != b
                    ]
                else:
                    java_blocks = []
                    native_blocks = []
                raise AssertionError(
                    f"case {index} {case}:\n"
                    f"java={json.dumps(java_summary, sort_keys=True)}\n"
                    f"native={json.dumps(native_summary, sort_keys=True)}\n"
                    f"block_lengths={len(java_blocks)},{len(native_blocks)} "
                    f"block_diff_count={len(block_diffs)} "
                    f"first_block_diffs={block_diffs[:32]}")
            if args.print_grass_goldens and case["crop"] == 2:
                print(
                    f"biome={case['biome']} seed={case['seed48']} "
                    f"cursor={java['world_seed48']} "
                    f"hash=0x{state_hash(java['blocks']):016x}")
            if (args.print_player_sapling_goldens
                    and case["player"] and case["crop"] == 6
                    and (case["age"] & 8) != 0):
                print(
                    f"meta={case['age']} seed={case['seed48']} "
                    f"mega={int(case['mega'])} blocked={int(case['blocked'])} "
                    f"target={java['crop_id']}:{java['crop_meta']} "
                    f"cursor={java['world_seed48']} "
                    f"hash=0x{state_hash(java['blocks']):016x}")
            if (args.print_natural_sapling_goldens
                    and case["natural"] and case["crop"] == 6
                    and (case["age"] & 8) != 0):
                print(
                    f"meta={case['age']} seed={case['seed48']} "
                    f"mega={int(case['mega'])} blocked={int(case['blocked'])} "
                    f"target={java['crop_id']}:{java['crop_meta']} "
                    f"cursor={java['world_seed48']} "
                    f"hash=0x{state_hash(java['blocks']):016x}")
        print(
            f"PASS real Java/native: {len(cases)} "
            "dispenser/player/natural plant rows")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
