#!/usr/bin/env python3
"""Compare active EntitySquid ticks against real Minecraft 1.11.2."""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
import subprocess
import sys


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
sys.path.insert(0, str(HERE))

from test_dragon_crystal_notification import request


SCENARIOS = (
    ("water_empty", 24),
    ("water_vector", 24),
    ("cycle_refresh", 8),
    ("age_stop", 8),
    ("dry", 12),
)
SEED48 = 0x123456789ABC


def double_hex(value: float) -> str:
    return f"{struct.unpack('>Q', struct.pack('>d', value))[0]:016x}"


def float_hex(value: float) -> str:
    return f"{struct.unpack('>I', struct.pack('>f', value))[0]:08x}"


def f32(value: float) -> float:
    return struct.unpack('>f', struct.pack('>f', value))[0]


def by_eid(state: dict, eid: int) -> dict:
    rows = [row for row in state["entities"] if row.get("eid") == eid]
    if len(rows) != 1:
        raise AssertionError(f"expected Squid eid {eid}, got {rows!r}")
    return rows[0]


def native_rows(scenario: str, ticks: int) -> list[dict]:
    result = subprocess.run(
        [str(MAGMA / "game/test_squid_runtime"),
         "--oracle", scenario, str(ticks)],
        cwd=MAGMA, check=True, capture_output=True, text=True,
    )
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    if [row.get("tick") for row in rows] != list(range(ticks)):
        raise AssertionError(
            f"native {scenario} rows are incomplete: {rows!r}")
    return rows


def terrain_and_fixture(scenario: str) -> tuple[list[list[int]], dict]:
    squid_x = 48.5 if scenario == "age_stop" else 18.5
    squid_y = 7.0
    squid_z = 8.5
    in_water = scenario != "dry"
    center_x = int(squid_x)
    blocks = []
    for x in range(center_x - 3, center_x + 4):
        for z in range(5, 13):
            blocks.append([x, 4, z, 1, 0])
            for y in range(5, 12):
                blocks.append([x, y, z, 9 if in_water else 0, 0])
    vector = (0.0, 0.0, 0.0) if scenario == "water_empty" \
        else (0.1, 0.02, -0.15)
    speed = 0.6
    rotation = 6.25 if scenario == "cycle_refresh" else 0.25
    action = {
        "type": "no_ai_mob", "entity_type": 14, "no_ai": False,
        "x": squid_x, "y": squid_y, "z": squid_z,
        "mx": f32(f32(vector[0]) * f32(speed)),
        "my": f32(f32(vector[1]) * f32(speed)),
        "mz": f32(f32(vector[2]) * f32(speed)),
        "yaw": 0.3, "pitch": 10.0,
        "prev_yaw": 0.2, "prev_pitch": 10.0,
        "on_ground": False,
        "squid_pitch": 12.0,
        "squid_prev_pitch": 10.0,
        "squid_yaw": 0.3,
        "squid_prev_yaw": 0.2,
        "squid_rotation": rotation,
        "squid_prev_rotation": rotation - 0.1,
        "squid_tentacle_angle": 0.2,
        "squid_last_tentacle_angle": 0.1,
        "squid_random_motion_speed": speed,
        "squid_rotation_velocity": (
            0.1 if scenario == "cycle_refresh" else 0.15),
        "squid_rotate_speed": 0.4,
        "squid_random_motion_x": vector[0],
        "squid_random_motion_y": vector[1],
        "squid_random_motion_z": vector[2],
        "squid_render_yaw_offset": 5.0,
        "entity_seed48": 2 if scenario == "cycle_refresh" else SEED48,
        "squid_entity_age": 100 if scenario == "age_stop" else 0,
        "squid_persistence_required": False,
    }
    return blocks, action


