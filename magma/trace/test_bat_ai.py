#!/usr/bin/env python3
"""Compare active EntityBat ticks against real Minecraft 1.11.2."""

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


SCENARIOS = (
    ("flight", 24),
    ("supported", 12),
    ("unsupported", 12),
    ("near_wake", 12),
    ("creative_hang", 12),
    ("invalid_target", 24),
    ("near_reset", 1),
    ("soft_keep", 1),
    ("soft_drop", 1),
    ("hard_drop", 1),
    ("persistent_far", 1),
)
SEED48 = 0x123456789ABC


def double_hex(value: float) -> str:
    return f"{struct.unpack('>Q', struct.pack('>d', value))[0]:016x}"


def float_hex(value: float) -> str:
    return f"{struct.unpack('>I', struct.pack('>f', value))[0]:08x}"


def by_eid(state: dict, eid: int) -> dict:
    rows = [row for row in state["entities"] if row.get("eid") == eid]
    if len(rows) != 1:
        raise AssertionError(f"expected Bat eid {eid}, got {rows!r}")
    return rows[0]


def native_rows(
        scenario: str, player_x: float, player_y: float,
        player_z: float, ticks: int) -> list[dict]:
    result = subprocess.run(
        [str(MAGMA / "game/test_bat_runtime"),
         "--oracle", scenario, repr(player_x), repr(player_y),
         repr(player_z), str(ticks)],
        cwd=MAGMA, check=True, capture_output=True, text=True,
    )
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    if [row.get("tick") for row in rows] != list(range(ticks)):
        raise AssertionError(
            f"native {scenario} rows are incomplete: {rows!r}")
    return rows


def terrain_and_fixture(
        scenario: str, player_x: float, player_y: float,
        player_z: float) -> tuple[list[list[int]], dict]:
    center_x = math.floor(player_x)
    center_z = math.floor(player_z)
    ground_y = math.floor(player_y) - 1
    near = scenario in {"near_wake", "creative_hang", "near_reset"}
    soft = scenario in {"soft_keep", "soft_drop"}
    hard = scenario in {"hard_drop", "persistent_far"}
    hanging = scenario in {
        "supported", "unsupported", "near_wake", "creative_hang",
    }
    bat_x = center_x + (2.5 if near else 10.5)
    bat_y = ground_y + (
        42.0 if soft else 132.0 if hard
        else 1.1 if near else 6.1 if hanging else 6.0)
    bat_z = center_z + 0.5
    target_x = math.trunc(bat_x) + 5
    target_y = math.trunc(bat_y) + 2
    target_z = math.trunc(bat_z) + 4
    blocks = []
    for x in range(center_x - 3, center_x + 19):
        for z in range(center_z - 5, center_z + 8):
            blocks.append([x, ground_y, z, 1, 0])
            for y in range(ground_y + 1, ground_y + 13):
                blocks.append([x, y, z, 0, 0])
    if scenario in {"supported", "near_wake", "creative_hang"}:
        blocks.append([
            math.floor(bat_x), math.floor(bat_y) + 1,
            math.floor(bat_z), 1, 0,
        ])
    if scenario == "invalid_target":
        blocks.append([target_x, target_y, target_z, 1, 0])
    action = {
        "type": "no_ai_mob", "entity_type": 24, "no_ai": False,
        "x": bat_x, "y": bat_y, "z": bat_z,
        "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "yaw": 0.0, "pitch": 0.0, "on_ground": False,
        "bat_hanging": hanging,
        "bat_spawn_valid": True,
        "bat_spawn_x": target_x,
        "bat_spawn_y": target_y,
        "bat_spawn_z": target_z,
        "bat_head_yaw": 0.0,
        "bat_render_yaw_offset": 0.0,
        "bat_body_rotation_tick_counter": 0,
        "bat_body_prev_head_yaw": 0.0,
        # 460 yields ambient nextInt(1000)=247, then despawn nextInt(800)=0.
        "entity_seed48": 460 if scenario == "soft_drop" else SEED48,
        "bat_entity_age": (
            600 if soft or scenario == "near_reset"
            else 1000 if scenario == "persistent_far" else 0),
        "bat_persistence_required": scenario == "persistent_far",
    }
    return blocks, action


