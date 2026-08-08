#!/usr/bin/env python3
"""Bit-compare Snow Golem ranged construction against real 1.11.2."""

import argparse
import json
import pathlib
import subprocess
import time

from test_dragon_crystal_notification import request


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
CASES = (
    ("baseline", (10.25, 70.0, -3.75), (17.5, 71.0, 2.25), 1,
     0x123456789ABC, 0x3456789ABCDE, 700000),
    ("high_target", (-12.5, 81.25, 20.75), (-3.0, 85.5, 15.125),
     0x111122223333, 0x444455556666, 0x777788889999, 710000),
    ("near_target", (4.0, 64.0, 4.0), (5.125, 63.5, 6.75),
     0xABCDEF123456, 0x0FEDCBA98765, 0x13579BDF2468, 720000),
)


def native(case):
    _, owner, target, owner_seed, entity_seed, uuid_seed, next_id = case
    raw = subprocess.check_output([
        str(MAGMA / "game" / "test_snowman_ranged_oracle"),
        *(repr(value) for value in (*owner, *target)),
        str(owner_seed), str(entity_seed), str(uuid_seed), str(next_id),
    ], text=True)
    return json.loads(raw)


def compare(name, java, magma):
    fields = (
        "ok", "eid", "owner_eid", "position_bits", "motion_bits",
        "rotation_bits", "seed48", "have_gaussian",
        "next_gaussian_bits", "uuid_most", "uuid_least",
        "owner_seed48", "entity_seed48", "server_uuid_seed48",
        "next_entity_id", "sound", "sound_volume_bits",
        "sound_pitch_bits",
    )
    for field in fields:
        if java.get(field) != magma.get(field):
            raise AssertionError(
                f"{name}.{field}: java={java.get(field)!r} "
                f"magma={magma.get(field)!r}")
    if java.get("sound_name") != "minecraft:entity.snowman.shoot" \
            or java.get("sound_category") != "neutral":
        raise AssertionError(
            f"{name}.sound identity: {java.get('sound_name')!r} "
            f"{java.get('sound_category')!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25600)
    args = parser.parse_args()
    locked = False
    deadline = time.monotonic() + 120.0
    while True:
        try:
            request(args.port, "obs")
            break
        except (OSError, RuntimeError, ValueError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.5)
    try:
        request(args.port, "server_step_lock")
        locked = True
        time.sleep(2.0)
        subprocess.run(
            ["make", "-C", str(MAGMA),
             "game/test_snowman_ranged_oracle"],
            check=True, stdout=subprocess.DEVNULL)
        for case in CASES:
            name, owner, target, owner_seed, entity_seed, uuid_seed, next_id = case
            java = request(args.port, "snowman_ranged_locked", {
                "owner_x": owner[0], "owner_y": owner[1],
                "owner_z": owner[2], "target_x": target[0],
                "target_y": target[1], "target_z": target[2],
                "owner_seed48": owner_seed,
                "entity_seed48": entity_seed,
                "server_uuid_seed48": uuid_seed,
                "next_entity_id": next_id,
            })
            compare(name, java, native(case))
        print("PASS java==magma: 3 Snow Golem ranged launches, exact owner, "
              "position, heading, RNG, UUID, EID and sound state")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
