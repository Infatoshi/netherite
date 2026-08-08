#!/usr/bin/env python3
"""Compare exact 1.11.2 Java and shared CPU igloo placement."""
import argparse
import json
import struct
import subprocess
import time
from pathlib import Path

from test_dragon_crystal_notification import request


MAGMA = Path(__file__).resolve().parents[1]
CASES = (
    (0, 0), (1, 1), (2, 2), (19, 19), (256, 256),
    (257, 257), (4096, 4096), (4097, 4097), (6144, 6144), (6145, 6145),
)
LOOT_SEEDS = (0, 42, 7230402065820649518, -7074434463822813898)


def java_result(port, component_seed, population_seed):
    return request(port, "igloo_locked", {
        "component_seed": component_seed,
        "population_seed": population_seed,
    })


def native(component_seed, population_seed):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_igloo_oracle"),
         str(component_seed), str(population_seed)],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def native_loot(seed):
    proc = subprocess.run(
        [str(MAGMA / "game" / "test_igloo_oracle"), "--loot", str(seed)],
        check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)["values"]


def chest_from_java(row):
    chests = [tile for tile in row["tiles"] if tile["type"] == "chest"]
    if not chests:
        return None
    if len(chests) != 1:
        raise AssertionError("Java igloo has multiple chests")
    chest = dict(chests[0])
    del chest["type"]
    block_index = ((chest["y"] + 54) * 25 + (chest["z"] + 12)) * 25 \
        + (chest["x"] + 12)
    chest["facing"] = row["blocks"][block_index] & 15
    return chest


def close(a, b):
    return abs(float(a) - float(b)) <= 1.0e-7


def f32(value):
    return struct.pack("=f", float(value))


def compare(java, cpu, label):
    for field in ("component_facing", "template_rotation", "has_basement",
                  "middle_count", "base_y", "population_seed48"):
        if java[field] != cpu[field]:
            raise AssertionError(f"{label}: {field}: {java[field]} != {cpu[field]}")
    if java["blocks"] != cpu["blocks"]:
        for index, (actual, expected) in enumerate(zip(java["blocks"], cpu["blocks"])):
            if actual != expected:
                dy, rem = divmod(index, 25 * 25)
                dz, dx = divmod(rem, 25)
                raise AssertionError(
                    f"{label}: first block mismatch at "
                    f"({dx - 12},{dy - 54},{dz - 12}): "
                    f"Java={actual:#x} CPU={expected:#x}")
        raise AssertionError(f"{label}: block volume length differs")
    if chest_from_java(java) != cpu["chest"]:
        raise AssertionError(
            f"{label}: chest {chest_from_java(java)} != {cpu['chest']}")
    expected_types = {"furnace"} if not java["has_basement"] else {
        "furnace", "sign", "chest", "brewing_stand", "flower_pot"}
    if {tile["type"] for tile in java["tiles"]} != expected_types:
        raise AssertionError(f"{label}: Java tile family differs: {java['tiles']}")
    jentities = sorted(java["entities"], key=lambda row: row["type"])
    centries = sorted(cpu["entities"], key=lambda row: row["type"])
    if len(jentities) != len(centries):
        raise AssertionError(f"{label}: entity count differs")
    for actual, expected in zip(jentities, centries):
        numeric = ("x", "y", "z", "vx", "vy", "vz")
        float32 = ("health", "yaw", "pitch")
        exact = ("conversion_time", "fire", "air", "profession",
                 "persistence", "on_ground")
        if (actual["type"] != expected["type"]
                or any(not close(actual[field], expected[field])
                       for field in numeric)
                or any(f32(actual[field]) != f32(expected[field])
                       for field in float32)
                or any(actual[field] != expected[field] for field in exact)):
            raise AssertionError(f"{label}: entity {actual} != {expected}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_igloo_oracle"], check=True)
    covered, depths, locked = set(), set(), False
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
        for component_seed, population_seed in CASES:
            java = java_result(args.port, component_seed, population_seed)
            cpu = native(component_seed, population_seed)
            compare(java, cpu, f"seed {component_seed}/{population_seed}")
            covered.add((java["template_rotation"], java["has_basement"]))
            if java["has_basement"]:
                depths.add(java["middle_count"])
        for seed in LOOT_SEEDS:
            java = request(args.port, "igloo_loot_locked", {"seed": seed})
            expected = native_loot(seed)
            if java["values"] != expected:
                bad = [index for index, pair in enumerate(
                    zip(java["values"], expected)) if pair[0] != pair[1]]
                raise AssertionError(
                    f"igloo loot mismatch seed {seed}, fields={bad[:16]}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
    if covered != {(rotation, basement) for rotation in range(4)
                    for basement in (False, True)} or not {4, 11} <= depths:
        raise AssertionError(f"incomplete igloo branch coverage: {covered} {depths}")
    print("PASS real Java/shared CPU: 10 igloos, four rotations, "
          "surface/basement, depth endpoints, blocks/tiles/loot/entity NBT/RNG, "
          "four deferred-loot seeds")


if __name__ == "__main__":
    main()