def canonical_java(entity: dict, tick: int) -> dict:
    row = {
        "tick": tick,
        "alive": 1,
        **{field: double_hex(float(entity[field]))
           for field in ("x", "y", "z", "vx", "vy", "vz")},
        "yaw": float_hex(float(entity["yaw"])),
        "pitch": float_hex(float(entity["pitch"])),
        "head_yaw": float_hex(float(entity["bat_head_yaw"])),
        "render_yaw": float_hex(float(entity["bat_render_yaw_offset"])),
        "body_prev_head_yaw": float_hex(float(
            entity["bat_body_prev_head_yaw"])),
        "body_tick": int(entity["bat_body_rotation_tick_counter"]),
        "fall": float_hex(float(entity["fall_distance"])),
        "hanging": int(bool(entity["bat_hanging"])),
        "spawn_valid": int(bool(entity["bat_spawn_valid"])),
        "spawn": [
            int(entity.get("bat_spawn_x", 0)),
            int(entity.get("bat_spawn_y", 0)),
            int(entity.get("bat_spawn_z", 0)),
        ],
        "seed48": int(entity["base_entity_seed48"]),
        "ticks_existed": int(entity["ticks_existed"]),
        "living_sound_time": int(entity["base_living_sound_time"]),
        "on_ground": int(bool(entity["on_ground"])),
        "entity_age": int(entity["bat_entity_age"]),
        "persistence_required": int(bool(
            entity["bat_persistence_required"])),
        "box": [double_hex(float(entity[field])) for field in (
            "base_box_min_x", "base_box_min_y", "base_box_min_z",
            "base_box_max_x", "base_box_max_y", "base_box_max_z",
        )],
    }
    return row


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_bat_runtime"],
        check=True, stdout=subprocess.DEVNULL,
    )
    rule = request(args.port, "runcmds", {
        "cmds": [
            "gamerule doMobSpawning false",
            "gamemode survival @p",
        ],
    })
    if not rule.get("ok") or rule.get("failed"):
        raise AssertionError(f"could not disable natural spawning: {rule}")
    request(args.port, "killentities")
    locked = False
    checked = 0
    try:
        parked = request(args.port, "server_step_lock")
        locked = True
        player = parked["authoritative"]
        player_x = math.floor(float(player["x"])) + 0.5
        player_y = float(math.floor(float(player["y"])))
        player_z = math.floor(float(player["z"])) + 0.5
        ground_y = math.floor(player_y) - 1
        for scenario, ticks in SCENARIOS:
            request(args.port, "clear_entities_locked")
            mode = request(args.port, "runcmds", {"cmds": [
                ("gamemode creative @p" if scenario == "creative_hang"
                 else "gamemode survival @p"),
            ]})
            if not mode.get("ok") or mode.get("failed"):
                raise AssertionError(
                    f"could not stage {scenario} game mode: {mode}")
            blocks, action = terrain_and_fixture(
                scenario, player_x, player_y, player_z)
            staged = request(args.port, "setblocks_locked", {
                "blocks": blocks,
            })
            if not staged.get("ok"):
                raise AssertionError(
                    f"could not stage {scenario} terrain: {staged}")
            moved = request(args.port, "setplayer_locked", {
                "x": player_x, "y": ground_y + 1.0, "z": player_z,
                "vx": 0.0, "vy": 0.0, "vz": 0.0,
                "on_ground": True, "fall_distance": 0.0,
            })
            if not moved.get("ok"):
                raise AssertionError(
                    f"could not stage {scenario} player: {moved}")
            spawned = request(args.port, "summon_locked", action)
            if not spawned.get("ok"):
                raise AssertionError(
                    f"could not stage {scenario} Bat: {spawned}")
            eid = int(spawned["eid"])
            initial = by_eid(spawned["authoritative"], eid)
            if initial.get("bat_active_exact") is not True:
                raise AssertionError(
                    f"oracle did not certify active Bat state: {initial!r}")
            expected = native_rows(
                scenario, player_x, player_y, player_z, ticks)
            for tick, native in enumerate(expected):
                state = request(args.port, "step")["authoritative"]
                rows = [row for row in state["entities"]
                        if row.get("eid") == eid]
                if not native.get("alive"):
                    java = {"tick": tick, "alive": 0}
                    if rows:
                        raise AssertionError(
                            f"{scenario} tick {tick}: Java retained Bat "
                            f"after native despawn: {rows!r}")
                    if java != native:
                        raise AssertionError(
                            f"{scenario} tick {tick}: despawn row mismatch "
                            f"Java={java!r} Native={native!r}")
                    checked += 1
                    continue
                entity = by_eid(state, eid)
                if entity.get("bat_active_exact") is not True:
                    raise AssertionError(
                        f"{scenario} tick {tick} lost exact marker: {entity!r}")
                java = canonical_java(entity, tick)
                if java != native:
                    different = [
                        key for key in java if java[key] != native.get(key)
                    ]
                    raise AssertionError(
                        f"{scenario} tick {tick}: Java/native Bat mismatch "
                        f"in {different}\nJava:  {java!r}\n"
                        f"Native:{native!r}\nEntity:{entity!r}")
                checked += 1
        print(
            f"PASS active Bat Java/native: {checked} exact ticks across "
            f"{len(SCENARIOS)} hanging/flight/retarget cases")
    finally:
        if locked:
            try:
                request(args.port, "server_step_unlock")
            except Exception:
                pass


if __name__ == "__main__":
    main()