def canonical_java(entity: dict, tick: int) -> dict:
    return {
        "tick": tick,
        "alive": 1,
        **{field: double_hex(float(entity[field]))
           for field in ("x", "y", "z", "vx", "vy", "vz")},
        "yaw": float_hex(float(entity["yaw"])),
        "pitch": float_hex(float(entity["squid_pitch"])),
        "prev_pitch": float_hex(float(entity["squid_prev_pitch"])),
        "squid_yaw": float_hex(float(entity["squid_yaw"])),
        "prev_yaw": float_hex(float(entity["squid_prev_yaw"])),
        "rotation": float_hex(float(entity["squid_rotation"])),
        "prev_rotation": float_hex(float(entity["squid_prev_rotation"])),
        "tentacle": float_hex(float(entity["squid_tentacle_angle"])),
        "last_tentacle": float_hex(float(
            entity["squid_last_tentacle_angle"])),
        "motion_speed": float_hex(float(
            entity["squid_random_motion_speed"])),
        "rotation_velocity": float_hex(float(
            entity["squid_rotation_velocity"])),
        "rotate_speed": float_hex(float(entity["squid_rotate_speed"])),
        "motion_x": float_hex(float(entity["squid_random_motion_x"])),
        "motion_y": float_hex(float(entity["squid_random_motion_y"])),
        "motion_z": float_hex(float(entity["squid_random_motion_z"])),
        "render_yaw": float_hex(float(
            entity["squid_render_yaw_offset"])),
        "seed48": int(entity["base_entity_seed48"]),
        "ticks_existed": int(entity["ticks_existed"]),
        "in_water": int(bool(entity["in_water"])),
        "entity_age": int(entity["squid_entity_age"]),
        "persistence_required": int(bool(
            entity["squid_persistence_required"])),
        "box": [double_hex(float(entity[field])) for field in (
            "base_box_min_x", "base_box_min_y", "base_box_min_z",
            "base_box_max_x", "base_box_max_y", "base_box_max_z",
        )],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    subprocess.run(
        ["make", "-C", str(MAGMA), "game/test_squid_runtime"],
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
        request(args.port, "server_step_lock")
        locked = True
        for scenario, ticks in SCENARIOS:
            request(args.port, "clear_entities_locked")
            blocks, action = terrain_and_fixture(scenario)
            staged = request(args.port, "setblocks_locked", {
                "blocks": blocks,
            })
            if not staged.get("ok"):
                raise AssertionError(
                    f"could not stage {scenario} terrain: {staged}")
            moved = request(args.port, "setplayer_locked", {
                "x": 8.5, "y": 5.0, "z": 8.5,
                "vx": 0.0, "vy": 0.0, "vz": 0.0,
                "on_ground": True, "fall_distance": 0.0,
            })
            if not moved.get("ok"):
                raise AssertionError(
                    f"could not stage {scenario} player: {moved}")
            spawned = request(args.port, "summon_locked", action)
            if not spawned.get("ok"):
                raise AssertionError(
                    f"could not stage {scenario} Squid: {spawned}")
            eid = int(spawned["eid"])
            initial = by_eid(spawned["authoritative"], eid)
            if initial.get("squid_active_exact") is not True:
                raise AssertionError(
                    f"oracle did not certify active Squid state: {initial!r}")
            expected = native_rows(scenario, ticks)
            for tick, native in enumerate(expected):
                state = request(args.port, "step")["authoritative"]
                rows = [row for row in state["entities"]
                        if row.get("eid") == eid]
                if not native.get("alive"):
                    java = {"tick": tick, "alive": 0}
                    if rows:
                        raise AssertionError(
                            f"{scenario} tick {tick}: Java retained Squid "
                            f"after native despawn: {rows!r}")
                else:
                    entity = by_eid(state, eid)
                    if entity.get("squid_active_exact") is not True:
                        raise AssertionError(
                            f"{scenario} tick {tick} lost exact marker: "
                            f"{entity!r}")
                    java = canonical_java(entity, tick)
                if java != native:
                    different = [
                        key for key in java if java[key] != native.get(key)
                    ]
                    raise AssertionError(
                        f"{scenario} tick {tick}: Java/native Squid mismatch "
                        f"in {different}\nJava:  {java!r}\n"
                        f"Native:{native!r}\n"
                        f"Entity:{rows[0] if rows else None!r}")
                checked += 1
        print(
            f"PASS active Squid Java/native: {checked} exact ticks across "
            f"{len(SCENARIOS)} water/dry/random-swim cases")
    finally:
        if locked:
            try:
                request(args.port, "server_step_unlock")
            except Exception:
                pass


if __name__ == "__main__":
    main()
