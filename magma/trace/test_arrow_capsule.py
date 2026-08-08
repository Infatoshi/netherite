#!/usr/bin/env python3
"""One-tick real-Java/native continuation from a player-arrow capsule."""

import argparse
import json
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
from trace_java import canonical_stack_payload, canonicalize


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
            prefix="arrow_capsule_", dir=temp_root) as raw_temp:
        temp = pathlib.Path(raw_temp)
        obs = request(args.port, "obs")
        request(args.port, "killentities")
        base_spawn = {
            "type": "player_arrow",
            "x": float(obs["x"]),
            "y": float(obs["y"]) + 20.0,
            "mx": 0.75, "my": 0.125, "mz": 0.25,
        }
        spawn_actions = [
            {**base_spawn, "z": float(obs["z"])},
            {
                **base_spawn, "z": float(obs["z"]) + 1.0,
                "arrow_kind": 1, "potion_type": 25,
                "custom_id": 5, "custom_amp": 1,
                "custom_duration": 300, "custom_flags": 1,
                "color": 0x123456,
            },
            {
                **base_spawn, "z": float(obs["z"]) + 2.0,
                "arrow_kind": 2, "spectral_duration": 321,
            },
        ]
        spawns = [request(args.port, "summon", action)
                  for action in spawn_actions]
        spawn_eids = {spawn["eid"] for spawn in spawns}
        try:
            before_lock = request(args.port, "server_step_lock")
            locked = True
            authoritative = before_lock["authoritative"]
            arrows_before = {
                entity["eid"]: entity
                for entity in authoritative["entities"]
                if entity.get("eid") in spawn_eids
            }
            if len(arrows_before) != len(spawn_eids) or any(
                    arrow.get("arrow_exact") is not True
                    for arrow in arrows_before.values()):
                raise AssertionError(
                    f"Java arrows were not capsule-exact: {arrows_before!r}")
            center_x = int(min(float(arrow["x"])
                               for arrow in arrows_before.values()) // 1)
            center_y = int(min(float(arrow["y"])
                               for arrow in arrows_before.values()) // 1)
            min_z = int(min(float(arrow["z"])
                            for arrow in arrows_before.values()) // 1)
            max_z = int(max(float(arrow["z"])
                            for arrow in arrows_before.values()) // 1)
            box = [
                center_x - 2, center_y - 2, min_z - 2,
                center_x + 2, center_y + 2, max_z + 2,
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
            arrows_after = {
                entity["eid"]: entity
                for entity in after_step["authoritative"]["entities"]
                if entity.get("eid") in spawn_eids
            }
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
        ], check=True, stdout=subprocess.DEVNULL, env={
            **os.environ, "MAGMA_CAPSULE_DIR": str(capsule),
        })
        native = json.loads(native_state.read_text(encoding="utf-8"))
        arrows_native = {
            entity["eid"]: entity for entity in native["entities"]
            if entity.get("kind") == "projectile"
            and entity.get("eid") in spawn_eids
        }
        if set(arrows_after) != spawn_eids \
                or set(arrows_native) != spawn_eids:
            raise AssertionError(
                f"arrow set changed: Java={set(arrows_after)} "
                f"native={set(arrows_native)} expected={spawn_eids}")
        for eid in sorted(spawn_eids):
            arrow_after = arrows_after[eid]
            arrow_native = arrows_native[eid]
            mismatches = []
            for field in ("x", "y", "z", "vx", "vy", "vz"):
                if dbits(arrow_after[field]) != dbits(arrow_native[field]):
                    mismatches.append(field)
            for field in ("yaw", "pitch"):
                if fbits(arrow_after[field]) != fbits(arrow_native[field]):
                    mismatches.append(field)
            for field in (
                    "ticks_in_air", "fire_ticks", "damage", "knockback",
                    "critical", "pickup_status", "in_ground", "shake",
                    "ticks_in_ground", "time_in_ground", "tile_x", "tile_y",
                    "tile_z", "tile_block", "tile_meta", "entity_seed48",
                    "entity_have_gaussian", "entity_gaussian", "arrow_kind",
                    "potion_type", "spectral_duration", "arrow_color",
                    "arrow_custom_color", "arrow_effects", "pickup_item",
                    "pickup_meta", "stack_payload", "uuid_most", "uuid_least",
            ):
                java_value = arrow_after.get(field)
                native_value = arrow_native.get(field)
                if field == "stack_payload":
                    java_value = canonical_stack_payload(java_value)
                    native_value = canonical_stack_payload(native_value)
                if java_value != native_value:
                    mismatches.append(field)
            if mismatches:
                raise AssertionError(
                    f"arrow {eid} continuation mismatch {mismatches}: "
                    f"Java={arrow_after!r} native={arrow_native!r}")
    print("PASS real Java/native: normal/tipped/spectral arrow capsule")


if __name__ == "__main__":
    main()
