#!/usr/bin/env python3
"""Real-Java mid-swim Squid state-capsule continuation."""

from __future__ import annotations

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
from test_squid_ai import SEED48, by_eid, terrain_and_fixture
from trace_java import canonicalize


WARMUP_TICKS = 9
CONTINUATION_TICKS = 16


def dbits(value):
    return struct.pack(">d", float(value))


def fbits(value):
    return struct.pack(">f", float(value))


def compare_squid(java, native, tick):
    mismatches = []
    for field in (
            "x", "y", "z", "vx", "vy", "vz",
            "base_box_min_x", "base_box_min_y", "base_box_min_z",
            "base_box_max_x", "base_box_max_y", "base_box_max_z",
            "base_entity_gaussian"):
        if dbits(java[field]) != dbits(native[field]):
            mismatches.append(field)
    for field in (
            "yaw", "pitch", "health", "fall_distance",
            "base_last_damage", "squid_pitch", "squid_prev_pitch",
            "squid_yaw", "squid_prev_yaw", "squid_rotation",
            "squid_prev_rotation", "squid_tentacle_angle",
            "squid_last_tentacle_angle", "squid_random_motion_speed",
            "squid_rotation_velocity", "squid_rotate_speed",
            "squid_random_motion_x", "squid_random_motion_y",
            "squid_random_motion_z", "squid_render_yaw_offset"):
        if fbits(java[field]) != fbits(native[field]):
            mismatches.append(field)
    for field in (
            "uuid_most", "uuid_least", "hurt_time", "death_time",
            "hurt_resistant_time", "no_ai", "air", "fire", "on_ground",
            "in_water", "ticks_existed", "base_living_sound_time",
            "base_entity_seed48", "base_entity_have_gaussian",
            "squid_entity_age", "squid_persistence_required"):
        if java[field] != native[field]:
            mismatches.append(field)
    if native.get("type") != 14:
        mismatches.append("type")
    if native.get("squid_active_exact") is not True:
        mismatches.append("squid_active_exact")
    if mismatches:
        raise AssertionError(
            f"Squid capsule tick {tick} mismatch {mismatches}: "
            f"Java={java!r} native={native!r}")


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
            prefix="squid_capsule_", dir=temp_root) as raw_temp:
        temp = pathlib.Path(raw_temp)
        obs = request(args.port, "obs")
        rules = request(args.port, "runcmds", {"cmds": [
            "gamerule doMobSpawning false",
            "gamerule doDaylightCycle false",
            "gamemode survival @p",
        ]})
        if not rules.get("ok") or rules.get("failed"):
            raise AssertionError(f"could not stabilize oracle: {rules}")
        request(args.port, "killentities")
        try:
            request(args.port, "server_step_lock")
            locked = True
            _, action = terrain_and_fixture("water_empty")
            center_x = math.floor(float(action["x"]))
            blocks = []
            for x in range(center_x - 6, center_x + 7):
                for z in range(2, 16):
                    blocks.append([x, 4, z, 1, 0])
                    for y in range(5, 10):
                        wall = x in (center_x - 6, center_x + 6) \
                            or z in (2, 15)
                        blocks.append([x, y, z, 1 if wall else 9, 0])
                    blocks.append([x, 10, z, 1, 0])
            staged = request(args.port, "setblocks_locked", {
                "blocks": blocks,
            })
            if not staged.get("ok"):
                raise AssertionError(f"could not stage Squid terrain: {staged}")
            moved = request(args.port, "setplayer_locked", {
                "x": 8.5, "y": 5.0, "z": 8.5,
                "vx": 0.0, "vy": 0.0, "vz": 0.0,
                "on_ground": True, "fall_distance": 0.0,
            })
            if not moved.get("ok"):
                raise AssertionError(f"could not stage player: {moved}")
            spawned = request(args.port, "summon_locked", action)
            if not spawned.get("ok"):
                raise AssertionError(f"could not stage Squid: {spawned}")
            eid = int(spawned["eid"])
            for _ in range(WARMUP_TICKS):
                authoritative = request(
                    args.port, "step")["authoritative"]
            initial = by_eid(authoritative, eid)
            if initial.get("squid_active_exact") is not True \
                    or initial.get("base_entity_seed48") == SEED48:
                raise AssertionError(
                    f"Java did not expose a warmed exact Squid: {initial!r}")

            selected = [initial, authoritative]
            box = [
                math.floor(min(8.5, *(float(value["x"])
                                     for value in selected))) - 8,
                1,
                math.floor(min(8.5, *(float(value["z"])
                                     for value in selected))) - 8,
                math.floor(max(8.5, *(float(value["x"])
                                     for value in selected))) + 9,
                18,
                math.floor(max(8.5, *(float(value["z"])
                                     for value in selected))) + 9,
            ]
            block_file = temp / "blocks.bin"
            request(args.port, "getblocks_locked", {
                "x0": box[0], "y0": box[1], "z0": box[2],
                "x1": box[3], "y1": box[4], "z1": box[5],
                "file": str(block_file),
            })
            observation = dict(obs)
            observation["authoritative"] = authoritative
            for field in ("x", "y", "z", "yaw", "pitch", "vx", "vy",
                          "vz", "fall_distance"):
                if field in authoritative:
                    observation[field] = authoritative[field]
            state = canonicalize(-1, observation, box)
            state_file = temp / "state.json"
            state_file.write_text(json.dumps(state), encoding="utf-8")
            capsule = temp / "capsule"
            create_capsule(
                state_file, block_file, box, capsule, seed=42,
                source_engine="minecraft-java", source_version="1.11.2",
            )
            script = temp / "load.jsonl"
            emit_magma(capsule, script)
            events = [json.loads(line) for line in
                      script.read_text(encoding="utf-8").splitlines()]
            restore = next(index for index, event in enumerate(events)
                           if event.get("type") == "restore_squid_ai_state")
            activate = next(index for index, event in enumerate(events)
                            if event.get("type") == "set_mob_no_ai"
                            and event.get("eid") == eid)
            if restore >= activate or events[activate].get("no_ai") != 0:
                raise AssertionError("capsule activated Squid out of order")
            java_states = [request(args.port, "step")["authoritative"]
                           for _ in range(CONTINUATION_TICKS)]
        finally:
            if locked:
                request(args.port, "server_step_unlock")
            request(args.port, "runcmds", {"cmds": [
                "gamerule doMobSpawning true",
                "gamerule doDaylightCycle true",
            ]})

        native_file = temp / "native.jsonl"
        subprocess.run([
            str(MAGMA / "magma_game"), "--world", "superflat",
            "--headless", "--ticks", str(CONTINUATION_TICKS),
            "--mobs", "off", "--script", str(script),
            "--state-out", str(native_file), "--render", "off",
            "--pace", "unlimited",
        ], check=True, stdout=subprocess.DEVNULL)
        native_states = [json.loads(line) for line in
                         native_file.read_text(encoding="utf-8").splitlines()]
        if len(native_states) != CONTINUATION_TICKS:
            raise AssertionError(
                f"native emitted {len(native_states)} continuation ticks")
        for tick, (java_state, native_state) in enumerate(
                zip(java_states, native_states), start=1):
            java = by_eid(java_state, eid)
            native = by_eid(native_state, eid)
            compare_squid(java, native, tick)
            if native_state.get("mob_update_order") != [eid]:
                raise AssertionError(
                    f"Squid tick {tick} update order: {native_state!r}")
    print(
        "PASS real Java/capsule/native active Squid: "
        f"{WARMUP_TICKS}-tick checkpoint and "
        f"{CONTINUATION_TICKS} exact continuation ticks")


if __name__ == "__main__":
    main()
