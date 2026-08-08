#!/usr/bin/env python3
"""Compare painting and leash-knot callbacks to real Minecraft 1.11.2."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import subprocess
import sys
import time


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
ROOT = MAGMA.parent
START = ROOT / "java" / "start_oracle_instance.sh"
sys.path.insert(0, str(HERE))

from test_dragon_crystal_notification import request


GEOMETRY_FIELDS = (
    "x_bits", "y_bits", "z_bits",
    "min_x_bits", "min_y_bits", "min_z_bits",
    "max_x_bits", "max_y_bits", "max_z_bits",
)

JAVA_RANDOM_MASK = (1 << 48) - 1
JAVA_RANDOM_MULT = 0x5DEECE66D
JAVA_RANDOM_ADD = 0xB


def java_random_next(seed: int, bits: int) -> tuple[int, int]:
    seed = (seed * JAVA_RANDOM_MULT + JAVA_RANDOM_ADD) & JAVA_RANDOM_MASK
    return seed, seed >> (48 - bits)


def signed32(value: int) -> int:
    return value if value < (1 << 31) else value - (1 << 32)


def painting_constructor_choice(generator_seed48: int, bound: int) -> int:
    generator_seed48, high = java_random_next(generator_seed48, 32)
    generator_seed48, low = java_random_next(generator_seed48, 32)
    entity_external_seed = (signed32(high) << 32) + signed32(low)
    entity_seed = (entity_external_seed ^ JAVA_RANDOM_MULT) & JAVA_RANDOM_MASK
    if bound & (bound - 1) == 0:
        entity_seed, bits = java_random_next(entity_seed, 31)
        return (bound * bits) >> 31
    while True:
        entity_seed, bits = java_random_next(entity_seed, 31)
        value = bits % bound
        if bits - value + bound - 1 < (1 << 31):
            return value


def native(action: dict) -> dict:
    binary = MAGMA / "game" / "test_hanging_runtime"
    if action["kind"] == "living_leash":
        command = [
            str(binary), "--oracle", "living_leash",
            str(action["x"]), str(action["y"]), str(action["z"]),
            str(action["class_index"]), str(action["operation"]),
        ]
    elif action["kind"] == "map":
        command = [
            str(binary), "--oracle", "map",
            str(action["x"]), str(action["y"]), str(action["z"]),
            str(action["facing"]), str(action["scale"]),
            str(action["center_x"]), str(action["center_z"]),
            "1" if action.get("tracking") else "0",
            str(action["operation"]), str(action["drop_chance"]),
        ]
    elif action["kind"] == "damage":
        command = [
            str(binary), "--oracle", "damage",
            str(action["x"]), str(action["y"]), str(action["z"]),
            str(action["entity_type"]), str(action["source"]),
            "1" if action.get("creative") else "0",
        ]
    elif action["kind"] == "mixed_order":
        command = [
            str(binary), "--oracle", "mixed_order",
            str(action["x"]), str(action["y"]), str(action["z"]),
            *(str(value) for value in action["order"]),
        ]
    elif action["kind"] == "painting":
        if action.get("constructor_art"):
            command = [
                str(binary), "--oracle", "painting_constructor",
                str(action["x"]), str(action["y"]), str(action["z"]),
                str(action["facing"]), str(action["support_width"]),
                str(action["support_height"]),
                str(action["entity_seed_generator_seed48"]),
            ]
        else:
            command = [
                str(binary), "--oracle", "painting",
                str(action["x"]), str(action["y"]), str(action["z"]),
                str(action["facing"]), str(action["art"]),
                str(action.get("tick_counter", 0)),
                "1" if action.get("remove_support") else "0",
            ]
    elif action["kind"] == "knot":
        command = [
            str(binary), "--oracle", "knot",
            str(action["x"]), str(action["y"]), str(action["z"]),
            "1" if action.get("attach") else "0",
            "1" if action.get("interact") else "0",
            "1" if action.get("llama_tick") else "0",
        ]
    else:
        command = [
            str(binary), "--oracle", "frame",
            str(action["x"]), str(action["y"]), str(action["z"]),
            str(action["facing"]), str(action["item"]),
            str(action["rotation"]), str(action["tick_counter"]),
            "1" if action.get("remove_support") else "0",
            str(action.get("hits", 0)),
            "1" if action.get("creative") else "0",
            "1" if action.get("explosion") else "0",
            str(action["drop_chance"]),
            str(action["entity_seed48"]),
            str(action["math_seed48"]),
            str(action.get("interactions", 0)),
        ]
    result = subprocess.run(
        command, check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def compare(case_id: str, action: dict, java: dict, c: dict) -> None:
    if action["kind"] == "living_leash":
        for field in (
                "class_index", "can_leash_before", "handled",
                "leashed_after", "holder_after", "held_count_after",
                "motion_x_bits", "motion_y_bits", "motion_z_bits",
                "eating_after"):
            if java.get(field) != c.get(field):
                raise AssertionError(
                    f"{case_id} {field}: Java={java.get(field)!r} "
                    f"native={c.get(field)!r}\n"
                    f"Java: {java!r}\nNative: {c!r}")
        java_drops = [
            (row["item"], row["count"]) for row in java.get("drops", [])]
        native_drops = [
            (row["item"], row["count"]) for row in c.get("drops", [])]
        if java_drops != native_drops:
            raise AssertionError(
                f"{case_id} drops: Java={java_drops!r} "
                f"native={native_drops!r}")
        return
    if action["kind"] == "map":
        for field in (
                "dead_after", "item_after", "rotation_after",
                "decoration_present", "decoration_type",
                "decoration_x", "decoration_z", "decoration_rotation"):
            if java.get(field) != c.get(field):
                raise AssertionError(
                    f"{case_id} {field}: Java={java.get(field)!r} "
                    f"native={c.get(field)!r}\n"
                    f"Java: {java!r}\nNative: {c!r}")
        java_drops = [
            (row["item"], row["count"]) for row in java.get("drops", [])]
        native_drops = [
            (row["item"], row["count"]) for row in c.get("drops", [])]
        if java_drops != native_drops:
            raise AssertionError(
                f"{case_id} drops: Java={java_drops!r} "
                f"native={native_drops!r}")
        return
    if action["kind"] == "damage":
        for field in ("dead_after", "item_after"):
            if java.get(field) != c.get(field):
                raise AssertionError(
                    f"{case_id} {field}: Java={java.get(field)!r} "
                    f"native={c.get(field)!r}")
        java_drops = [
            (row["item"], row["count"]) for row in java.get("drops", [])]
        native_drops = [
            (row["item"], row["count"]) for row in c.get("drops", [])]
        if java_drops != native_drops:
            raise AssertionError(
                f"{case_id} drops: Java={java_drops!r} "
                f"native={native_drops!r}")
        return
    if action["kind"] == "mixed_order":
        for field in (
                "loaded_kinds", "frame_dead", "painting_dead", "knot_dead"):
            if java.get(field) != c.get(field):
                raise AssertionError(
                    f"{case_id} {field}: Java={java.get(field)!r} "
                    f"native={c.get(field)!r}")
        java_drops = [
            (row["item"], row["count"]) for row in java.get("drops", [])]
        native_drops = [
            (row["item"], row["count"]) for row in c.get("drops", [])]
        if java_drops != native_drops:
            raise AssertionError(
                f"{case_id} drop order: Java={java_drops!r} "
                f"native={native_drops!r}")
        return
    for field in ("valid_before", *GEOMETRY_FIELDS, "dead_after"):
        if java.get(field) != c.get(field):
            raise AssertionError(
                f"{case_id} {field}: Java={java.get(field)!r} "
                f"native={c.get(field)!r}\nJava: {java!r}\nNative: {c!r}")
    if action["kind"] == "painting":
        for field in (
                "art", "width", "height", "facing",
                "tick_counter_after"):
            if java.get(field) != c.get(field):
                raise AssertionError(
                    f"{case_id} {field}: Java={java.get(field)!r} "
                    f"native={c.get(field)!r}\nJava: {java!r}\nNative: {c!r}")
        if java.get("drops") != c.get("drops"):
            raise AssertionError(
                f"{case_id} painting drops differ\n"
                f"Java: {java.get('drops')!r}\n"
                f"Native: {c.get('drops')!r}")
        if action.get("constructor_art"):
            for field in (
                    "entity_seed_generator_seed48_after",
                    "entity_seed48_after"):
                if java.get(field) != c.get(field):
                    raise AssertionError(
                        f"{case_id} {field}: Java={java.get(field)!r} "
                        f"native={c.get(field)!r}")
        return
    if action["kind"] == "frame":
        for field in (
                "item_after", "count_after", "meta_after",
                "rotation_after", "tick_counter_after",
                "entity_seed48_after", "held_count_after",
                "math_seed48_after"):
            if java.get(field) != c.get(field):
                raise AssertionError(
                    f"{case_id} {field}: Java={java.get(field)!r} "
                    f"native={c.get(field)!r}\nJava: {java!r}\nNative: {c!r}")
        if java.get("drops") != c.get("drops"):
            raise AssertionError(
                f"{case_id} item-frame drops differ\n"
                f"Java: {java.get('drops')!r}\n"
                f"Native: {c.get('drops')!r}")
        return
    for field in (
            "holder_before", "llama_leashed_after", "holder_after"):
        if java.get(field) != c.get(field):
            raise AssertionError(
                f"{case_id} {field}: Java={java.get(field)!r} "
                f"native={c.get(field)!r}")
    # The parked command calls llama.onUpdate directly, while the native case
    # completes the ordinary loaded-entity boundary, so the newly appended
    # lead receives its same-tick EntityItem update only in the latter. The
    # callback contract here is drop identity/count; item continuation is
    # already covered by the EntityItem gates.
    java_drops = [
        (row["item"], row["count"]) for row in java.get("drops", [])]
    native_drops = [
        (row["item"], row["count"]) for row in c.get("drops", [])]
    if java_drops != native_drops:
        raise AssertionError(
            f"{case_id} knot drop identities differ: "
            f"Java={java_drops!r} native={native_drops!r}")


def run(port: int) -> None:
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_hanging_runtime"],
        check=True, stdout=subprocess.DEVNULL)
    unit = subprocess.run(
        [str(MAGMA / "game" / "test_hanging_runtime")],
        check=True, capture_output=True, text=True)
    if unit.stdout.strip() != "hanging runtime: PASS":
        raise AssertionError(f"unexpected native unit output: {unit.stdout!r}")
    deadline = time.monotonic() + 120.0
    while True:
        try:
            obs = request(port, "obs")
            if "x" in obs:
                break
        except (OSError, RuntimeError, ValueError):
            pass
        if time.monotonic() >= deadline:
            raise RuntimeError("cold Java oracle did not produce a player")
        time.sleep(0.1)
    request(port, "server_step_lock")
    try:
        base_x = math.floor(float(obs["x"])) + 5
        base_y = max(8, math.floor(float(obs["y"])) + 2)
        base_z = math.floor(float(obs["z"]))
        cases = (
            ("painting_16_north", {
                "kind": "painting", "x": base_x, "y": base_y,
                "z": base_z, "facing": 2, "art": 0,
                "tick_counter": 0,
            }),
            ("painting_64_north", {
                "kind": "painting", "x": base_x + 6, "y": base_y,
                "z": base_z, "facing": 2, "art": 21,
                "tick_counter": 0,
            }),
            ("painting_48_south", {
                "kind": "painting", "x": base_x + 13, "y": base_y,
                "z": base_z, "facing": 3, "art": 24,
                "tick_counter": 0,
            }),
            ("painting_32_west", {
                "kind": "painting", "x": base_x + 19, "y": base_y,
                "z": base_z, "facing": 4, "art": 14,
                "tick_counter": 0,
            }),
            ("painting_counter_99", {
                "kind": "painting", "x": base_x + 23, "y": base_y,
                "z": base_z, "facing": 2, "art": 0,
                "tick_counter": 99, "remove_support": True,
            }),
            ("painting_counter_100_break", {
                "kind": "painting", "x": base_x + 25, "y": base_y,
                "z": base_z, "facing": 2, "art": 0,
                "tick_counter": 100, "remove_support": True,
            }),
            ("knot_standalone", {
                "kind": "knot", "x": base_x + 27, "y": base_y,
                "z": base_z, "attach": False,
            }),
            ("knot_attach", {
                "kind": "knot", "x": base_x + 29, "y": base_y,
                "z": base_z, "attach": True,
            }),
            ("knot_break_leash", {
                "kind": "knot", "x": base_x + 31, "y": base_y,
                "z": base_z, "attach": True, "interact": True,
                "llama_tick": True,
            }),
            ("frame_empty_north", {
                "kind": "frame", "x": base_x + 34, "y": base_y,
                "z": base_z, "facing": 2, "item": 0, "rotation": 0,
                "tick_counter": 0, "drop_chance": 1.0,
                "entity_seed48": 0x123456789ABC,
                "math_seed48": 0x123456789AB,
            }),
            ("frame_filled_south", {
                "kind": "frame", "x": base_x + 36, "y": base_y,
                "z": base_z, "facing": 3, "item": 276, "rotation": 7,
                "tick_counter": 42, "drop_chance": 1.0,
                "entity_seed48": 0x23456789ABCD,
                "math_seed48": 0x23456789ABC,
            }),
            ("frame_filled_west", {
                "kind": "frame", "x": base_x + 38, "y": base_y,
                "z": base_z, "facing": 4, "item": 276, "rotation": 3,
                "tick_counter": 1, "drop_chance": 1.0,
                "entity_seed48": 0x3456789ABCDE,
                "math_seed48": 0x3456789ABCD,
            }),
            ("frame_filled_east", {
                "kind": "frame", "x": base_x + 40, "y": base_y,
                "z": base_z, "facing": 5, "item": 276, "rotation": 4,
                "tick_counter": 100, "drop_chance": 1.0,
                "entity_seed48": 0x456789ABCDEF,
                "math_seed48": 0x456789ABCDE,
            }),
            ("frame_counter_99", {
                "kind": "frame", "x": base_x + 42, "y": base_y,
                "z": base_z, "facing": 2, "item": 276, "rotation": 5,
                "tick_counter": 99, "remove_support": True,
                "drop_chance": 1.0,
                "entity_seed48": 0x56789ABCDEF0,
                "math_seed48": 0x56789ABCDEF,
            }),
            ("frame_counter_100_break", {
                "kind": "frame", "x": base_x + 44, "y": base_y,
                "z": base_z, "facing": 2, "item": 276, "rotation": 5,
                "tick_counter": 100, "remove_support": True,
                "drop_chance": 1.0,
                "entity_seed48": 0x6789ABCDEF01,
                "math_seed48": 0x6789ABCDEF0,
            }),
            ("frame_filled_hit", {
                "kind": "frame", "x": base_x + 46, "y": base_y,
                "z": base_z, "facing": 2, "item": 276, "rotation": 6,
                "tick_counter": 73, "hits": 1, "drop_chance": 1.0,
                "entity_seed48": 0x789ABCDEF012,
                "math_seed48": 0x789ABCDEF01,
            }),
            ("frame_zero_chance_hit", {
                "kind": "frame", "x": base_x + 48, "y": base_y,
                "z": base_z, "facing": 2, "item": 276, "rotation": 2,
                "tick_counter": 12, "hits": 1, "drop_chance": 0.0,
                "entity_seed48": 0x89ABCDEF0123,
                "math_seed48": 0x89ABCDEF012,
            }),
            ("frame_two_hits", {
                "kind": "frame", "x": base_x + 50, "y": base_y,
                "z": base_z, "facing": 2, "item": 276, "rotation": 1,
                "tick_counter": 7, "hits": 2, "drop_chance": 1.0,
                "entity_seed48": 0x9ABCDEF01234,
                "math_seed48": 0x9ABCDEF0123,
            }),
            ("frame_explosion", {
                "kind": "frame", "x": base_x + 52, "y": base_y,
                "z": base_z, "facing": 2, "item": 276, "rotation": 3,
                "tick_counter": 8, "hits": 1, "explosion": True,
                "drop_chance": 1.0,
                "entity_seed48": 0xABCDEF012345,
                "math_seed48": 0xABCDEF01234,
            }),
            ("frame_creative_hit", {
                "kind": "frame", "x": base_x + 54, "y": base_y,
                "z": base_z, "facing": 2, "item": 276, "rotation": 4,
                "tick_counter": 9, "hits": 1, "creative": True,
                "drop_chance": 1.0,
                "entity_seed48": 0xBCDEF0123456,
                "math_seed48": 0xBCDEF012345,
            }),
            ("frame_insert", {
                "kind": "frame", "x": base_x + 56, "y": base_y,
                "z": base_z, "facing": 2, "item": 0, "rotation": 0,
                "tick_counter": 10, "interactions": 1,
                "drop_chance": 1.0,
                "entity_seed48": 0xCDEF01234567,
                "math_seed48": 0xCDEF0123456,
            }),
            ("frame_insert_rotate", {
                "kind": "frame", "x": base_x + 58, "y": base_y,
                "z": base_z, "facing": 2, "item": 0, "rotation": 0,
                "tick_counter": 11, "interactions": 2,
                "drop_chance": 1.0,
                "entity_seed48": 0xDEF012345678,
                "math_seed48": 0xDEF01234567,
            }),
        )
        constructor_cases = []
        support_sizes = (
            (16, 16), (32, 16), (16, 32),
            (32, 32), (64, 32), (64, 48),
        )
        for index, (width, height) in enumerate(support_sizes):
            constructor_cases.append((
                f"painting_constructor_{width}x{height}", {
                    "kind": "painting", "x": base_x + 70 + index * 8,
                    "y": base_y, "z": base_z, "facing": 2, "art": 0,
                    "constructor_art": True,
                    "support_width": width, "support_height": height,
                    "entity_seed_generator_seed48":
                        0x102030405060 + index * 0x10101,
                }))
        selected = {}
        generator_seed = 0
        while len(selected) < 26:
            art = painting_constructor_choice(generator_seed, 26)
            selected.setdefault(art, generator_seed)
            generator_seed += 1
        for art in range(26):
            constructor_cases.append((
                f"painting_constructor_all_art_{art}", {
                    "kind": "painting", "x": base_x + 120 + art * 8,
                    "y": base_y, "z": base_z, "facing": 2, "art": 0,
                    "constructor_art": True,
                    "support_width": 64, "support_height": 64,
                    "entity_seed_generator_seed48": selected[art],
                }))
        mixed_cases = [
            (f"mixed_hanging_order_{''.join(map(str, order))}", {
                "kind": "mixed_order", "x": base_x + 340,
                "y": base_y, "z": base_z, "order": list(order),
            })
            for order in (
                (0, 1, 2), (0, 2, 1), (1, 0, 2),
                (1, 2, 0), (2, 0, 1), (2, 1, 0),
            )
        ]
        damage_cases = [
            (f"damage_entity_{entity_type}_source_{source}_creative_{creative}", {
                "kind": "damage", "x": base_x + 350,
                "y": base_y, "z": base_z,
                "entity_type": entity_type, "source": source,
                "creative": bool(creative),
            })
            for entity_type in range(4)
            for source in range(8)
            for creative in range(2)
        ]
        map_x = base_x + 350
        map_cases = [
            (f"map_facing_{facing}_scale_{scale}", {
                "kind": "map", "x": map_x, "y": base_y, "z": base_z,
                "facing": facing, "scale": scale,
                "center_x": map_x, "center_z": base_z,
                "tracking": True, "operation": 0, "drop_chance": 1.0,
            })
            for facing in range(2, 6)
            for scale in range(5)
        ]
        for offset in (-64, -63, -1, 0, 1, 63, 64):
            map_cases.append((f"map_boundary_{offset}", {
                "kind": "map", "x": map_x, "y": base_y, "z": base_z,
                "facing": 2, "scale": 0,
                "center_x": map_x - offset, "center_z": base_z,
                "tracking": True, "operation": 0, "drop_chance": 1.0,
            }))
        for name, tracking, operation, drop_chance in (
                ("tracking_off", False, 0, 1.0),
                ("rotate", True, 3, 1.0),
                ("remove_drop", True, 1, 1.0),
                ("remove_no_drop", True, 1, 0.0),
                ("remove_creative", True, 2, 1.0)):
            map_cases.append((f"map_{name}", {
                "kind": "map", "x": map_x, "y": base_y, "z": base_z,
                "facing": 2, "scale": 2,
                "center_x": map_x - 31, "center_z": base_z + 17,
                "tracking": tracking, "operation": operation,
                "drop_chance": drop_chance,
            }))
        leash_cases = [
            (f"living_leash_class_{class_index}_op_{operation}", {
                "kind": "living_leash", "x": base_x + 360,
                "y": base_y, "z": base_z,
                "class_index": class_index, "operation": operation,
            })
            for class_index in range(16)
            for operation in range(4)
        ]
        leash_cases.extend((
            (f"living_leash_sitting_{class_index}", {
                "kind": "living_leash", "x": base_x + 360,
                "y": base_y, "z": base_z,
                "class_index": class_index, "operation": 4,
            })
            for class_index in (5, 8)
        ))
        leash_cases.extend((
            (f"living_leash_eating_{class_index}", {
                "kind": "living_leash", "x": base_x + 360,
                "y": base_y, "z": base_z,
                "class_index": class_index, "operation": 5,
            })
            for class_index in (10, 11, 12, 15)
        ))
        leash_cases.append(("living_leash_angry_wolf", {
            "kind": "living_leash", "x": base_x + 360,
            "y": base_y, "z": base_z,
            "class_index": 5, "operation": 6,
        }))
        for case_id, action in (
                *cases, *constructor_cases, *mixed_cases,
                *damage_cases, *map_cases, *leash_cases):
            request(port, "clear_entities_locked")
            java = request(port, "hanging_entity_locked", action)
            java.pop("ok", None)
            java.pop("eid", None)
            java.pop("title", None)
            java.pop("authoritative", None)
            compare(case_id, action, java, native(action))
    finally:
        request(port, "server_step_unlock")
    print(
        "PASS hanging entities: 227 real-Java/native rows, bit-exact pose/AABB, "
        "post-increment support clock, exact painting drop callback, and "
        "exact constructor RNG, mixed order, exhaustive damage equivalence, "
        "map decoration lifecycle, all 16 vanilla leashable living classes, "
        "knot, leash, and frame state")


def oracle(action: str, instance: int, seed: int,
           environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    subprocess.run(command, cwd=ROOT, env=environment, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int)
    parser.add_argument("--start-oracle", action="store_true")
    parser.add_argument("--instance", type=int, default=99)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    if args.port is None and not args.start_oracle:
        parser.error("pass --port or --start-oracle")
    environment = dict(os.environ)
    started = False
    if args.start_oracle:
        run_root = ROOT / ".tmp" / f"hanging-oracle-{os.getpid()}"
        environment.update({
            "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
            "ORACLE_POOL_OUT_ROOT": str(run_root),
            "ORACLE_POOL_WORLD_TYPE": "flat",
            "ORACLE_POOL_USERNAME": "HangingProbe99",
            "ORACLE_POOL_WAIT": "1",
            "TMPDIR": str(ROOT / ".tmp"),
            "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
        })
        oracle("start", args.instance, args.seed, environment)
        started = True
        port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) \
            + args.instance
    else:
        port = args.port
    try:
        run(port)
    finally:
        if started:
            oracle("stop", args.instance, args.seed, environment)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, json.JSONDecodeError, OSError,
            subprocess.CalledProcessError, RuntimeError, ValueError) as error:
        print(f"FAIL hanging entities: {error}", file=sys.stderr)
        raise SystemExit(1)
