#!/usr/bin/env python3
"""One-tick real-Java/native continuation from plain-item capsules."""

import argparse
import json
import math
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
sys.path.insert(0, str(HERE))

from state_capsule import create_capsule, emit_magma
from test_dragon_crystal_notification import request
from trace_java import canonicalize


def dbits(value):
    return struct.pack(">d", float(value))


def fbits(value):
    return struct.pack(">f", float(value))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game"], check=True,
        stdout=subprocess.DEVNULL,
    )
    temp_root = pathlib.Path(os.environ.get(
        "TMPDIR", str(MAGMA.parent / ".tmp")))
    temp_root.mkdir(parents=True, exist_ok=True)
    locked = False
    with tempfile.TemporaryDirectory(
            prefix="item_capsule_", dir=temp_root) as raw_temp:
        temp = pathlib.Path(raw_temp)
        obs = request(args.port, "obs")
        request(args.port, "killentities")
        platform_x = math.floor(float(obs["x"])) + 12
        platform_y = 159
        platform_z = math.floor(float(obs["z"])) + 8
        platform = [
            [x, platform_y, z, 1, 0]
            for z in range(platform_z - 2, platform_z + 3)
            for x in range(platform_x - 2, platform_x + 3)
        ]
        clear = [
            [x, y, z, 0, 0]
            for y in range(platform_y + 1, platform_y + 4)
            for z in range(platform_z - 2, platform_z + 3)
            for x in range(platform_x - 2, platform_x + 3)
        ]
        request(args.port, "setblocks", {"blocks": platform + clear})
        airborne = request(args.port, "summon", {
            "type": "item", "item": "minecraft:diamond",
            "count": 3, "meta": 0, "pickupdelay": 9,
            "x": platform_x - 4.5,
            "y": platform_y + 21.0,
            "z": platform_z + 0.5,
            "mx": 0.125, "my": 0.25, "mz": -0.125,
        })
        grounded = request(args.port, "summon", {
            "type": "item", "item": "minecraft:iron_ingot",
            "count": 5, "meta": 0, "pickupdelay": 13,
            "x": platform_x + 0.5,
            "y": platform_y + 1.0,
            "z": platform_z + 0.5,
            "mx": 0.125, "my": 0.0, "mz": -0.125,
        })
        landing = request(args.port, "summon", {
            "type": "item", "item": "minecraft:gold_ingot",
            "count": 2, "meta": 0, "pickupdelay": 17,
            "x": platform_x - 1.5,
            "y": platform_y + 1.22,
            "z": platform_z - 1.5,
            "mx": 0.05, "my": -0.1, "mz": 0.025,
        })
        try:
            before_lock = request(args.port, "server_step_lock")
            locked = True
            request(args.port, "setblocks_locked", {"blocks": [
                [platform_x + 2, platform_y + 10, platform_z - 1, 8, 1],
                [platform_x + 1, platform_y + 10, platform_z - 1, 9, 0],
                [platform_x + 4, platform_y + 9, platform_z - 1, 12, 0],
                [platform_x + 4, platform_y + 10, platform_z - 1, 81, 0],
                [platform_x + 7, platform_y + 16, platform_z, 1, 0],
            ]})
            water = request(args.port, "summon_locked", {
                "type": "item", "item": 4,
                "count": 1, "meta": 0, "pickup_delay": 19,
                "x": platform_x + 2.5,
                "y": platform_y + 10.0,
                "z": platform_z - 0.5,
                "mx": 0.0, "my": 0.0, "mz": 0.0,
            })
            burning = request(args.port, "summon_locked", {
                "type": "item", "item": 260,
                "count": 1, "meta": 0, "pickup_delay": 23,
                "fire_seconds": 1,
                "x": platform_x + 2.5,
                "y": platform_y + 14.0,
                "z": platform_z + 0.5,
                "mx": 0.0, "my": 0.0, "mz": 0.0,
            })
            cactus = request(args.port, "summon_locked", {
                "type": "item", "item": 265,
                "count": 1, "meta": 0, "pickup_delay": 29,
                "x": platform_x + 5.2,
                "y": platform_y + 10.0,
                "z": platform_z - 0.5,
                "mx": -0.15, "my": 0.0, "mz": 0.0,
            })
            pushout = request(args.port, "summon_locked", {
                "type": "item", "item": 266,
                "count": 1, "meta": 0, "pickup_delay": 31,
                "x": platform_x + 7.2,
                "y": platform_y + 16.0,
                "z": platform_z + 0.5,
                "mx": 0.02, "my": 0.03, "mz": 0.04,
            })
            merge_first = request(args.port, "summon_locked", {
                "type": "item", "item": 264,
                "count": 3, "meta": 0, "pickup_delay": 9,
                "x": platform_x + 3.95,
                "y": platform_y + 21.0,
                "z": platform_z + 2.0,
                "mx": 0.10, "my": 0.0, "mz": 0.0,
            })
            merge_second = request(args.port, "summon_locked", {
                "type": "item", "item": 264,
                "count": 5, "meta": 0, "pickup_delay": 13,
                "x": platform_x + 4.25,
                "y": platform_y + 21.0,
                "z": platform_z + 2.0,
                "mx": 0.0, "my": 0.0, "mz": 0.0,
            })
            expected_before = (
                airborne["eid"], grounded["eid"], landing["eid"],
                water["eid"], burning["eid"], cactus["eid"],
                pushout["eid"], merge_first["eid"], merge_second["eid"],
            )
            expected_after = (
                airborne["eid"], grounded["eid"], landing["eid"],
                water["eid"], burning["eid"], cactus["eid"],
                pushout["eid"], merge_second["eid"],
            )
            authoritative = merge_second["authoritative"]
            items_before = {
                entity["eid"]: entity
                for entity in authoritative["entities"]
                if entity.get("eid") in expected_before
            }
            if set(items_before) != set(expected_before) or any(
                    item.get("item_exact") is not True
                    for item in items_before.values()):
                raise AssertionError(
                    f"Java items were not capsule-exact: {items_before!r}")
            if items_before[grounded["eid"]]["on_ground"] is not True:
                raise AssertionError(
                    f"Java ground fixture did not settle: {items_before!r}")
            if items_before[landing["eid"]]["on_ground"] is not False:
                raise AssertionError(
                    f"Java landing fixture began grounded: {items_before!r}")
            if (items_before[water["eid"]]["first_update"] is not True
                    or items_before[water["eid"]]["in_water"] is not False):
                raise AssertionError(
                    f"Java water fixture did not begin at entry: "
                    f"{items_before!r}")
            if items_before[burning["eid"]]["fire"] != 20:
                raise AssertionError(
                    f"Java fire fixture did not begin at fire=20: "
                    f"{items_before!r}")
            center_x = min(int(float(item["x"]) // 1)
                           for item in items_before.values())
            far_x = max(int(float(item["x"]) // 1)
                        for item in items_before.values())
            center_y = min(int(float(item["y"]) // 1)
                           for item in items_before.values())
            far_y = max(int(float(item["y"]) // 1)
                        for item in items_before.values())
            center_z = min(int(float(item["z"]) // 1)
                           for item in items_before.values())
            far_z = max(int(float(item["z"]) // 1)
                        for item in items_before.values())
            box = [
                center_x - 2, center_y - 2, center_z - 2,
                far_x + 2, far_y + 2, far_z + 2,
            ]
            blocks = temp / "blocks.bin"
            request(args.port, "getblocks_locked", {
                "x0": box[0], "y0": box[1], "z0": box[2],
                "x1": box[3], "y1": box[4], "z1": box[5],
                "file": str(blocks),
            })
            observation = dict(obs)
            observation["authoritative"] = authoritative
            state = canonicalize(-1, observation, box)
            state_file = temp / "state.json"
            state_file.write_text(json.dumps(state), encoding="utf-8")
            capsule = temp / "capsule"
            create_capsule(
                state_file, blocks, box, capsule,
                seed=42, source_engine="minecraft-java",
                source_version="1.11.2",
            )
            script = temp / "load.jsonl"
            emit_magma(capsule, script)
            after_step = request(args.port, "step")
            items_after = {
                entity["eid"]: entity
                for entity in after_step["authoritative"]["entities"]
                if entity.get("eid") in expected_before
            }
            if set(items_after) != set(expected_after):
                raise AssertionError(
                    f"Java merge survivor set is wrong: {items_after!r}")
            if items_after[merge_second["eid"]]["count"] != 8:
                raise AssertionError(
                    f"Java merge count is wrong: {items_after!r}")
            if items_after[landing["eid"]]["on_ground"] is not True:
                raise AssertionError(
                    f"Java landing fixture did not collide: {items_after!r}")
            if (items_after[water["eid"]]["first_update"] is not False
                    or items_after[water["eid"]]["in_water"] is not True):
                raise AssertionError(
                    f"Java water fixture did not enter water: {items_after!r}")
            if (items_after[burning["eid"]]["health"] != 4
                    or items_after[burning["eid"]]["fire"] != 19):
                raise AssertionError(
                    f"Java fire fixture did not take periodic damage: "
                    f"{items_after!r}")
            if items_after[cactus["eid"]]["health"] != 4:
                raise AssertionError(
                    f"Java cactus fixture did not take contact damage: "
                    f"{items_after!r}")
            if items_after[pushout["eid"]]["no_clip"] is not True:
                raise AssertionError(
                    f"Java full-cube fixture did not push out: "
                    f"{items_after!r}")
        finally:
            if locked:
                request(args.port, "server_step_unlock")

        native_state = temp / "native.jsonl"
        subprocess.run([
            str(MAGMA / "magma_game"),
            "--world", "superflat", "--headless", "--ticks", "1",
            "--mobs", "off", "--script", str(script),
            "--state-out", str(native_state),
            "--render", "off", "--pace", "unlimited",
        ], check=True, stdout=subprocess.DEVNULL)
        native = json.loads(native_state.read_text(encoding="utf-8"))
        mismatches = []
        native_items = {
            entity["eid"]: entity for entity in native["entities"]
            if entity.get("kind") == "item"
            and entity.get("eid") in expected_before
        }
        if set(native_items) != set(expected_after):
            raise AssertionError(
                f"native merge survivor set is wrong: {native_items!r}")
        for eid in expected_after:
            item_after = items_after[eid]
            item_native = native_items[eid]
            for field in ("x", "y", "z", "vx", "vy", "vz"):
                if dbits(item_after[field]) != dbits(item_native[field]):
                    mismatches.append((eid, field))
            for field in ("yaw", "hover_start"):
                if fbits(item_after[field]) != fbits(item_native[field]):
                    mismatches.append((eid, field))
            for field in (
                    "item", "count", "meta", "age", "ticks_existed",
                    "pickup_delay",
                    "health", "lifespan", "on_ground", "no_gravity",
                    "no_clip", "fire", "in_water", "first_update",
                    "entity_seed48"):
                if item_after[field] != item_native[field]:
                    mismatches.append((eid, field))
        if mismatches:
            raise AssertionError(
                f"item continuation mismatch {mismatches}: "
                f"Java={items_after!r} native={native_items!r}")
    print("PASS real Java/native: exact air/ground/landing/water/fire/cactus/push-out/merge item capsule continuation")


if __name__ == "__main__":
    main()
