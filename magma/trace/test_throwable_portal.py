#!/usr/bin/env python3
"""Bit-exact Java/native EntityThrowable portal reconstruction seam."""

import argparse
import json
import pathlib
import subprocess
import sys
import time
import uuid


JAVA = pathlib.Path(__file__).resolve().parents[2] / "java"
sys.path.insert(0, str(JAVA))
from qrl_client import NetheriteEnv  # noqa: E402


CASES = (
    ("egg", 7, 772001, 12345, 23456, 34567,
     0.25, 0.1, -0.125, -130.0, -33.0, 7),
    ("snowball", 8, 772002, 12346, 23457, 34568,
     -0.125, 0.25, 0.375, -58.75, -14.5, 8),
    ("xp_bottle", 9, 772003, 12347, 23458, 34569,
     0.375, -0.05, 0.125, 12.5, 4.0, 9),
    ("ender_pearl", 12, 772004, 12348, 23459, 34570,
     -0.25, 0.175, -0.25, 83.75, 22.5, 10),
)

DIRECTIONS = {"south": 0, "west": 1, "north": 2, "east": 3}
OBSTRUCTIONS = {"negative": -1, "none": 0, "positive": 1}


def signed64(value):
    return value if value < (1 << 63) else value - (1 << 64)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=25575)
    parser.add_argument("--native", type=pathlib.Path, required=True)
    parser.add_argument("--progress", action="store_true")
    parser.add_argument("--orientation-only", action="store_true")
    parser.add_argument("--base-only", action="store_true")
    parser.add_argument("--axis", choices=("x", "z"))
    args = parser.parse_args()
    env = NetheriteEnv(args.host, args.port)
    initial = env.reset({
        "seed": 708,
        "mode": "survival",
        "type": "flat",
        "structures": False,
    })
    if not initial.get("ok"):
        raise RuntimeError(f"oracle reset failed: {initial}")
    locked = env._cmd({"cmd": "server_step_lock"})
    if not locked.get("ok"):
        raise RuntimeError(f"server lock failed: {locked}")
    try:
        if int(locked["authoritative"]["dim"]) != 0:
            moved = env._cmd({
                "cmd": "transfer_dimension_locked", "action": {"id": 0},
            })
            if not moved.get("ok"):
                raise AssertionError(moved)
        comparisons = 0
        # reset() can retain Forge's already-initialized Nether WorldServer.
        # Use a remote per-process coordinate so an earlier loaded-positive
        # run cannot satisfy the unloaded-area negative control.
        portal_base = 1_000_000 + time.time_ns() % 1_000_000
        def compare_case(action, native_type, old_eid, next_id,
                         old_seed, clone_seed, uuid_seed,
                         uuid_most, uuid_least, label):
            expected = env._cmd({
                "cmd": "throwable_portal_locked", "action": action,
            })
            if not expected.get("ok"):
                raise AssertionError(expected)
            portal_axis = action.get("portal_axis", "x")
            native = subprocess.run([
                str(args.native.resolve()), str(native_type),
                str(old_eid), str(next_id), str(old_seed),
                str(clone_seed), str(uuid_seed), str(uuid_most),
                str(uuid_least), str(action["x"]), str(action["y"]),
                str(action["z"]), str(action["vx"]), str(action["vy"]),
                str(action["vz"]), str(action["yaw"]),
                str(action["pitch"]), str(action["ticks_existed"]),
                str(action["ticks_in_air"]), str(action["portal_x"]),
                str(action["portal_y"]), str(action["portal_z"]),
                str(action["continuation_ticks"]),
                "1" if action.get("destination_loaded") else "0",
                "1" if portal_axis == "x" else "2",
                str(action.get("last_portal_vec_x", 0.5)),
                str(action.get("last_portal_vec_y", 0.5)),
                str(DIRECTIONS[action.get(
                    "teleport_direction", "north")]),
                str(OBSTRUCTIONS[action.get("obstruction_side", "none")]),
            ], check=True, capture_output=True, text=True)
            actual = json.loads(native.stdout)
            if actual != expected:
                keys = sorted(
                    key for key in set(actual) | set(expected)
                    if actual.get(key) != expected.get(key))
                raise AssertionError(
                    f"{label} mismatch in {keys}\n"
                    f"Java:  {expected}\nNative: {actual}")

        # Exercise every unloaded fixture first. Loading the destination's
        # surrounding chunks is persistent within this locked Java world.
        for destination_loaded in (() if args.orientation_only
                                   else (False, True)):
            for index, case in enumerate(CASES):
                (kind, native_type, old_eid, old_seed, clone_seed, uuid_seed,
                 vx, vy, vz, yaw, pitch, age) = case
                next_id = 773001 + index
                identity = uuid.UUID(
                    f"00000000-0000-4000-8000-{old_eid:012d}")
                uuid_most = signed64(identity.int >> 64)
                uuid_least = signed64(identity.int & ((1 << 64) - 1))
                base_action = {
                    "kind": kind,
                    "entity_seed48": old_seed,
                    "clone_generator_seed48": clone_seed,
                    "server_uuid_seed48": uuid_seed,
                    "eid": old_eid,
                    "next_entity_id": next_id,
                    "uuid": str(identity),
                    # Keep the unloaded-area negative control outside the
                    # Nether spawn chunks. Staging the old x=31 portal itself
                    # loaded enough spawn neighbors that a second run could
                    # silently turn the negative into an admitted update.
                    "x": portal_base * 8.0 + 0.5, "y": 220.0,
                    "z": portal_base * 8.0 + 0.5,
                    "vx": vx, "vy": vy, "vz": vz,
                    "yaw": yaw, "pitch": pitch,
                    "ticks_existed": age,
                    "ticks_in_air": age,
                    "portal_x": portal_base, "portal_y": 220,
                    "portal_z": portal_base,
                    "portal_axis": "x",
                    "last_portal_vec_x": 0.5,
                    "last_portal_vec_y": 0.5,
                    "teleport_direction": "north",
                    "obstruction_side": "none",
                    "destination_loaded": destination_loaded,
                }
                for continuation_ticks in (0, 1, 2, 3):
                    action = dict(base_action)
                    action["continuation_ticks"] = continuation_ticks
                    admission = "loaded" if destination_loaded \
                        else "unloaded"
                    compare_case(
                        action, native_type, old_eid, next_id,
                        old_seed, clone_seed, uuid_seed,
                        uuid_most, uuid_least,
                        f"{kind} {admission} portal +{continuation_ticks}")
                    comparisons += 1
                    if args.progress and comparisons % 8 == 0:
                        print(f"portal cases: {comparisons}", flush=True)

        # EntityBoat shares Entity.changeDimension's replacement seam but
        # has distinct persisted/transient state. Cross both destination
        # axes, every horizontal entry direction, and two off-center vectors.
        boat_eid = 772101
        boat_next_id = 773101
        boat_identity = uuid.UUID(
            f"00000000-0000-4000-8000-{boat_eid:012d}")
        boat_uuid_most = signed64(boat_identity.int >> 64)
        boat_uuid_least = signed64(
            boat_identity.int & ((1 << 64) - 1))
        for portal_axis in ("x", "z"):
            for teleport_direction in DIRECTIONS:
                for vec_x, vec_y in ((0.25, 0.75), (0.8, 0.2)):
                    action = {
                        "kind": "boat", "boat_variant": 4,
                        "entity_seed48": 12345,
                        "clone_generator_seed48": 23456,
                        "server_uuid_seed48": 34567,
                        "eid": boat_eid,
                        "next_entity_id": boat_next_id,
                        "uuid": str(boat_identity),
                        "x": portal_base * 8.0 + 0.5,
                        "y": 220.0,
                        "z": portal_base * 8.0 + 0.5,
                        "vx": 0.25, "vy": 0.1, "vz": -0.125,
                        "yaw": -130.0, "pitch": -33.0,
                        "ticks_existed": 7, "ticks_in_air": 0,
                        "portal_x": portal_base, "portal_y": 220,
                        "portal_z": portal_base,
                        "portal_axis": portal_axis,
                        "last_portal_vec_x": vec_x,
                        "last_portal_vec_y": vec_y,
                        "teleport_direction": teleport_direction,
                        "obstruction_side": "none",
                        "destination_loaded": True,
                        "continuation_ticks": 0,
                    }
                    compare_case(
                        action, 37, boat_eid, boat_next_id,
                        12345, 23456, 34567,
                        boat_uuid_most, boat_uuid_least,
                        f"boat {portal_axis} {teleport_direction} "
                        f"{vec_x}/{vec_y}")
                    comparisons += 1

        # The original seam was an identity transform: X-axis destination,
        # NORTH entry, centered vector. Cross the two portal axes with all
        # four entry facings and off-center vertical/horizontal placement so
        # rotation, motion, BlockPos quantization, and loaded-area admission
        # cannot pass accidentally.
        (kind, native_type, old_eid, old_seed, clone_seed, uuid_seed,
         vx, vy, vz, yaw, pitch, age) = CASES[-1]
        identity = uuid.UUID(
            f"00000000-0000-4000-8000-{old_eid:012d}")
        uuid_most = signed64(identity.int >> 64)
        uuid_least = signed64(identity.int & ((1 << 64) - 1))
        portal_axes = () if args.base_only else (
            (args.axis,) if args.axis else ("x", "z"))
        for portal_axis in portal_axes:
            for teleport_direction in DIRECTIONS:
                for continuation_ticks in (0, 1):
                    action = {
                        "kind": kind,
                        "entity_seed48": old_seed,
                        "clone_generator_seed48": clone_seed,
                        "server_uuid_seed48": uuid_seed,
                        "eid": old_eid,
                        "next_entity_id": 773901,
                        "uuid": str(identity),
                        "x": 248.5, "y": 220.0, "z": 248.5,
                        "vx": vx, "vy": vy, "vz": vz,
                        "yaw": yaw, "pitch": pitch,
                        "ticks_existed": age,
                        "ticks_in_air": age,
                        "portal_x": 31, "portal_y": 220,
                        "portal_z": 31,
                        "portal_axis": portal_axis,
                        "last_portal_vec_x": 0.25,
                        "last_portal_vec_y": 0.75,
                        "teleport_direction": teleport_direction,
                        "obstruction_side": "none",
                        "destination_loaded": True,
                        "continuation_ticks": continuation_ticks,
                    }
                    compare_case(
                        action, native_type, old_eid, 773901,
                        old_seed, clone_seed, uuid_seed,
                        uuid_most, uuid_least,
                        f"{kind} {portal_axis}-axis "
                        f"{teleport_direction} +{continuation_ticks}")
                    comparisons += 1
                    if args.progress and comparisons % 8 == 0:
                        print(f"portal cases: {comparisons}", flush=True)
        for portal_axis in portal_axes:
            for obstruction_side in ("negative", "positive"):
                action = {
                    "kind": kind,
                    "entity_seed48": old_seed,
                    "clone_generator_seed48": clone_seed,
                    "server_uuid_seed48": uuid_seed,
                    "eid": old_eid,
                    "next_entity_id": 773901,
                    "uuid": str(identity),
                    "x": 248.5, "y": 220.0, "z": 248.5,
                    "vx": vx, "vy": vy, "vz": vz,
                    "yaw": yaw, "pitch": pitch,
                    "ticks_existed": age,
                    "ticks_in_air": age,
                    "portal_x": 31, "portal_y": 220,
                    "portal_z": 31,
                    "portal_axis": portal_axis,
                    "last_portal_vec_x": 0.25,
                    "last_portal_vec_y": 0.75,
                    "teleport_direction": "north",
                    "obstruction_side": obstruction_side,
                    "destination_loaded": True,
                    "continuation_ticks": 1,
                }
                compare_case(
                    action, native_type, old_eid, 773901,
                    old_seed, clone_seed, uuid_seed,
                    uuid_most, uuid_least,
                    f"{kind} {portal_axis}-axis {obstruction_side} wall")
                comparisons += 1
    finally:
        unlocked = env._cmd({"cmd": "server_step_unlock"})
        env.close()
        if not unlocked.get("ok"):
            raise RuntimeError(f"server unlock failed: {unlocked}")
    print(f"PASS real Java/native: {comparisons} exact entity portal "
          "reconstruction/continuations")


if __name__ == "__main__":
    main()
