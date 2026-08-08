#!/usr/bin/env python3
"""Compare real-1.11.2 and native llama caravan task boundaries."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import struct
import subprocess
import sys


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
sys.path.insert(0, str(HERE))

from test_dragon_crystal_notification import request


CASES = (
    ("inside", 5.0, True, 3.0239998626708982, 7),
    ("far_accel", 27.0, True, 2.519999885559082, 5),
    ("far_grace", 27.0, True, 3.0239998626708982, 1),
    ("far_expired", 27.0, True, 3.0239998626708982, 0),
    ("reset_preserve", 5.0, False, 3.0239998626708982, 7),
)
TERRAIN_TICKS = 20
DYNAMIC_TICKS = 8
FOLLOW_PARENT_TICKS = 16
PANIC_TICKS = 12
MATE_TICKS = 16
RANGED_TICKS = 41


def double_hex(value: float) -> str:
    return f"{struct.unpack('>Q', struct.pack('>d', value))[0]:016x}"


def native_rows() -> dict[str, str]:
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_llama_runtime"],
        check=True, stdout=subprocess.DEVNULL,
    )
    result = subprocess.run(
        [str(MAGMA / "game/test_llama_runtime"), "--caravan-oracle"],
        check=True, capture_output=True, text=True,
    )
    rows = {}
    for line in result.stdout.splitlines():
        name = line.split(" ", 1)[0]
        if name in rows:
            raise AssertionError(f"duplicate native caravan row {name}")
        rows[name] = line
    if set(rows) != {case[0] for case in CASES}:
        raise AssertionError(f"native caravan rows incomplete: {rows!r}")
    return rows


def native_terrain_rows(
        player_x: float, player_y: float, player_z: float,
        ticks: int, dynamic_obstacles: bool = False,
        follow_parent: bool = False) -> list[dict]:
    if follow_parent:
        mode = "--follow-parent-terrain-oracle"
    else:
        mode = ("--caravan-dynamic-oracle" if dynamic_obstacles
                else "--caravan-terrain-oracle")
    result = subprocess.run(
        [str(MAGMA / "game/test_llama_runtime"),
         mode, repr(player_x), repr(player_y),
         repr(player_z), str(ticks)],
        check=True, capture_output=True, text=True,
    )
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    if [row.get("tick") for row in rows] != list(range(ticks)):
        raise AssertionError(f"native terrain rows incomplete: {rows!r}")
    return rows


def native_task_conflict_rows() -> dict[str, int]:
    result = subprocess.run(
        [str(MAGMA / "game/test_llama_runtime"),
         "--caravan-task-conflicts"],
        check=True, capture_output=True, text=True,
    )
    rows = {}
    for line in result.stdout.splitlines():
        name, mask = line.split()
        if name in rows:
            raise AssertionError(f"duplicate native task-conflict row {name}")
        rows[name] = int(mask)
    expected = {
        "caravan_ranged", "caravan_mate", "caravan_swim",
        "caravan_follow_parent",
    }
    if set(rows) != expected:
        raise AssertionError(f"native task-conflict rows incomplete: {rows!r}")
    return rows


def native_lower_task_rows(
        player_x: float, player_y: float, player_z: float) -> dict[str, dict]:
    result = subprocess.run(
        [str(MAGMA / "game/test_llama_runtime"),
         "--llama-lower-tasks", repr(player_x), repr(player_y),
         repr(player_z)],
        check=True, capture_output=True, text=True,
    )
    rows = {}
    fields = ("x", "y", "z", "vx", "vy", "vz")
    for line in result.stdout.splitlines():
        values = line.split()
        if len(values) != 9:
            raise AssertionError(f"malformed native lower-task row: {line!r}")
        name, mask, seed48, *motion = values
        if name in rows:
            raise AssertionError(f"duplicate native lower-task row {name}")
        rows[name] = {
            "task_mask": int(mask),
            "entity_seed48": int(seed48),
            **dict(zip(fields, motion)),
        }
    if set(rows) != {"wander", "watch", "idle"}:
        raise AssertionError(f"native lower-task rows incomplete: {rows!r}")
    return rows


def native_panic_rows(
        player_x: float, player_y: float, player_z: float,
        water_target: bool) -> list[dict]:
    result = subprocess.run(
        [str(MAGMA / "game/test_llama_runtime"),
         "--llama-panic-terrain", repr(player_x), repr(player_y),
         repr(player_z), str(PANIC_TICKS), "1" if water_target else "0"],
        check=True, capture_output=True, text=True,
    )
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    if [row.get("tick") for row in rows] != list(range(PANIC_TICKS)):
        raise AssertionError(f"native panic rows incomplete: {rows!r}")
    return rows


def native_mate_rows(
        player_x: float, player_y: float, player_z: float) -> list[dict]:
    result = subprocess.run(
        [str(MAGMA / "game/test_llama_runtime"),
         "--llama-mate-terrain", repr(player_x), repr(player_y),
         repr(player_z), str(MATE_TICKS)],
        check=True, capture_output=True, text=True,
    )
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    if [row.get("tick") for row in rows] != list(range(MATE_TICKS)):
        raise AssertionError(f"native mating rows incomplete: {rows!r}")
    return rows


def native_ranged_rows(
        player_x: float, player_y: float, player_z: float) -> list[dict]:
    result = subprocess.run(
        [str(MAGMA / "game/test_llama_runtime"),
         "--llama-ranged-terrain", repr(player_x), repr(player_y),
         repr(player_z), str(RANGED_TICKS)],
        check=True, capture_output=True, text=True,
    )
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    if [row.get("tick") for row in rows] != list(range(RANGED_TICKS)):
        raise AssertionError(f"native ranged rows incomplete: {rows!r}")
    return rows


def by_eid(state: dict, eid: int) -> dict:
    found = [entity for entity in state["entities"]
             if entity.get("eid") == eid]
    if len(found) != 1:
        raise AssertionError(f"expected eid {eid}, got {found!r}")
    return found[0]


def navigation_row(entity: dict) -> dict:
    navigation = entity.get("llama_navigation")
    if not isinstance(navigation, dict):
        raise AssertionError(f"missing Java llama navigation: {entity!r}")
    target = navigation.get("target")
    path = navigation.get("path")
    if not isinstance(target, dict):
        raise AssertionError(
            f"incomplete Java llama navigation: {navigation!r}\n"
            f"Entity: {entity!r}")
    if path is None:
        return {
            "target": [target[axis] for axis in ("x", "y", "z")],
            "index": None, "points": None,
        }
    if not isinstance(path, dict):
        raise AssertionError(
            f"invalid Java llama path: {navigation!r}\nEntity: {entity!r}")
    points = path.get("points")
    if not isinstance(points, list):
        raise AssertionError(f"missing Java llama path points: {navigation!r}")
    return {
        "target": [target[axis] for axis in ("x", "y", "z")],
        "index": path["index"],
        "points": [[point[axis] for axis in ("x", "y", "z")]
                   for point in points],
    }


def terrain_blocks(
        player_x: float, player_y: float, player_z: float) -> list[list[int]]:
    center_x = math.floor(player_x)
    center_z = math.floor(player_z)
    ground_y = math.floor(player_y) - 1
    blocks = []
    for x in range(center_x + 2, center_x + 21):
        for z in range(center_z - 8, center_z + 9):
            blocks.append([x, ground_y, z, 1, 0])
            for y in range(ground_y + 1, ground_y + 5):
                blocks.append([x, y, z, 0, 0])
    for z in range(center_z - 1, center_z + 2):
        for y in range(ground_y + 1, ground_y + 3):
            blocks.append([center_x + 12, y, z, 1, 0])
    return blocks


def lower_task_blocks(
        player_x: float, player_y: float, player_z: float) -> list[list[int]]:
    center_x = math.floor(player_x)
    center_z = math.floor(player_z)
    ground_y = math.floor(player_y) - 1
    blocks = []
    for x in range(center_x - 16, center_x + 29):
        for z in range(center_z - 20, center_z + 21):
            blocks.append([x, ground_y, z, 1, 0])
            for y in range(ground_y + 1, ground_y + 6):
                blocks.append([x, y, z, 0, 0])
    return blocks


def panic_task_blocks(
        player_x: float, player_y: float, player_z: float) -> list[list[int]]:
    blocks = lower_task_blocks(player_x, player_y, player_z)
    center_x = math.floor(player_x)
    center_z = math.floor(player_z)
    ground_y = math.floor(player_y) - 1
    for x in range(center_x - 16, center_x + 29):
        for z in range(center_z - 20, center_z + 21):
            for y in range(max(1, ground_y - 4), ground_y):
                blocks.append([x, y, z, 0, 0])
    return blocks


def ranged_task_blocks(
        player_x: float, player_y: float, player_z: float) -> list[list[int]]:
    center_x = math.floor(player_x)
    center_z = math.floor(player_z)
    ground_y = math.floor(player_y) - 1
    blocks = []
    for x in range(center_x - 4, center_x + 29):
        for z in range(center_z - 10, center_z + 11):
            for y in range(max(1, ground_y - 4), ground_y + 6):
                blocks.append([x, y, z, 0, 0])
            if ((x != center_x + 5
                    or not center_z - 1 <= z <= center_z + 1)
                    and (x != center_x + 1 or z != center_z + 1)):
                blocks.append([x, ground_y, z, 1, 0])
    return blocks


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    expected = native_rows()
    rule = request(args.port, "runcmds", {
        "cmds": ["gamerule doMobSpawning false"],
    })
    if not rule.get("ok") or rule.get("failed"):
        raise AssertionError(f"could not disable natural spawning: {rule}")
    request(args.port, "killentities")
    locked = False
    try:
        parked = request(args.port, "server_step_lock")
        locked = True
        player = parked["authoritative"]
        x = float(player["x"])
        y = float(player["y"])
        z = float(player["z"])
        for name, distance, leashed, speed, counter in CASES:
            request(args.port, "clear_entities_locked")
            leader = request(args.port, "summon_locked", {
                "type": "horse", "horse_kind": "llama",
                "x": x + 8.0, "y": y, "z": z,
                "no_ai": True,
                "llama_leash_player": leashed,
                "entity_seed48": 0x123456789ABC,
            })
            follower = request(args.port, "summon_locked", {
                "type": "horse", "horse_kind": "llama",
                "x": x + 8.0 + distance, "y": y, "z": z,
                "no_ai": False,
                "llama_caravan_head_eid": leader["eid"],
                "llama_caravan_speed": speed,
                "llama_caravan_dist_counter": counter,
                "llama_caravan_running": True,
                "entity_seed48": 0x123456789ABD,
            })
            state = request(args.port, "step")["authoritative"]
            value = by_eid(state, follower["eid"])
            head = value["llama_caravan_head_eid"]
            active = 1 if head >= 0 else 0
            normalized_head = 940 if active else -1
            row = (f"{name} {active} {normalized_head} "
                   f"{double_hex(value['llama_caravan_speed'])} "
                   f"{value['llama_caravan_dist_counter']}")
            if row != expected[name]:
                raise AssertionError(
                    f"{name}: Java/native mismatch\n"
                    f"Java:  {row}\nNative: {expected[name]}\n"
                    f"State: {value!r}")
        request(args.port, "clear_entities_locked")
        staged = request(args.port, "setblocks_locked", {
            "blocks": terrain_blocks(x, y, z),
        })
        if not staged.get("ok"):
            raise AssertionError(f"could not stage caravan terrain: {staged}")
        center_x = math.floor(x)
        center_z = math.floor(z)
        ground_y = math.floor(y) - 1
        leader = request(args.port, "summon_locked", {
            "type": "horse", "horse_kind": "llama",
            "x": center_x + 8.5, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "no_ai": True, "on_ground": True,
            "max_health": 20.0, "health": 20.0,
            "movement_speed": 0.175, "jump_strength": 0.5,
            "variant": 0, "llama_strength": 1,
            "llama_leash_player": True,
            "entity_seed48": 0x123456789ABC,
        })
        follower = request(args.port, "summon_locked", {
            "type": "horse", "horse_kind": "llama",
            "x": center_x + 14.5, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "no_ai": False, "on_ground": True,
            "max_health": 20.0, "health": 20.0,
            "movement_speed": 0.175, "jump_strength": 0.5,
            "variant": 0, "llama_strength": 1,
            "llama_caravan_head_eid": leader["eid"],
            "llama_caravan_speed": 2.0999999046325684,
            "llama_caravan_dist_counter": 0,
            "llama_caravan_running": True,
            "entity_seed48": 0x123456789ABD,
        })
        staged_follower = by_eid(
            follower["authoritative"], follower["eid"])
        staged_motion = {
            field: double_hex(float(staged_follower[field]))
            for field in ("x", "y", "z", "vx", "vy", "vz")
        }
        expected_staged_motion = {
            "x": double_hex(center_x + 14.5),
            "y": double_hex(ground_y + 1.0),
            "z": double_hex(center_z + 0.5),
            "vx": double_hex(0.0), "vy": double_hex(0.0),
            "vz": double_hex(0.0),
        }
        if staged_motion != expected_staged_motion:
            raise AssertionError(
                "locked Java terrain fixture ticked during staging\n"
                f"Java:    {staged_motion!r}\n"
                f"Expected:{expected_staged_motion!r}\n"
                f"State: {staged_follower!r}")
        terrain_expected = native_terrain_rows(x, y, z, TERRAIN_TICKS)
        null_path_ticks = []
        for tick, native in enumerate(terrain_expected):
            state = request(args.port, "step")["authoritative"]
            value = by_eid(state, follower["eid"])
            java_navigation = navigation_row(value)
            native_navigation = {
                key: native[key] for key in ("target", "index", "points")
            }
            if java_navigation["points"] is None:
                null_path_ticks.append(tick)
            elif java_navigation != native_navigation:
                raise AssertionError(
                    f"terrain tick {tick}: Java/native navigation mismatch\n"
                    f"Java:  {java_navigation!r}\n"
                    f"Native: {native_navigation!r}\n"
                    f"State: {value!r}")
            java_motion = {
                field: double_hex(float(value[field]))
                for field in ("x", "y", "z", "vx", "vy", "vz")
            }
            native_motion = {
                field: native[field]
                for field in ("x", "y", "z", "vx", "vy", "vz")
            }
            if java_motion != native_motion:
                raise AssertionError(
                    f"terrain tick {tick}: Java/native motion mismatch\n"
                    f"Java:  {java_motion!r}\n"
                    f"Native: {native_motion!r}\n"
                    f"State: {value!r}")
            if bool(value["llama_on_ground"]) != bool(native["on_ground"]):
                raise AssertionError(
                    f"terrain tick {tick}: Java/native onGround mismatch\n"
                    f"Java: {value['llama_on_ground']!r}\n"
                    f"Native: {native['on_ground']!r}\nState: {value!r}")
        if null_path_ticks != [1]:
            raise AssertionError(
                "unexpected Java currentPath lifecycle: "
                f"null after ticks {null_path_ticks!r}, expected [1]")

        # EntityAIFollowParent has no mutex bits. Its child-sized navigator
        # survives the first airborne boundary and receives a fresh parent
        # request every ten goal ticks.
        request(args.port, "clear_entities_locked")
        staged = request(args.port, "setblocks_locked", {
            "blocks": terrain_blocks(x, y, z),
        })
        if not staged.get("ok"):
            raise AssertionError(
                f"could not stage follow-parent terrain: {staged}")
        request(args.port, "summon_locked", {
            "type": "horse", "horse_kind": "llama",
            "x": center_x + 8.5, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "no_ai": True, "on_ground": True,
            "max_health": 20.0, "health": 20.0,
            "movement_speed": 0.175, "jump_strength": 0.5,
            "variant": 0, "llama_strength": 1,
            "entity_seed48": 0x123456789ABC,
        })
        child = request(args.port, "summon_locked", {
            "type": "horse", "horse_kind": "llama",
            "x": center_x + 17.0, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "no_ai": False, "on_ground": True,
            "max_health": 20.0, "health": 20.0,
            "movement_speed": 0.175, "jump_strength": 0.5,
            "growing_age": -100,
            "variant": 0, "llama_strength": 1,
            "entity_seed48": 0x123456789ABD,
        })
        follow_expected = native_terrain_rows(
            x, y, z, FOLLOW_PARENT_TICKS, follow_parent=True)
        for tick, native in enumerate(follow_expected):
            state = request(args.port, "step")["authoritative"]
            value = by_eid(state, child["eid"])
            java_navigation = navigation_row(value)
            native_navigation = {
                key: native[key] for key in ("target", "index", "points")
            }
            if java_navigation != native_navigation:
                raise AssertionError(
                    f"follow-parent tick {tick}: navigation mismatch\n"
                    f"Java:  {java_navigation!r}\n"
                    f"Native: {native_navigation!r}\nState: {value!r}")
            java_motion = {
                field: double_hex(float(value[field]))
                for field in ("x", "y", "z", "vx", "vy", "vz")
            }
            native_motion = {
                field: native[field]
                for field in ("x", "y", "z", "vx", "vy", "vz")
            }
            if java_motion != native_motion:
                raise AssertionError(
                    f"follow-parent tick {tick}: motion mismatch\n"
                    f"Java:  {java_motion!r}\n"
                    f"Native: {native_motion!r}\nState: {value!r}")
            if int(value["llama_task_mask"]) != native["task_mask"]:
                raise AssertionError(
                    f"follow-parent tick {tick}: task-mask mismatch\n"
                    f"Java: {value['llama_task_mask']!r}\n"
                    f"Native: {native['task_mask']!r}")
            if int(value["llama_entity_seed48"]) \
                    != native["entity_seed48"]:
                raise AssertionError(
                    f"follow-parent tick {tick}: entity RNG mismatch\n"
                    f"Java: {value['llama_entity_seed48']!r}\n"
                    f"Native: {native['entity_seed48']!r}")

        # Re-stage the same deterministic wall. A distant collision-box edit
        # must leave the materialized path alone; opening the upper center of
        # the nearby wall must replace it through PathWorldListener.
        request(args.port, "clear_entities_locked")
        staged = request(args.port, "setblocks_locked", {
            "blocks": terrain_blocks(x, y, z),
        })
        if not staged.get("ok"):
            raise AssertionError(
                f"could not stage dynamic caravan terrain: {staged}")
        leader = request(args.port, "summon_locked", {
            "type": "horse", "horse_kind": "llama",
            "x": center_x + 8.5, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "no_ai": True, "on_ground": True,
            "max_health": 20.0, "health": 20.0,
            "movement_speed": 0.175, "jump_strength": 0.5,
            "variant": 0, "llama_strength": 1,
            "llama_leash_player": True,
            "entity_seed48": 0x123456789ABC,
        })
        follower = request(args.port, "summon_locked", {
            "type": "horse", "horse_kind": "llama",
            "x": center_x + 14.5, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "no_ai": False, "on_ground": True,
            "max_health": 20.0, "health": 20.0,
            "movement_speed": 0.175, "jump_strength": 0.5,
            "variant": 0, "llama_strength": 1,
            "llama_caravan_head_eid": leader["eid"],
            "llama_caravan_speed": 2.0999999046325684,
            "llama_caravan_dist_counter": 0,
            "llama_caravan_running": True,
            "entity_seed48": 0x123456789ABD,
        })
        dynamic_expected = native_terrain_rows(
            x, y, z, DYNAMIC_TICKS, dynamic_obstacles=True)
        initial_points = None
        dynamic_motion_mismatches = []
        for tick, native in enumerate(dynamic_expected):
            if tick == 3:
                mutation = request(args.port, "setblocks_locked", {
                    "blocks": [[center_x + 20, ground_y + 1,
                                center_z + 8, 1, 0]],
                })
                if not mutation.get("ok"):
                    raise AssertionError(
                        f"distant dynamic edit failed: {mutation}")
            if tick == 4:
                mutation = request(args.port, "setblocks_locked", {
                    "blocks": [[center_x + 12, ground_y + 2,
                                center_z, 0, 0]],
                })
                if not mutation.get("ok"):
                    raise AssertionError(
                        f"near dynamic edit failed: {mutation}")
            state = request(args.port, "step")["authoritative"]
            value = by_eid(state, follower["eid"])
            java_navigation = navigation_row(value)
            native_navigation = {
                key: native[key] for key in ("target", "index", "points")
            }
            if (java_navigation["points"] is not None
                    and java_navigation != native_navigation):
                raise AssertionError(
                    f"dynamic tick {tick}: Java/native navigation mismatch\n"
                    f"Java:  {java_navigation!r}\n"
                    f"Native: {native_navigation!r}\nState: {value!r}")
            if tick == 2:
                initial_points = java_navigation["points"]
            elif tick == 3 and java_navigation["points"] != initial_points:
                raise AssertionError(
                    "distant collision edit replaced the caravan path")
            elif tick == 4 and java_navigation["points"] == initial_points:
                raise AssertionError(
                    "near wall opening did not replace the caravan path")
            java_motion = {
                field: double_hex(float(value[field]))
                for field in ("x", "y", "z", "vx", "vy", "vz")
            }
            native_motion = {
                field: native[field]
                for field in ("x", "y", "z", "vx", "vy", "vz")
            }
            if java_motion != native_motion:
                dynamic_motion_mismatches.append({
                    "tick": tick,
                    "java": java_motion,
                    "native": native_motion,
                })
            if bool(value["llama_on_ground"]) != bool(native["on_ground"]):
                dynamic_motion_mismatches.append({
                    "tick": tick,
                    "java_on_ground": bool(value["llama_on_ground"]),
                    "native_on_ground": bool(native["on_ground"]),
                })
        if dynamic_motion_mismatches:
            raise AssertionError(
                "dynamic Java/native motion mismatches:\n"
                + json.dumps(dynamic_motion_mismatches, indent=2))

        task_expected = native_task_conflict_rows()
        flat = []
        for block_x in range(center_x + 4, center_x + 19):
            for block_z in range(center_z - 3, center_z + 4):
                flat.append([block_x, ground_y, block_z, 1, 0])

        def stage_task_caravan(*, ranged: bool = False,
                               mating: bool = False,
                               swimming: bool = False,
                               follow_parent: bool = False) -> int:
            request(args.port, "clear_entities_locked")
            blocks = list(flat)
            blocks.append([center_x + 14, ground_y + 1,
                           center_z, 0, 0])
            if swimming:
                blocks.append([center_x + 14, ground_y + 1,
                               center_z, 9, 0])
            staged_task = request(args.port, "setblocks_locked", {
                "blocks": blocks,
            })
            if not staged_task.get("ok"):
                raise AssertionError(
                    f"could not stage task-conflict terrain: {staged_task}")
            task_leader = request(args.port, "summon_locked", {
                "type": "horse", "horse_kind": "llama",
                "x": center_x + 8.5, "y": ground_y + 1.0,
                "z": center_z + 0.5,
                "no_ai": True, "on_ground": True,
                "llama_leash_player": True,
                "entity_seed48": 0x123456789ABC,
            })
            task_follower_action = {
                "type": "horse", "horse_kind": "llama",
                "x": center_x + 14.5, "y": ground_y + 1.0,
                "z": center_z + 0.5,
                "no_ai": False, "on_ground": True,
                "llama_caravan_head_eid": task_leader["eid"],
                "llama_caravan_running": True,
                "entity_seed48": 0x123456789ABD,
            }
            if follow_parent:
                task_follower_action["growing_age"] = -100
            if ranged:
                task_follower_action["llama_ranged_running"] = True
            if mating:
                task_follower_action["in_love"] = 600
            task_follower = request(
                args.port, "summon_locked", task_follower_action)
            if mating:
                request(args.port, "summon_locked", {
                    "type": "horse", "horse_kind": "llama",
                    "x": center_x + 16.0, "y": ground_y + 1.0,
                    "z": center_z + 0.5,
                    "no_ai": True, "on_ground": True,
                    "in_love": 600,
                    "entity_seed48": 0x123456789ABE,
                })
            task_state = request(args.port, "step")["authoritative"]
            task_value = by_eid(task_state, task_follower["eid"])
            return int(task_value["llama_task_mask"])

        task_java = {
            "caravan_ranged": stage_task_caravan(ranged=True),
            "caravan_mate": stage_task_caravan(mating=True),
            "caravan_swim": stage_task_caravan(swimming=True),
            "caravan_follow_parent": stage_task_caravan(
                follow_parent=True),
        }
        if task_java != task_expected:
            raise AssertionError(
                "llama task-priority mismatch\n"
                f"Java:   {task_java!r}\nNative: {task_expected!r}")

        # Pin the three probabilistic tail goals at their accepted boundary.
        # Each row compares the post-tick entity RNG cursor and all six
        # position/motion doubles, so an extra Random call or a different
        # accepted wander target fails immediately.
        lower_expected = native_lower_task_rows(x, y, z)
        lower_seeds = {"wander": 160, "watch": 62, "idle": 7}
        lower_offsets = {"wander": 8.0, "watch": 4.0, "idle": 8.0}
        lower_java = {}
        for name in ("wander", "watch", "idle"):
            request(args.port, "clear_entities_locked")
            staged_lower = request(args.port, "setblocks_locked", {
                "blocks": lower_task_blocks(x, y, z),
            })
            if not staged_lower.get("ok"):
                raise AssertionError(
                    f"could not stage {name} lower-task terrain: "
                    f"{staged_lower}")
            lower_llama = request(args.port, "summon_locked", {
                "type": "horse", "horse_kind": "llama",
                "x": center_x + 0.5 + lower_offsets[name],
                "y": ground_y + 1.0,
                "z": center_z + 0.5,
                "no_ai": False, "on_ground": True,
                "max_health": 20.0, "health": 20.0,
                "movement_speed": 0.175, "jump_strength": 0.5,
                "variant": 0, "llama_strength": 1,
                "entity_seed48": lower_seeds[name],
            })
            lower_state = request(args.port, "step")["authoritative"]
            lower_value = by_eid(lower_state, lower_llama["eid"])
            lower_java[name] = {
                "task_mask": int(lower_value["llama_task_mask"]),
                "entity_seed48": int(lower_value["llama_entity_seed48"]),
                **{
                    field: double_hex(float(lower_value[field]))
                    for field in ("x", "y", "z", "vx", "vy", "vz")
                },
            }
        if lower_java != lower_expected:
            raise AssertionError(
                "llama lower-task Java/native mismatch\n"
                f"Java:   {json.dumps(lower_java, indent=2)}\n"
                f"Native: {json.dumps(lower_expected, indent=2)}")

        # Burning panic first scans for the nearest water without consuming
        # target-selection RNG, then falls back to the exact ten-candidate
        # RandomPositionGenerator path when the scan is empty.
        for panic_name, water_target in (
                ("water", True), ("random", False)):
            request(args.port, "clear_entities_locked")
            panic_blocks = panic_task_blocks(x, y, z)
            if water_target:
                panic_blocks.append(
                    [center_x + 13, ground_y + 1, center_z, 9, 0])
            staged_panic = request(args.port, "setblocks_locked", {
                "blocks": panic_blocks,
            })
            if not staged_panic.get("ok"):
                raise AssertionError(
                    f"could not stage {panic_name} panic terrain: "
                    f"{staged_panic}")
            panic_llama = request(args.port, "summon_locked", {
                "type": "horse", "horse_kind": "llama",
                "x": center_x + 8.5, "y": ground_y + 1.0,
                "z": center_z + 0.5,
                "no_ai": False, "on_ground": True,
                "max_health": 20.0, "health": 20.0,
                "movement_speed": 0.175, "jump_strength": 0.5,
                "variant": 0, "llama_strength": 1,
                "entity_seed48": 0x123456789ABD,
                "fire_ticks": 99,
            })
            panic_expected = native_panic_rows(
                x, y, z, water_target=water_target)
            for tick, native in enumerate(panic_expected):
                panic_state = request(args.port, "step")["authoritative"]
                panic_value = by_eid(panic_state, panic_llama["eid"])
                java_navigation = navigation_row(panic_value)
                native_navigation = {
                    key: native[key] for key in ("target", "index", "points")
                }
                if java_navigation != native_navigation:
                    raise AssertionError(
                        f"{panic_name} panic tick {tick}: navigation mismatch\n"
                        f"Java:  {java_navigation!r}\n"
                        f"Native: {native_navigation!r}\n"
                        f"State: {panic_value!r}")
                java_panic = {
                    "task_mask": int(panic_value["llama_task_mask"]),
                    "entity_seed48": int(
                        panic_value["llama_entity_seed48"]),
                    "on_ground": int(bool(panic_value["llama_on_ground"])),
                    **{
                        field: double_hex(float(panic_value[field]))
                        for field in ("x", "y", "z", "vx", "vy", "vz")
                    },
                }
                native_panic = {
                    key: native[key] for key in java_panic
                }
                if java_panic != native_panic:
                    raise AssertionError(
                        f"{panic_name} panic tick {tick}: state mismatch\n"
                        f"Java:  {java_panic!r}\n"
                        f"Native: {native_panic!r}\n"
                        f"State: {panic_value!r}")

        # EntityAIMate requests the stationary partner every tick. The wall
        # forces a real A* detour and crosses the same airborne request-clear
        # boundary as the caravan fixture.
        request(args.port, "clear_entities_locked")
        staged_mate = request(args.port, "setblocks_locked", {
            "blocks": terrain_blocks(x, y, z),
        })
        if not staged_mate.get("ok"):
            raise AssertionError(
                f"could not stage mating terrain: {staged_mate}")
        request(args.port, "summon_locked", {
            "type": "horse", "horse_kind": "llama",
            "x": center_x + 8.5, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "no_ai": True, "on_ground": True, "tame": True,
            "in_love": 600,
            "max_health": 20.0, "health": 20.0,
            "movement_speed": 0.175, "jump_strength": 0.5,
            "variant": 0, "llama_strength": 1,
            "entity_seed48": 0x123456789ABC,
        })
        active_mate = request(args.port, "summon_locked", {
            "type": "horse", "horse_kind": "llama",
            "x": center_x + 17.0, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "no_ai": False, "on_ground": True, "tame": True,
            "in_love": 600,
            "max_health": 20.0, "health": 20.0,
            "movement_speed": 0.175, "jump_strength": 0.5,
            "variant": 0, "llama_strength": 1,
            "entity_seed48": 0x123456789ABD,
        })
        mate_expected = native_mate_rows(x, y, z)
        for tick, native in enumerate(mate_expected):
            mate_state = request(args.port, "step")["authoritative"]
            mate_value = by_eid(mate_state, active_mate["eid"])
            java_navigation = navigation_row(mate_value)
            native_navigation = {
                key: native[key] for key in ("target", "index", "points")
            }
            if java_navigation != native_navigation:
                raise AssertionError(
                    f"mating tick {tick}: navigation mismatch\n"
                    f"Java:  {java_navigation!r}\n"
                    f"Native: {native_navigation!r}\nState: {mate_value!r}")
            java_mate = {
                "task_mask": int(mate_value["llama_task_mask"]),
                "entity_seed48": int(mate_value["llama_entity_seed48"]),
                "on_ground": int(bool(mate_value["llama_on_ground"])),
                **{
                    field: double_hex(float(mate_value[field]))
                    for field in ("x", "y", "z", "vx", "vy", "vz")
                },
            }
            native_mate = {key: native[key] for key in java_mate}
            if java_mate != native_mate:
                raise AssertionError(
                    f"mating tick {tick}: state mismatch\n"
                    f"Java:  {java_mate!r}\nNative: {native_mate!r}\n"
                    f"State: {mate_value!r}")

        # A seed-accepted AIDefendTarget acquires the stationary wild wolf on
        # the first selector boundary. The floor gap forces a materialized
        # entity-target path while preserving line of sight, so the fixture
        # crosses airborne path retention, the 20-visible-tick clear, and the
        # fixed 40-tick llama spit cooldown.
        request(args.port, "clear_entities_locked")
        staged_ranged = request(args.port, "setblocks_locked", {
            "blocks": ranged_task_blocks(x, y, z),
        })
        if not staged_ranged.get("ok"):
            raise AssertionError(
                f"could not stage ranged terrain: {staged_ranged}")
        ranged_wolf = request(args.port, "summon_locked", {
            "type": "no_ai_mob", "entity_type": 11,
            "x": center_x + 1.5, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "on_ground": True, "tamed": False,
            "entity_seed48": 0x123456789ABC,
        })
        ranged_llama = request(args.port, "summon_locked", {
            "type": "horse", "horse_kind": "llama",
            "x": center_x + 9.5, "y": ground_y + 1.0,
            "z": center_z + 0.5,
            "no_ai": False, "on_ground": True,
            "max_health": 20.0, "health": 20.0,
            "movement_speed": 0.175, "jump_strength": 0.5,
            "variant": 0, "llama_strength": 1,
            "entity_seed48": 6,
        })
        ranged_expected = native_ranged_rows(x, y, z)
        for tick, native in enumerate(ranged_expected):
            ranged_state = request(args.port, "step")["authoritative"]
            ranged_value = by_eid(ranged_state, ranged_llama["eid"])
            java_navigation = navigation_row(ranged_value)
            native_navigation = {
                key: native[key] for key in ("target", "index", "points")
            }
            if java_navigation != native_navigation:
                raise AssertionError(
                    f"ranged tick {tick}: navigation mismatch\n"
                    f"Java:  {java_navigation!r}\n"
                    f"Native: {native_navigation!r}\n"
                    f"State: {ranged_value!r}")
            java_ranged = {
                "task_mask": int(ranged_value["llama_task_mask"]),
                "entity_seed48": int(
                    ranged_value["llama_entity_seed48"]),
                "attack_time": int(
                    ranged_value["llama_ranged_attack_time"]),
                "see_time": int(ranged_value["llama_ranged_see_time"]),
                "did_spit": int(bool(ranged_value["llama_did_spit"])),
                "on_ground": int(bool(ranged_value["llama_on_ground"])),
                **{
                    field: double_hex(float(ranged_value[field]))
                    for field in ("x", "y", "z", "vx", "vy", "vz")
                },
            }
            native_ranged = {key: native[key] for key in java_ranged}
            if java_ranged != native_ranged:
                raise AssertionError(
                    f"ranged tick {tick}: state mismatch\n"
                    f"Java:  {java_ranged!r}\n"
                    f"Native: {native_ranged!r}\n"
                    f"State: {ranged_value!r}")
            if int(ranged_value["llama_attack_target_eid"]) \
                    != int(ranged_wolf["eid"]):
                raise AssertionError(
                    f"ranged tick {tick}: wolf target was not retained\n"
                    f"State: {ranged_value!r}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")
        request(args.port, "runcmds", {
            "cmds": ["gamerule doMobSpawning true"],
        })
    print("PASS real Java/native llama caravan: private clock boundaries, "
          "20-tick obstacle motion, dynamic distant/near block updates, "
          "16-tick follow-parent path/motion/RNG, task-priority conflicts, "
          "accepted wander/watch/idle RNG+motion, 24 panic path/motion/RNG "
          "ticks, 16 mating path/motion/RNG ticks, 41 ranged path/clock/"
          "motion/RNG ticks, and all materialized paths")


if __name__ == "__main__":
    main()
